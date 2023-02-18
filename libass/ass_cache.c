/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2011 Grigori Goronzy <greg@chown.ath.cx>
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"
#include "ass_compat.h"

#include <inttypes.h>
#include <ft2build.h>
#include FT_OUTLINE_H
#include <assert.h>

#include "ass_utils.h"
#include "ass_font.h"
#include "ass_outline.h"
#include "ass_cache.h"
#include "ass_threading.h"

// Always enable native-endian mode, since we don't care about cross-platform consistency of the hash
#define WYHASH_LITTLE_ENDIAN 1
#include "wyhash.h"

// With wyhash any arbitrary 64 bit value will suffice
#define ASS_HASH_INIT 0xb3e46a540bd36cd4ULL

static inline ass_hashcode ass_hash_buf(const void *buf, size_t len, ass_hashcode hval)
{
    return wyhash(buf, len, hval, _wyp);
}

// type-specific functions
// create hash/compare functions for bitmap, outline and composite cache
#define CREATE_HASH_FUNCTIONS
#include "ass_cache_template.h"
#define CREATE_COMPARISON_FUNCTIONS
#include "ass_cache_template.h"

// font cache
static bool font_key_move(void *dst, void *src)
{
    ASS_FontDesc *d = dst, *s = src;
    if (!d)
        return true;

    *d = *s;
    d->family.str = ass_copy_string(s->family);
    return d->family.str;
}

static void font_destruct(void *key, void *value)
{
    ass_font_clear(value);
}

size_t ass_font_construct(void *key, void *value, void *priv);

const CacheDesc font_cache_desc = {
    .hash_func = font_hash,
    .compare_func = font_compare,
    .key_move_func = font_key_move,
    .construct_func = ass_font_construct,
    .destruct_func = font_destruct,
    .key_size = sizeof(ASS_FontDesc),
    .value_size = sizeof(ASS_Font)
};


// bitmap cache
static bool bitmap_key_move(void *dst, void *src)
{
    BitmapHashKey *d = dst, *s = src;
    if (d)
        *d = *s;
    else
        ass_cache_dec_ref(s->outline);
    return true;
}

static void bitmap_destruct(void *key, void *value)
{
    BitmapHashKey *k = key;
    ass_free_bitmap(value);
    ass_cache_dec_ref(k->outline);
}

size_t ass_bitmap_construct(void *key, void *value, void *priv);

const CacheDesc bitmap_cache_desc = {
    .hash_func = bitmap_hash,
    .compare_func = bitmap_compare,
    .key_move_func = bitmap_key_move,
    .construct_func = ass_bitmap_construct,
    .destruct_func = bitmap_destruct,
    .key_size = sizeof(BitmapHashKey),
    .value_size = sizeof(Bitmap)
};


// composite cache
static ass_hashcode composite_hash(void *key, ass_hashcode hval)
{
    CompositeHashKey *k = key;
    hval = filter_hash(&k->filter, hval);
    for (size_t i = 0; i < k->bitmap_count; i++)
        hval = bitmap_ref_hash(&k->bitmaps[i], hval);
    return hval;
}

static bool composite_compare(void *a, void *b)
{
    CompositeHashKey *ak = a;
    CompositeHashKey *bk = b;
    if (!filter_compare(&ak->filter, &bk->filter))
        return false;
    if (ak->bitmap_count != bk->bitmap_count)
        return false;
    for (size_t i = 0; i < ak->bitmap_count; i++)
        if (!bitmap_ref_compare(&ak->bitmaps[i], &bk->bitmaps[i]))
            return false;
    return true;
}

static bool composite_key_move(void *dst, void *src)
{
    CompositeHashKey *d = dst, *s = src;
    if (d) {
        *d = *s;
        return true;
    }

    for (size_t i = 0; i < s->bitmap_count; i++) {
        ass_cache_dec_ref(s->bitmaps[i].bm);
        ass_cache_dec_ref(s->bitmaps[i].bm_o);
    }
    free(s->bitmaps);
    return true;
}

static void composite_destruct(void *key, void *value)
{
    CompositeHashValue *v = value;
    CompositeHashKey *k = key;
    ass_free_bitmap(&v->bm);
    ass_free_bitmap(&v->bm_o);
    ass_free_bitmap(&v->bm_s);
    for (size_t i = 0; i < k->bitmap_count; i++) {
        ass_cache_dec_ref(k->bitmaps[i].bm);
        ass_cache_dec_ref(k->bitmaps[i].bm_o);
    }
    free(k->bitmaps);
}

size_t ass_composite_construct(void *key, void *value, void *priv);

const CacheDesc composite_cache_desc = {
    .hash_func = composite_hash,
    .compare_func = composite_compare,
    .key_move_func = composite_key_move,
    .construct_func = ass_composite_construct,
    .destruct_func = composite_destruct,
    .key_size = sizeof(CompositeHashKey),
    .value_size = sizeof(CompositeHashValue)
};


// outline cache
static ass_hashcode outline_hash(void *key, ass_hashcode hval)
{
    OutlineHashKey *k = key;
    switch (k->type) {
    case OUTLINE_GLYPH:
        return glyph_hash(&k->u, hval);
    case OUTLINE_DRAWING:
        return drawing_hash(&k->u, hval);
    case OUTLINE_BORDER:
        return border_hash(&k->u, hval);
    default:  // OUTLINE_BOX
        return hval;
    }
}

static bool outline_compare(void *a, void *b)
{
    OutlineHashKey *ak = a;
    OutlineHashKey *bk = b;
    if (ak->type != bk->type)
        return false;
    switch (ak->type) {
    case OUTLINE_GLYPH:
        return glyph_compare(&ak->u, &bk->u);
    case OUTLINE_DRAWING:
        return drawing_compare(&ak->u, &bk->u);
    case OUTLINE_BORDER:
        return border_compare(&ak->u, &bk->u);
    default:  // OUTLINE_BOX
        return true;
    }
}

static bool outline_key_move(void *dst, void *src)
{
    OutlineHashKey *d = dst, *s = src;
    if (!d) {
        if (s->type == OUTLINE_GLYPH)
            ass_cache_dec_ref(s->u.glyph.font);
        return true;
    }

    *d = *s;
    if (s->type == OUTLINE_DRAWING) {
        d->u.drawing.text.str = ass_copy_string(s->u.drawing.text);
        return d->u.drawing.text.str;
    }
    if (s->type == OUTLINE_BORDER)
        ass_cache_inc_ref(s->u.border.outline);
    return true;
}

static void outline_destruct(void *key, void *value)
{
    OutlineHashValue *v = value;
    OutlineHashKey *k = key;
    ass_outline_free(&v->outline[0]);
    ass_outline_free(&v->outline[1]);
    switch (k->type) {
    case OUTLINE_GLYPH:
        ass_cache_dec_ref(k->u.glyph.font);
        break;
    case OUTLINE_DRAWING:
        free((char *) k->u.drawing.text.str);
        break;
    case OUTLINE_BORDER:
        ass_cache_dec_ref(k->u.border.outline);
        break;
    default:  // OUTLINE_BOX
        break;
    }
}

size_t ass_outline_construct(void *key, void *value, void *priv);

const CacheDesc outline_cache_desc = {
    .hash_func = outline_hash,
    .compare_func = outline_compare,
    .key_move_func = outline_key_move,
    .construct_func = ass_outline_construct,
    .destruct_func = outline_destruct,
    .key_size = sizeof(OutlineHashKey),
    .value_size = sizeof(OutlineHashValue)
};


// glyph metric cache
static bool glyph_metrics_key_move(void *dst, void *src)
{
    GlyphMetricsHashKey *d = dst, *s = src;
    if (!d)
        return true;

    *d = *s;
    ass_cache_inc_ref(s->font);
    return true;
}

static void glyph_metrics_destruct(void *key, void *value)
{
    GlyphMetricsHashKey *k = key;
    ass_cache_dec_ref(k->font);
}

size_t ass_glyph_metrics_construct(void *key, void *value, void *priv);

const CacheDesc glyph_metrics_cache_desc = {
    .hash_func = glyph_metrics_hash,
    .compare_func = glyph_metrics_compare,
    .key_move_func = glyph_metrics_key_move,
    .construct_func = ass_glyph_metrics_construct,
    .destruct_func = glyph_metrics_destruct,
    .key_size = sizeof(GlyphMetricsHashKey),
    .value_size = sizeof(FT_Glyph_Metrics)
};



// Cache data
typedef struct cache_item {
    Cache *_Atomic cache;
    const CacheDesc *desc;
    struct cache_item *next, **prev;
    struct cache_item *queue_next, **queue_prev;
    size_t size;
    _Atomic size_t ref_count;
} CacheItem;

struct cache {
    unsigned buckets;
    CacheItem **map;
    CacheItem *queue_first, **queue_last;

    const CacheDesc *desc;

    _Atomic size_t cache_size;
    _Atomic unsigned hits;
    _Atomic unsigned misses;
    _Atomic unsigned items;

#if ENABLE_THREADS
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
#endif
};

#define CACHE_ALIGN 8
#define CACHE_ITEM_SIZE ((sizeof(CacheItem) + (CACHE_ALIGN - 1)) & ~(CACHE_ALIGN - 1))

static inline size_t align_cache(size_t size)
{
    return (size + (CACHE_ALIGN - 1)) & ~(CACHE_ALIGN - 1);
}

static inline CacheItem *value_to_item(void *value)
{
    return (CacheItem *) ((char *) value - CACHE_ITEM_SIZE);
}


// Create a cache with type-specific hash/compare/destruct/size functions
Cache *ass_cache_create(const CacheDesc *desc)
{
    Cache *cache = calloc(1, sizeof(*cache));
    if (!cache)
        return NULL;
    cache->buckets = 0xFFFF;
    cache->queue_last = &cache->queue_first;
    cache->desc = desc;
    cache->map = calloc(cache->buckets, sizeof(CacheItem *));
    if (!cache->map) {
        goto fail;
    }

#if ENABLE_THREADS
    if (pthread_mutex_init(&cache->mutex, NULL) != 0)
        goto fail;

    if (pthread_cond_init(&cache->cond, NULL) != 0) {
        pthread_mutex_destroy(&cache->mutex);
        goto fail;
    }
#endif

    return cache;

fail:
    free(cache->map);
    free(cache);
    return NULL;
}

void *ass_cache_get(Cache *cache, void *key, void *priv)
{
    const CacheDesc *desc = cache->desc;
    size_t key_offs = CACHE_ITEM_SIZE + align_cache(desc->value_size);
    unsigned bucket = desc->hash_func(key, ASS_HASH_INIT) % cache->buckets;

    pthread_mutex_lock(&cache->mutex);

    CacheItem *item = cache->map[bucket];
    while (item) {
        if (desc->compare_func(key, (char *) item + key_offs)) {
            if (!item->queue_prev || item->queue_next) {
                if (item->queue_prev) {
                    item->queue_next->queue_prev = item->queue_prev;
                    *item->queue_prev = item->queue_next;
                } else
                    inc_ref(&item->ref_count);
                *cache->queue_last = item;
                item->queue_prev = cache->queue_last;
                cache->queue_last = &item->queue_next;
                item->queue_next = NULL;
            }
            cache->hits++;
            desc->key_move_func(NULL, key);
            inc_ref(&item->ref_count);

#if ENABLE_THREADS
            while (!item->size)
                pthread_cond_wait(&cache->cond, &cache->mutex);

            pthread_mutex_unlock(&cache->mutex);
#endif

            return (char *) item + CACHE_ITEM_SIZE;
        }
        item = item->next;
    }
    cache->misses++;

    item = malloc(key_offs + desc->key_size);
    if (!item) {
        desc->key_move_func(NULL, key);
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }
    void *new_key = (char *) item + key_offs;
    if (!desc->key_move_func(new_key, key)) {
        free(item);
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }

    item->cache = cache;
    item->desc = desc;
    item->size = 0;

    CacheItem **bucketptr = &cache->map[bucket];
    if (*bucketptr)
        (*bucketptr)->prev = &item->next;
    item->prev = bucketptr;
    item->next = *bucketptr;
    *bucketptr = item;

    *cache->queue_last = item;
    item->queue_prev = cache->queue_last;
    cache->queue_last = &item->queue_next;
    item->queue_next = NULL;
    item->ref_count = 2;

    cache->items++;

    pthread_mutex_unlock(&cache->mutex);

    void *value = (char *) item + CACHE_ITEM_SIZE;
    size_t size = desc->construct_func(new_key, value, priv);
    assert(size);

    pthread_mutex_lock(&cache->mutex);

    cache->cache_size += size;
    item->size = size;

    pthread_mutex_unlock(&cache->mutex);

    pthread_cond_broadcast(&cache->cond);

    return value;
}

void *ass_cache_key(void *value)
{
    CacheItem *item = value_to_item(value);
    return (char *) value + align_cache(item->desc->value_size);
}

static inline void destroy_item(const CacheDesc *desc, CacheItem *item)
{
    assert(item->desc == desc);
    char *value = (char *) item + CACHE_ITEM_SIZE;
    desc->destruct_func(value + align_cache(desc->value_size), value);
    free(item);
}

void ass_cache_inc_ref(void *value)
{
    if (!value)
        return;
    CacheItem *item = value_to_item(value);
    assert(item->size && item->ref_count);
    inc_ref(&item->ref_count);
}

void ass_cache_dec_ref(void *value)
{
    if (!value)
        return;
    CacheItem *item = value_to_item(value);
    assert(item->size && item->ref_count);
    if (dec_ref(&item->ref_count))
        return;

    Cache *cache = item->cache;
    if (cache) {
        pthread_mutex_lock(&cache->mutex);

        if (item->next)
            item->next->prev = item->prev;
        *item->prev = item->next;

        cache->cache_size -= item->size;
        cache->items--;

        pthread_mutex_unlock(&cache->mutex);
    }

    destroy_item(item->desc, item);
}

void ass_cache_cut(Cache *cache, size_t max_size)
{
    if (cache->cache_size <= max_size)
        return;

    pthread_mutex_lock(&cache->mutex);

    do {
        CacheItem *item = cache->queue_first;
        if (!item)
            break;
        assert(item->size);

        cache->queue_first = item->queue_next;
        if (dec_ref(&item->ref_count)) {
            item->queue_prev = NULL;
            continue;
        }

        if (item->next)
            item->next->prev = item->prev;
        *item->prev = item->next;

        cache->items--;
        cache->cache_size -= item->size;

        pthread_mutex_unlock(&cache->mutex);

        destroy_item(cache->desc, item);

        pthread_mutex_lock(&cache->mutex);
    } while (cache->cache_size > max_size);
    if (cache->queue_first)
        cache->queue_first->queue_prev = &cache->queue_first;
    else
        cache->queue_last = &cache->queue_first;

    pthread_mutex_unlock(&cache->mutex);
}

void ass_cache_stats(Cache *cache, size_t *size, unsigned *hits,
                     unsigned *misses, unsigned *count)
{
    if (size)
        *size = cache->cache_size;
    if (hits)
        *hits = cache->hits;
    if (misses)
        *misses = cache->misses;
    if (count)
        *count = cache->items;
}

void ass_cache_empty(Cache *cache)
{
    for (int i = 0; i < cache->buckets; i++) {
        CacheItem *item = cache->map[i];
        while (item) {
            assert(item->size);
            CacheItem *next = item->next;

            if (!item->queue_prev)
                inc_ref(&item->ref_count);
            item->cache = NULL;
            if (!dec_ref(&item->ref_count)) {
                if (item->next)
                    item->next->prev = item->prev;
                *item->prev = item->next;

                destroy_item(cache->desc, item);
                cache->items--;
            }
            item = next;
        }
        cache->map[i] = NULL;
    }

    cache->queue_first = NULL;
    cache->queue_last = &cache->queue_first;
    cache->cache_size = 0;
    cache->hits = cache->misses = 0;
}

void ass_cache_done(Cache *cache)
{
    ass_cache_empty(cache);

    free(cache->map);
    cache->map = NULL;

#if ENABLE_THREADS
    pthread_mutex_destroy(&cache->mutex);
    pthread_cond_destroy(&cache->cond);
#endif

    free(cache);
}

// Type-specific creation function
Cache *ass_font_cache_create(void)
{
    return ass_cache_create(&font_cache_desc);
}

Cache *ass_outline_cache_create(void)
{
    return ass_cache_create(&outline_cache_desc);
}

Cache *ass_glyph_metrics_cache_create(void)
{
    return ass_cache_create(&glyph_metrics_cache_desc);
}

Cache *ass_bitmap_cache_create(void)
{
    return ass_cache_create(&bitmap_cache_desc);
}

Cache *ass_composite_cache_create(void)
{
    return ass_cache_create(&composite_cache_desc);
}

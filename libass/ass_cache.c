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

static void font_key_destruct(void *k)
{
    ASS_FontDesc *key = k;
    free((void *) key->family.str);
}

static void font_destruct(void *value)
{
    ass_font_clear(value);
}

size_t ass_font_construct(void *key, void *value, void *priv);

const CacheDesc font_cache_desc = {
    .hash_func = font_hash,
    .compare_func = font_compare,
    .key_move_func = font_key_move,
    .key_destruct_func = font_key_destruct,
    .construct_func = ass_font_construct,
    .value_destruct_func = font_destruct,
    .key_size = sizeof(ASS_FontDesc),
    .value_size = sizeof(ASS_Font)
};


// bitmap cache
static bool bitmap_key_move(void *dst, void *src)
{
    BitmapHashKey *d = dst, *s = src;
    if (d) {
        *d = *s;
        ass_cache_inc_ref(d->outline);
    }
    return true;
}

static void bitmap_key_destruct(void *key)
{
    BitmapHashKey *k = key;
    ass_cache_dec_ref(k->outline);
}

static void bitmap_destruct(void *value)
{
    ass_free_bitmap(value);
}

size_t ass_bitmap_construct(void *key, void *value, void *priv);

const CacheDesc bitmap_cache_desc = {
    .hash_func = bitmap_hash,
    .compare_func = bitmap_compare,
    .key_move_func = bitmap_key_move,
    .key_destruct_func = bitmap_key_destruct,
    .construct_func = ass_bitmap_construct,
    .value_destruct_func = bitmap_destruct,
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
        for (size_t i = 0; i < d->bitmap_count; i++) {
            ass_cache_inc_ref(d->bitmaps[i].bm);
            ass_cache_inc_ref(d->bitmaps[i].bm_o);
        }
        return true;
    }

    free(s->bitmaps);
    return true;
}

static void composite_key_destruct(void *key)
{
    CompositeHashKey *k = key;
    for (size_t i = 0; i < k->bitmap_count; i++) {
        ass_cache_dec_ref(k->bitmaps[i].bm);
        ass_cache_dec_ref(k->bitmaps[i].bm_o);
    }
    free(k->bitmaps);
}

static void composite_destruct(void *value)
{
    CompositeHashValue *v = value;
    ass_free_bitmap(&v->bm);
    ass_free_bitmap(&v->bm_o);
    ass_free_bitmap(&v->bm_s);
}

size_t ass_composite_construct(void *key, void *value, void *priv);

const CacheDesc composite_cache_desc = {
    .hash_func = composite_hash,
    .compare_func = composite_compare,
    .key_move_func = composite_key_move,
    .key_destruct_func = composite_key_destruct,
    .construct_func = ass_composite_construct,
    .value_destruct_func = composite_destruct,
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
    if (!d)
        return true;

    *d = *s;
    if (s->type == OUTLINE_DRAWING) {
        d->u.drawing.text.str = ass_copy_string(s->u.drawing.text);
        return d->u.drawing.text.str;
    }
    if (s->type == OUTLINE_BORDER)
        ass_cache_inc_ref(s->u.border.outline);
    else if (s->type == OUTLINE_GLYPH)
        ass_cache_inc_ref(s->u.glyph.font);
    return true;
}

static void outline_key_destruct(void *key)
{
    OutlineHashKey *k = key;
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

static void outline_destruct(void *value)
{
    OutlineHashValue *v = value;
    ass_outline_free(&v->outline[0]);
    ass_outline_free(&v->outline[1]);
}

size_t ass_outline_construct(void *key, void *value, void *priv);

const CacheDesc outline_cache_desc = {
    .hash_func = outline_hash,
    .compare_func = outline_compare,
    .key_move_func = outline_key_move,
    .key_destruct_func = outline_key_destruct,
    .construct_func = ass_outline_construct,
    .value_destruct_func = outline_destruct,
    .key_size = sizeof(OutlineHashKey),
    .value_size = sizeof(OutlineHashValue)
};


// font-face size metric cache
static bool face_size_metrics_key_move(void *dst, void *src)
{
    FaceSizeMetricsHashKey *d = dst, *s = src;
    if (!d)
        return true;

    *d = *s;
    ass_cache_inc_ref(s->font);
    return true;
}

static void face_size_metrics_key_destruct(void *key)
{
    FaceSizeMetricsHashKey *k = key;
    ass_cache_dec_ref(k->font);
}

size_t ass_face_size_metrics_construct(void *key, void *value, void *priv);

const CacheDesc face_size_metrics_cache_desc = {
    .hash_func = face_size_metrics_hash,
    .compare_func = face_size_metrics_compare,
    .key_move_func = face_size_metrics_key_move,
    .key_destruct_func = face_size_metrics_key_destruct,
    .construct_func = ass_face_size_metrics_construct,
    .key_size = sizeof(FaceSizeMetricsHashKey),
    .value_size = sizeof(FT_Size_Metrics)
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

static void glyph_metrics_key_destruct(void *key)
{
    GlyphMetricsHashKey *k = key;
    ass_cache_dec_ref(k->font);
}

size_t ass_glyph_metrics_construct(void *key, void *value, void *priv);

const CacheDesc glyph_metrics_cache_desc = {
    .hash_func = glyph_metrics_hash,
    .compare_func = glyph_metrics_compare,
    .key_move_func = glyph_metrics_key_move,
    .key_destruct_func = glyph_metrics_key_destruct,
    .construct_func = ass_glyph_metrics_construct,
    .key_size = sizeof(GlyphMetricsHashKey),
    .value_size = sizeof(FT_Glyph_Metrics)
};



// Cache data
typedef struct cache_item {
    Cache *cache;
    const CacheDesc *desc;
    struct cache_item *_Atomic next, *_Atomic *prev;
    struct cache_item *queue_next, **queue_prev;
    struct cache_item *promote_next;
    _Atomic AtomicInt size, ref_count;
    ass_hashcode hash;

    _Atomic AtomicInt last_used_frame;

#if ENABLE_THREADS
    struct cache_client *creating_client;
#endif
} CacheItem;

struct cache {
    unsigned buckets;
    CacheItem *_Atomic *map;
    CacheItem *queue_first, **queue_last;

    const CacheDesc *desc;

    _Atomic AtomicInt cache_size;

    uintptr_t cur_frame;
};

struct cache_client {
    CacheItem *promote_first;

    struct cache_client *next;

#if ENABLE_THREADS
    pthread_mutex_t mutex;
    pthread_cond_t cond;
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
        free(cache);
        return NULL;
    }

#if ENABLE_THREADS
    for (size_t i = 0; i < cache->buckets; i++)
        atomic_init(&cache->map[i], NULL);

    atomic_init(&cache->cache_size, 0);
#endif

    return cache;
}

bool ass_cache_client_set_init(CacheClientSet *set)
{
    memset(set, 0, sizeof(*set));

#if ENABLE_THREADS
    if (pthread_mutex_init(&set->mutex, NULL) != 0)
        return false;
#endif

    return true;
}

static void cache_client_done(CacheClient *client)
{
#if ENABLE_THREADS
    pthread_mutex_destroy(&client->mutex);
    pthread_cond_destroy(&client->cond);
#endif

    free(client);
}

void ass_cache_client_set_clear(CacheClientSet *set)
{
    CacheClient *client = set->first_client;

    while (client) {
        CacheClient *next = client->next;
        cache_client_done(client);
        client = next;
    }

    set->first_client = NULL;
}

void ass_cache_client_set_done(CacheClientSet *set)
{
    ass_cache_client_set_clear(set);

#if ENABLE_THREADS
    pthread_mutex_destroy(&set->mutex);
#endif
}

CacheClient *ass_cache_client_create(CacheClientSet *set)
{
    CacheClient *client = calloc(1, sizeof(*client));
    if (!client)
        return NULL;

#if ENABLE_THREADS
    if (pthread_mutex_init(&client->mutex, NULL) != 0) {
        free(client);
        return NULL;
    }

    if (pthread_cond_init(&client->cond, NULL) != 0) {
        pthread_cond_destroy(&client->cond);
        free(client);
        return NULL;
    }

    pthread_mutex_lock(&set->mutex);
#endif

    client->next = set->first_client;
    set->first_client = client;

#if ENABLE_THREADS
    pthread_mutex_unlock(&set->mutex);
#endif

    return client;
}

// Retrieve a value corresponding to a particular cache key,
// creating one if it does not already exist.
// The returned item is guaranteed to be valid until the next ass_cache_cut call;
// to extend its lifetime further, call ass_cache_inc_ref().
void *ass_cache_get(Cache *cache, CacheClient *client, void *key, void *priv)
{
    const CacheDesc *desc = cache->desc;
    size_t key_offs = CACHE_ITEM_SIZE + align_cache(desc->value_size);
    ass_hashcode hash = desc->hash_func(key, ASS_HASH_INIT);
    unsigned bucket = hash % cache->buckets;

    CacheItem *_Atomic *bucketptr = &cache->map[bucket];
    CacheItem *stop_at = NULL;
    CacheItem *item, *new_item = NULL;
    void *new_key = NULL;
    CacheItem *start = atomic_load_explicit(bucketptr, memory_order_acquire);

retry:
    for (item = start; item && item != stop_at; item = atomic_load_explicit(&item->next, memory_order_acquire)) {
        if (item->hash == hash && desc->compare_func(key, (char *)item + key_offs))
            break;
    }

    if (item != NULL && item != stop_at) {
        if (atomic_load_explicit(&item->last_used_frame, memory_order_consume) != cache->cur_frame) {
            uintptr_t last_used = atomic_exchange_explicit(&item->last_used_frame, cache->cur_frame, memory_order_consume);

            if (last_used != cache->cur_frame) {
                ass_assert1(!item->promote_next);
                item->promote_next = client->promote_first;
                client->promote_first = item;
            }
        }

        if (!new_item)
            desc->key_move_func(NULL, key);
        else if (desc->key_destruct_func)
            desc->key_destruct_func(key);

#if ENABLE_THREADS
        if (!atomic_load_explicit(&item->size, memory_order_acquire)) {
            pthread_mutex_lock(&item->creating_client->mutex);

            while (!atomic_load_explicit(&item->size, memory_order_relaxed))
                pthread_cond_wait(&item->creating_client->cond, &item->creating_client->mutex);

            pthread_mutex_unlock(&item->creating_client->mutex);
        }

        free(new_item);
#endif

        return (char *) item + CACHE_ITEM_SIZE;
    }

    stop_at = start;

    if (!new_item) {
        // Risk of cache miss. Set up a new item to insert if we win the race.
        new_item = malloc(key_offs + desc->key_size);
        if (!new_item) {
            desc->key_move_func(NULL, key);
            return NULL;
        }

        new_key = (char *) new_item + key_offs;
        if (!desc->key_move_func(new_key, key)) {
            free(new_item);
            return NULL;
        }

        key = new_key;

        new_item->cache = cache;
        new_item->desc = desc;
        atomic_init(&new_item->size, 0);
        new_item->hash = hash;
        atomic_init(&new_item->last_used_frame, cache->cur_frame);
        atomic_init(&new_item->ref_count, 1);
        atomic_init(&new_item->next, NULL);
        new_item->queue_next = NULL;
        new_item->queue_prev = NULL;
#if ENABLE_THREADS
        new_item->creating_client = client;
#endif
        new_item->promote_next = client->promote_first;
    }

    atomic_store_explicit(&new_item->next, start, memory_order_release);
    new_item->prev = bucketptr;

    if (!atomic_compare_exchange_weak_explicit(bucketptr, &start, new_item, memory_order_acq_rel, memory_order_acquire))
        goto retry;

    // We won the race; finish inserting our new item
    if (start)
        start->prev = &new_item->next;

    item = new_item;

    client->promote_first = item;

    void *value = (char *) item + CACHE_ITEM_SIZE;
    size_t size = desc->construct_func(new_key, value, priv);
    ass_assert1(size);

    atomic_fetch_add_explicit(&cache->cache_size, size + (size == 1 ? 0 : CACHE_ITEM_SIZE), memory_order_relaxed);

#if ENABLE_THREADS
    pthread_mutex_lock(&client->mutex);
#endif

    atomic_store_explicit(&item->size, size, memory_order_release);

#if ENABLE_THREADS
    pthread_mutex_unlock(&client->mutex);
    pthread_cond_broadcast(&client->cond);
#endif

    return value;
}

void *ass_cache_key(void *value)
{
    CacheItem *item = value_to_item(value);
    return (char *) value + align_cache(item->desc->value_size);
}

static inline void destroy_item(const CacheDesc *desc, CacheItem *item)
{
    ass_assert1(item->desc == desc);
    ass_assert2(!atomic_load_explicit(&item->next, memory_order_acquire) && !item->prev);
    char *value = (char *) item + CACHE_ITEM_SIZE;
    if (desc->key_destruct_func)
        desc->key_destruct_func(value + align_cache(desc->value_size));
    if (desc->value_destruct_func)
        desc->value_destruct_func(value);
    free(item);
}

void ass_cache_inc_ref(void *value)
{
    if (!value)
        return;
    CacheItem *item = value_to_item(value);
    ass_assert2(atomic_load_explicit(&item->size, memory_order_acquire) && atomic_load_explicit(&item->ref_count, memory_order_acquire));
    inc_ref(&item->ref_count);
}

static void dec_ref_item(CacheItem *item)
{
    ass_assert2(atomic_load_explicit(&item->size, memory_order_acquire) && atomic_load_explicit(&item->ref_count, memory_order_acquire));
    if (dec_ref(&item->ref_count) == 0)
        destroy_item(item->desc, item);
}

void ass_cache_dec_ref(void *value)
{
    if (!value)
        return;
    CacheItem *item = value_to_item(value);
    dec_ref_item(item);
}

void ass_cache_promote(CacheClientSet *set)
{
    for (CacheClient *client = set->first_client; client; client = client->next) {
        while (client->promote_first) {
            CacheItem *item = client->promote_first;
            ass_assert2(item->prev);
            ass_assert2(item->queue_prev || !item->queue_next);
            ass_assert2(item->cache->queue_last);
            ass_assert2(item->queue_next != item);
            ass_assert2(item->queue_prev != &item->queue_next);

            // Skip if we're already at the end of the queue
            if (!item->queue_prev || item->queue_next) {
                if (item->queue_prev)
                    *item->queue_prev = item->queue_next;
                if (item->queue_next)
                    item->queue_next->queue_prev = item->queue_prev;
                item->queue_next = NULL;

                item->queue_prev = item->cache->queue_last;
                *item->cache->queue_last = item;
                item->cache->queue_last = &item->queue_next;

                ass_assert2(item->queue_prev != &item->queue_next);
            }

            client->promote_first = item->promote_next;
            item->promote_next = NULL;
        }
    }
}

void ass_cache_cut(Cache *cache, size_t max_size)
{
    ass_assert1(cache->queue_last);

    while (atomic_load_explicit(&cache->cache_size, memory_order_relaxed) > max_size && cache->queue_first) {
        CacheItem *item = cache->queue_first;
        ass_assert2(item->prev);
        ass_assert2(atomic_load_explicit(&item->size, memory_order_acquire));
        ass_assert2(item->queue_prev == &cache->queue_first);
        ass_assert2(item->queue_next != item);
        ass_assert2(item->queue_prev != &item->queue_next);
        ass_assert2(!item->promote_next);

        if (atomic_load_explicit(&item->last_used_frame, memory_order_relaxed) == cache->cur_frame) {
            // everything after this must have been last used this frame
            break;
        }

        if (item->queue_next) {
            ass_assert2(item->queue_next->queue_prev != &item->queue_next->queue_next);
            item->queue_next->queue_prev = &cache->queue_first;
        } else {
            cache->queue_last = &cache->queue_first;
        }

        cache->queue_first = item->queue_next;

        CacheItem *item_next = atomic_load_explicit(&item->next, memory_order_relaxed);
        if (item_next)
            item_next->prev = item->prev;
        atomic_store_explicit(item->prev, item_next, memory_order_relaxed);

        atomic_store_explicit(&item->next, NULL, memory_order_relaxed);
        item->prev = NULL;
        item->queue_prev = NULL;
        item->queue_next = NULL;

        uintptr_t item_size = atomic_load_explicit(&item->size, memory_order_relaxed);
        atomic_fetch_sub_explicit(&cache->cache_size, item_size + (item_size == 1 ? 0 : CACHE_ITEM_SIZE), memory_order_relaxed);

        dec_ref_item(item);
    }

    cache->cur_frame++;
}

void ass_cache_empty(Cache *cache)
{
    for (int i = 0; i < cache->buckets; i++) {
        CacheItem *item = atomic_load_explicit(&cache->map[i], memory_order_relaxed);
        while (item) {
            ass_assert2(item->queue_prev || !item->queue_next);
            ass_assert2(item->prev);
            ass_assert2(atomic_load_explicit(&item->size, memory_order_acquire));
            CacheItem *next = atomic_load_explicit(&item->next, memory_order_relaxed);

            atomic_store_explicit(&item->next, NULL, memory_order_relaxed);
            item->prev = NULL;
            item->queue_prev = NULL;
            item->queue_next = NULL;
            item->promote_next = NULL;

            uintptr_t item_size = atomic_load_explicit(&item->size, memory_order_relaxed);
            atomic_fetch_sub_explicit(&cache->cache_size, item_size + (item_size == 1 ? 0 : CACHE_ITEM_SIZE), memory_order_relaxed);

            dec_ref_item(item);

            item = next;
        }
        atomic_store_explicit(&cache->map[i], NULL, memory_order_release);
    }

    ass_assert2(!atomic_load_explicit(&cache->cache_size, memory_order_acquire));

    cache->queue_first = NULL;
    cache->queue_last = &cache->queue_first;
    atomic_store_explicit(&cache->cache_size, 0, memory_order_release);
}

void ass_cache_done(Cache *cache)
{
    ass_cache_empty(cache);
    free(cache->map);
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

Cache *ass_face_size_metrics_cache_create(void)
{
    return ass_cache_create(&face_size_metrics_cache_desc);
}

Cache *ass_bitmap_cache_create(void)
{
    return ass_cache_create(&bitmap_cache_desc);
}

Cache *ass_composite_cache_create(void)
{
    return ass_cache_create(&composite_cache_desc);
}

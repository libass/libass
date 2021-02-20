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

#ifndef LIBASS_CACHE_H
#define LIBASS_CACHE_H

#include "ass.h"
#include "ass_font.h"
#include "ass_outline.h"
#include "ass_bitmap.h"

typedef struct cache Cache;
typedef uint64_t ass_hashcode;

// cache values

typedef struct {
    Bitmap bm, bm_o, bm_s;
} CompositeHashValue;

typedef struct {
    bool valid;
    ASS_Outline outline[2];
    ASS_Rect cbox;  // bounding box of all control points
    int advance;    // 26.6, advance distance to the next outline in line
    int asc, desc;  // ascender/descender
} OutlineHashValue;

// Create definitions for bitmap, outline and composite hash keys
#define CREATE_STRUCT_DEFINITIONS
#include "ass_cache_template.h"

// Type-specific function pointers
typedef ass_hashcode (*HashFunction)(void *key, ass_hashcode hval);
typedef bool (*HashCompare)(void *a, void *b);
typedef bool (*CacheKeyMove)(void *dst, void *src);
typedef size_t (*CacheValueConstructor)(void *key, void *value, void *priv);
typedef void (*CacheItemDestructor)(void *key, void *value);

// cache hash keys

typedef struct outline_hash_key {
    enum {
        OUTLINE_GLYPH,
        OUTLINE_DRAWING,
        OUTLINE_BORDER,
        OUTLINE_BOX,
    } type;
    union {
        GlyphHashKey glyph;
        DrawingHashKey drawing;
        BorderHashKey border;
    } u;
} OutlineHashKey;

enum {
    FILTER_BORDER_STYLE_3 = 0x01,
    FILTER_NONZERO_BORDER = 0x02,
    FILTER_NONZERO_SHADOW = 0x04,
    FILTER_FILL_IN_SHADOW = 0x08,
    FILTER_FILL_IN_BORDER = 0x10,
};

typedef struct {
    FilterDesc filter;
    size_t bitmap_count;
    BitmapRef *bitmaps;
} CompositeHashKey;

typedef struct
{
    HashFunction hash_func;
    HashCompare compare_func;
    CacheKeyMove key_move_func;
    CacheValueConstructor construct_func;
    CacheItemDestructor destruct_func;
    size_t key_size;
    size_t value_size;
} CacheDesc;

Cache *ass_cache_create(const CacheDesc *desc);
void *ass_cache_get(Cache *cache, void *key, void *priv);
void *ass_cache_key(void *value);
void ass_cache_inc_ref(void *value);
void ass_cache_dec_ref(void *value);
void ass_cache_cut(Cache *cache, size_t max_size);
void ass_cache_stats(Cache *cache, size_t *size, unsigned *hits,
                     unsigned *misses, unsigned *count);
void ass_cache_empty(Cache *cache);
void ass_cache_done(Cache *cache);
Cache *ass_font_cache_create(void);
Cache *ass_outline_cache_create(void);
Cache *ass_glyph_metrics_cache_create(void);
Cache *ass_bitmap_cache_create(void);
Cache *ass_composite_cache_create(void);

#endif                          /* LIBASS_CACHE_H */

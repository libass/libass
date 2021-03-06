/*
 * Copyright (C) 2021 rcombs <rcombs@rcombs.me>
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

#ifndef LIBASS_BITMAP_ENGINE_H
#define LIBASS_BITMAP_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "ass_cpu.h"

struct segment;
typedef void (*FillSolidTileFunc)(uint8_t *buf, ptrdiff_t stride, int set);
typedef void (*FillHalfplaneTileFunc)(uint8_t *buf, ptrdiff_t stride,
                                      int32_t a, int32_t b, int64_t c, int32_t scale);
typedef void (*FillGenericTileFunc)(uint8_t *buf, ptrdiff_t stride,
                                    const struct segment *line, size_t n_lines,
                                    int winding);

typedef void (*BitmapBlendFunc)(uint8_t *dst, intptr_t dst_stride,
                                uint8_t *src, intptr_t src_stride,
                                intptr_t height, intptr_t width);
typedef void (*BitmapMulFunc)(uint8_t *dst, intptr_t dst_stride,
                              uint8_t *src1, intptr_t src1_stride,
                              uint8_t *src2, intptr_t src2_stride,
                              intptr_t width, intptr_t height);

typedef void (*BeBlurFunc)(uint8_t *buf, intptr_t w, intptr_t h,
                           intptr_t stride, uint16_t *tmp);

// intermediate bitmaps represented as sets of verical stripes of int16_t[alignment / 2]
typedef void (*Convert8to16Func)(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
                                 uintptr_t width, uintptr_t height);
typedef void (*Convert16to8Func)(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
                                 uintptr_t width, uintptr_t height);
typedef void (*FilterFunc)(int16_t *dst, const int16_t *src,
                           uintptr_t src_width, uintptr_t src_height);
typedef void (*ParamFilterFunc)(int16_t *dst, const int16_t *src,
                                uintptr_t src_width, uintptr_t src_height,
                                const int16_t *param);

#if (defined(__i386__) || defined(__x86_64__))
#define ASS_ALIGNMENT 32
#else
#define ASS_ALIGNMENT 16
#endif

typedef struct {
    // rasterizer functions
    int tile_order;  // log2(tile_size)
    FillSolidTileFunc fill_solid;
    FillHalfplaneTileFunc fill_halfplane;
    FillGenericTileFunc fill_generic;

    // blend functions
    BitmapBlendFunc add_bitmaps, sub_bitmaps;
    BitmapMulFunc mul_bitmaps;

    // be blur function
    BeBlurFunc be_blur;

    // gaussian blur functions
    Convert8to16Func stripe_unpack;
    Convert16to8Func stripe_pack;
    FilterFunc shrink_horz, shrink_vert;
    FilterFunc expand_horz, expand_vert;
    ParamFilterFunc blur_horz[5], blur_vert[5];
} BitmapEngine;

void ass_bitmap_engine_init(BitmapEngine* engine, ASS_CPUFlags mask);

#endif /* LIBASS_BITMAP_ENGINE_H */

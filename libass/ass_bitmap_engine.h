/*
 * Copyright (C) 2021-2022 libass contributors
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

struct segment;
typedef void FillSolidTileFunc(uint8_t *buf, ptrdiff_t stride, int set);
typedef void FillHalfplaneTileFunc(uint8_t *buf, ptrdiff_t stride,
                                   int32_t a, int32_t b, int64_t c, int32_t scale);
typedef void FillGenericTileFunc(uint8_t *buf, ptrdiff_t stride,
                                 const struct segment *line, size_t n_lines,
                                 int winding);
typedef void MergeTileFunc(uint8_t *buf, ptrdiff_t stride, const uint8_t *tile);

typedef void BitmapBlendFunc(uint8_t *dst, ptrdiff_t dst_stride,
                             const uint8_t *src, ptrdiff_t src_stride,
                             size_t width, size_t height);
typedef void BitmapMulFunc(uint8_t *dst, ptrdiff_t dst_stride,
                           const uint8_t *src1, ptrdiff_t src1_stride,
                           const uint8_t *src2, ptrdiff_t src2_stride,
                           size_t width, size_t height);

typedef void BeBlurFunc(uint8_t *buf, ptrdiff_t stride,
                        size_t width, size_t height, uint16_t *tmp);

// intermediate bitmaps represented as sets of verical stripes of int16_t[alignment / 2]
typedef void Convert8to16Func(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
                              size_t width, size_t height);
typedef void Convert16to8Func(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
                              size_t width, size_t height);
typedef void FilterFunc(int16_t *dst, const int16_t *src,
                        size_t src_width, size_t src_height);
typedef void ParamFilterFunc(int16_t *dst, const int16_t *src,
                             size_t src_width, size_t src_height,
                             const int16_t *param);

typedef struct {
    int align_order;  // log2(alignment)

    // rasterizer functions
    int tile_order;  // log2(tile_size)
    FillSolidTileFunc *fill_solid;
    FillHalfplaneTileFunc *fill_halfplane;
    FillGenericTileFunc *fill_generic;
    MergeTileFunc *merge;

    // blend functions
    BitmapBlendFunc *add_bitmaps, *imul_bitmaps;
    BitmapMulFunc *mul_bitmaps;

    // be blur function
    BeBlurFunc *be_blur;

    // gaussian blur functions
    Convert8to16Func *stripe_unpack;
    Convert16to8Func *stripe_pack;
    FilterFunc *shrink_horz, *shrink_vert;
    FilterFunc *expand_horz, *expand_vert;
    ParamFilterFunc *blur_horz[5], *blur_vert[5];
} BitmapEngine;

enum {
    ASS_CPU_FLAG_NONE          = 0x0000,
#if ARCH_X86
    ASS_CPU_FLAG_X86_SSE2      = 0x0001,
    ASS_CPU_FLAG_X86_SSSE3     = 0x0002,
    ASS_CPU_FLAG_X86_AVX2      = 0x0004,
#elif ARCH_AARCH64
    ASS_CPU_FLAG_ARM_NEON      = 0x0001,
#endif
    ASS_CPU_FLAG_ALL           = 0x0FFF,
    ASS_FLAG_LARGE_TILES       = 0x1000,
    ASS_FLAG_WIDE_STRIPE       = 0x2000,  // for C version only
};

unsigned ass_get_cpu_flags(unsigned mask);

BitmapEngine ass_bitmap_engine_init(unsigned mask);

#endif /* LIBASS_BITMAP_ENGINE_H */

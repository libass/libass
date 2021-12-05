/*
 * Copyright (C) 2014-2022 libass contributors
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

#include <stddef.h>
#include <stdint.h>

#include "ass_utils.h"


/**
 * \brief Add two bitmaps together at a given position
 * Uses additive blending, clipped to [0,255]. Pure C implementation.
 */
void ass_add_bitmaps_c(uint8_t *dst, ptrdiff_t dst_stride,
                       const uint8_t *src, ptrdiff_t src_stride,
                       size_t width, size_t height)
{
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        for (size_t x = 0; x < width; x++) {
            unsigned out = dst[x] + src[x];
            dst[x] = FFMIN(out, 255);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

void ass_imul_bitmaps_c(uint8_t *dst, ptrdiff_t dst_stride,
                        const uint8_t *src, ptrdiff_t src_stride,
                        size_t width, size_t height)
{
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        for (size_t x = 0; x < width; x++) {
            dst[x] = (dst[x] * (255 - src[x]) + 255) >> 8;
        }
        dst += dst_stride;
        src += src_stride;
    }
}

void ass_mul_bitmaps_c(uint8_t *dst, ptrdiff_t dst_stride,
                       const uint8_t *src1, ptrdiff_t src1_stride,
                       const uint8_t *src2, ptrdiff_t src2_stride,
                       size_t width, size_t height)
{
    uint8_t *end = dst + dst_stride * height;
    while (dst < end) {
        for (size_t x = 0; x < width; x++) {
            dst[x] = (src1[x] * src2[x] + 255) >> 8;
        }
        dst  += dst_stride;
        src1 += src1_stride;
        src2 += src2_stride;
    }
}

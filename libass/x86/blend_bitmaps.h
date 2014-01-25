/*
 * Copyright (C) 2013 Rodger Combs <rcombs@rcombs.me>
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

#ifndef INTEL_BLEND_BITMAPS_H
#define INTEL_BLEND_BITMAPS_H

void ass_add_bitmaps_avx2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width );

void ass_add_bitmaps_sse2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width );

void ass_add_bitmaps_x86( uint8_t *dst, intptr_t dst_stride,
                          uint8_t *src, intptr_t src_stride,
                          intptr_t height, intptr_t width );

void ass_sub_bitmaps_avx2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width );

void ass_sub_bitmaps_sse2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width );

void ass_sub_bitmaps_x86( uint8_t *dst, intptr_t dst_stride,
                          uint8_t *src, intptr_t src_stride,
                          intptr_t height, intptr_t width );

void ass_mul_bitmaps_avx2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src1, intptr_t src1_stride,
                           uint8_t *src2, intptr_t src2_stride,
                           intptr_t width, intptr_t height );

void ass_mul_bitmaps_sse2( uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src1, intptr_t src1_stride,
                           uint8_t *src2, intptr_t src2_stride,
                           intptr_t width, intptr_t height );

#endif

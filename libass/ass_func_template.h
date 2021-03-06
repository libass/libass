/*
 * Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
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

#define DECORATE_SUFFIX2(name, suffix) ass_##name##_##suffix
#define DECORATE_SUFFIX(name, suffix) DECORATE_SUFFIX2(name, suffix)
#define DECORATE(name) DECORATE_SUFFIX(name, ENGINE_SUFFIX)

void DECORATE(fill_solid_tile16)(uint8_t *buf, ptrdiff_t stride, int set);
void DECORATE(fill_solid_tile32)(uint8_t *buf, ptrdiff_t stride, int set);
void DECORATE(fill_halfplane_tile16)(uint8_t *buf, ptrdiff_t stride,
                                     int32_t a, int32_t b, int64_t c, int32_t scale);
void DECORATE(fill_halfplane_tile32)(uint8_t *buf, ptrdiff_t stride,
                                     int32_t a, int32_t b, int64_t c, int32_t scale);
void DECORATE(fill_generic_tile16)(uint8_t *buf, ptrdiff_t stride,
                                   const struct segment *line, size_t n_lines,
                                   int winding);
void DECORATE(fill_generic_tile32)(uint8_t *buf, ptrdiff_t stride,
                                   const struct segment *line, size_t n_lines,
                                   int winding);

void DECORATE(add_bitmaps)(uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width);
void DECORATE(sub_bitmaps)(uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src, intptr_t src_stride,
                           intptr_t height, intptr_t width);
void DECORATE(mul_bitmaps)(uint8_t *dst, intptr_t dst_stride,
                           uint8_t *src1, intptr_t src1_stride,
                           uint8_t *src2, intptr_t src2_stride,
                           intptr_t width, intptr_t height);

void DECORATE(be_blur)(uint8_t *buf, intptr_t w, intptr_t h,
                       intptr_t stride, uint16_t *tmp);

void DECORATE(stripe_unpack)(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
                             uintptr_t width, uintptr_t height);
void DECORATE(stripe_pack)(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
                           uintptr_t width, uintptr_t height);
void DECORATE(shrink_horz)(int16_t *dst, const int16_t *src,
                           uintptr_t src_width, uintptr_t src_height);
void DECORATE(shrink_vert)(int16_t *dst, const int16_t *src,
                           uintptr_t src_width, uintptr_t src_height);
void DECORATE(expand_horz)(int16_t *dst, const int16_t *src,
                           uintptr_t src_width, uintptr_t src_height);
void DECORATE(expand_vert)(int16_t *dst, const int16_t *src,
                           uintptr_t src_width, uintptr_t src_height);
void DECORATE(blur4_horz)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur4_vert)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur5_horz)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur5_vert)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur6_horz)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur6_vert)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur7_horz)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur7_vert)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur8_horz)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);
void DECORATE(blur8_vert)(int16_t *dst, const int16_t *src,
                          uintptr_t src_width, uintptr_t src_height,
                          const int16_t *param);

#undef DECORATE_SUFFIX2
#undef DECORATE_SUFFIX
#undef DECORATE

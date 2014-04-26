/*
 * Copyright (C) 2014 Vabishchevich Nikolay <vabnick@gmail.com>
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

#ifndef X86_RASTERIZER_H
#define X86_RASTERIZER_H

#include <stddef.h>
#include <stdint.h>


struct segment;

void ass_fill_solid_tile16_sse2(uint8_t *buf, ptrdiff_t stride);
void ass_fill_solid_tile32_sse2(uint8_t *buf, ptrdiff_t stride);
void ass_fill_halfplane_tile16_sse2(uint8_t *buf, ptrdiff_t stride,
                                    int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_halfplane_tile32_sse2(uint8_t *buf, ptrdiff_t stride,
                                    int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_generic_tile16_sse2(uint8_t *buf, ptrdiff_t stride,
                                  const struct segment *line, size_t n_lines,
                                  int winding);
void ass_fill_generic_tile32_sse2(uint8_t *buf, ptrdiff_t stride,
                                  const struct segment *line, size_t n_lines,
                                  int winding);

void ass_fill_solid_tile16_avx2(uint8_t *buf, ptrdiff_t stride);
void ass_fill_solid_tile32_avx2(uint8_t *buf, ptrdiff_t stride);
void ass_fill_halfplane_tile16_avx2(uint8_t *buf, ptrdiff_t stride,
                                    int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_halfplane_tile32_avx2(uint8_t *buf, ptrdiff_t stride,
                                    int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_generic_tile16_avx2(uint8_t *buf, ptrdiff_t stride,
                                  const struct segment *line, size_t n_lines,
                                  int winding);
void ass_fill_generic_tile32_avx2(uint8_t *buf, ptrdiff_t stride,
                                  const struct segment *line, size_t n_lines,
                                  int winding);


#endif                          /* X86_RASTERIZER_H */


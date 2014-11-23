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

#ifndef LIBASS_RASTERIZER_H
#define LIBASS_RASTERIZER_H

#include <stddef.h>
#include <stdint.h>

#include "ass.h"
#include "ass_font.h"


enum {
    SEGFLAG_DN = 1,
    SEGFLAG_UL_DR = 2,
    SEGFLAG_EXACT_LEFT = 4,
    SEGFLAG_EXACT_RIGHT = 8,
    SEGFLAG_EXACT_TOP = 16,
    SEGFLAG_EXACT_BOTTOM = 32,
};

// Polyline segment struct
struct segment {
    int64_t c;
    int32_t a, b, scale, flags;
    int32_t x_min, x_max, y_min, y_max;
};


typedef void (*FillSolidTileFunc)(uint8_t *buf, ptrdiff_t stride);
typedef void (*FillHalfplaneTileFunc)(uint8_t *buf, ptrdiff_t stride,
                                      int32_t a, int32_t b, int64_t c, int32_t scale);
typedef void (*FillGenericTileFunc)(uint8_t *buf, ptrdiff_t stride,
                                    const struct segment *line, size_t n_lines,
                                    int winding);

void ass_fill_solid_tile16_c(uint8_t *buf, ptrdiff_t stride);
void ass_fill_solid_tile32_c(uint8_t *buf, ptrdiff_t stride);
void ass_fill_halfplane_tile16_c(uint8_t *buf, ptrdiff_t stride,
                                 int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_halfplane_tile32_c(uint8_t *buf, ptrdiff_t stride,
                                 int32_t a, int32_t b, int64_t c, int32_t scale);
void ass_fill_generic_tile16_c(uint8_t *buf, ptrdiff_t stride,
                               const struct segment *line, size_t n_lines,
                               int winding);
void ass_fill_generic_tile32_c(uint8_t *buf, ptrdiff_t stride,
                               const struct segment *line, size_t n_lines,
                               int winding);

typedef struct ass_rasterizer {
    int outline_error;  // acceptable error (in 1/64 pixel units)

    int tile_order;  // log2(tile_size)
    FillSolidTileFunc fill_solid;
    FillHalfplaneTileFunc fill_halfplane;
    FillGenericTileFunc fill_generic;

    int32_t x_min, x_max, y_min, y_max;  // usable after rasterizer_set_outline

    // internal buffers
    struct segment *linebuf[2];
    size_t size[2], capacity[2];
} ASS_Rasterizer;

void rasterizer_init(ASS_Rasterizer *rst);
void rasterizer_done(ASS_Rasterizer *rst);
/**
 * \brief Convert FreeType outline to polyline and calculate exact bounds
 */
int rasterizer_set_outline(ASS_Rasterizer *rst, const ASS_Outline *path);
/**
 * \brief Polyline rasterization function
 * \param x0, y0, width, height in: source window (full pixel units)
 * \param buf out: aligned output buffer (size = stride * height)
 * \param stride output buffer stride (aligned)
 * \return zero on error
 * Deletes preprocessed polyline after work.
 */
int rasterizer_fill(ASS_Rasterizer *rst, uint8_t *buf, int x0, int y0,
                    int width, int height, ptrdiff_t stride);


#endif                          /* LIBASS_RASTERIZER_H */

/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
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

#ifndef LIBASS_BITMAP_H
#define LIBASS_BITMAP_H

#include <stdbool.h>
#include <ft2build.h>
#include FT_GLYPH_H

#include "ass.h"

typedef struct ass_synth_priv ASS_SynthPriv;

ASS_SynthPriv *ass_synth_init(double);
void ass_synth_done(ASS_SynthPriv *priv);

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

#define C_ALIGN_ORDER 5

typedef struct {
    int align_order;  // log2(alignment)

    // rasterizer functions
#if CONFIG_RASTERIZER
    int tile_order;  // log2(tile_size)
    FillSolidTileFunc fill_solid;
    FillHalfplaneTileFunc fill_halfplane;
    FillGenericTileFunc fill_generic;
#endif

    // blend functions
    BitmapBlendFunc add_bitmaps, sub_bitmaps;
    BitmapMulFunc mul_bitmaps;

    // be blur function
    BeBlurFunc be_blur;
} BitmapEngine;

extern const BitmapEngine ass_bitmap_engine_c;
extern const BitmapEngine ass_bitmap_engine_sse2;
extern const BitmapEngine ass_bitmap_engine_avx2;


typedef struct {
    size_t n_contours, max_contours;
    size_t *contours;
    size_t n_points, max_points;
    FT_Vector *points;
    char *tags;
} ASS_Outline;

#define EFFICIENT_CONTOUR_COUNT 8

typedef struct {
    int left, top;
    int w, h;                   // width, height
    int stride;
    unsigned char *buffer;      // h * stride buffer
} Bitmap;

Bitmap *alloc_bitmap(const BitmapEngine *engine, int w, int h);
Bitmap *copy_bitmap(const BitmapEngine *engine, const Bitmap *src);
void ass_free_bitmap(Bitmap *bm);

Bitmap *outline_to_bitmap(ASS_Renderer *render_priv,
                          ASS_Outline *outline, int bord);

void ass_synth_blur(const BitmapEngine *engine,
                    ASS_SynthPriv *priv_blur, int opaque_box, int be,
                    double blur_radius, Bitmap *bm_g, Bitmap *bm_o);

/**
 * \brief perform glyph rendering
 * \param outline original glyph
 * \param border "border" glyph, produced from outline by FreeType's glyph stroker
 * \param bm_g out: pointer to the bitmap of original glyph is returned here
 * \param bm_o out: pointer to the bitmap of border glyph is returned here
 */
int outline_to_bitmap2(ASS_Renderer *render_priv,
                       ASS_Outline *outline, ASS_Outline *border,
                       Bitmap **bm_g, Bitmap **bm_o);

void ass_gauss_blur(unsigned char *buffer, unsigned *tmp2,
                    int width, int height, int stride,
                    unsigned *m2, int r, int mwidth);
int be_padding(int be);
void be_blur_pre(uint8_t *buf, intptr_t w,
                 intptr_t h, intptr_t stride);
void be_blur_post(uint8_t *buf, intptr_t w,
                  intptr_t h, intptr_t stride);
void shift_bitmap(Bitmap *bm, int shift_x, int shift_y);
void fix_outline(Bitmap *bm_g, Bitmap *bm_o);

#endif                          /* LIBASS_BITMAP_H */

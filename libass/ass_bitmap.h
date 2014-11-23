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

#include <ft2build.h>
#include FT_GLYPH_H

#include "ass.h"

typedef struct ass_synth_priv ASS_SynthPriv;

ASS_SynthPriv *ass_synth_init(double);
void ass_synth_done(ASS_SynthPriv *priv);

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

Bitmap *outline_to_bitmap(ASS_Renderer *render_priv,
                          ASS_Outline *outline, int bord);

Bitmap *alloc_bitmap(int w, int h);

void ass_synth_blur(ASS_SynthPriv *priv_blur, int opaque_box, int be,
                    double blur_radius, Bitmap *bm_g, Bitmap *bm_o);

/**
 * \brief perform glyph rendering
 * \param glyph original glyph
 * \param outline_glyph "border" glyph, produced from original by FreeType's glyph stroker
 * \param bm_g out: pointer to the bitmap of original glyph is returned here
 * \param bm_o out: pointer to the bitmap of outline (border) glyph is returned here
 * \param bm_g out: pointer to the bitmap of glyph shadow is returned here
 * \param be 1 = produces blurred bitmaps, 0 = normal bitmaps
 * \param border_visible whether border is visible if border_style is 3
 */
int outline_to_bitmap3(ASS_Renderer *render_priv, ASS_Outline *outline, ASS_Outline *border,
                       Bitmap **bm_g, Bitmap **bm_o, Bitmap **bm_s,
                       int be, double blur_radius, FT_Vector shadow_offset,
                       int border_style, int border_visible);

void ass_free_bitmap(Bitmap *bm);
void ass_gauss_blur(unsigned char *buffer, unsigned *tmp2,
                    int width, int height, int stride,
                    unsigned *m2, int r, int mwidth);
void be_blur_c(uint8_t *buf, intptr_t w,
               intptr_t h, intptr_t stride,
               uint16_t *tmp);
void add_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src, intptr_t src_stride,
                   intptr_t height, intptr_t width);
void sub_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src, intptr_t src_stride,
                   intptr_t height, intptr_t width);
void mul_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src1, intptr_t src1_stride,
                   uint8_t *src2, intptr_t src2_stride,
                   intptr_t w, intptr_t h);
void shift_bitmap(Bitmap *bm, int shift_x, int shift_y);
void fix_outline(Bitmap *bm_g, Bitmap *bm_o);
Bitmap *copy_bitmap(const Bitmap *src);

#endif                          /* LIBASS_BITMAP_H */

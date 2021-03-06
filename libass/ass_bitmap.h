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
#include "ass_cpu.h"
#include "ass_outline.h"
#include "ass_bitmap_engine.h"

typedef struct {
    int32_t left, top;
    int32_t w, h;         // width, height
    ptrdiff_t stride;
    uint8_t *buffer;      // h * stride buffer
} Bitmap;

bool alloc_bitmap(const BitmapEngine *engine, Bitmap *bm, int32_t w, int32_t h, bool zero);
bool realloc_bitmap(const BitmapEngine *engine, Bitmap *bm, int32_t w, int32_t h);
bool copy_bitmap(const BitmapEngine *engine, Bitmap *dst, const Bitmap *src);
void ass_free_bitmap(Bitmap *bm);

bool outline_to_bitmap(ASS_Renderer *render_priv, Bitmap *bm,
                       ASS_Outline *outline1, ASS_Outline *outline2);

void ass_synth_blur(const BitmapEngine *engine, Bitmap *bm,
                    int be, double blur_r2);

int be_padding(int be);
void be_blur_pre(uint8_t *buf, intptr_t w,
                 intptr_t h, intptr_t stride);
void be_blur_post(uint8_t *buf, intptr_t w,
                  intptr_t h, intptr_t stride);
bool ass_gaussian_blur(const BitmapEngine *engine, Bitmap *bm, double r2);
void shift_bitmap(Bitmap *bm, int shift_x, int shift_y);
void fix_outline(Bitmap *bm_g, Bitmap *bm_o);

#endif                          /* LIBASS_BITMAP_H */

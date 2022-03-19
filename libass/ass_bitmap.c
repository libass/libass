/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2011 Grigori Goronzy <greg@chown.ath.cx>
 * Copyright (c) 2011-2014, Yu Zhuohuang <yuzhuohuang@qq.com>
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include "ass_utils.h"
#include "ass_outline.h"
#include "ass_bitmap.h"
#include "ass_render.h"


static void be_blur_pre(uint8_t *buf, intptr_t stride, intptr_t width, intptr_t height)
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            // This is equivalent to (value * 64 + 127) / 255 for all
            // values from 0 to 256 inclusive. Assist vectorizing
            // compilers by noting that all temporaries fit in 8 bits.
            buf[y * stride + x] =
                (uint8_t) ((buf[y * stride + x] >> 1) + 1) >> 1;
        }
    }
}

static void be_blur_post(uint8_t *buf, intptr_t stride, intptr_t width, intptr_t height)
{
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            // This is equivalent to (value * 255 + 32) / 64 for all values
            // from 0 to 96 inclusive, and we only care about 0 to 64.
            uint8_t value = buf[y * stride + x];
            buf[y * stride + x] = (value << 2) - (value > 32);
        }
    }
}

void ass_synth_blur(const BitmapEngine *engine, Bitmap *bm,
                    int be, double blur_r2x, double blur_r2y)
{
    if (!bm->buffer)
        return;

    // Apply gaussian blur
    if (blur_r2x > 0.001 || blur_r2y > 0.001)
        ass_gaussian_blur(engine, bm, blur_r2x, blur_r2y);

    if (!be)
        return;

    // Apply box blur (multiple passes, if requested)
    unsigned align = 1 << engine->align_order;
    size_t size = sizeof(uint16_t) * bm->stride * 2;
    uint16_t *tmp = ass_aligned_alloc(align, size, false);
    if (!tmp)
        return;

    int32_t w = bm->w;
    int32_t h = bm->h;
    ptrdiff_t stride = bm->stride;
    uint8_t *buf = bm->buffer;
    if (--be) {
        be_blur_pre(buf, stride, w, h);
        do {
            engine->be_blur(buf, stride, w, h, tmp);
        } while (--be);
        be_blur_post(buf, stride, w, h);
    }
    engine->be_blur(buf, stride, w, h, tmp);
    ass_aligned_free(tmp);
}

bool ass_alloc_bitmap(const BitmapEngine *engine, Bitmap *bm,
                      int32_t w, int32_t h, bool zero)
{
    unsigned align = 1 << engine->align_order;
    size_t s = ass_align(align, w);
    // Too often we use ints as offset for bitmaps => use INT_MAX.
    if (s > (INT_MAX - align) / FFMAX(h, 1))
        return false;
    uint8_t *buf = ass_aligned_alloc(align, s * h + align, zero);
    if (!buf)
        return false;
    bm->w = w;
    bm->h = h;
    bm->stride = s;
    bm->buffer = buf;
    return true;
}

bool ass_realloc_bitmap(const BitmapEngine *engine, Bitmap *bm, int32_t w, int32_t h)
{
    uint8_t *old = bm->buffer;
    if (!ass_alloc_bitmap(engine, bm, w, h, false))
        return false;
    ass_aligned_free(old);
    return true;
}

void ass_free_bitmap(Bitmap *bm)
{
    ass_aligned_free(bm->buffer);
}

bool ass_copy_bitmap(const BitmapEngine *engine, Bitmap *dst, const Bitmap *src)
{
    if (!src->buffer) {
        memset(dst, 0, sizeof(*dst));
        return true;
    }
    if (!ass_alloc_bitmap(engine, dst, src->w, src->h, false))
        return false;
    dst->left = src->left;
    dst->top  = src->top;
    memcpy(dst->buffer, src->buffer, src->stride * src->h);
    return true;
}

bool ass_outline_to_bitmap(RenderContext *state, Bitmap *bm,
                           ASS_Outline *outline1, ASS_Outline *outline2)
{
    ASS_Renderer *render_priv = state->renderer;
    RasterizerData *rst = &state->rasterizer;
    if (outline1 && !ass_rasterizer_set_outline(rst, outline1, false)) {
        ass_msg(render_priv->library, MSGL_WARN, "Failed to process glyph outline!\n");
        return false;
    }
    if (outline2 && !ass_rasterizer_set_outline(rst, outline2, !!outline1)) {
        ass_msg(render_priv->library, MSGL_WARN, "Failed to process glyph outline!\n");
        return false;
    }
    if (rst->bbox.x_min > rst->bbox.x_max || rst->bbox.y_min > rst->bbox.y_max)
        return false;

    // enlarge by 1/64th of pixel to bypass slow rasterizer path, add 1 pixel for shift_bitmap
    int32_t x_min = (rst->bbox.x_min -   1) >> 6;
    int32_t y_min = (rst->bbox.y_min -   1) >> 6;
    int32_t x_max = (rst->bbox.x_max + 127) >> 6;
    int32_t y_max = (rst->bbox.y_max + 127) >> 6;
    int32_t w = x_max - x_min;
    int32_t h = y_max - y_min;

    int mask = (1 << render_priv->engine.tile_order) - 1;

    // XXX: is that possible to trigger at all?
    if (w < 0 || h < 0 || w > INT_MAX - mask || h > INT_MAX - mask) {
        ass_msg(render_priv->library, MSGL_WARN,
                "Glyph bounding box too large: %dx%dpx", w, h);
        return false;
    }

    int32_t tile_w = (w + mask) & ~mask;
    int32_t tile_h = (h + mask) & ~mask;
    if (!ass_alloc_bitmap(&render_priv->engine, bm, tile_w, tile_h, false))
        return false;
    bm->left = x_min;
    bm->top  = y_min;

    if (!ass_rasterizer_fill(&render_priv->engine, rst, bm->buffer,
                             x_min, y_min, bm->stride, tile_h, bm->stride)) {
        ass_msg(render_priv->library, MSGL_WARN, "Failed to rasterize glyph!\n");
        ass_free_bitmap(bm);
        return false;
    }

    return true;
}

/**
 * \brief fix outline bitmap
 *
 * The glyph bitmap is subtracted from outline bitmap. This way looks much
 * better in some cases.
 */
void ass_fix_outline(Bitmap *bm_g, Bitmap *bm_o)
{
    if (!bm_g->buffer || !bm_o->buffer)
        return;

    int32_t l = FFMAX(bm_o->left, bm_g->left);
    int32_t t = FFMAX(bm_o->top,  bm_g->top);
    int32_t r = FFMIN(bm_o->left + bm_o->stride, bm_g->left + bm_g->stride);
    int32_t b = FFMIN(bm_o->top  + bm_o->h,      bm_g->top  + bm_g->h);

    uint8_t *g = bm_g->buffer + (t - bm_g->top) * bm_g->stride + (l - bm_g->left);
    uint8_t *o = bm_o->buffer + (t - bm_o->top) * bm_o->stride + (l - bm_o->left);

    for (int32_t y = 0; y < b - t; y++) {
        for (int32_t x = 0; x < r - l; x++)
            o[x] = (o[x] > g[x]) ? o[x] - (g[x] / 2) : 0;
        g += bm_g->stride;
        o += bm_o->stride;
    }
}

/**
 * \brief Shift a bitmap by the fraction of a pixel in x and y direction
 * expressed in 26.6 fixed point
 */
void ass_shift_bitmap(Bitmap *bm, int shift_x, int shift_y)
{
    assert((shift_x & ~63) == 0 && (shift_y & ~63) == 0);

    if (!bm->buffer)
        return;

    int32_t w = bm->w, h = bm->h;
    ptrdiff_t s = bm->stride;
    uint8_t *buf = bm->buffer;

    // Shift in x direction
    if (shift_x)
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = w - 1; x > 0; x--) {
                uint8_t b = buf[x + y * s - 1] * shift_x >> 6;
                buf[x + y * s - 1] -= b;
                buf[x + y * s] += b;
            }
        }

    // Shift in y direction
    if (shift_y)
        for (int32_t x = 0; x < w; x++) {
            for (int32_t y = h - 1; y > 0; y--) {
                uint8_t b = buf[x + y * s - s] * shift_y >> 6;
                buf[x + y * s - s] -= b;
                buf[x + y * s] += b;
            }
        }
}

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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <ft2build.h>
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include "ass_utils.h"
#include "ass_bitmap.h"
#include "ass_render.h"

static const unsigned base = 256;

int generate_tables(ASS_SynthPriv *priv, double radius)
{
    double A = log(1.0 / base) / (radius * radius * 2);
    int mx, i;
    double volume_diff, volume_factor = 0;
    unsigned volume;

    if (priv->radius == radius)
        return 0;
    else
        priv->radius = radius;

    priv->g_r = ceil(radius);
    priv->g_w = 2 * priv->g_r + 1;

    if (priv->g_r) {
        priv->g0 = realloc(priv->g0, priv->g_w * sizeof(double));
        priv->g = realloc(priv->g, priv->g_w * sizeof(unsigned));
        priv->gt2 = realloc(priv->gt2, 256 * priv->g_w * sizeof(unsigned));
        if (priv->g == NULL || priv->gt2 == NULL) {
            return -1;
        }
    }

    if (priv->g_r) {
        // exact gaussian curve
        for (i = 0; i < priv->g_w; ++i) {
            priv->g0[i] = exp(A * (i - priv->g_r) * (i - priv->g_r));
        }

        // integer gaussian curve with volume = 65536
        for (volume_diff = 10000000; volume_diff > 0.0000001;
             volume_diff *= 0.5) {
            volume_factor += volume_diff;
            volume = 0;
            for (i = 0; i < priv->g_w; ++i) {
                priv->g[i] = (unsigned) (priv->g0[i] * volume_factor + .5);
                volume += priv->g[i];
            }
            if (volume > 65536)
                volume_factor -= volume_diff;
        }
        volume = 0;
        for (i = 0; i < priv->g_w; ++i) {
            priv->g[i] = (unsigned) (priv->g0[i] * volume_factor + .5);
            volume += priv->g[i];
        }

        // gauss table:
        for (mx = 0; mx < priv->g_w; mx++) {
            for (i = 0; i < 256; i++) {
                priv->gt2[mx + i * priv->g_w] = i * priv->g[mx];
            }
        }
    }

    return 0;
}

void resize_tmp(ASS_SynthPriv *priv, int w, int h)
{
    if (priv->tmp_w >= w && priv->tmp_h >= h)
        return;
    if (priv->tmp_w == 0)
        priv->tmp_w = 64;
    if (priv->tmp_h == 0)
        priv->tmp_h = 64;
    while (priv->tmp_w < w)
        priv->tmp_w *= 2;
    while (priv->tmp_h < h)
        priv->tmp_h *= 2;
    ass_aligned_free(priv->tmp);
    priv->tmp =
        ass_aligned_alloc(32, (priv->tmp_w + 1) * priv->tmp_h * sizeof(unsigned));
}

ASS_SynthPriv *ass_synth_init(double radius)
{
    ASS_SynthPriv *priv = calloc(1, sizeof(ASS_SynthPriv));
    generate_tables(priv, radius);
    return priv;
}

void ass_synth_done(ASS_SynthPriv *priv)
{
    ass_aligned_free(priv->tmp);
    free(priv->g0);
    free(priv->g);
    free(priv->gt2);
    free(priv);
}

Bitmap *alloc_bitmap(int w, int h)
{
    Bitmap *bm;

    unsigned align = (w >= 32) ? 32 : ((w >= 16) ? 16 : 1);
    unsigned s = ass_align(align, w);
    bm = malloc(sizeof(Bitmap));
    bm->buffer = ass_aligned_alloc(align, s * h + 32);
    memset(bm->buffer, 0, s * h + 32);
    bm->w = w;
    bm->h = h;
    bm->stride = s;
    bm->left = bm->top = 0;
    return bm;
}

void ass_free_bitmap(Bitmap *bm)
{
    if (bm)
        ass_aligned_free(bm->buffer);
    free(bm);
}

Bitmap *copy_bitmap(const Bitmap *src)
{
    Bitmap *dst = alloc_bitmap(src->w, src->h);
    dst->left = src->left;
    dst->top = src->top;
    memcpy(dst->buffer, src->buffer, src->stride * src->h);
    return dst;
}

Bitmap *outline_to_bitmap(ASS_Library *library, FT_Library ftlib,
                          FT_Outline *outline, int bord)
{
    Bitmap *bm;
    int w, h;
    int error;
    FT_BBox bbox;
    FT_Bitmap bitmap;

    FT_Outline_Get_CBox(outline, &bbox);
    if (bbox.xMin >= bbox.xMax || bbox.yMin >= bbox.yMax) {
        bm = alloc_bitmap(2 * bord, 2 * bord);
        bm->left = bm->top = -bord;
        return bm;
    }

    // move glyph to origin (0, 0)
    bbox.xMin &= ~63;
    bbox.yMin &= ~63;
    FT_Outline_Translate(outline, -bbox.xMin, -bbox.yMin);
    // bitmap size
    bbox.xMax = (bbox.xMax + 63) & ~63;
    bbox.yMax = (bbox.yMax + 63) & ~63;
    w = (bbox.xMax - bbox.xMin) >> 6;
    h = (bbox.yMax - bbox.yMin) >> 6;
    // pen offset
    bbox.xMin >>= 6;
    bbox.yMax >>= 6;

    if (w * h > 8000000) {
        ass_msg(library, MSGL_WARN, "Glyph bounding box too large: %dx%dpx",
                w, h);
        return NULL;
    }

    // allocate and set up bitmap
    bm = alloc_bitmap(w + 2 * bord, h + 2 * bord);
    bm->left = bbox.xMin - bord;
    bm->top = -bbox.yMax - bord;
    bitmap.width = w;
    bitmap.rows = h;
    bitmap.pitch = bm->stride;
    bitmap.buffer = bm->buffer + bord + bm->stride * bord;
    bitmap.num_grays = 256;
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;

    // render into target bitmap
    if ((error = FT_Outline_Get_Bitmap(ftlib, outline, &bitmap))) {
        ass_msg(library, MSGL_WARN, "Failed to rasterize glyph: %d\n", error);
        ass_free_bitmap(bm);
        return NULL;
    }

    return bm;
}

/**
 * \brief fix outline bitmap
 *
 * The glyph bitmap is subtracted from outline bitmap. This way looks much
 * better in some cases.
 */
void fix_outline(Bitmap *bm_g, Bitmap *bm_o)
{
    int x, y;
    const int l = bm_o->left > bm_g->left ? bm_o->left : bm_g->left;
    const int t = bm_o->top > bm_g->top ? bm_o->top : bm_g->top;
    const int r =
        bm_o->left + bm_o->stride <
        bm_g->left + bm_g->stride ? bm_o->left + bm_o->stride : bm_g->left + bm_g->stride;
    const int b =
        bm_o->top + bm_o->h <
        bm_g->top + bm_g->h ? bm_o->top + bm_o->h : bm_g->top + bm_g->h;

    unsigned char *g =
        bm_g->buffer + (t - bm_g->top) * bm_g->stride + (l - bm_g->left);
    unsigned char *o =
        bm_o->buffer + (t - bm_o->top) * bm_o->stride + (l - bm_o->left);

    for (y = 0; y < b - t; ++y) {
        for (x = 0; x < r - l; ++x) {
            unsigned char c_g, c_o;
            c_g = g[x];
            c_o = o[x];
            o[x] = (c_o > c_g) ? c_o - (c_g / 2) : 0;
        }
        g += bm_g->stride;
        o += bm_o->stride;
    }
}

/**
 * \brief Shift a bitmap by the fraction of a pixel in x and y direction
 * expressed in 26.6 fixed point
 */
void shift_bitmap(Bitmap *bm, int shift_x, int shift_y)
{
    int x, y, b;
    int w = bm->w;
    int h = bm->h;
    int s = bm->stride;
    unsigned char *buf = bm->buffer;

    // Shift in x direction
    if (shift_x > 0) {
        for (y = 0; y < h; y++) {
            for (x = w - 1; x > 0; x--) {
                b = (buf[x + y * s - 1] * shift_x) >> 6;
                buf[x + y * s - 1] -= b;
                buf[x + y * s] += b;
            }
        }
    } else if (shift_x < 0) {
        shift_x = -shift_x;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w - 1; x++) {
                b = (buf[x + y * s + 1] * shift_x) >> 6;
                buf[x + y * s + 1] -= b;
                buf[x + y * s] += b;
            }
        }
    }

    // Shift in y direction
    if (shift_y > 0) {
        for (x = 0; x < w; x++) {
            for (y = h - 1; y > 0; y--) {
                b = (buf[x + (y - 1) * s] * shift_y) >> 6;
                buf[x + (y - 1) * s] -= b;
                buf[x + y * s] += b;
            }
        }
    } else if (shift_y < 0) {
        shift_y = -shift_y;
        for (x = 0; x < w; x++) {
            for (y = 0; y < h - 1; y++) {
                b = (buf[x + (y + 1) * s] * shift_y) >> 6;
                buf[x + (y + 1) * s] -= b;
                buf[x + y * s] += b;
            }
        }
    }
}

/*
 * Gaussian blur.  An fast pure C implementation from MPlayer.
 */
void ass_gauss_blur(unsigned char *buffer, unsigned *tmp2,
                    int width, int height, int stride,
                    unsigned *m2, int r, int mwidth)
{

    int x, y;

    unsigned char *s = buffer;
    unsigned *t = tmp2 + 1;
    for (y = 0; y < height; y++) {
        memset(t - 1, 0, (width + 1) * sizeof(unsigned));
        t[-1] = 32768;

        for (x = 0; x < r; x++) {
            const int src = s[x];
            if (src) {
                register unsigned *dstp = t + x - r;
                int mx;
                unsigned *m3 = m2 + src * mwidth;
                for (mx = r - x; mx < mwidth; mx++) {
                    dstp[mx] += m3[mx];
                }
            }
        }

        for (; x < width - r; x++) {
            const int src = s[x];
            if (src) {
                register unsigned *dstp = t + x - r;
                int mx;
                unsigned *m3 = m2 + src * mwidth;
                for (mx = 0; mx < mwidth; mx++) {
                    dstp[mx] += m3[mx];
                }
            }
        }

        for (; x < width; x++) {
            const int src = s[x];
            if (src) {
                register unsigned *dstp = t + x - r;
                int mx;
                const int x2 = r + width - x;
                unsigned *m3 = m2 + src * mwidth;
                for (mx = 0; mx < x2; mx++) {
                    dstp[mx] += m3[mx];
                }
            }
        }

        s += stride;
        t += width + 1;
    }

    t = tmp2;
    for (x = 0; x < width; x++) {
        for (y = 0; y < r; y++) {
            unsigned *srcp = t + y * (width + 1) + 1;
            int src = *srcp;
            if (src) {
                register unsigned *dstp = srcp - 1 - y * (width + 1);
                const int src2 = (src + 32768) >> 16;
                unsigned *m3 = m2 + src2 * mwidth;

                int mx;
                *srcp = 32768;
                for (mx = r - y; mx < mwidth; mx++) {
                    *dstp += m3[mx];
                    dstp += width + 1;
                }
            }
        }
        for (; y < height - r; y++) {
            unsigned *srcp = t + y * (width + 1) + 1;
            int src = *srcp;
            if (src) {
                register unsigned *dstp = srcp - 1 - r * (width + 1);
                const int src2 = (src + 32768) >> 16;
                unsigned *m3 = m2 + src2 * mwidth;

                int mx;
                *srcp = 32768;
                for (mx = 0; mx < mwidth; mx++) {
                    *dstp += m3[mx];
                    dstp += width + 1;
                }
            }
        }
        for (; y < height; y++) {
            unsigned *srcp = t + y * (width + 1) + 1;
            int src = *srcp;
            if (src) {
                const int y2 = r + height - y;
                register unsigned *dstp = srcp - 1 - r * (width + 1);
                const int src2 = (src + 32768) >> 16;
                unsigned *m3 = m2 + src2 * mwidth;

                int mx;
                *srcp = 32768;
                for (mx = 0; mx < y2; mx++) {
                    *dstp += m3[mx];
                    dstp += width + 1;
                }
            }
        }
        t++;
    }

    t = tmp2;
    s = buffer;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            s[x] = t[x] >> 16;
        }
        s += stride;
        t += width + 1;
    }
}

/**
 * \brief Blur with [[1,2,1]. [2,4,2], [1,2,1]] kernel
 * This blur is the same as the one employed by vsfilter.
 * Pure C implementation.
 */
void be_blur_c(uint8_t *buf, intptr_t w,
               intptr_t h, intptr_t stride,
               uint16_t *tmp)
{
    unsigned short *col_pix_buf = tmp;
    unsigned short *col_sum_buf = tmp + w * sizeof(unsigned short);
    unsigned x, y, old_pix, old_sum, temp1, temp2;
    unsigned char *src, *dst;
    memset(col_pix_buf, 0, w * sizeof(unsigned short));
    memset(col_sum_buf, 0, w * sizeof(unsigned short));
    {
        y = 0;
        src=buf+y*stride;

        x = 2;
        old_pix = src[x-1];
        old_sum = old_pix + src[x-2];
        for ( ; x < w; x++) {
            temp1 = src[x];
            temp2 = old_pix + temp1;
            old_pix = temp1;
            temp1 = old_sum + temp2;
            old_sum = temp2;
            col_pix_buf[x] = temp1;
        }
    }
    {
        y = 1;
        src=buf+y*stride;

        x = 2;
        old_pix = src[x-1];
        old_sum = old_pix + src[x-2];
        for ( ; x < w; x++) {
            temp1 = src[x];
            temp2 = old_pix + temp1;
            old_pix = temp1;
            temp1 = old_sum + temp2;
            old_sum = temp2;

            temp2 = col_pix_buf[x] + temp1;
            col_pix_buf[x] = temp1;
            col_sum_buf[x] = temp2;
        }
    }

    for (y = 2; y < h; y++) {
        src=buf+y*stride;
        dst=buf+(y-1)*stride;

        x = 2;
        old_pix = src[x-1];
        old_sum = old_pix + src[x-2];
        for ( ; x < w; x++) {
            temp1 = src[x];
            temp2 = old_pix + temp1;
            old_pix = temp1;
            temp1 = old_sum + temp2;
            old_sum = temp2;

            temp2 = col_pix_buf[x] + temp1;
            col_pix_buf[x] = temp1;
            dst[x-1] = (col_sum_buf[x] + temp2) >> 4;
            col_sum_buf[x] = temp2;
        }
    }
}

int outline_to_bitmap3(ASS_Library *library, ASS_SynthPriv *priv_blur,
                       FT_Library ftlib, FT_Outline *outline, FT_Outline *border,
                       Bitmap **bm_g, Bitmap **bm_o, Bitmap **bm_s,
                       int be, double blur_radius, FT_Vector shadow_offset,
                       int border_style, int border_visible)
{
    blur_radius *= 2;
    int bbord = be > 0 ? sqrt(2 * be) : 0;
    int gbord = blur_radius > 0.0 ? blur_radius + 1 : 0;
    int bord = FFMAX(bbord, gbord);
    if (bord == 0 && (shadow_offset.x || shadow_offset.y))
        bord = 1;

    assert(bm_g && bm_o && bm_s);

    *bm_g = *bm_o = *bm_s = 0;

    if (outline)
        *bm_g = outline_to_bitmap(library, ftlib, outline, bord);
    if (!*bm_g)
        return 1;

    if (border) {
        *bm_o = outline_to_bitmap(library, ftlib, border, bord);
        if (!*bm_o) {
            return 1;
        }
    }

    return 0;
}

/**
 * \brief Add two bitmaps together at a given position
 * Uses additive blending, clipped to [0,255]. Pure C implementation.
 */
void add_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src, intptr_t src_stride,
                   intptr_t height, intptr_t width)
{
    unsigned out;
    uint8_t* end = dst + dst_stride * height;
    while (dst < end) {
        for (unsigned j = 0; j < width; ++j) {
            out = dst[j] + src[j];
            dst[j] = FFMIN(out, 255);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

void sub_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src, intptr_t src_stride,
                   intptr_t height, intptr_t width)
{
    short out;
    uint8_t* end = dst + dst_stride * height;
    while (dst < end) {
        for (unsigned j = 0; j < width; ++j) {
            out = dst[j] - src[j];
            dst[j] = FFMAX(out, 0);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

void restride_bitmap_c(uint8_t *dst, intptr_t dst_stride,
                       uint8_t *src, intptr_t src_stride,
                       intptr_t width, intptr_t height)
{
    uint8_t* end = dst + dst_stride * height;
    while (dst < end) {
        memcpy(dst, src, width);
        dst += dst_stride;
        src += src_stride;
    }
}

void mul_bitmaps_c(uint8_t *dst, intptr_t dst_stride,
                   uint8_t *src1, intptr_t src1_stride,
                   uint8_t *src2, intptr_t src2_stride,
                   intptr_t w, intptr_t h)
{
    uint8_t* end = src1 + src1_stride * h;
    while (src1 < end) {
        for (unsigned x = 0; x < w; ++x) {
            dst[x] = (src1[x] * src2[x] + 255) >> 8;
        }
        dst  += dst_stride;
        src1 += src1_stride;
        src2 += src2_stride;
    }
}

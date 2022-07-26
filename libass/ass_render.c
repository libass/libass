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

#include "config.h"
#include "ass_compat.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#ifdef CONFIG_UNIBREAK
#include <linebreak.h>
#endif

#include "ass.h"
#include "ass_outline.h"
#include "ass_render.h"
#include "ass_parse.h"
#include "ass_priv.h"
#include "ass_shaper.h"

#define MAX_GLYPHS_INITIAL 1024
#define MAX_LINES_INITIAL 64
#define MAX_BITMAPS_INITIAL 16
#define MAX_SUB_BITMAPS_INITIAL 64
#define SUBPIXEL_MASK 63
#define STROKER_PRECISION 16     // stroker error in integer units, unrelated to final accuracy
#define RASTERIZER_PRECISION 16  // rasterizer spline approximation error in 1/64 pixel units
#define POSITION_PRECISION 8.0   // rough estimate of transform error in 1/64 pixel units
#define MAX_PERSP_SCALE 16.0
#define SUBPIXEL_ORDER 3  // ~ log2(64 / POSITION_PRECISION)
#define BLUR_PRECISION (1.0 / 256)  // blur error as fraction of full input range


static bool text_info_init(TextInfo* text_info)
{
    text_info->max_bitmaps = MAX_BITMAPS_INITIAL;
    text_info->max_glyphs = MAX_GLYPHS_INITIAL;
    text_info->max_lines = MAX_LINES_INITIAL;
    text_info->n_bitmaps = 0;
    text_info->combined_bitmaps = calloc(MAX_BITMAPS_INITIAL, sizeof(CombinedBitmapInfo));
    text_info->glyphs = calloc(MAX_GLYPHS_INITIAL, sizeof(GlyphInfo));
    text_info->event_text = calloc(MAX_GLYPHS_INITIAL, sizeof(FriBidiChar));
    text_info->breaks = malloc(MAX_GLYPHS_INITIAL);
    text_info->lines = calloc(MAX_LINES_INITIAL, sizeof(LineInfo));

    if (!text_info->combined_bitmaps || !text_info->glyphs || !text_info->lines ||
        !text_info->breaks || !text_info->event_text)
        return false;

    return true;
}

static void text_info_done(TextInfo* text_info)
{
    free(text_info->glyphs);
    free(text_info->event_text);
    free(text_info->breaks);
    free(text_info->lines);
    free(text_info->combined_bitmaps);
}

ASS_Renderer *ass_renderer_init(ASS_Library *library)
{
    int error;
    FT_Library ft;
    ASS_Renderer *priv = 0;
    int vmajor, vminor, vpatch;

    ass_msg(library, MSGL_INFO, "libass API version: 0x%X", LIBASS_VERSION);
    ass_msg(library, MSGL_INFO, "libass source: %s", CONFIG_SOURCEVERSION);

    error = FT_Init_FreeType(&ft);
    if (error) {
        ass_msg(library, MSGL_FATAL, "%s failed", "FT_Init_FreeType");
        goto fail;
    }

    FT_Library_Version(ft, &vmajor, &vminor, &vpatch);
    ass_msg(library, MSGL_V, "Raster: FreeType %d.%d.%d",
           vmajor, vminor, vpatch);

    priv = calloc(1, sizeof(ASS_Renderer));
    if (!priv) {
        FT_Done_FreeType(ft);
        goto fail;
    }

    priv->library = library;
    priv->ftlibrary = ft;
    // images_root and related stuff is zero-filled in calloc

    unsigned flags = ASS_CPU_FLAG_ALL;
#if CONFIG_LARGE_TILES
    flags |= ASS_FLAG_LARGE_TILES;
#endif
    priv->engine = ass_bitmap_engine_init(flags);

    if (!ass_rasterizer_init(&priv->engine, &priv->state.rasterizer, RASTERIZER_PRECISION))
        goto fail;

    priv->cache.font_cache = ass_font_cache_create();
    priv->cache.bitmap_cache = ass_bitmap_cache_create();
    priv->cache.composite_cache = ass_composite_cache_create();
    priv->cache.outline_cache = ass_outline_cache_create();
    priv->cache.metrics_cache = ass_glyph_metrics_cache_create();
    if (!priv->cache.font_cache || !priv->cache.bitmap_cache || !priv->cache.composite_cache || !priv->cache.outline_cache || !priv->cache.metrics_cache)
        goto fail;

    priv->cache.glyph_max = GLYPH_CACHE_MAX;
    priv->cache.bitmap_max_size = BITMAP_CACHE_MAX_SIZE;
    priv->cache.composite_max_size = COMPOSITE_CACHE_MAX_SIZE;

    if (!text_info_init(&priv->text_info))
        goto fail;

    priv->user_override_style.Name = "OverrideStyle"; // name insignificant

    priv->state.renderer = priv;
    priv->state.text_info = &priv->text_info;

    priv->settings.font_size_coeff = 1.;
    priv->settings.selective_style_overrides = ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE;

    if (!(priv->state.shaper = ass_shaper_new(priv->cache.metrics_cache)))
        goto fail;

    ass_shaper_info(library);
    priv->settings.shaper = ASS_SHAPING_COMPLEX;

    ass_msg(library, MSGL_V, "Initialized");

    return priv;

fail:
    ass_msg(library, MSGL_ERR, "Initialization failed");
    ass_renderer_done(priv);

    return NULL;
}

void ass_renderer_done(ASS_Renderer *render_priv)
{
    if (!render_priv)
        return;

    ass_frame_unref(render_priv->images_root);
    ass_frame_unref(render_priv->prev_images_root);

    ass_cache_done(render_priv->cache.composite_cache);
    ass_cache_done(render_priv->cache.bitmap_cache);
    ass_cache_done(render_priv->cache.outline_cache);
    ass_cache_done(render_priv->cache.metrics_cache);
    ass_shaper_free(render_priv->state.shaper);
    ass_cache_done(render_priv->cache.font_cache);

    ass_rasterizer_done(&render_priv->state.rasterizer);

    if (render_priv->fontselect)
        ass_fontselect_free(render_priv->fontselect);
    if (render_priv->ftlibrary)
        FT_Done_FreeType(render_priv->ftlibrary);
    free(render_priv->eimg);

    text_info_done(&render_priv->text_info);

    free(render_priv->settings.default_font);
    free(render_priv->settings.default_family);

    free(render_priv->user_override_style.FontName);

    free(render_priv);
}

/**
 * \brief Create a new ASS_Image
 * Parameters are the same as ASS_Image fields.
 */
static ASS_Image *my_draw_bitmap(unsigned char *bitmap, int bitmap_w,
                                 int bitmap_h, int stride, int dst_x,
                                 int dst_y, uint32_t color,
                                 CompositeHashValue *source)
{
    ASS_ImagePriv *img = malloc(sizeof(ASS_ImagePriv));
    if (!img) {
        if (!source)
            ass_aligned_free(bitmap);
        return NULL;
    }

    img->result.w = bitmap_w;
    img->result.h = bitmap_h;
    img->result.stride = stride;
    img->result.bitmap = bitmap;
    img->result.color = color;
    img->result.dst_x = dst_x;
    img->result.dst_y = dst_y;

    img->source = source;
    ass_cache_inc_ref(source);
    img->buffer = source ? NULL : bitmap;
    img->ref_count = 0;

    return &img->result;
}

/**
 * \brief Mapping between script and screen coordinates
 */
static double x2scr_pos(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->frame_content_width / render_priv->par_scale_x / render_priv->track->PlayResX +
        render_priv->settings.left_margin;
}
static double x2scr_left(RenderContext *state, double x)
{
    ASS_Renderer *render_priv = state->renderer;
    if (state->explicit || !render_priv->settings.use_margins)
        return x2scr_pos(render_priv, x);
    return x * render_priv->fit_width / render_priv->par_scale_x /
        render_priv->track->PlayResX;
}
static double x2scr_right(RenderContext *state, double x)
{
    ASS_Renderer *render_priv = state->renderer;
    if (state->explicit || !render_priv->settings.use_margins)
        return x2scr_pos(render_priv, x);
    return x * render_priv->fit_width / render_priv->par_scale_x /
        render_priv->track->PlayResX +
        (render_priv->width - render_priv->fit_width);
}
static double x2scr_pos_scaled(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->frame_content_width / render_priv->track->PlayResX +
        render_priv->settings.left_margin;
}
/**
 * \brief Mapping between script and screen coordinates
 */
static double y2scr_pos(ASS_Renderer *render_priv, double y)
{
    return y * render_priv->frame_content_height / render_priv->track->PlayResY +
        render_priv->settings.top_margin;
}
static double y2scr(RenderContext *state, double y)
{
    ASS_Renderer *render_priv = state->renderer;
    if (state->explicit || !render_priv->settings.use_margins)
        return y2scr_pos(render_priv, y);
    return y * render_priv->fit_height /
        render_priv->track->PlayResY +
        (render_priv->height - render_priv->fit_height) * 0.5;
}

// the same for toptitles
static double y2scr_top(RenderContext *state, double y)
{
    ASS_Renderer *render_priv = state->renderer;
    if (state->explicit || !render_priv->settings.use_margins)
        return y2scr_pos(render_priv, y);
    return y * render_priv->fit_height /
        render_priv->track->PlayResY;
}
// the same for subtitles
static double y2scr_sub(RenderContext *state, double y)
{
    ASS_Renderer *render_priv = state->renderer;
    if (state->explicit || !render_priv->settings.use_margins)
        return y2scr_pos(render_priv, y);
    return y * render_priv->fit_height /
        render_priv->track->PlayResY +
        (render_priv->height - render_priv->fit_height);
}

/*
 * \brief Convert bitmap glyphs into ASS_Image list with inverse clipping
 *
 * Inverse clipping with the following strategy:
 * - find rectangle from (x0, y0) to (cx0, y1)
 * - find rectangle from (cx0, y0) to (cx1, cy0)
 * - find rectangle from (cx0, cy1) to (cx1, y1)
 * - find rectangle from (cx1, y0) to (x1, y1)
 * These rectangles can be invalid and in this case are discarded.
 * Afterwards, they are clipped against the screen coordinates.
 * In an additional pass, the rectangles need to be split up left/right for
 * karaoke effects.  This can result in a lot of bitmaps (6 to be exact).
 */
static ASS_Image **render_glyph_i(RenderContext *state,
                                  Bitmap *bm, int dst_x, int dst_y,
                                  uint32_t color, uint32_t color2, int brk,
                                  ASS_Image **tail, unsigned type,
                                  CompositeHashValue *source)
{
    ASS_Renderer *render_priv = state->renderer;
    int i, j, x0, y0, x1, y1, cx0, cy0, cx1, cy1, sx, sy, zx, zy;
    Rect r[4];
    ASS_Image *img;

    dst_x += bm->left;
    dst_y += bm->top;
    brk -= dst_x;

    // we still need to clip against screen boundaries
    zx = x2scr_pos_scaled(render_priv, 0);
    zy = y2scr_pos(render_priv, 0);
    sx = x2scr_pos_scaled(render_priv, render_priv->track->PlayResX);
    sy = y2scr_pos(render_priv, render_priv->track->PlayResY);

    x0 = 0;
    y0 = 0;
    x1 = bm->w;
    y1 = bm->h;
    cx0 = state->clip_x0 - dst_x;
    cy0 = state->clip_y0 - dst_y;
    cx1 = state->clip_x1 - dst_x;
    cy1 = state->clip_y1 - dst_y;

    // calculate rectangles and discard invalid ones while we're at it.
    i = 0;
    r[i].x0 = x0;
    r[i].y0 = y0;
    r[i].x1 = (cx0 > x1) ? x1 : cx0;
    r[i].y1 = y1;
    if (r[i].x1 > r[i].x0 && r[i].y1 > r[i].y0) i++;
    r[i].x0 = (cx0 < 0) ? x0 : cx0;
    r[i].y0 = y0;
    r[i].x1 = (cx1 > x1) ? x1 : cx1;
    r[i].y1 = (cy0 > y1) ? y1 : cy0;
    if (r[i].x1 > r[i].x0 && r[i].y1 > r[i].y0) i++;
    r[i].x0 = (cx0 < 0) ? x0 : cx0;
    r[i].y0 = (cy1 < 0) ? y0 : cy1;
    r[i].x1 = (cx1 > x1) ? x1 : cx1;
    r[i].y1 = y1;
    if (r[i].x1 > r[i].x0 && r[i].y1 > r[i].y0) i++;
    r[i].x0 = (cx1 < 0) ? x0 : cx1;
    r[i].y0 = y0;
    r[i].x1 = x1;
    r[i].y1 = y1;
    if (r[i].x1 > r[i].x0 && r[i].y1 > r[i].y0) i++;

    // clip each rectangle to screen coordinates
    for (j = 0; j < i; j++) {
        r[j].x0 = (r[j].x0 + dst_x < zx) ? zx - dst_x : r[j].x0;
        r[j].y0 = (r[j].y0 + dst_y < zy) ? zy - dst_y : r[j].y0;
        r[j].x1 = (r[j].x1 + dst_x > sx) ? sx - dst_x : r[j].x1;
        r[j].y1 = (r[j].y1 + dst_y > sy) ? sy - dst_y : r[j].y1;
    }

    // draw the rectangles
    for (j = 0; j < i; j++) {
        int lbrk = brk;
        // kick out rectangles that are invalid now
        if (r[j].x1 <= r[j].x0 || r[j].y1 <= r[j].y0)
            continue;
        // split up into left and right for karaoke, if needed
        if (lbrk > r[j].x0) {
            if (lbrk > r[j].x1) lbrk = r[j].x1;
            img = my_draw_bitmap(bm->buffer + r[j].y0 * bm->stride + r[j].x0,
                                 lbrk - r[j].x0, r[j].y1 - r[j].y0, bm->stride,
                                 dst_x + r[j].x0, dst_y + r[j].y0, color, source);
            if (!img) break;
            img->type = type;
            *tail = img;
            tail = &img->next;
        }
        if (lbrk < r[j].x1) {
            if (lbrk < r[j].x0) lbrk = r[j].x0;
            img = my_draw_bitmap(bm->buffer + r[j].y0 * bm->stride + lbrk,
                                 r[j].x1 - lbrk, r[j].y1 - r[j].y0, bm->stride,
                                 dst_x + lbrk, dst_y + r[j].y0, color2, source);
            if (!img) break;
            img->type = type;
            *tail = img;
            tail = &img->next;
        }
    }

    return tail;
}

/**
 * \brief convert bitmap glyph into ASS_Image struct(s)
 * \param bit freetype bitmap glyph, FT_PIXEL_MODE_GRAY
 * \param dst_x bitmap x coordinate in video frame
 * \param dst_y bitmap y coordinate in video frame
 * \param color first color, RGBA
 * \param color2 second color, RGBA
 * \param brk x coordinate relative to glyph origin, color is used to the left of brk, color2 - to the right
 * \param tail pointer to the last image's next field, head of the generated list should be stored here
 * \return pointer to the new list tail
 * Performs clipping. Uses my_draw_bitmap for actual bitmap convertion.
 */
static ASS_Image **
render_glyph(RenderContext *state, Bitmap *bm, int dst_x, int dst_y,
             uint32_t color, uint32_t color2, int brk, ASS_Image **tail,
             unsigned type, CompositeHashValue *source)
{
    // Inverse clipping in use?
    if (state->clip_mode)
        return render_glyph_i(state, bm, dst_x, dst_y, color, color2,
                              brk, tail, type, source);

    // brk is absolute
    // color = color left of brk
    // color2 = color right of brk
    int b_x0, b_y0, b_x1, b_y1; // visible part of the bitmap
    int clip_x0, clip_y0, clip_x1, clip_y1;
    int tmp;
    ASS_Image *img;
    ASS_Renderer *render_priv = state->renderer;

    dst_x += bm->left;
    dst_y += bm->top;
    brk -= dst_x;

    // clipping
    clip_x0 = FFMINMAX(state->clip_x0, 0, render_priv->width);
    clip_y0 = FFMINMAX(state->clip_y0, 0, render_priv->height);
    clip_x1 = FFMINMAX(state->clip_x1, 0, render_priv->width);
    clip_y1 = FFMINMAX(state->clip_y1, 0, render_priv->height);
    b_x0 = 0;
    b_y0 = 0;
    b_x1 = bm->w;
    b_y1 = bm->h;

    tmp = dst_x - clip_x0;
    if (tmp < 0)
        b_x0 = -tmp;
    tmp = dst_y - clip_y0;
    if (tmp < 0)
        b_y0 = -tmp;
    tmp = clip_x1 - dst_x - bm->w;
    if (tmp < 0)
        b_x1 = bm->w + tmp;
    tmp = clip_y1 - dst_y - bm->h;
    if (tmp < 0)
        b_y1 = bm->h + tmp;

    if ((b_y0 >= b_y1) || (b_x0 >= b_x1))
        return tail;

    if (brk > b_x0) {           // draw left part
        if (brk > b_x1)
            brk = b_x1;
        img = my_draw_bitmap(bm->buffer + bm->stride * b_y0 + b_x0,
                             brk - b_x0, b_y1 - b_y0, bm->stride,
                             dst_x + b_x0, dst_y + b_y0, color, source);
        if (!img) return tail;
        img->type = type;
        *tail = img;
        tail = &img->next;
    }
    if (brk < b_x1) {           // draw right part
        if (brk < b_x0)
            brk = b_x0;
        img = my_draw_bitmap(bm->buffer + bm->stride * b_y0 + brk,
                             b_x1 - brk, b_y1 - b_y0, bm->stride,
                             dst_x + brk, dst_y + b_y0, color2, source);
        if (!img) return tail;
        img->type = type;
        *tail = img;
        tail = &img->next;
    }
    return tail;
}

static bool quantize_transform(double m[3][3], ASS_Vector *pos,
                               ASS_DVector *offset, bool first,
                               BitmapHashKey *key)
{
    // Full transform:
    // x_out = (m_xx * x + m_xy * y + m_xz) / z,
    // y_out = (m_yx * x + m_yy * y + m_yz) / z,
    // z     =  m_zx * x + m_zy * y + m_zz.

    const double max_val = 1000000;

    const ASS_Rect *bbox = &key->outline->cbox;
    double x0 = (bbox->x_min + bbox->x_max) / 2.0;
    double y0 = (bbox->y_min + bbox->y_max) / 2.0;
    double dx = (bbox->x_max - bbox->x_min) / 2.0 + 64;
    double dy = (bbox->y_max - bbox->y_min) / 2.0 + 64;

    // Change input coordinates' origin to (x0, y0),
    // after that transformation x:[-dx, dx], y:[-dy, dy],
    // max|x| = dx and max|y| = dy.
    for (int i = 0; i < 3; i++)
        m[i][2] += m[i][0] * x0 + m[i][1] * y0;

    if (m[2][2] <= 0)
        return false;

    double w = 1 / m[2][2];
    // Transformed center of bounding box
    double center[2] = { m[0][2] * w, m[1][2] * w };
    // Change output coordinates' origin to center,
    // m_xz and m_yz is skipped as it becomes 0 and no longer needed.
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            m[i][j] -= m[2][j] * center[i];

    double delta[2] = {0};
    if (!first) {
        delta[0] = offset->x;
        delta[1] = offset->y;
    }

    int32_t qr[2];  // quantized center position
    for (int i = 0; i < 2; i++) {
        center[i] /= 64 >> SUBPIXEL_ORDER;
        center[i] -= delta[i];
        if (!(fabs(center[i]) < max_val))
            return false;
        qr[i] = lrint(center[i]);
    }

    // Minimal bounding box z coordinate
    double z0 = m[2][2] - fabs(m[2][0]) * dx - fabs(m[2][1]) * dy;
    // z0 clamped to z_center / MAX_PERSP_SCALE to mitigate problems with small z
    w = 1.0 / POSITION_PRECISION / FFMAX(z0, m[2][2] / MAX_PERSP_SCALE);
    double mul[2] = { dx * w, dy * w };  // 1 / q_x, 1 / q_y

    // z0 = m_zz - |m_zx| * dx - |m_zy| * dy,
    // m_zz = z0 + |m_zx| * dx + |m_zy| * dy,
    // z = m_zx * x + m_zy * y + m_zz
    //  = m_zx * (x + sign(m_zx) * dx) + m_zy * (y + sign(m_zy) * dy) + z0.

    // Let D(f) denote the absolute error of a quantity f.
    // Our goal is to determine tolerable error for matrix coefficients,
    // so that the total error of the output x_out, y_out is still acceptable.
    // As glyph dimensions are usually larger than a couple of pixels, errors
    // will be relatively small and we can use first order approximation.

    // z0 is effectively a scale factor and can thus be treated as a constant.
    // Error of constants is obviously zero, so:  D(dx) = D(dy) = D(z0) = 0.
    // For arbitrary quantities A, B, C with C not zero, the following holds true:
    //   D(A * B) <= D(A) * max|B| + max|A| * D(B),
    //   D(1 / C) <= D(C) * max|1 / C^2|.
    // Write ~ for 'same magnitude' and ~= for 'approximately'.

    // D(x_out) = D((m_xx * x + m_xy * y) / z)
    //  <= D(m_xx * x + m_xy * y) * max|1 / z| + max|m_xx * x + m_xy * y| * D(1 / z)
    //  <= (D(m_xx) * dx + D(m_xy) * dy) / z0 + (|m_xx| * dx + |m_xy| * dy) * D(z) / z0^2,
    // D(y_out) = D((m_yx * x + m_yy * y) / z)
    //  <= D(m_yx * x + m_yy * y) * max|1 / z| + max|m_yx * x + m_yy * y| * D(1 / z)
    //  <= (D(m_yx) * dx + D(m_yy) * dy) / z0 + (|m_yx| * dx + |m_yy| * dy) * D(z) / z0^2,
    // |m_xx| * dx + |m_xy| * dy = x_lim,
    // |m_yx| * dx + |m_yy| * dy = y_lim,
    // D(z) <= 2 * (D(m_zx) * dx + D(m_zy) * dy),
    // D(x_out) <= (D(m_xx) * dx + D(m_xy) * dy) / z0
    //       + 2 * (D(m_zx) * dx + D(m_zy) * dy) * x_lim / z0^2,
    // D(y_out) <= (D(m_yx) * dx + D(m_yy) * dy) / z0
    //       + 2 * (D(m_zx) * dx + D(m_zy) * dy) * y_lim / z0^2.

    // To estimate acceptable error in a matrix coefficient, pick ACCURACY for this substep,
    // set error in all other coefficients to zero and solve the system
    // D(x_out) <= ACCURACY, D(y_out) <= ACCURACY for desired D(m_ij).
    // Note that ACCURACY isn't equal to total error.
    // Total error is larger than each ACCURACY, but still of the same magnitude.
    // Via our choice of ACCURACY, we get a total error of up to several POSITION_PRECISION.

    // Quantization steps (pick: ACCURARY = POSITION_PRECISION):
    // D(m_xx), D(m_yx) ~ q_x = POSITION_PRECISION * z0 / dx,
    // D(m_xy), D(m_yy) ~ q_y = POSITION_PRECISION * z0 / dy,
    // qm_xx = round(m_xx / q_x), qm_xy = round(m_xy / q_y),
    // qm_yx = round(m_yx / q_x), qm_yy = round(m_yy / q_y).

    int32_t qm[3][2];
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            double val = m[i][j] * mul[j];
            if (!(fabs(val) < max_val))
                return false;
            qm[i][j] = lrint(val);
        }

    // x_lim = |m_xx| * dx + |m_xy| * dy
    //  ~= |qm_xx| * q_x * dx + |qm_xy| * q_y * dy
    //  = (|qm_xx| + |qm_xy|) * POSITION_PRECISION * z0,
    // y_lim = |m_yx| * dx + |m_yy| * dy
    //  ~= |qm_yx| * q_x * dx + |qm_yy| * q_y * dy
    //  = (|qm_yx| + |qm_yy|) * POSITION_PRECISION * z0,
    // max(x_lim, y_lim) / z0 ~= w
    //  = max(|qm_xx| + |qm_xy|, |qm_yx| + |qm_yy|) * POSITION_PRECISION.

    // Quantization steps (pick: ACCURACY = 2 * POSITION_PRECISION):
    // D(m_zx) ~ POSITION_PRECISION * z0^2 / max(x_lim, y_lim) / dx ~= q_zx = q_x / w,
    // D(m_zy) ~ POSITION_PRECISION * z0^2 / max(x_lim, y_lim) / dy ~= q_zy = q_y / w,
    // qm_zx = round(m_zx / q_zx), qm_zy = round(m_zy / q_zy).

    int32_t qmx = abs(qm[0][0]) + abs(qm[0][1]);
    int32_t qmy = abs(qm[1][0]) + abs(qm[1][1]);
    w = POSITION_PRECISION * FFMAX(qmx, qmy);
    mul[0] *= w;
    mul[1] *= w;

    for (int j = 0; j < 2; j++) {
        double val = m[2][j] * mul[j];
        if (!(fabs(val) < max_val))
            return false;
        qm[2][j] = lrint(val);
    }

    if (first && offset) {
        offset->x = center[0] - qr[0];
        offset->y = center[1] - qr[1];
    }
    *pos = (ASS_Vector) {
        .x = qr[0] >> SUBPIXEL_ORDER,
        .y = qr[1] >> SUBPIXEL_ORDER,
    };
    key->offset.x = qr[0] & ((1 << SUBPIXEL_ORDER) - 1);
    key->offset.y = qr[1] & ((1 << SUBPIXEL_ORDER) - 1);
    key->matrix_x.x = qm[0][0];  key->matrix_x.y = qm[0][1];
    key->matrix_y.x = qm[1][0];  key->matrix_y.y = qm[1][1];
    key->matrix_z.x = qm[2][0];  key->matrix_z.y = qm[2][1];
    return true;
}

static void restore_transform(double m[3][3], const BitmapHashKey *key)
{
    const ASS_Rect *bbox = &key->outline->cbox;
    double x0 = (bbox->x_min + bbox->x_max) / 2.0;
    double y0 = (bbox->y_min + bbox->y_max) / 2.0;
    double dx = (bbox->x_max - bbox->x_min) / 2.0 + 64;
    double dy = (bbox->y_max - bbox->y_min) / 2.0 + 64;

    // Arbitrary scale has chosen so that z0 = 1
    double q_x = POSITION_PRECISION / dx;
    double q_y = POSITION_PRECISION / dy;
    m[0][0] = key->matrix_x.x * q_x;
    m[0][1] = key->matrix_x.y * q_y;
    m[1][0] = key->matrix_y.x * q_x;
    m[1][1] = key->matrix_y.y * q_y;

    int32_t qmx = abs(key->matrix_x.x) + abs(key->matrix_x.y);
    int32_t qmy = abs(key->matrix_y.x) + abs(key->matrix_y.y);
    double scale_z = 1.0 / POSITION_PRECISION / FFMAX(qmx, qmy);
    m[2][0] = key->matrix_z.x * q_x * scale_z;  // qm_zx * q_zx
    m[2][1] = key->matrix_z.y * q_y * scale_z;  // qm_zy * q_zy

    m[0][2] = m[1][2] = 0;
    m[2][2] = 1 + fabs(m[2][0]) * dx + fabs(m[2][1]) * dy;
    m[2][2] = FFMIN(m[2][2], MAX_PERSP_SCALE);

    double center[2] = {
        key->offset.x * (64 >> SUBPIXEL_ORDER),
        key->offset.y * (64 >> SUBPIXEL_ORDER),
    };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] += m[2][j] * center[i];

    for (int i = 0; i < 3; i++)
        m[i][2] -= m[i][0] * x0 + m[i][1] * y0;
}

// Calculate bitmap memory footprint
static inline size_t bitmap_size(const Bitmap *bm)
{
    return bm->stride * bm->h;
}

/**
 * Iterate through a list of bitmaps and blend with clip vector, if
 * applicable. The blended bitmaps are added to a free list which is freed
 * at the start of a new frame.
 */
static void blend_vector_clip(RenderContext *state, ASS_Image *head)
{
    if (!state->clip_drawing_text.str)
        return;

    ASS_Renderer *render_priv = state->renderer;

    OutlineHashKey ol_key;
    ol_key.type = OUTLINE_DRAWING;
    ol_key.u.drawing.text = state->clip_drawing_text;

    double m[3][3] = {{0}};
    int32_t scale_base = lshiftwrapi(1, state->clip_drawing_scale - 1);
    double w = scale_base > 0 ? (1.0 / scale_base) : 0;
    m[0][0] = state->screen_scale_x * w;
    m[1][1] = state->screen_scale_y * w;
    m[2][2] = 1;

    m[0][2] = int_to_d6(render_priv->settings.left_margin);
    m[1][2] = int_to_d6(render_priv->settings.top_margin);

    ASS_Vector pos;
    BitmapHashKey key;
    key.outline = ass_cache_get(render_priv->cache.outline_cache, &ol_key, render_priv);
    if (!key.outline || !key.outline->valid ||
            !quantize_transform(m, &pos, NULL, true, &key)) {
        ass_cache_dec_ref(key.outline);
        return;
    }
    Bitmap *clip_bm = ass_cache_get(render_priv->cache.bitmap_cache, &key, state);
    if (!clip_bm)
        return;

    // Iterate through bitmaps and blend/clip them
    for (ASS_Image *cur = head; cur; cur = cur->next) {
        int left, top, right, bottom, w, h;
        int ax, ay, aw, ah, as;
        int bx, by, bw, bh, bs;
        int aleft, atop, bleft, btop;
        unsigned char *abuffer, *bbuffer, *nbuffer;

        abuffer = cur->bitmap;
        bbuffer = clip_bm->buffer;
        ax = cur->dst_x;
        ay = cur->dst_y;
        aw = cur->w;
        ah = cur->h;
        as = cur->stride;
        bx = pos.x + clip_bm->left;
        by = pos.y + clip_bm->top;
        bw = clip_bm->w;
        bh = clip_bm->h;
        bs = clip_bm->stride;

        // Calculate overlap coordinates
        left = (ax > bx) ? ax : bx;
        top = (ay > by) ? ay : by;
        right = ((ax + aw) < (bx + bw)) ? (ax + aw) : (bx + bw);
        bottom = ((ay + ah) < (by + bh)) ? (ay + ah) : (by + bh);
        aleft = left - ax;
        atop = top - ay;
        w = right - left;
        h = bottom - top;
        bleft = left - bx;
        btop = top - by;

        unsigned align = 1 << render_priv->engine.align_order;
        if (state->clip_drawing_mode) {
            // Inverse clip
            if (ax + aw < bx || ay + ah < by || ax > bx + bw ||
                ay > by + bh || !h || !w) {
                continue;
            }

            // Allocate new buffer and add to free list
            nbuffer = ass_aligned_alloc(align, as * ah + align, false);
            if (!nbuffer)
                break;

            // Blend together
            memcpy(nbuffer, abuffer, ((ah - 1) * as) + aw);
            render_priv->engine.imul_bitmaps(nbuffer + atop * as + aleft, as,
                                             bbuffer + btop * bs + bleft, bs,
                                             w, h);
        } else {
            // Regular clip
            if (ax + aw < bx || ay + ah < by || ax > bx + bw ||
                ay > by + bh || !h || !w) {
                cur->w = cur->h = cur->stride = 0;
                continue;
            }

            // Allocate new buffer and add to free list
            unsigned ns = ass_align(align, w);
            nbuffer = ass_aligned_alloc(align, ns * h + align, false);
            if (!nbuffer)
                break;

            // Blend together
            render_priv->engine.mul_bitmaps(nbuffer, ns,
                                            abuffer + atop * as + aleft, as,
                                            bbuffer + btop * bs + bleft, bs,
                                            w, h);
            cur->dst_x += aleft;
            cur->dst_y += atop;
            cur->w = w;
            cur->h = h;
            cur->stride = ns;
        }

        ASS_ImagePriv *priv = (ASS_ImagePriv *) cur;
        priv->buffer = cur->bitmap = nbuffer;
        ass_cache_dec_ref(priv->source);
        priv->source = NULL;
    }

    ass_cache_dec_ref(clip_bm);
}

/**
 * \brief Convert TextInfo struct to ASS_Image list
 * Splits glyphs in halves when needed (for \kf karaoke).
 */
static ASS_Image *render_text(RenderContext *state)
{
    ASS_Image *head;
    ASS_Image **tail = &head;
    unsigned n_bitmaps = state->text_info->n_bitmaps;
    CombinedBitmapInfo *bitmaps = state->text_info->combined_bitmaps;

    for (unsigned i = 0; i < n_bitmaps; i++) {
        CombinedBitmapInfo *info = &bitmaps[i];
        if (!info->bm_s || state->border_style == 4)
            continue;

        tail =
            render_glyph(state, info->bm_s, info->x, info->y, info->c[3], 0,
                         1000000, tail, IMAGE_TYPE_SHADOW, info->image);
    }

    for (unsigned i = 0; i < n_bitmaps; i++) {
        CombinedBitmapInfo *info = &bitmaps[i];
        if (!info->bm_o)
            continue;

        if ((info->effect_type == EF_KARAOKE_KO)
                && (info->effect_timing <= 0)) {
            // do nothing
        } else {
            tail =
                render_glyph(state, info->bm_o, info->x, info->y, info->c[2],
                             0, 1000000, tail, IMAGE_TYPE_OUTLINE, info->image);
        }
    }

    for (unsigned i = 0; i < n_bitmaps; i++) {
        CombinedBitmapInfo *info = &bitmaps[i];
        if (!info->bm)
            continue;

        if ((info->effect_type == EF_KARAOKE)
                || (info->effect_type == EF_KARAOKE_KO)) {
            if (info->effect_timing > 0)
                tail =
                    render_glyph(state, info->bm, info->x, info->y,
                                 info->c[0], 0, 1000000, tail,
                                 IMAGE_TYPE_CHARACTER, info->image);
            else
                tail =
                    render_glyph(state, info->bm, info->x, info->y,
                                 info->c[1], 0, 1000000, tail,
                                 IMAGE_TYPE_CHARACTER, info->image);
        } else if (info->effect_type == EF_KARAOKE_KF) {
            tail =
                render_glyph(state, info->bm, info->x, info->y, info->c[0],
                             info->c[1], info->effect_timing, tail,
                             IMAGE_TYPE_CHARACTER, info->image);
        } else
            tail =
                render_glyph(state, info->bm, info->x, info->y, info->c[0],
                             0, 1000000, tail, IMAGE_TYPE_CHARACTER, info->image);
    }

    for (unsigned i = 0; i < n_bitmaps; i++)
        ass_cache_dec_ref(bitmaps[i].image);

    *tail = 0;
    blend_vector_clip(state, head);

    return head;
}

static void compute_string_bbox(TextInfo *text, ASS_DRect *bbox)
{
    if (text->length > 0) {
        bbox->x_min = +32000;
        bbox->x_max = -32000;
        bbox->y_min = -text->lines[0].asc;
        bbox->y_max = bbox->y_min + text->height;

        for (int i = 0; i < text->length; i++) {
            GlyphInfo *info = text->glyphs + i;
            if (info->skip) continue;
            double s = d6_to_double(info->pos.x);
            double e = s + d6_to_double(info->cluster_advance.x);
            bbox->x_min = FFMIN(bbox->x_min, s);
            bbox->x_max = FFMAX(bbox->x_max, e);
        }
    } else
        bbox->x_min = bbox->x_max = bbox->y_min = bbox->y_max = 0;
}

static ASS_Style *handle_selective_style_overrides(RenderContext *state,
                                                   ASS_Style *rstyle)
{
    // The script style is the one the event was declared with.
    ASS_Renderer *render_priv = state->renderer;
    ASS_Style *script = render_priv->track->styles +
                        state->event->Style;
    // The user style was set with ass_set_selective_style_override().
    ASS_Style *user = &render_priv->user_override_style;
    ASS_Style *new = &state->override_style_temp_storage;
    int explicit = state->explicit;
    int requested = render_priv->settings.selective_style_overrides;
    double scale;

    // Either the event's style, or the style forced with a \r tag.
    if (!rstyle)
        rstyle = script;

    // Create a new style that contains a mix of the original style and
    // user_style (the user's override style). Copy only fields from the
    // script's style that are deemed necessary.
    *new = *rstyle;

    state->apply_font_scale =
        !explicit || !(requested & ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE);

    // On positioned events, do not apply most overrides.
    if (explicit)
        requested = 0;

    if (requested & ASS_OVERRIDE_BIT_STYLE)
        requested |= ASS_OVERRIDE_BIT_FONT_NAME |
                     ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS |
                     ASS_OVERRIDE_BIT_COLORS |
                     ASS_OVERRIDE_BIT_BORDER |
                     ASS_OVERRIDE_BIT_ATTRIBUTES;

    // Copies fields even not covered by any of the other bits.
    if (requested & ASS_OVERRIDE_FULL_STYLE)
        *new = *user;

    // The user style is supposed to be independent of the script resolution.
    // Treat the user style's values as if they were specified for a script with
    // PlayResY=288, and rescale the values to the current script.
    scale = render_priv->track->PlayResY / 288.0;

    if (requested & ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS) {
        new->FontSize = user->FontSize * scale;
        new->Spacing = user->Spacing * scale;
        new->ScaleX = user->ScaleX;
        new->ScaleY = user->ScaleY;
    }

    if (requested & ASS_OVERRIDE_BIT_FONT_NAME) {
        new->FontName = user->FontName;
        new->treat_fontname_as_pattern = user->treat_fontname_as_pattern;
    }

    if (requested & ASS_OVERRIDE_BIT_COLORS) {
        new->PrimaryColour = user->PrimaryColour;
        new->SecondaryColour = user->SecondaryColour;
        new->OutlineColour = user->OutlineColour;
        new->BackColour = user->BackColour;
    }

    if (requested & ASS_OVERRIDE_BIT_ATTRIBUTES) {
        new->Bold = user->Bold;
        new->Italic = user->Italic;
        new->Underline = user->Underline;
        new->StrikeOut = user->StrikeOut;
    }

    if (requested & ASS_OVERRIDE_BIT_BORDER) {
        new->BorderStyle = user->BorderStyle;
        new->Outline = user->Outline * scale;
        new->Shadow = user->Shadow * scale;
    }

    if (requested & ASS_OVERRIDE_BIT_ALIGNMENT)
        new->Alignment = user->Alignment;

    if (requested & ASS_OVERRIDE_BIT_JUSTIFY)
        new->Justify = user->Justify;

    if (requested & ASS_OVERRIDE_BIT_MARGINS) {
        new->MarginL = user->MarginL;
        new->MarginR = user->MarginR;
        new->MarginV = user->MarginV;
    }

    if (!new->FontName)
        new->FontName = rstyle->FontName;

    state->style = new;
    state->overrides = requested;

    return new;
}

ASS_Vector ass_layout_res(ASS_Renderer *render_priv)
{
    ASS_Track *track = render_priv->track;
    if (track->LayoutResX > 0 && track->LayoutResY > 0)
        return (ASS_Vector) { track->LayoutResX, track->LayoutResY };

    ASS_Settings *settings = &render_priv->settings;
    if (settings->storage_width > 0 && settings->storage_height > 0)
        return (ASS_Vector) { settings->storage_width, settings->storage_height };

    if (settings->par <= 0 || settings->par == 1 ||
            !render_priv->frame_content_width || !render_priv->frame_content_height)
        return (ASS_Vector) { track->PlayResX, track->PlayResY };
    if (settings->par > 1)
        return (ASS_Vector) {
            FFMAX(1, lround(track->PlayResY * render_priv->frame_content_width
                    / render_priv->frame_content_height / settings->par)),
            track->PlayResY
        };
    else
        return (ASS_Vector) {
            track->PlayResX,
            FFMAX(1, lround(track->PlayResX * render_priv->frame_content_height
                    / render_priv->frame_content_width * settings->par))
        };
}

static void init_font_scale(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    ASS_Settings *settings_priv = &render_priv->settings;

    double font_scr_w = render_priv->frame_content_width;
    double font_scr_h = render_priv->frame_content_height;
    if (!state->explicit && render_priv->settings.use_margins) {
        font_scr_w = render_priv->fit_width;
        font_scr_h = render_priv->fit_height;
    }

    state->screen_scale_x = font_scr_w / render_priv->track->PlayResX;
    state->screen_scale_y = font_scr_h / render_priv->track->PlayResY;

    ASS_Vector layout_res = ass_layout_res(render_priv);
    state->blur_scale_x = font_scr_w / layout_res.x;
    state->blur_scale_y = font_scr_h / layout_res.y;
    if (render_priv->track->ScaledBorderAndShadow) {
        state->border_scale_x = state->screen_scale_x;
        state->border_scale_y = state->screen_scale_y;
    } else {
        state->border_scale_x = state->blur_scale_x;
        state->border_scale_y = state->blur_scale_y;
    }

    if (state->apply_font_scale) {
        state->screen_scale_x *= settings_priv->font_size_coeff;
        state->screen_scale_y *= settings_priv->font_size_coeff;
        state->border_scale_x *= settings_priv->font_size_coeff;
        state->border_scale_y *= settings_priv->font_size_coeff;
        state->blur_scale_x *= settings_priv->font_size_coeff;
        state->blur_scale_y *= settings_priv->font_size_coeff;
    }
}

/**
 * \brief partially reset render_context to style values
 * Works like {\r}: resets some style overrides
 */
void ass_reset_render_context(RenderContext *state, ASS_Style *style)
{
    style = handle_selective_style_overrides(state, style);

    init_font_scale(state);

    state->c[0] = style->PrimaryColour;
    state->c[1] = style->SecondaryColour;
    state->c[2] = style->OutlineColour;
    state->c[3] = style->BackColour;
    state->flags =
        (style->Underline ? DECO_UNDERLINE : 0) |
        (style->StrikeOut ? DECO_STRIKETHROUGH : 0);
    state->font_size = style->FontSize;

    state->family.str = style->FontName;
    state->family.len = strlen(style->FontName);
    state->treat_family_as_pattern = style->treat_fontname_as_pattern;
    state->bold = style->Bold;
    state->italic = style->Italic;
    ass_update_font(state);

    state->border_style = style->BorderStyle;
    state->border_x = style->Outline;
    state->border_y = style->Outline;
    state->scale_x = style->ScaleX;
    state->scale_y = style->ScaleY;
    state->hspacing = style->Spacing;
    state->be = 0;
    state->blur = style->Blur;
    state->shadow_x = style->Shadow;
    state->shadow_y = style->Shadow;
    state->frx = state->fry = 0.;
    state->frz = style->Angle;
    state->fax = state->fay = 0.;
    state->font_encoding = style->Encoding;
}

/**
 * \brief Start new event. Reset state.
 */
static void
init_render_context(RenderContext *state, ASS_Event *event)
{
    ASS_Renderer *render_priv = state->renderer;

    state->event = event;
    state->parsed_tags = 0;
    state->evt_type = EVENT_NORMAL;

    state->wrap_style = render_priv->track->WrapStyle;

    state->pos_x = 0;
    state->pos_y = 0;
    state->org_x = 0;
    state->org_y = 0;
    state->have_origin = 0;
    state->clip_x0 = 0;
    state->clip_y0 = 0;
    state->clip_x1 = render_priv->track->PlayResX;
    state->clip_y1 = render_priv->track->PlayResY;
    state->clip_mode = 0;
    state->detect_collisions = 1;
    state->fade = 0;
    state->drawing_scale = 0;
    state->pbo = 0;
    state->effect_type = EF_NONE;
    state->effect_timing = 0;
    state->effect_skip_timing = 0;
    state->reset_effect = false;

    ass_apply_transition_effects(state);
    state->explicit = state->evt_type != EVENT_NORMAL ||
                      ass_event_has_hard_overrides(event->Text);

    ass_reset_render_context(state, NULL);
    state->alignment = state->style->Alignment;
    state->justify = state->style->Justify;
}

static void free_render_context(RenderContext *state)
{
    ass_cache_dec_ref(state->font);

    state->font = NULL;
    state->family.str = NULL;
    state->family.len = 0;
    state->clip_drawing_text.str = NULL;
    state->clip_drawing_text.len = 0;

    if (state->text_info)
        state->text_info->length = 0;
}

/**
 * \brief Get normal and outline (border) glyphs
 * \param info out: struct filled with extracted data
 * Tries to get both glyphs from cache.
 * If they can't be found, gets a glyph from font face, generates outline,
 * and add them to cache.
 */
static void
get_outline_glyph(RenderContext *state, GlyphInfo *info)
{
    ASS_Renderer *priv = state->renderer;
    OutlineHashValue *val;
    ASS_DVector scale, offset = {0};

    int32_t asc, desc;
    OutlineHashKey key;
    if (info->drawing_text.str) {
        key.type = OUTLINE_DRAWING;
        key.u.drawing.text = info->drawing_text;
        val = ass_cache_get(priv->cache.outline_cache, &key, priv);
        if (!val || !val->valid) {
            ass_cache_dec_ref(val);
            return;
        }

        int32_t scale_base = lshiftwrapi(1, info->drawing_scale - 1);
        double w = scale_base > 0 ? (1.0 / scale_base) : 0;
        scale.x = info->scale_x * w * state->screen_scale_x / priv->par_scale_x;
        scale.y = info->scale_y * w * state->screen_scale_y;
        desc = 64 * info->drawing_pbo;
        asc = val->asc - desc;

        offset.y = -asc * scale.y;
    } else {
        key.type = OUTLINE_GLYPH;
        GlyphHashKey *k = &key.u.glyph;
        k->font = info->font;
        k->size = info->font_size;
        k->face_index = info->face_index;
        k->glyph_index = info->glyph_index;
        k->bold = info->bold;
        k->italic = info->italic;
        k->flags = info->flags;

        val = ass_cache_get(priv->cache.outline_cache, &key, priv);
        if (!val || !val->valid) {
            ass_cache_dec_ref(val);
            return;
        }

        scale.x = info->scale_x;
        scale.y = info->scale_y;
        asc  = val->asc;
        desc = val->desc;
    }

    info->outline = val;
    info->transform.scale = scale;
    info->transform.offset = offset;

    info->bbox.x_min = lrint(val->cbox.x_min * scale.x + offset.x);
    info->bbox.y_min = lrint(val->cbox.y_min * scale.y + offset.y);
    info->bbox.x_max = lrint(val->cbox.x_max * scale.x + offset.x);
    info->bbox.y_max = lrint(val->cbox.y_max * scale.y + offset.y);

    if (info->drawing_text.str || priv->settings.shaper == ASS_SHAPING_SIMPLE) {
        info->cluster_advance.x = info->advance.x = lrint(val->advance * scale.x);
        info->cluster_advance.y = info->advance.y = 0;
    }
    info->asc  = lrint(asc  * scale.y);
    info->desc = lrint(desc * scale.y);
}

size_t ass_outline_construct(void *key, void *value, void *priv)
{
    ASS_Renderer *render_priv = priv;
    OutlineHashKey *outline_key = key;
    OutlineHashValue *v = value;
    memset(v, 0, sizeof(*v));

    switch (outline_key->type) {
    case OUTLINE_GLYPH:
        {
            GlyphHashKey *k = &outline_key->u.glyph;
            ass_face_set_size(k->font->faces[k->face_index], k->size);
            if (!ass_font_get_glyph(k->font, k->face_index, k->glyph_index,
                                    render_priv->settings.hinting))
                return 1;
            if (!ass_get_glyph_outline(&v->outline[0], &v->advance,
                                       k->font->faces[k->face_index],
                                       k->flags))
                return 1;
            ass_font_get_asc_desc(k->font, k->face_index,
                                  &v->asc, &v->desc);
            break;
        }
    case OUTLINE_DRAWING:
        {
            ASS_Rect bbox;
            const char *text = outline_key->u.drawing.text.str;  // always zero-terminated
            if (!ass_drawing_parse(&v->outline[0], &bbox, text, render_priv->library))
                return 1;

            v->advance = bbox.x_max - bbox.x_min;
            v->asc = bbox.y_max - bbox.y_min;
            v->desc = 0;
            break;
        }
    case OUTLINE_BORDER:
        {
            BorderHashKey *k = &outline_key->u.border;
            if (!k->border.x && !k->border.y)
                break;
            if (!k->outline->outline[0].n_points)
                break;

            ASS_Outline src;
            if (!ass_outline_scale_pow2(&src, &k->outline->outline[0],
                                        k->scale_ord_x, k->scale_ord_y))
                return 1;
            if (!ass_outline_stroke(&v->outline[0], &v->outline[1], &src,
                                    k->border.x * STROKER_PRECISION,
                                    k->border.y * STROKER_PRECISION,
                                    STROKER_PRECISION)) {
                ass_msg(render_priv->library, MSGL_WARN, "Cannot stroke outline");
                ass_outline_free(&v->outline[0]);
                ass_outline_free(&v->outline[1]);
                ass_outline_free(&src);
                return 1;
            }
            ass_outline_free(&src);
            break;
        }
    case OUTLINE_BOX:
        {
            ASS_Outline *ol = &v->outline[0];
            if (!ass_outline_alloc(ol, 4, 4))
                return 1;
            ol->points[0].x = ol->points[3].x = 0;
            ol->points[1].x = ol->points[2].x = 64;
            ol->points[0].y = ol->points[1].y = 0;
            ol->points[2].y = ol->points[3].y = 64;
            ol->segments[0] = OUTLINE_LINE_SEGMENT;
            ol->segments[1] = OUTLINE_LINE_SEGMENT;
            ol->segments[2] = OUTLINE_LINE_SEGMENT;
            ol->segments[3] = OUTLINE_LINE_SEGMENT | OUTLINE_CONTOUR_END;
            ol->n_points = ol->n_segments = 4;
            break;
        }
    default:
        return 1;
    }

    rectangle_reset(&v->cbox);
    ass_outline_update_cbox(&v->outline[0], &v->cbox);
    ass_outline_update_cbox(&v->outline[1], &v->cbox);
    if (v->cbox.x_min > v->cbox.x_max || v->cbox.y_min > v->cbox.y_max)
        v->cbox.x_min = v->cbox.y_min = v->cbox.x_max = v->cbox.y_max = 0;
    v->valid = true;
    return 1;
}

/**
 * \brief Calculate outline transformation matrix
 */
static void calc_transform_matrix(RenderContext *state,
                                  GlyphInfo *info, double m[3][3])
{
    ASS_Renderer *render_priv = state->renderer;

    double frx = ASS_PI / 180 * info->frx;
    double fry = ASS_PI / 180 * info->fry;
    double frz = ASS_PI / 180 * info->frz;

    double sx = -sin(frx), cx = cos(frx);
    double sy =  sin(fry), cy = cos(fry);
    double sz = -sin(frz), cz = cos(frz);

    double fax = info->fax * info->scale_x / info->scale_y;
    double fay = info->fay * info->scale_y / info->scale_x;
    double x1[3] = { 1, fax, info->shift.x + info->asc * fax };
    double y1[3] = { fay, 1, info->shift.y };

    double x2[3], y2[3];
    for (int i = 0; i < 3; i++) {
        x2[i] = x1[i] * cz - y1[i] * sz;
        y2[i] = x1[i] * sz + y1[i] * cz;
    }

    double y3[3], z3[3];
    for (int i = 0; i < 3; i++) {
        y3[i] = y2[i] * cx;
        z3[i] = y2[i] * sx;
    }

    double x4[3], z4[3];
    for (int i = 0; i < 3; i++) {
        x4[i] = x2[i] * cy - z3[i] * sy;
        z4[i] = x2[i] * sy + z3[i] * cy;
    }

    double dist = 20000 * state->blur_scale_y;
    z4[2] += dist;

    double scale_x = dist * render_priv->par_scale_x;
    double offs_x = info->pos.x - info->shift.x * render_priv->par_scale_x;
    double offs_y = info->pos.y - info->shift.y;
    for (int i = 0; i < 3; i++) {
        m[0][i] = z4[i] * offs_x + x4[i] * scale_x;
        m[1][i] = z4[i] * offs_y + y3[i] * dist;
        m[2][i] = z4[i];
    }
}

/**
 * \brief Get bitmaps for a glyph
 * \param info glyph info
 * Tries to get glyph bitmaps from bitmap cache.
 * If they can't be found, they are generated by rotating and rendering the glyph.
 * After that, bitmaps are added to the cache.
 * They are returned in info->bm (glyph), info->bm_o (outline).
 */
static void
get_bitmap_glyph(RenderContext *state, GlyphInfo *info,
                 int32_t *leftmost_x,
                 ASS_Vector *pos, ASS_Vector *pos_o,
                 ASS_DVector *offset, bool first, int flags)
{
    ASS_Renderer *render_priv = state->renderer;

    if (!info->outline || info->symbol == '\n' || info->symbol == 0 || info->skip) {
        ass_cache_dec_ref(info->outline);
        return;
    }

    double m1[3][3], m2[3][3], m[3][3];
    const ASS_Transform *tr = &info->transform;
    calc_transform_matrix(state, info, m1);
    for (int i = 0; i < 3; i++) {
        m2[i][0] = m1[i][0] * tr->scale.x;
        m2[i][1] = m1[i][1] * tr->scale.y;
        m2[i][2] = m1[i][0] * tr->offset.x + m1[i][1] * tr->offset.y + m1[i][2];
    }
    memcpy(m, m2, sizeof(m));

    if (info->effect_type == EF_KARAOKE_KF)
        ass_outline_update_min_transformed_x(&info->outline->outline[0], m, leftmost_x);

    BitmapHashKey key;
    key.outline = info->outline;
    if (!quantize_transform(m, pos, offset, first, &key)) {
        ass_cache_dec_ref(info->outline);
        return;
    }
    info->bm = ass_cache_get(render_priv->cache.bitmap_cache, &key, state);
    if (!info->bm || !info->bm->buffer) {
        ass_cache_dec_ref(info->bm);
        info->bm = NULL;
    }
    *pos_o = *pos;

    OutlineHashKey ol_key;
    if (flags & FILTER_BORDER_STYLE_3) {
        if (!(flags & (FILTER_NONZERO_BORDER | FILTER_NONZERO_SHADOW)))
            return;

        ol_key.type = OUTLINE_BOX;

        ASS_DVector bord = {
            64 * info->border_x * state->border_scale_x /
                render_priv->par_scale_x,
            64 * info->border_y * state->border_scale_y,
        };
        double width = info->hspacing_scaled + info->advance.x;
        double height = info->asc + info->desc;

        ASS_DVector orig_scale;
        orig_scale.x = info->scale_x * info->scale_fix;
        orig_scale.y = info->scale_y * info->scale_fix;

        // Emulate the WTFish behavior of VSFilter, i.e. double-scale
        // the sizes of the opaque box.
        bord.x *= orig_scale.x;
        bord.y *= orig_scale.y;
        width  *= orig_scale.x;
        height *= orig_scale.y;

        // to avoid gaps
        bord.x = FFMAX(64, bord.x);
        bord.y = FFMAX(64, bord.y);

        ASS_DVector scale = {
            (width  + 2 * bord.x) / 64,
            (height + 2 * bord.y) / 64,
        };
        ASS_DVector offset = { -bord.x, -bord.y - info->asc };
        for (int i = 0; i < 3; i++) {
            m[i][0] = m1[i][0] * scale.x;
            m[i][1] = m1[i][1] * scale.y;
            m[i][2] = m1[i][0] * offset.x + m1[i][1] * offset.y + m1[i][2];
        }
    } else {
        if (!(flags & FILTER_NONZERO_BORDER))
            return;

        ol_key.type = OUTLINE_BORDER;
        BorderHashKey *k = &ol_key.u.border;
        k->outline = info->outline;

        double bord_x =
            64 * state->border_scale_x * info->border_x / tr->scale.x /
                render_priv->par_scale_x;
        double bord_y =
            64 * state->border_scale_y * info->border_y / tr->scale.y;

        const ASS_Rect *bbox = &info->outline->cbox;
        // Estimate bounding box half size after stroking
        double dx = (bbox->x_max - bbox->x_min) / 2.0 + (bord_x + 64);
        double dy = (bbox->y_max - bbox->y_min) / 2.0 + (bord_y + 64);

        // Matrix after quantize_transform() has
        // input and output origin at bounding box center.
        double mxx = fabs(m[0][0]), mxy = fabs(m[0][1]);
        double myx = fabs(m[1][0]), myy = fabs(m[1][1]);
        double mzx = fabs(m[2][0]), mzy = fabs(m[2][1]);

        double z0 = m[2][2] - mzx * dx - mzy * dy;
        double w = 1 / FFMAX(z0, m[2][2] / MAX_PERSP_SCALE);

        // Notation from quantize_transform().
        // Note that goal here is to estimate acceptable error for stroking, i. e. D(x) and D(y).
        // Matrix coefficients are constants now, so D(m_ij) = 0 for all i, j from {x, y, z}.

        // D(z) <= |m_zx| * D(x) + |m_zy| * D(y),
        // D(x_out) = D((m_xx * x + m_xy * y) / z)
        //  <= (|m_xx| * D(x) + |m_xy| * D(y)) / z0 + x_lim * D(z) / z0^2
        //  <= (|m_xx| / z0 + |m_zx| * x_lim / z0^2) * D(x)
        //   + (|m_xy| / z0 + |m_zy| * x_lim / z0^2) * D(y),
        // D(y_out) = D((m_yx * x + m_yy * y) / z)
        //  <= (|m_yx| * D(x) + |m_yy| * D(y)) / z0 + y_lim * D(z) / z0^2
        //  <= (|m_yx| / z0 + |m_zx| * y_lim / z0^2) * D(x)
        //   + (|m_yy| / z0 + |m_zy| * y_lim / z0^2) * D(y).

        // Quantization steps (pick: ACCURACY = POSITION_PRECISION):
        // STROKER_PRECISION / 2^scale_ord_x ~ D(x) ~ POSITION_PRECISION /
        //   (max(|m_xx|, |m_yx|) / z0 + |m_zx| * max(x_lim, y_lim) / z0^2),
        // STROKER_PRECISION / 2^scale_ord_y ~ D(y) ~ POSITION_PRECISION /
        //   (max(|m_xy|, |m_yy|) / z0 + |m_zy| * max(x_lim, y_lim) / z0^2).

        double x_lim = mxx * dx + mxy * dy;
        double y_lim = myx * dx + myy * dy;
        double rz = FFMAX(x_lim, y_lim) * w;

        w *= STROKER_PRECISION / POSITION_PRECISION;
        frexp(w * (FFMAX(mxx, myx) + mzx * rz), &k->scale_ord_x);
        frexp(w * (FFMAX(mxy, myy) + mzy * rz), &k->scale_ord_y);
        bord_x = ldexp(bord_x, k->scale_ord_x);
        bord_y = ldexp(bord_y, k->scale_ord_y);
        if (!(bord_x < OUTLINE_MAX && bord_y < OUTLINE_MAX))
            return;
        k->border.x = lrint(bord_x / STROKER_PRECISION);
        k->border.y = lrint(bord_y / STROKER_PRECISION);
        if (!k->border.x && !k->border.y) {
            ass_cache_inc_ref(info->bm);
            info->bm_o = info->bm;
            return;
        }

        for (int i = 0; i < 3; i++) {
            m[i][0] = ldexp(m2[i][0], -k->scale_ord_x);
            m[i][1] = ldexp(m2[i][1], -k->scale_ord_y);
            m[i][2] = m2[i][2];
        }
    }

    key.outline = ass_cache_get(render_priv->cache.outline_cache, &ol_key, render_priv);
    if (!key.outline || !key.outline->valid ||
            !quantize_transform(m, pos_o, offset, false, &key)) {
        ass_cache_dec_ref(key.outline);
        return;
    }
    info->bm_o = ass_cache_get(render_priv->cache.bitmap_cache, &key, state);
    if (!info->bm_o || !info->bm_o->buffer) {
        ass_cache_dec_ref(info->bm_o);
        info->bm_o = NULL;
        *pos_o = *pos;
    } else if (!info->bm)
        *pos = *pos_o;
}

static inline size_t outline_size(const ASS_Outline* outline)
{
    return sizeof(ASS_Vector) * outline->n_points + outline->n_segments;
}

size_t ass_bitmap_construct(void *key, void *value, void *priv)
{
    RenderContext *state = priv;
    BitmapHashKey *k = key;
    Bitmap *bm = value;

    double m[3][3];
    restore_transform(m, k);

    ASS_Outline outline[2];
    if (k->matrix_z.x || k->matrix_z.y) {
        ass_outline_transform_3d(&outline[0], &k->outline->outline[0], m);
        ass_outline_transform_3d(&outline[1], &k->outline->outline[1], m);
    } else {
        ass_outline_transform_2d(&outline[0], &k->outline->outline[0], m);
        ass_outline_transform_2d(&outline[1], &k->outline->outline[1], m);
    }

    if (!ass_outline_to_bitmap(state, bm, &outline[0], &outline[1]))
        memset(bm, 0, sizeof(*bm));
    ass_outline_free(&outline[0]);
    ass_outline_free(&outline[1]);

    return sizeof(BitmapHashKey) + sizeof(Bitmap) + bitmap_size(bm) +
           sizeof(OutlineHashValue) + outline_size(&k->outline->outline[0]) + outline_size(&k->outline->outline[1]);
}

static void measure_text_on_eol(RenderContext *state, double scale, int cur_line,
                                int max_asc, int max_desc,
                                double max_border_x, double max_border_y)
{
    TextInfo *text_info = state->text_info;
    text_info->lines[cur_line].asc  = scale * max_asc;
    text_info->lines[cur_line].desc = scale * max_desc;
    text_info->height += scale * max_asc + scale * max_desc;
    // For *VSFilter compatibility do biased rounding on max_border*
    // https://github.com/Cyberbeing/xy-VSFilter/blob/xy_sub_filter_rc4@%7B2020-05-17%7D/src/subtitles/RTS.cpp#L1465
    text_info->border_bottom = (int) (state->border_scale_y * max_border_y + 0.5);
    if (cur_line == 0)
        text_info->border_top = text_info->border_bottom;
    // VSFilter takes max \bordx into account for collision, even if far from edge
    text_info->border_x = FFMAX(text_info->border_x,
            (int) (state->border_scale_x * max_border_x + 0.5));
}


/**
 * This function goes through text_info and calculates text parameters.
 * The following text_info fields are filled:
 *   height
 *   border_top
 *   border_bottom
 *   border_x
 *   lines[].asc
 *   lines[].desc
 */
static void measure_text(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    text_info->height = 0;
    text_info->border_x = 0;

    int cur_line = 0;
    double scale = 0.5 / 64;
    int max_asc = 0, max_desc = 0;
    double max_border_y = 0, max_border_x = 0;
    bool empty_trimmed_line = true;
    for (int i = 0; i < text_info->length; i++) {
        if (text_info->glyphs[i].linebreak) {
            measure_text_on_eol(state, scale, cur_line,
                    max_asc, max_desc, max_border_x, max_border_y);
            empty_trimmed_line = true;
            max_asc = max_desc = 0;
            max_border_y = max_border_x = 0;
            scale = 0.5 / 64;
            cur_line++;
        }
        GlyphInfo *cur = text_info->glyphs + i;
        // VSFilter ignores metrics of line-leading/trailing (trimmed)
        // whitespace, except when the line becomes empty after trimming
        if (empty_trimmed_line && !cur->is_trimmed_whitespace) {
            empty_trimmed_line = false;
            // Forget metrics of line-leading whitespace
            max_asc = max_desc = 0;
            max_border_y = max_border_x = 0;
        } else if (!empty_trimmed_line && cur->is_trimmed_whitespace) {
            // Ignore metrics of line-trailing whitespace
            continue;
        }
        max_asc  = FFMAX(max_asc,  cur->asc);
        max_desc = FFMAX(max_desc, cur->desc);
        max_border_y = FFMAX(max_border_y, cur->border_y);
        max_border_x = FFMAX(max_border_x, cur->border_x);
        if (cur->symbol != '\n')
            scale = 1.0 / 64;
    }
    assert(cur_line == text_info->n_lines - 1);
    measure_text_on_eol(state, scale, cur_line,
            max_asc, max_desc, max_border_x, max_border_y);
    text_info->height += cur_line * render_priv->settings.line_spacing;
}

/**
 * Mark extra whitespace for later removal.
 */
#define IS_WHITESPACE(x) ((x->symbol == ' ' || x->symbol == '\n') \
                          && !x->linebreak)
static void trim_whitespace(RenderContext *state)
{
    int i, j;
    GlyphInfo *cur;
    TextInfo *ti = state->text_info;

    // Mark trailing spaces
    i = ti->length - 1;
    cur = ti->glyphs + i;
    while (i && IS_WHITESPACE(cur)) {
        cur->skip = true;
        cur->is_trimmed_whitespace = true;
        cur = ti->glyphs + --i;
    }

    // Mark leading whitespace
    i = 0;
    cur = ti->glyphs;
    while (i < ti->length && IS_WHITESPACE(cur)) {
        cur->skip = true;
        cur->is_trimmed_whitespace = true;
        cur = ti->glyphs + ++i;
    }
    if (i < ti->length)
        cur->starts_new_run = true;

    // Mark all extraneous whitespace inbetween
    for (i = 0; i < ti->length; ++i) {
        cur = ti->glyphs + i;
        if (cur->linebreak) {
            // Mark whitespace before
            j = i - 1;
            cur = ti->glyphs + j;
            while (j && IS_WHITESPACE(cur)) {
                cur->skip = true;
                cur->is_trimmed_whitespace = true;
                cur = ti->glyphs + --j;
            }
            // A break itself can contain a whitespace, too
            cur = ti->glyphs + i;
            if (cur->symbol == ' ' || cur->symbol == '\n') {
                cur->skip = true;
                cur->is_trimmed_whitespace = true;
                // Mark whitespace after
                j = i + 1;
                cur = ti->glyphs + j;
                while (j < ti->length && IS_WHITESPACE(cur)) {
                    cur->skip = true;
                    cur->is_trimmed_whitespace = true;
                    cur = ti->glyphs + ++j;
                }
                i = j - 1;
            }
            if (cur < ti->glyphs + ti->length)
                cur->starts_new_run = true;
        }
    }
}
#undef IS_WHITESPACE

#ifdef CONFIG_UNIBREAK
    #define ALLOWBREAK(glyph, index) (unibrks ? unibrks[index] == LINEBREAK_ALLOWBREAK : glyph == ' ')
    #define FORCEBREAK(glyph, index) (unibrks ? unibrks[index] == LINEBREAK_MUSTBREAK  : glyph == '\n')
#else
    #define ALLOWBREAK(glyph, index) (glyph == ' ')
    #define FORCEBREAK(glyph, index) (glyph == '\n')
#endif

/*
 * Starts a new line on the first breakable character after overflow
 */
static void
wrap_lines_naive(RenderContext *state, double max_text_width, char *unibrks)
{
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    GlyphInfo *s1  = text_info->glyphs; // current line start
    int last_breakable = -1;
    int break_type = 0;

    text_info->n_lines = 1;
    for (int i = 0; i < text_info->length; ++i) {
        GlyphInfo *cur = text_info->glyphs + i;
        int break_at = -1;
        double s_offset = d6_to_double(s1->bbox.x_min + s1->pos.x);
        double len = d6_to_double(cur->bbox.x_max + cur->pos.x) - s_offset;

        if (FORCEBREAK(cur->symbol, i)) {
            break_type = 2;
            break_at = i;
            ass_msg(render_priv->library, MSGL_DBG2,
                    "forced line break at %d", break_at);
        } else if (len >= max_text_width &&
                   cur->symbol != ' ' /* get trimmed */ &&
                   (state->wrap_style != 2)) {
            break_type = 1;
            break_at = last_breakable;
            if (break_at >= 0)
                ass_msg(render_priv->library, MSGL_DBG2, "line break at %d",
                        break_at);
        }
        if (ALLOWBREAK(cur->symbol, i)) {
            last_breakable = i;
        }

        if (break_at != -1) {
            // need to use one more line
            // marking break_at+1 as start of a new line
            int lead = break_at + 1;    // the first symbol of the new line
            if (text_info->n_lines >= text_info->max_lines) {
                // Try to raise the maximum number of lines
                bool success = false;
                if (text_info->max_lines <= INT_MAX / 2) {
                    text_info->max_lines *= 2;
                    success = ASS_REALLOC_ARRAY(text_info->lines, text_info->max_lines);
                }
                // If realloc fails it's screwed and due to error-info not propagating (FIXME),
                // the best we can do is to avoid UB by discarding the previous break
                if (!success) {
                    s1->linebreak = 0;
                    text_info->n_lines--;
                }
            }
            if (lead < text_info->length) {
                text_info->glyphs[lead].linebreak = break_type;
                last_breakable = -1;
                s1 = text_info->glyphs + lead;
                text_info->n_lines++;
            }
        }
    }
}

/*
 * Rewind from a linestart position back to the first non-whitespace (0x20)
 * character. Trailing ASCII whitespace gets trimmed in rendering.
 * Assumes both arguments are part of the same array.
 */
static inline GlyphInfo *rewind_trailing_spaces(GlyphInfo *start1, GlyphInfo* start2)
{
    GlyphInfo *g = start2;
    do {
        --g;
    } while ((g > start1) && (g->symbol == ' '));
    return g;
}

/*
 * Shift soft linebreaks to balance out line lengths
 * Does not change the linebreak count
 * FIXME: implement style 0 and 3 correctly
 */
static void
wrap_lines_rebalance(RenderContext *state, double max_text_width, char *unibrks)
{
    TextInfo *text_info = state->text_info;
    int exit = 0;

#define DIFF(x,y) (((x) < (y)) ? (y - x) : (x - y))
    while (!exit && state->wrap_style != 1) {
        exit = 1;
        GlyphInfo  *s1, *s2, *s3;
        s3 = text_info->glyphs;
        s1 = s2 = 0;
        for (int i = 0; i <= text_info->length; ++i) {
            GlyphInfo *cur = text_info->glyphs + i;
            if ((i == text_info->length) || cur->linebreak) {
                s1 = s2;
                s2 = s3;
                s3 = cur;
                if (s1 && (s2->linebreak == 1)) {       // have at least 2 lines, and linebreak is 'soft'
                    double l1, l2, l1_new, l2_new;

                    // Find last word of line and trim surrounding whitespace before measuring
                    // (whitespace ' ' will also get trimmed in rendering)
                    GlyphInfo *w = rewind_trailing_spaces(s1, s2);
                    GlyphInfo *e1_old = w;
                    while ((w > s1) && (!ALLOWBREAK(w->symbol, w - text_info->glyphs))) {
                        --w;
                    }
                    GlyphInfo *e1 = w;
                    while ((e1 > s1) && (e1->symbol == ' ')) {
                        --e1;
                    }
                    if (w->symbol == ' ')
                        ++w;
                    if (w == s1)
                        continue; // Merging linebreaks is never beneficial

                    GlyphInfo *e2 = rewind_trailing_spaces(s2, s3);

                    l1 = d6_to_double(
                        (e1_old->bbox.x_max + e1_old->pos.x) -
                        (s1->bbox.x_min + s1->pos.x));
                    l2 = d6_to_double(
                        (e2->bbox.x_max + e2->pos.x) -
                        (s2->bbox.x_min + s2->pos.x));
                    l1_new = d6_to_double(
                        (e1->bbox.x_max + e1->pos.x) -
                        (s1->bbox.x_min + s1->pos.x));
                    l2_new = d6_to_double(
                        (e2->bbox.x_max + e2->pos.x) -
                        (w->bbox.x_min + w->pos.x));

                    if (DIFF(l1_new, l2_new) < DIFF(l1, l2)) {
                        w->linebreak = 1;
                        s2->linebreak = 0;
                        s2 = w;
                        exit = 0;
                    }
                }
            }
            if (i == text_info->length)
                break;
        }

    }
    assert(text_info->n_lines >= 1);
#undef DIFF
}

static void
wrap_lines_measure(RenderContext *state, char *unibrks)
{
    TextInfo *text_info = state->text_info;
    int cur_line = 1;
    int i = 0;

    while (i < text_info->length && text_info->glyphs[i].skip)
        ++i;
    double pen_shift_x = d6_to_double(-text_info->glyphs[i].pos.x);
    double pen_shift_y = 0.;

    for (i = 0; i < text_info->length; ++i) {
        GlyphInfo *cur = text_info->glyphs + i;
        if (cur->linebreak) {
            while (i < text_info->length && cur->skip && !FORCEBREAK(cur->symbol, i))
                cur = text_info->glyphs + ++i;
            double height =
                text_info->lines[cur_line - 1].desc +
                text_info->lines[cur_line].asc;
            text_info->lines[cur_line - 1].len = i -
                text_info->lines[cur_line - 1].offset;
            text_info->lines[cur_line].offset = i;
            cur_line++;
            pen_shift_x = d6_to_double(-cur->pos.x);
            pen_shift_y += height + state->renderer->settings.line_spacing;
        }
        cur->pos.x += double_to_d6(pen_shift_x);
        cur->pos.y += double_to_d6(pen_shift_y);
    }
    text_info->lines[cur_line - 1].len =
        text_info->length - text_info->lines[cur_line - 1].offset;
}

#undef ALLOWBREAK
#undef FORCEBREAK

/**
 * \brief rearrange text between lines
 * \param max_text_width maximal text line width in pixels
 * The algo is similar to the one in libvo/sub.c:
 * 1. Place text, wrapping it when current line is full
 * 2. Try moving words from the end of a line to the beginning of the next one while it reduces
 * the difference in lengths between this two lines.
 * The result may not be optimal, but usually is good enough.
 *
 * FIXME: implement style 0 and 3 correctly
 */
static void
wrap_lines_smart(RenderContext *state, double max_text_width)
{
    char *unibrks = NULL;

#ifdef CONFIG_UNIBREAK
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    if (render_priv->track->parser_priv->feature_flags & FEATURE_MASK(ASS_FEATURE_WRAP_UNICODE)) {
        unibrks = text_info->breaks;
        set_linebreaks_utf32(
            text_info->event_text, text_info->length,
            render_priv->track->Language, unibrks);
#if UNIBREAK_VERSION < 0x0500UL
        // Prior to 5.0 libunibreaks always ended text with LINE_BREAKMUSTBREAK, matching
        // Unicode spec, but messing with our text-overflow detection.
        // Thus reevaluate the last char in a different context.
        // (Later versions set either MUSTBREAK or the newly added INDETERMINATE)
        unibrks[text_info->length - 1] = is_line_breakable(
            text_info->event_text[text_info->length - 1],
            ' ',
            render_priv->track->Language
        );
#endif
    }
#endif

    wrap_lines_naive(state, max_text_width, unibrks);
    wrap_lines_rebalance(state, max_text_width, unibrks);

    trim_whitespace(state);
    measure_text(state);
    wrap_lines_measure(state, unibrks);
}

/**
 * \brief Calculate base point for positioning and rotation
 * \param bbox text bbox
 * \param alignment alignment
 * \param bx, by out: base point coordinates
 */
static void get_base_point(ASS_DRect *bbox, int alignment, double *bx, double *by)
{
    const int halign = alignment & 3;
    const int valign = alignment & 12;
    if (bx)
        switch (halign) {
        case HALIGN_LEFT:
            *bx = bbox->x_min;
            break;
        case HALIGN_CENTER:
            *bx = (bbox->x_max + bbox->x_min) / 2.0;
            break;
        case HALIGN_RIGHT:
            *bx = bbox->x_max;
            break;
        }
    if (by)
        switch (valign) {
        case VALIGN_TOP:
            *by = bbox->y_min;
            break;
        case VALIGN_CENTER:
            *by = (bbox->y_max + bbox->y_min) / 2.0;
            break;
        case VALIGN_SUB:
            *by = bbox->y_max;
            break;
        }
}

/**
 * \brief Adjust the glyph's font size and scale factors to ensure smooth
 *  scaling and handle pathological font sizes. The main problem here is
 *  freetype's grid fitting, which destroys animations by font size, or will
 *  result in incorrect final text size if font sizes are very small and
 *  scale factors very large. See Google Code issue #46.
 * \param priv guess what
 * \param glyph the glyph to be modified
 */
static void
fix_glyph_scaling(ASS_Renderer *priv, GlyphInfo *glyph)
{
    double ft_size;
    if (priv->settings.hinting == ASS_HINTING_NONE) {
        // arbitrary, not too small to prevent grid fitting rounding effects
        // XXX: this is a rather crude hack
        ft_size = 256.0;
    } else {
        // If hinting is enabled, we want to pass the real font size
        // to freetype. Normalize scale_y to 1.0.
        ft_size = glyph->scale_y * glyph->font_size;
    }

    if (!ft_size || !glyph->font_size)
        return;

    double mul = glyph->font_size / ft_size;
    glyph->scale_fix = 1 / mul;
    glyph->scale_x *= mul;
    glyph->scale_y *= mul;
    glyph->font_size = ft_size;
}

// Initial run splitting based purely on the characters' styles
static void split_style_runs(RenderContext *state)
{
    TextInfo *text_info = state->text_info;
    Effect last_effect_type = text_info->glyphs[0].effect_type;
    text_info->glyphs[0].starts_new_run = true;
    for (int i = 1; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + i;
        GlyphInfo *last = text_info->glyphs + (i - 1);
        Effect effect_type = info->effect_type;
        info->starts_new_run =
            info->effect_timing ||  // but ignore effect_skip_timing
            (effect_type != EF_NONE && effect_type != last_effect_type) ||
            info->drawing_text.str ||
            last->drawing_text.str ||
            !ass_string_equal(last->font->desc.family, info->font->desc.family) ||
            last->font->desc.vertical != info->font->desc.vertical ||
            last->font_size != info->font_size ||
            last->c[0] != info->c[0] ||
            last->c[1] != info->c[1] ||
            last->c[2] != info->c[2] ||
            last->c[3] != info->c[3] ||
            last->be != info->be ||
            last->blur != info->blur ||
            last->shadow_x != info->shadow_x ||
            last->shadow_y != info->shadow_y ||
            last->frx != info->frx ||
            last->fry != info->fry ||
            last->frz != info->frz ||
            last->fax != info->fax ||
            last->fay != info->fay ||
            last->scale_x != info->scale_x ||
            last->scale_y != info->scale_y ||
            last->border_style != info->border_style ||
            last->border_x != info->border_x ||
            last->border_y != info->border_y ||
            last->hspacing != info->hspacing ||
            last->italic != info->italic ||
            last->bold != info->bold ||
            ((last->flags ^ info->flags) & ~DECO_ROTATE);
        if (effect_type != EF_NONE)
            last_effect_type = effect_type;
    }
}

// Parse event text.
// Fill render_priv->text_info.
static bool parse_events(RenderContext *state, ASS_Event *event)
{
    TextInfo *text_info = state->text_info;
    ASS_Renderer *render_priv = state->renderer;

    char *p = event->Text, *q;

    // Event parsing.
    while (true) {
        ASS_StringView drawing_text = {NULL, 0};

        // get next char, executing style override
        // this affects render_context
        unsigned code = 0;
        while (*p) {
            if ((*p == '{') && (q = strchr(p, '}'))) {
                p = ass_parse_tags(state, p, q, 1., false);
                assert(*p == '}');
                p++;
            } else if (state->drawing_scale) {
                q = p;
                if (*p == '{')
                    q++;
                while ((*q != '{') && (*q != 0))
                    q++;
                drawing_text.str = p;
                drawing_text.len = q - p;
                code = 0xfffc; // object replacement character
                p = q;
                break;
            } else {
                code = ass_get_next_char(state, &p);
                break;
            }
        }

        if (code == 0)
            break;

        // face could have been changed in get_next_char
        if (!state->font)
            goto fail;

        if (text_info->length >= text_info->max_glyphs) {
            // Raise maximum number of glyphs
            int new_max = 2 * FFMIN(FFMAX(text_info->max_glyphs, text_info->length / 2 + 1),
                                    INT_MAX / 2);
            if (text_info->length >= new_max)
                goto fail;
            if (!ASS_REALLOC_ARRAY(text_info->glyphs, new_max) ||
                    !ASS_REALLOC_ARRAY(text_info->event_text, new_max) ||
                    !ASS_REALLOC_ARRAY(text_info->breaks, new_max))
                goto fail;
            text_info->max_glyphs = new_max;
        }

        GlyphInfo *info = &text_info->glyphs[text_info->length];

        // Clear current GlyphInfo
        memset(info, 0, sizeof(GlyphInfo));

        // Parse drawing
        if (drawing_text.str) {
            info->drawing_text = drawing_text;
            info->drawing_scale = state->drawing_scale;
            info->drawing_pbo = state->pbo;
        }

        // Fill glyph information
        info->symbol = code;
        info->font = state->font;
        if (!drawing_text.str)
            ass_cache_inc_ref(info->font);
        for (int i = 0; i < 4; i++) {
            uint32_t clr = state->c[i];
            info->a_pre_fade[i] = _a(clr);
            ass_apply_fade(&clr, state->fade);
            info->c[i] = clr;
        }

        info->effect_type = state->effect_type;
        info->effect_timing = state->effect_timing;
        info->effect_skip_timing = state->effect_skip_timing;
        info->reset_effect = state->reset_effect;
        // VSFilter compatibility: font glyphs use PlayResY scaling in both dimensions
        info->font_size =
            fabs(state->font_size * state->screen_scale_y);
        info->be = state->be;
        info->blur = state->blur;
        info->shadow_x = state->shadow_x;
        info->shadow_y = state->shadow_y;
        info->scale_x = state->scale_x;
        info->scale_y = state->scale_y;
        info->border_style = state->border_style;
        info->border_x = state->border_x;
        info->border_y = state->border_y;
        info->hspacing = state->hspacing;
        info->bold = state->bold;
        info->italic = state->italic;
        info->flags = state->flags;
        if (info->font->desc.vertical && code >= VERTICAL_LOWER_BOUND)
            info->flags |= DECO_ROTATE;
        info->frx = state->frx;
        info->fry = state->fry;
        info->frz = state->frz;
        info->fax = state->fax;
        info->fay = state->fay;
        info->fade = state->fade;

        info->hspacing_scaled = 0;
        info->scale_fix = 1;

        if (!drawing_text.str) {
            info->hspacing_scaled = double_to_d6(info->hspacing *
                    state->screen_scale_x / render_priv->par_scale_x *
                    info->scale_x);
            fix_glyph_scaling(render_priv, info);
        }

        text_info->length++;

        state->effect_type = EF_NONE;
        state->effect_timing = 0;
        state->effect_skip_timing = 0;
        state->reset_effect = false;
    }

    return true;

fail:
    free_render_context(state);
    return false;
}

// Process render_priv->text_info and load glyph outlines.
static void retrieve_glyphs(RenderContext *state)
{
    GlyphInfo *glyphs = state->text_info->glyphs;
    int i;

    for (i = 0; i < state->text_info->length; i++) {
        GlyphInfo *info = glyphs + i;
        do {
            get_outline_glyph(state, info);
            info = info->next;
        } while (info);
        info = glyphs + i;

        // Add additional space after italic to non-italic style changes
        if (i && glyphs[i - 1].italic && !info->italic) {
            int back = i - 1;
            GlyphInfo *og = &glyphs[back];
            while (back && og->bbox.x_max - og->bbox.x_min == 0
                    && og->italic)
                og = &glyphs[--back];
            if (og->bbox.x_max > og->cluster_advance.x)
                og->cluster_advance.x = og->bbox.x_max;
        }

        // add horizontal letter spacing
        info->cluster_advance.x += info->hspacing_scaled;
    }
}

// Preliminary layout (for line wrapping)
static void preliminary_layout(RenderContext *state)
{
    ASS_Vector pen = { 0, 0 };
    for (int i = 0; i < state->text_info->length; i++) {
        GlyphInfo *info = state->text_info->glyphs + i;
        ASS_Vector cluster_pen = pen;
        do {
            info->pos.x = cluster_pen.x;
            info->pos.y = cluster_pen.y;

            cluster_pen.x += info->advance.x;
            cluster_pen.y += info->advance.y;

            info = info->next;
        } while (info);
        info = state->text_info->glyphs + i;
        pen.x += info->cluster_advance.x;
        pen.y += info->cluster_advance.y;
    }
}

// Reorder text into visual order
static void reorder_text(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    FriBidiStrIndex *cmap = ass_shaper_reorder(state->shaper, text_info);
    if (!cmap) {
        ass_msg(render_priv->library, MSGL_ERR, "Failed to reorder text");
        ass_shaper_cleanup(state->shaper, text_info);
        free_render_context(state);
        return;
    }

    // Reposition according to the map
    ASS_Vector pen = { 0, 0 };
    int lineno = 1;
    for (int i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + cmap[i];
        if (text_info->glyphs[i].linebreak) {
            pen.x = 0;
            pen.y += double_to_d6(text_info->lines[lineno-1].desc);
            pen.y += double_to_d6(text_info->lines[lineno].asc);
            pen.y += double_to_d6(render_priv->settings.line_spacing);
            lineno++;
        }
        if (info->skip)
            continue;
        ASS_Vector cluster_pen = pen;
        pen.x += info->cluster_advance.x;
        pen.y += info->cluster_advance.y;
        while (info) {
            info->pos.x = info->offset.x + cluster_pen.x;
            info->pos.y = info->offset.y + cluster_pen.y;
            cluster_pen.x += info->advance.x;
            cluster_pen.y += info->advance.y;
            info = info->next;
        }
    }
}

static void apply_baseline_shear(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    FriBidiStrIndex *cmap = ass_shaper_get_reorder_map(state->shaper);
    int32_t shear = 0;
    bool whole_text_layout =
        render_priv->track->parser_priv->feature_flags &
        FEATURE_MASK(ASS_FEATURE_WHOLE_TEXT_LAYOUT);
    for (int i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + cmap[i];
        if (text_info->glyphs[i].linebreak ||
            (!whole_text_layout && text_info->glyphs[i].starts_new_run))
            shear = 0;
        if (!info->scale_x || !info->scale_y)
            info->skip = true;
        if (info->skip)
            continue;
        for (GlyphInfo *cur = info; cur; cur = cur->next)
            cur->pos.y += shear;
        shear += (info->fay / info->scale_x * info->scale_y) * info->cluster_advance.x;
    }
}

static void align_lines(RenderContext *state, double max_text_width)
{
    TextInfo *text_info = state->text_info;
    GlyphInfo *glyphs = text_info->glyphs;
    int i, j;
    double width = 0;
    int last_break = -1;
    int halign = state->alignment & 3;
    int justify = state->justify;
    double max_width = 0;

    if (state->evt_type & EVENT_HSCROLL) {
        justify = halign;
        halign = HALIGN_LEFT;
    }

    for (i = 0; i <= text_info->length; ++i) {   // (text_info->length + 1) is the end of the last line
        if ((i == text_info->length) || glyphs[i].linebreak) {
            max_width = FFMAX(max_width,width);
            width = 0;
        }
        if (i < text_info->length && !glyphs[i].skip &&
                glyphs[i].symbol != '\n' && glyphs[i].symbol != 0) {
            width += d6_to_double(glyphs[i].cluster_advance.x);
        }
    }
    for (i = 0; i <= text_info->length; ++i) {   // (text_info->length + 1) is the end of the last line
        if ((i == text_info->length) || glyphs[i].linebreak) {
            double shift = 0;
            if (halign == HALIGN_LEFT) {    // left aligned, no action
                if (justify == ASS_JUSTIFY_RIGHT) {
                    shift = max_width - width;
                } else if (justify == ASS_JUSTIFY_CENTER) {
                    shift = (max_width - width) / 2.0;
                } else {
                    shift = 0;
                }
            } else if (halign == HALIGN_RIGHT) {    // right aligned
                if (justify == ASS_JUSTIFY_LEFT) {
                    shift = max_text_width - max_width;
                } else if (justify == ASS_JUSTIFY_CENTER) {
                    shift = max_text_width - max_width + (max_width - width) / 2.0;
                } else {
                    shift = max_text_width - width;
                }
            } else if (halign == HALIGN_CENTER) {   // centered
                if (justify == ASS_JUSTIFY_LEFT) {
                    shift = (max_text_width - max_width) / 2.0;
                } else if (justify == ASS_JUSTIFY_RIGHT) {
                    shift = (max_text_width - max_width) / 2.0 + max_width - width;
                } else {
                    shift = (max_text_width - width) / 2.0;
                }
            }
            for (j = last_break + 1; j < i; ++j) {
                GlyphInfo *info = glyphs + j;
                while (info) {
                    info->pos.x += double_to_d6(shift);
                    info = info->next;
                }
            }
            last_break = i - 1;
            width = 0;
        }
        if (i < text_info->length && !glyphs[i].skip &&
                glyphs[i].symbol != '\n' && glyphs[i].symbol != 0) {
            width += d6_to_double(glyphs[i].cluster_advance.x);
        }
    }
}

static void calculate_rotation_params(RenderContext *state, ASS_DRect *bbox,
                                      double device_x, double device_y)
{
    ASS_Renderer *render_priv = state->renderer;
    ASS_DVector center;
    if (state->have_origin) {
        center.x = x2scr_pos(render_priv, state->org_x);
        center.y = y2scr_pos(render_priv, state->org_y);
    } else {
        double bx = 0., by = 0.;
        get_base_point(bbox, state->alignment, &bx, &by);
        center.x = device_x + bx;
        center.y = device_y + by;
    }

    TextInfo *text_info = state->text_info;
    for (int i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + i;
        while (info) {
            info->shift.x = info->pos.x + double_to_d6(device_x - center.x +
                    info->shadow_x * state->border_scale_x /
                    render_priv->par_scale_x);
            info->shift.y = info->pos.y + double_to_d6(device_y - center.y +
                    info->shadow_y * state->border_scale_y);
            info = info->next;
        }
    }
}


static int quantize_blur(double radius, int32_t *shadow_mask)
{
    // Gaussian filter kernel (1D):
    // G(x, r2) = exp(-x^2 / (2 * r2)) / sqrt(2 * pi * r2),
    // position unit is 1/64th of pixel, r = 64 * radius, r2 = r^2.

    // Difference between kernels with different but near r2:
    // G(x, r2 + dr2) - G(x, r2) ~= dr2 * G(x, r2) * (x^2 - r2) / (2 * r2^2).
    // Maximal possible error relative to full pixel value is half of
    // integral (from -inf to +inf) of absolute value of that difference.
    // E_max ~= dr2 / 2 * integral(G(x, r2) * |x^2 - r2| / (2 * r2^2), x)
    //  = dr2 / (4 * r2) * integral(G(y, 1) * |y^2 - 1|, y)
    //  = dr2 / (4 * r2) * 4 / sqrt(2 * pi * e)
    //  ~ dr2 / (4 * r2) ~= dr / (2 * r).
    // E_max ~ BLUR_PRECISION / 2 as we have 2 dimensions.

    // To get discretized blur radius solve the following
    // differential equation (n--quantization index):
    // dr(n) / dn = BLUR_PRECISION * r + POSITION_PRECISION, r(0) = 0,
    // r(n) = (exp(BLUR_PRECISION * n) - 1) * POSITION_PRECISION / BLUR_PRECISION,
    // n = log(1 + r * BLUR_PRECISION / POSITION_PRECISION) / BLUR_PRECISION.

    // To get shadow offset quantization estimate difference of
    // G(x + dx, r2) - G(x, r2) ~= dx * G(x, r2) * (-x / r2).
    // E_max ~= dx / 2 * integral(G(x, r2) * |x| / r2, x)
    //  = dx / sqrt(2 * pi * r2) ~ dx / (2 * r).
    // 2^ord ~ dx ~ BLUR_PRECISION * r + POSITION_PRECISION.

    const double scale = 64 * BLUR_PRECISION / POSITION_PRECISION;
    radius *= scale;

    int ord;
    // ord = floor(log2(BLUR_PRECISION * r + POSITION_PRECISION))
    //     = floor(log2(64 * radius * BLUR_PRECISION + POSITION_PRECISION))
    //     = floor(log2((radius * scale + 1) * POSITION_PRECISION)),
    // floor(log2(x)) = frexp(x) - 1 = frexp(x / 2).
    frexp((1 + radius) * (POSITION_PRECISION / 2), &ord);
    *shadow_mask = ((uint32_t) 1 << ord) - 1;
    return lrint(log1p(radius) / BLUR_PRECISION);
}

static double restore_blur(int qblur)
{
    const double scale = 64 * BLUR_PRECISION / POSITION_PRECISION;
    double sigma = expm1(BLUR_PRECISION * qblur) / scale;
    return sigma * sigma;
}

// Convert glyphs to bitmaps, combine them, apply blur, generate shadows.
static void render_and_combine_glyphs(RenderContext *state,
                                      double device_x, double device_y)
{
    ASS_Renderer *render_priv = state->renderer;
    TextInfo *text_info = state->text_info;
    int left = render_priv->settings.left_margin;
    device_x = (device_x - left) * render_priv->par_scale_x + left;
    unsigned nb_bitmaps = 0;
    bool new_run = true;
    CombinedBitmapInfo *combined_info = text_info->combined_bitmaps;
    CombinedBitmapInfo *current_info = NULL;
    ASS_DVector offset;
    for (int i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + i;
        if (info->starts_new_run) new_run = true;
        if (info->skip) {
            for (; info; info = info->next)
                ass_cache_dec_ref(info->outline);
            continue;
        }
        for (; info; info = info->next) {
            int flags = 0;
            if (info->border_style == 3)
                flags |= FILTER_BORDER_STYLE_3;
            if (info->border_x || info->border_y)
                flags |= FILTER_NONZERO_BORDER;
            if (info->shadow_x || info->shadow_y)
                flags |= FILTER_NONZERO_SHADOW;
            if (flags & FILTER_NONZERO_SHADOW &&
                (info->effect_type == EF_KARAOKE_KF ||
                 info->effect_type == EF_KARAOKE_KO ||
                 (info->a_pre_fade[0]) != 0xFF ||
                 info->border_style == 3))
                flags |= FILTER_FILL_IN_SHADOW;
            if (!(flags & FILTER_NONZERO_BORDER) &&
                !(flags & FILTER_FILL_IN_SHADOW))
                flags &= ~FILTER_NONZERO_SHADOW;
            if ((flags & FILTER_NONZERO_BORDER &&
                 info->a_pre_fade[0] == 0 &&
                 info->a_pre_fade[1] == 0 &&
                 info->fade == 0) ||
                info->border_style == 3)
                flags |= FILTER_FILL_IN_BORDER;

            if (new_run) {
                if (nb_bitmaps >= text_info->max_bitmaps) {
                    size_t new_size = 2 * text_info->max_bitmaps;
                    if (!ASS_REALLOC_ARRAY(text_info->combined_bitmaps, new_size)) {
                        ass_cache_dec_ref(info->outline);
                        continue;
                    }
                    text_info->max_bitmaps = new_size;
                    combined_info = text_info->combined_bitmaps;
                }
                current_info = &combined_info[nb_bitmaps];

                memcpy(&current_info->c, &info->c, sizeof(info->c));
                current_info->effect_type = info->effect_type;
                current_info->effect_timing = info->effect_timing;
                current_info->leftmost_x = OUTLINE_MAX;

                FilterDesc *filter = &current_info->filter;
                filter->flags = flags;
                filter->be = info->be;

                int32_t shadow_mask_x, shadow_mask_y;
                double blur_radius_scale = 2 / sqrt(log(256));
                double blur_scale_x = state->blur_scale_x * blur_radius_scale;
                double blur_scale_y = state->blur_scale_y * blur_radius_scale;
                filter->blur_x = quantize_blur(info->blur * blur_scale_x, &shadow_mask_x);
                filter->blur_y = quantize_blur(info->blur * blur_scale_y, &shadow_mask_y);
                if (flags & FILTER_NONZERO_SHADOW) {
                    int32_t x = double_to_d6(info->shadow_x * state->border_scale_x);
                    int32_t y = double_to_d6(info->shadow_y * state->border_scale_y);
                    filter->shadow.x = (x + (shadow_mask_x >> 1)) & ~shadow_mask_x;
                    filter->shadow.y = (y + (shadow_mask_y >> 1)) & ~shadow_mask_y;
                } else
                    filter->shadow.x = filter->shadow.y = 0;

                current_info->x = current_info->y = INT_MAX;
                current_info->bm = current_info->bm_o = current_info->bm_s = NULL;
                current_info->image = NULL;

                current_info->bitmap_count = current_info->max_bitmap_count = 0;
                current_info->bitmaps = malloc(MAX_SUB_BITMAPS_INITIAL * sizeof(BitmapRef));
                if (!current_info->bitmaps) {
                    ass_cache_dec_ref(info->outline);
                    continue;
                }
                current_info->max_bitmap_count = MAX_SUB_BITMAPS_INITIAL;

                nb_bitmaps++;
                new_run = false;
            }
            assert(current_info);

            ASS_Vector pos, pos_o;
            info->pos.x = double_to_d6(device_x + d6_to_double(info->pos.x) * render_priv->par_scale_x);
            info->pos.y = double_to_d6(device_y) + info->pos.y;
            get_bitmap_glyph(state, info, &current_info->leftmost_x, &pos, &pos_o,
                             &offset, !current_info->bitmap_count, flags);

            if (!info->bm && !info->bm_o) {
                ass_cache_dec_ref(info->bm);
                ass_cache_dec_ref(info->bm_o);
                continue;
            }

            if (current_info->bitmap_count >= current_info->max_bitmap_count) {
                size_t new_size = 2 * current_info->max_bitmap_count;
                if (!ASS_REALLOC_ARRAY(current_info->bitmaps, new_size)) {
                    ass_cache_dec_ref(info->bm);
                    ass_cache_dec_ref(info->bm_o);
                    continue;
                }
                current_info->max_bitmap_count = new_size;
            }
            current_info->bitmaps[current_info->bitmap_count].bm   = info->bm;
            current_info->bitmaps[current_info->bitmap_count].bm_o = info->bm_o;
            current_info->bitmaps[current_info->bitmap_count].pos   = pos;
            current_info->bitmaps[current_info->bitmap_count].pos_o = pos_o;
            current_info->bitmap_count++;

            current_info->x = FFMIN(current_info->x, pos.x);
            current_info->y = FFMIN(current_info->y, pos.y);
        }
    }

    for (int i = 0; i < nb_bitmaps; i++) {
        CombinedBitmapInfo *info = &combined_info[i];
        if (!info->bitmap_count) {
            free(info->bitmaps);
            continue;
        }

        if (info->effect_type == EF_KARAOKE_KF)
            info->effect_timing = lround(d6_to_double(info->leftmost_x) +
                d6_to_double(info->effect_timing) * render_priv->par_scale_x);

        for (int j = 0; j < info->bitmap_count; j++) {
            info->bitmaps[j].pos.x -= info->x;
            info->bitmaps[j].pos.y -= info->y;
            info->bitmaps[j].pos_o.x -= info->x;
            info->bitmaps[j].pos_o.y -= info->y;
        }

        CompositeHashKey key;
        key.filter = info->filter;
        key.bitmap_count = info->bitmap_count;
        key.bitmaps = info->bitmaps;
        CompositeHashValue *val = ass_cache_get(render_priv->cache.composite_cache, &key, render_priv);
        if (!val)
            continue;

        if (val->bm.buffer)
            info->bm = &val->bm;
        if (val->bm_o.buffer)
            info->bm_o = &val->bm_o;
        if (val->bm_s.buffer)
            info->bm_s = &val->bm_s;
        info->image = val;
        continue;
    }

    text_info->n_bitmaps = nb_bitmaps;
}

static inline void rectangle_combine(ASS_Rect *rect, const Bitmap *bm, ASS_Vector pos)
{
    pos.x += bm->left;
    pos.y += bm->top;
    rectangle_update(rect, pos.x, pos.y, pos.x + bm->w, pos.y + bm->h);
}

/*
 * To find these values, simulate blur on the border between two
 * half-planes, one zero-filled (background) and the other filled
 * with the maximum supported value (foreground). Keep incrementing
 * the \be argument. The necessary padding is the distance by which
 * the blurred foreground image extends beyond the original border
 * and into the background. Initially it increases along with \be,
 * but very soon it grinds to a halt. At some point, the blurred
 * image actually reaches a stationary point and stays unchanged
 * forever after, simply _shifting_ by one pixel for each \be
 * step--moving in the direction of the non-zero half-plane and
 * thus decreasing the necessary padding (although the large
 * padding is still needed for intermediate results). In practice,
 * images are finite rather than infinite like half-planes, but
 * this can only decrease the required padding. Half-planes filled
 * with extreme values are the theoretical limit of the worst case.
 * Make sure to use the right pixel value range in the simulation!
 */
int ass_be_padding(int be)
{
    if (be <= 3)
        return be;
    if (be <= 7)
        return 4;
    return 5;
}


size_t ass_composite_construct(void *key, void *value, void *priv)
{
    ASS_Renderer *render_priv = priv;
    CompositeHashKey *k = key;
    CompositeHashValue *v = value;
    memset(v, 0, sizeof(*v));

    ASS_Rect rect, rect_o;
    rectangle_reset(&rect);
    rectangle_reset(&rect_o);

    size_t n_bm = 0, n_bm_o = 0;
    BitmapRef *last = NULL, *last_o = NULL;
    for (int i = 0; i < k->bitmap_count; i++) {
        BitmapRef *ref = &k->bitmaps[i];
        if (ref->bm) {
            rectangle_combine(&rect, ref->bm, ref->pos);
            last = ref;
            n_bm++;
        }
        if (ref->bm_o) {
            rectangle_combine(&rect_o, ref->bm_o, ref->pos_o);
            last_o = ref;
            n_bm_o++;
        }
    }

    int bord = ass_be_padding(k->filter.be);
    if (!bord && n_bm == 1) {
        ass_copy_bitmap(&render_priv->engine, &v->bm, last->bm);
        v->bm.left += last->pos.x;
        v->bm.top  += last->pos.y;
    } else if (n_bm && ass_alloc_bitmap(&render_priv->engine, &v->bm,
                                        rect.x_max - rect.x_min + 2 * bord,
                                        rect.y_max - rect.y_min + 2 * bord,
                                        true)) {
        Bitmap *dst = &v->bm;
        dst->left = rect.x_min - bord;
        dst->top  = rect.y_min - bord;
        for (int i = 0; i < k->bitmap_count; i++) {
            Bitmap *src = k->bitmaps[i].bm;
            if (!src)
                continue;
            int x = k->bitmaps[i].pos.x + src->left - dst->left;
            int y = k->bitmaps[i].pos.y + src->top  - dst->top;
            assert(x >= 0 && x + src->w <= dst->w);
            assert(y >= 0 && y + src->h <= dst->h);
            unsigned char *buf = dst->buffer + y * dst->stride + x;
            render_priv->engine.add_bitmaps(buf, dst->stride,
                                            src->buffer, src->stride,
                                            src->w, src->h);
        }
    }
    if (!bord && n_bm_o == 1) {
        ass_copy_bitmap(&render_priv->engine, &v->bm_o, last_o->bm_o);
        v->bm_o.left += last_o->pos_o.x;
        v->bm_o.top  += last_o->pos_o.y;
    } else if (n_bm_o && ass_alloc_bitmap(&render_priv->engine, &v->bm_o,
                                          rect_o.x_max - rect_o.x_min + 2 * bord,
                                          rect_o.y_max - rect_o.y_min + 2 * bord,
                                          true)) {
        Bitmap *dst = &v->bm_o;
        dst->left = rect_o.x_min - bord;
        dst->top  = rect_o.y_min - bord;
        for (int i = 0; i < k->bitmap_count; i++) {
            Bitmap *src = k->bitmaps[i].bm_o;
            if (!src)
                continue;
            int x = k->bitmaps[i].pos_o.x + src->left - dst->left;
            int y = k->bitmaps[i].pos_o.y + src->top  - dst->top;
            assert(x >= 0 && x + src->w <= dst->w);
            assert(y >= 0 && y + src->h <= dst->h);
            unsigned char *buf = dst->buffer + y * dst->stride + x;
            render_priv->engine.add_bitmaps(buf, dst->stride,
                                            src->buffer, src->stride,
                                            src->w, src->h);
        }
    }

    int flags = k->filter.flags;
    double r2x = restore_blur(k->filter.blur_x);
    double r2y = restore_blur(k->filter.blur_y);
    if (!(flags & FILTER_NONZERO_BORDER) || (flags & FILTER_BORDER_STYLE_3))
        ass_synth_blur(&render_priv->engine, &v->bm, k->filter.be, r2x, r2y);
    ass_synth_blur(&render_priv->engine, &v->bm_o, k->filter.be, r2x, r2y);

    if (!(flags & FILTER_FILL_IN_BORDER) && !(flags & FILTER_FILL_IN_SHADOW))
        ass_fix_outline(&v->bm, &v->bm_o);

    if (flags & FILTER_NONZERO_SHADOW) {
        if (flags & FILTER_NONZERO_BORDER) {
            ass_copy_bitmap(&render_priv->engine, &v->bm_s, &v->bm_o);
            if ((flags & FILTER_FILL_IN_BORDER) && !(flags & FILTER_FILL_IN_SHADOW))
                ass_fix_outline(&v->bm, &v->bm_s);
        } else if (flags & FILTER_BORDER_STYLE_3) {
            v->bm_s = v->bm_o;
            memset(&v->bm_o, 0, sizeof(v->bm_o));
        } else {
            ass_copy_bitmap(&render_priv->engine, &v->bm_s, &v->bm);
        }

        // Works right even for negative offsets
        // '>>' rounds toward negative infinity, '&' returns correct remainder
        v->bm_s.left += k->filter.shadow.x >> 6;
        v->bm_s.top  += k->filter.shadow.y >> 6;
        ass_shift_bitmap(&v->bm_s, k->filter.shadow.x & SUBPIXEL_MASK, k->filter.shadow.y & SUBPIXEL_MASK);
    }

    if ((flags & FILTER_FILL_IN_SHADOW) && !(flags & FILTER_FILL_IN_BORDER))
        ass_fix_outline(&v->bm, &v->bm_o);

    return sizeof(CompositeHashKey) + sizeof(CompositeHashValue) +
        k->bitmap_count * sizeof(BitmapRef) +
        bitmap_size(&v->bm) + bitmap_size(&v->bm_o) + bitmap_size(&v->bm_s);
}

static void add_background(RenderContext *state, EventImages *event_images)
{
    ASS_Renderer *render_priv = state->renderer;
    int size_x = state->shadow_x > 0 ?
        lround(state->shadow_x * state->border_scale_x) : 0;
    int size_y = state->shadow_y > 0 ?
        lround(state->shadow_y * state->border_scale_y) : 0;
    int left    = event_images->left - size_x;
    int top     = event_images->top  - size_y;
    int right   = event_images->left + event_images->width  + size_x;
    int bottom  = event_images->top  + event_images->height + size_y;
    left        = FFMINMAX(left,   0, render_priv->width);
    top         = FFMINMAX(top,    0, render_priv->height);
    right       = FFMINMAX(right,  0, render_priv->width);
    bottom      = FFMINMAX(bottom, 0, render_priv->height);
    int w = right - left;
    int h = bottom - top;
    if (w < 1 || h < 1)
        return;
    void *nbuffer = ass_aligned_alloc(1, w * h, false);
    if (!nbuffer)
        return;
    memset(nbuffer, 0xFF, w * h);
    ASS_Image *img = my_draw_bitmap(nbuffer, w, h, w, left, top,
                                    state->c[3], NULL);
    if (img) {
        img->next = event_images->imgs;
        event_images->imgs = img;
    }
}

/**
 * \brief Main ass rendering function, glues everything together
 * \param event event to render
 * \param event_images struct containing resulting images, will also be initialized
 * Process event, appending resulting ASS_Image's to images_root.
 */
static bool
ass_render_event(RenderContext *state, ASS_Event *event,
                 EventImages *event_images)
{
    ASS_Renderer *render_priv = state->renderer;
    if (event->Style >= render_priv->track->n_styles) {
        ass_msg(render_priv->library, MSGL_WARN, "No style found");
        return false;
    }
    if (!event->Text) {
        ass_msg(render_priv->library, MSGL_WARN, "Empty event");
        return false;
    }

    free_render_context(state);
    init_render_context(state, event);

    if (!parse_events(state, event))
        return false;

    TextInfo *text_info = state->text_info;
    if (text_info->length == 0) {
        // no valid symbols in the event; this can be smth like {comment}
        free_render_context(state);
        return false;
    }

    split_style_runs(state);

    // Find shape runs and shape text
    ass_shaper_set_base_direction(state->shaper,
            ass_resolve_base_direction(state->font_encoding));
    ass_shaper_find_runs(state->shaper, render_priv, text_info->glyphs,
            text_info->length);
    if (!ass_shaper_shape(state->shaper, text_info)) {
        ass_msg(render_priv->library, MSGL_ERR, "Failed to shape text");
        free_render_context(state);
        return false;
    }

    retrieve_glyphs(state);

    preliminary_layout(state);

    int valign = state->alignment & 12;

    int MarginL =
        (event->MarginL) ? event->MarginL : state->style->MarginL;
    int MarginR =
        (event->MarginR) ? event->MarginR : state->style->MarginR;
    int MarginV =
        (event->MarginV) ? event->MarginV : state->style->MarginV;

    // calculate max length of a line
    double max_text_width =
        x2scr_right(state, render_priv->track->PlayResX - MarginR) -
        x2scr_left(state, MarginL);

    // wrap lines
    wrap_lines_smart(state, max_text_width);

    // depends on glyph x coordinates being monotonous within runs, so it should be done before reorder
    ass_process_karaoke_effects(state);

    reorder_text(state);

    align_lines(state, max_text_width);

    // determing text bounding box
    ASS_DRect bbox;
    compute_string_bbox(text_info, &bbox);

    apply_baseline_shear(state);

    // determine device coordinates for text
    double device_x = 0;
    double device_y = 0;

    // handle positioned events first: an event can be both positioned and
    // scrolling, and the scrolling effect overrides the position on one axis
    if (state->evt_type & EVENT_POSITIONED) {
        double base_x = 0;
        double base_y = 0;
        get_base_point(&bbox, state->alignment, &base_x, &base_y);
        device_x =
            x2scr_pos(render_priv, state->pos_x) - base_x;
        device_y =
            y2scr_pos(render_priv, state->pos_y) - base_y;
    }

    // x coordinate
    if (state->evt_type & EVENT_HSCROLL) {
        if (state->scroll_direction == SCROLL_RL)
            device_x =
                x2scr_pos(render_priv,
                      render_priv->track->PlayResX -
                      state->scroll_shift);
        else if (state->scroll_direction == SCROLL_LR)
            device_x =
                x2scr_pos(render_priv, state->scroll_shift) -
                (bbox.x_max - bbox.x_min);
    } else if (!(state->evt_type & EVENT_POSITIONED)) {
        device_x = x2scr_left(state, MarginL);
    }

    // y coordinate
    if (state->evt_type & EVENT_VSCROLL) {
        if (state->scroll_direction == SCROLL_TB)
            device_y =
                y2scr(state,
                      state->scroll_y0 +
                      state->scroll_shift) -
                bbox.y_max;
        else if (state->scroll_direction == SCROLL_BT)
            device_y =
                y2scr(state,
                      state->scroll_y1 -
                      state->scroll_shift) -
                bbox.y_min;
    } else if (!(state->evt_type & EVENT_POSITIONED)) {
        if (valign == VALIGN_TOP) {     // toptitle
            device_y =
                y2scr_top(state,
                          MarginV) + text_info->lines[0].asc;
        } else if (valign == VALIGN_CENTER) {   // midtitle
            double scr_y =
                y2scr(state, render_priv->track->PlayResY / 2.0);
            device_y = scr_y - (bbox.y_max + bbox.y_min) / 2.0;
        } else {                // subtitle
            double line_pos = state->explicit ?
                0 : render_priv->settings.line_position;
            double scr_top, scr_bottom, scr_y0;
            if (valign != VALIGN_SUB)
                ass_msg(render_priv->library, MSGL_V,
                       "Invalid valign, assuming 0 (subtitle)");
            scr_bottom =
                y2scr_sub(state,
                          render_priv->track->PlayResY - MarginV);
            scr_top = y2scr_top(state, 0); //xxx not always 0?
            device_y = scr_bottom + (scr_top - scr_bottom) * line_pos / 100.0;
            device_y -= text_info->height;
            device_y += text_info->lines[0].asc;
            // clip to top to avoid confusion if line_position is very high,
            // turning the subtitle into a toptitle
            // also, don't change behavior if line_position is not used
            scr_y0 = scr_top + text_info->lines[0].asc;
            if (device_y < scr_y0 && line_pos > 0) {
                device_y = scr_y0;
            }
        }
    }

    // fix clip coordinates
    if (state->explicit || !render_priv->settings.use_margins) {
        state->clip_x0 =
            lround(x2scr_pos_scaled(render_priv, state->clip_x0));
        state->clip_x1 =
            lround(x2scr_pos_scaled(render_priv, state->clip_x1));
        state->clip_y0 =
            lround(y2scr_pos(render_priv, state->clip_y0));
        state->clip_y1 =
            lround(y2scr_pos(render_priv, state->clip_y1));

        if (state->explicit) {
            // we still need to clip against screen boundaries
            int zx = render_priv->settings.left_margin;
            int zy = render_priv->settings.top_margin;
            int sx = zx + render_priv->frame_content_width;
            int sy = zy + render_priv->frame_content_height;

            state->clip_x0 = FFMAX(state->clip_x0, zx);
            state->clip_y0 = FFMAX(state->clip_y0, zy);
            state->clip_x1 = FFMIN(state->clip_x1, sx);
            state->clip_y1 = FFMIN(state->clip_y1, sy);
        }
    } else {
        // no \clip (explicit==0) and use_margins => only clip to screen with margins
        state->clip_x0 = 0;
        state->clip_y0 = 0;
        state->clip_x1 = render_priv->settings.frame_width;
        state->clip_y1 = render_priv->settings.frame_height;
    }

    if (state->evt_type & EVENT_VSCROLL) {
        int y0 = lround(y2scr_pos(render_priv, state->scroll_y0));
        int y1 = lround(y2scr_pos(render_priv, state->scroll_y1));

        state->clip_y0 = FFMAX(state->clip_y0, y0);
        state->clip_y1 = FFMIN(state->clip_y1, y1);
    }

    calculate_rotation_params(state, &bbox, device_x, device_y);

    render_and_combine_glyphs(state, device_x, device_y);

    memset(event_images, 0, sizeof(*event_images));
    // VSFilter does *not* shift lines with a border > margin to be within the
    // frame, so negative values for top and left may occur
    event_images->top = device_y - text_info->lines[0].asc - text_info->border_top;
    event_images->height =
        text_info->height + text_info->border_bottom + text_info->border_top;
    event_images->left =
        (device_x + bbox.x_min) * render_priv->par_scale_x - text_info->border_x + 0.5;
    event_images->width =
        (bbox.x_max - bbox.x_min) * render_priv->par_scale_x
        + 2 * text_info->border_x + 0.5;
    event_images->detect_collisions = state->detect_collisions;
    event_images->shift_direction = (valign == VALIGN_SUB) ? -1 : 1;
    event_images->event = event;
    event_images->imgs = render_text(state);

    if (state->border_style == 4)
        add_background(state, event_images);

    ass_shaper_cleanup(state->shaper, text_info);
    free_render_context(state);

    return true;
}

/**
 * \brief Check cache limits and reset cache if they are exceeded
 */
static void check_cache_limits(ASS_Renderer *priv, CacheStore *cache)
{
    ass_cache_cut(cache->composite_cache, cache->composite_max_size);
    ass_cache_cut(cache->bitmap_cache, cache->bitmap_max_size);
    ass_cache_cut(cache->outline_cache, cache->glyph_max);
}

static void setup_shaper(ASS_Shaper *shaper, ASS_Renderer *render_priv)
{
    ASS_Track *track = render_priv->track;

    ass_shaper_set_kerning(shaper, track->Kerning);
    ass_shaper_set_language(shaper, track->Language);
    ass_shaper_set_level(shaper, render_priv->settings.shaper);
#ifdef USE_FRIBIDI_EX_API
    ass_shaper_set_bidi_brackets(shaper,
            track->parser_priv->feature_flags & FEATURE_MASK(ASS_FEATURE_BIDI_BRACKETS));
#endif
    ass_shaper_set_whole_text_layout(shaper,
            track->parser_priv->feature_flags & FEATURE_MASK(ASS_FEATURE_WHOLE_TEXT_LAYOUT));
}

/**
 * \brief Start a new frame
 */
static bool
ass_start_frame(ASS_Renderer *render_priv, ASS_Track *track,
                long long now)
{
    if (!render_priv->settings.frame_width
        && !render_priv->settings.frame_height)
        return false;               // library not initialized

    if (!render_priv->fontselect)
        return false;

    if (render_priv->library != track->library)
        return false;

    if (track->n_events == 0)
        return false;               // nothing to do

    render_priv->track = track;
    render_priv->time = now;

    ass_lazy_track_init(render_priv->library, render_priv->track);

    if (render_priv->library->num_fontdata != render_priv->num_emfonts) {
        assert(render_priv->library->num_fontdata > render_priv->num_emfonts);
        render_priv->num_emfonts = ass_update_embedded_fonts(
            render_priv->fontselect, render_priv->num_emfonts);
    }

    setup_shaper(render_priv->state.shaper, render_priv);

    // PAR correction
    double par = render_priv->settings.par;
    bool lr_track = track->LayoutResX > 0 && track->LayoutResY > 0;
    if (par == 0. || lr_track) {
        if (render_priv->frame_content_width && render_priv->frame_content_height && (lr_track ||
                (render_priv->settings.storage_width && render_priv->settings.storage_height))) {
            double dar = ((double) render_priv->frame_content_width) /
                         render_priv->frame_content_height;
            ASS_Vector layout_res = ass_layout_res(render_priv);
            double sar = ((double) layout_res.x) / layout_res.y;
            par = dar / sar;
        } else
            par = 1.0;
    }
    render_priv->par_scale_x = par;

    render_priv->prev_images_root = render_priv->images_root;
    render_priv->images_root = NULL;

    check_cache_limits(render_priv, &render_priv->cache);

    return true;
}

static int cmp_event_layer(const void *p1, const void *p2)
{
    ASS_Event *e1 = ((EventImages *) p1)->event;
    ASS_Event *e2 = ((EventImages *) p2)->event;
    if (e1->Layer < e2->Layer)
        return -1;
    if (e1->Layer > e2->Layer)
        return 1;
    if (e1->ReadOrder < e2->ReadOrder)
        return -1;
    if (e1->ReadOrder > e2->ReadOrder)
        return 1;
    return 0;
}

static ASS_RenderPriv *get_render_priv(ASS_Renderer *render_priv,
                                       ASS_Event *event)
{
    if (!event->render_priv) {
        event->render_priv = calloc(1, sizeof(ASS_RenderPriv));
        if (!event->render_priv)
            return NULL;
    }
    if (render_priv->render_id != event->render_priv->render_id) {
        memset(event->render_priv, 0, sizeof(ASS_RenderPriv));
        event->render_priv->render_id = render_priv->render_id;
    }

    return event->render_priv;
}

static int overlap(Rect *s1, Rect *s2)
{
    if (s1->y0 >= s2->y1 || s2->y0 >= s1->y1 ||
        s1->x0 >= s2->x1 || s2->x0 >= s1->x1)
        return 0;
    return 1;
}

static int cmp_rect_y0(const void *p1, const void *p2)
{
    return ((Rect *) p1)->y0 - ((Rect *) p2)->y0;
}

static void
shift_event(ASS_Renderer *render_priv, EventImages *ei, int shift)
{
    ASS_Image *cur = ei->imgs;
    while (cur) {
        cur->dst_y += shift;
        // clip top and bottom
        if (cur->dst_y < 0) {
            int clip = -cur->dst_y;
            cur->h -= clip;
            cur->bitmap += clip * cur->stride;
            cur->dst_y = 0;
        }
        if (cur->dst_y + cur->h >= render_priv->height) {
            int clip = cur->dst_y + cur->h - render_priv->height;
            cur->h -= clip;
        }
        if (cur->h <= 0) {
            cur->h = 0;
            cur->dst_y = 0;
        }
        cur = cur->next;
    }
    ei->top += shift;
}

// dir: 1 - move down
//      -1 - move up
static int fit_rect(Rect *s, Rect *fixed, int *cnt, int dir)
{
    int i;
    int shift = 0;

    if (dir == 1)               // move down
        for (i = 0; i < *cnt; ++i) {
            if (s->y1 + shift <= fixed[i].y0 || s->y0 + shift >= fixed[i].y1 ||
                s->x1 <= fixed[i].x0 || s->x0 >= fixed[i].x1)
                continue;
            shift = fixed[i].y1 - s->y0;
    } else                      // dir == -1, move up
        for (i = *cnt - 1; i >= 0; --i) {
            if (s->y1 + shift <= fixed[i].y0 || s->y0 + shift >= fixed[i].y1 ||
                s->x1 <= fixed[i].x0 || s->x0 >= fixed[i].x1)
                continue;
            shift = fixed[i].y0 - s->y1;
        }

    fixed[*cnt].y0 = s->y0 + shift;
    fixed[*cnt].y1 = s->y1 + shift;
    fixed[*cnt].x0 = s->x0;
    fixed[*cnt].x1 = s->x1;
    (*cnt)++;
    qsort(fixed, *cnt, sizeof(*fixed), cmp_rect_y0);

    return shift;
}

static void
fix_collisions(ASS_Renderer *render_priv, EventImages *imgs, int cnt)
{
    Rect *used = ass_realloc_array(NULL, cnt, sizeof(*used));
    int cnt_used = 0;
    int i, j;

    if (!used)
        return;

    // fill used[] with fixed events
    for (i = 0; i < cnt; ++i) {
        ASS_RenderPriv *priv;
        // VSFilter considers events colliding if their intersections area is non-zero,
        // zero-area events are therefore effectively fixed as well
        if (!imgs[i].detect_collisions || !imgs[i].height  || !imgs[i].width)
            continue;
        priv = get_render_priv(render_priv, imgs[i].event);
        if (priv && priv->height > 0) { // it's a fixed event
            Rect s;
            s.y0 = priv->top;
            s.y1 = priv->top + priv->height;
            s.x0 = priv->left;
            s.x1 = priv->left + priv->width;
            if (priv->height != imgs[i].height) {       // no, it's not
                ass_msg(render_priv->library, MSGL_WARN,
                        "Event height has changed");
                priv->top = 0;
                priv->height = 0;
                priv->left = 0;
                priv->width = 0;
            }
            for (j = 0; j < cnt_used; ++j)
                if (overlap(&s, used + j)) {    // no, it's not
                    priv->top = 0;
                    priv->height = 0;
                    priv->left = 0;
                    priv->width = 0;
                }
            if (priv->height > 0) {     // still a fixed event
                used[cnt_used].y0 = priv->top;
                used[cnt_used].y1 = priv->top + priv->height;
                used[cnt_used].x0 = priv->left;
                used[cnt_used].x1 = priv->left + priv->width;
                cnt_used++;
                shift_event(render_priv, imgs + i, priv->top - imgs[i].top);
            }
        }
    }
    qsort(used, cnt_used, sizeof(*used), cmp_rect_y0);

    // try to fit other events in free spaces
    for (i = 0; i < cnt; ++i) {
        ASS_RenderPriv *priv;
        if (!imgs[i].detect_collisions || !imgs[i].height  || !imgs[i].width)
            continue;
        priv = get_render_priv(render_priv, imgs[i].event);
        if (priv && priv->height == 0) {        // not a fixed event
            int shift;
            Rect s;
            s.y0 = imgs[i].top;
            s.y1 = imgs[i].top + imgs[i].height;
            s.x0 = imgs[i].left;
            s.x1 = imgs[i].left + imgs[i].width;
            shift = fit_rect(&s, used, &cnt_used, imgs[i].shift_direction);
            if (shift)
                shift_event(render_priv, imgs + i, shift);
            // make it fixed
            priv->top = imgs[i].top;
            priv->height = imgs[i].height;
            priv->left = imgs[i].left;
            priv->width = imgs[i].width;
        }

    }

    free(used);
}

/**
 * \brief compare two images
 * \param i1 first image
 * \param i2 second image
 * \return 0 if identical, 1 if different positions, 2 if different content
 */
static int ass_image_compare(ASS_Image *i1, ASS_Image *i2)
{
    if (i1->w != i2->w)
        return 2;
    if (i1->h != i2->h)
        return 2;
    if (i1->stride != i2->stride)
        return 2;
    if (i1->color != i2->color)
        return 2;
    if (i1->bitmap != i2->bitmap)
        return 2;
    if (i1->dst_x != i2->dst_x)
        return 1;
    if (i1->dst_y != i2->dst_y)
        return 1;
    return 0;
}

/**
 * \brief compare current and previous image list
 * \param priv library handle
 * \return 0 if identical, 1 if different positions, 2 if different content
 */
static int ass_detect_change(ASS_Renderer *priv)
{
    ASS_Image *img, *img2;
    int diff;

    img = priv->prev_images_root;
    img2 = priv->images_root;
    diff = 0;
    while (img && diff < 2) {
        ASS_Image *next, *next2;
        next = img->next;
        if (img2) {
            int d = ass_image_compare(img, img2);
            if (d > diff)
                diff = d;
            next2 = img2->next;
        } else {
            // previous list is shorter
            diff = 2;
            break;
        }
        img = next;
        img2 = next2;
    }

    // is the previous list longer?
    if (img2)
        diff = 2;

    return diff;
}

/**
 * \brief render a frame
 * \param priv library handle
 * \param track track
 * \param now current video timestamp (ms)
 * \param detect_change a value describing how the new images differ from the previous ones will be written here:
 *        0 if identical, 1 if different positions, 2 if different content.
 *        Can be NULL, in that case no detection is performed.
 */
ASS_Image *ass_render_frame(ASS_Renderer *priv, ASS_Track *track,
                            long long now, int *detect_change)
{
    // init frame
    if (!ass_start_frame(priv, track, now)) {
        if (detect_change)
            *detect_change = 2;
        return NULL;
    }

    // render events separately
    int cnt = 0;
    for (int i = 0; i < track->n_events; i++) {
        ASS_Event *event = track->events + i;
        if ((event->Start <= now)
            && (now < (event->Start + event->Duration))) {
            if (cnt >= priv->eimg_size) {
                priv->eimg_size += 100;
                priv->eimg =
                    realloc(priv->eimg,
                            priv->eimg_size * sizeof(EventImages));
            }
            if (ass_render_event(&priv->state, event, priv->eimg + cnt))
                cnt++;
        }
    }

    // sort by layer
    if (cnt > 0)
        qsort(priv->eimg, cnt, sizeof(EventImages), cmp_event_layer);

    // call fix_collisions for each group of events with the same layer
    EventImages *last = priv->eimg;
    for (int i = 1; i < cnt; i++)
        if (last->event->Layer != priv->eimg[i].event->Layer) {
            fix_collisions(priv, last, priv->eimg + i - last);
            last = priv->eimg + i;
        }
    if (cnt > 0)
        fix_collisions(priv, last, priv->eimg + cnt - last);

    // concat lists
    ASS_Image **tail = &priv->images_root;
    for (int i = 0; i < cnt; i++) {
        ASS_Image *cur = priv->eimg[i].imgs;
        while (cur) {
            *tail = cur;
            tail = &cur->next;
            cur = cur->next;
        }
    }
    ass_frame_ref(priv->images_root);

    if (detect_change)
        *detect_change = ass_detect_change(priv);

    // free the previous image list
    ass_frame_unref(priv->prev_images_root);
    priv->prev_images_root = NULL;

    return priv->images_root;
}

/**
 * \brief Add reference to a frame image list.
 * \param image_list image list returned by ass_render_frame()
 */
void ass_frame_ref(ASS_Image *img)
{
    if (!img)
        return;
    ((ASS_ImagePriv *) img)->ref_count++;
}

/**
 * \brief Release reference to a frame image list.
 * \param image_list image list returned by ass_render_frame()
 */
void ass_frame_unref(ASS_Image *img)
{
    if (!img || --((ASS_ImagePriv *) img)->ref_count)
        return;
    do {
        ASS_ImagePriv *priv = (ASS_ImagePriv *) img;
        img = img->next;
        ass_cache_dec_ref(priv->source);
        ass_aligned_free(priv->buffer);
        free(priv);
    } while (img);
}

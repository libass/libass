/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2010 Grigori Goronzy <greg@geekmind.org>
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

#include <limits.h>
#include <stdint.h>

#include "ass_render.h"
#include "ass_utils.h"

static void ass_reconfigure(ASS_Renderer *priv)
{
    ASS_Settings *settings = &priv->settings;

    priv->render_id++;
    ass_cache_empty(priv->cache.composite_cache);
    ass_cache_empty(priv->cache.bitmap_cache);
    ass_cache_empty(priv->cache.outline_cache);

    priv->width = settings->frame_width;
    priv->height = settings->frame_height;
    priv->frame_content_width = settings->frame_width - settings->left_margin -
        settings->right_margin;
    priv->frame_content_height = settings->frame_height - settings->top_margin -
        settings->bottom_margin;
    priv->fit_width =
        (long long) priv->frame_content_width * priv->height >=
        (long long) priv->frame_content_height * priv->width ?
            priv->width :
            (double) priv->frame_content_width * priv->height / priv->frame_content_height;
    priv->fit_height =
        (long long) priv->frame_content_width * priv->height <=
        (long long) priv->frame_content_height * priv->width ?
            priv->height :
            (double) priv->frame_content_height * priv->width / priv->frame_content_width;
}

void ass_set_frame_size(ASS_Renderer *priv, int w, int h)
{
    if (w <= 0 || h <= 0 || w > FFMIN(INT_MAX, SIZE_MAX) / h)
        w = h = 0;
    if (priv->settings.frame_width != w || priv->settings.frame_height != h) {
        priv->settings.frame_width = w;
        priv->settings.frame_height = h;
        ass_reconfigure(priv);
    }
}

void ass_set_storage_size(ASS_Renderer *priv, int w, int h)
{
    if (w <= 0 || h <= 0 || w > FFMIN(INT_MAX, SIZE_MAX) / h)
        w = h = 0;
    if (priv->settings.storage_width != w ||
        priv->settings.storage_height != h) {
        priv->settings.storage_width = w;
        priv->settings.storage_height = h;
        ass_reconfigure(priv);
    }
}

void ass_set_shaper(ASS_Renderer *priv, ASS_ShapingLevel level)
{
    // select the complex shaper for illegal values
    if (level == ASS_SHAPING_SIMPLE || level == ASS_SHAPING_COMPLEX)
        priv->settings.shaper = level;
    else
        priv->settings.shaper = ASS_SHAPING_COMPLEX;
}

void ass_set_margins(ASS_Renderer *priv, int t, int b, int l, int r)
{
    if (priv->settings.left_margin != l || priv->settings.right_margin != r ||
        priv->settings.top_margin != t || priv->settings.bottom_margin != b) {
        priv->settings.left_margin = l;
        priv->settings.right_margin = r;
        priv->settings.top_margin = t;
        priv->settings.bottom_margin = b;
        ass_reconfigure(priv);
    }
}

void ass_set_use_margins(ASS_Renderer *priv, int use)
{
    priv->settings.use_margins = use;
}

void ass_set_aspect_ratio(ASS_Renderer *priv, double dar, double sar)
{
    ass_set_pixel_aspect(priv, dar / sar);
}

void ass_set_pixel_aspect(ASS_Renderer *priv, double par)
{
    if (par < 0) par = 0;
    if (priv->settings.par != par) {
        priv->settings.par = par;
        ass_reconfigure(priv);
    }
}

void ass_set_font_scale(ASS_Renderer *priv, double font_scale)
{
    if (priv->settings.font_size_coeff != font_scale) {
        priv->settings.font_size_coeff = font_scale;
        ass_reconfigure(priv);
    }
}

void ass_set_hinting(ASS_Renderer *priv, ASS_Hinting ht)
{
    if (priv->settings.hinting != ht) {
        priv->settings.hinting = ht;
        ass_reconfigure(priv);
    }
}

void ass_set_line_spacing(ASS_Renderer *priv, double line_spacing)
{
    priv->settings.line_spacing = line_spacing;
}

void ass_set_line_position(ASS_Renderer *priv, double line_position)
{
    if (priv->settings.line_position != line_position) {
        priv->settings.line_position = line_position;
        ass_reconfigure(priv);
    }
}

void ass_set_fonts(ASS_Renderer *priv, const char *default_font,
                   const char *default_family, int dfp,
                   const char *config, int update)
{
    free(priv->settings.default_font);
    free(priv->settings.default_family);
    priv->settings.default_font = default_font ? strdup(default_font) : 0;
    priv->settings.default_family =
        default_family ? strdup(default_family) : 0;

    ass_reconfigure(priv);

    ass_cache_empty(priv->cache.font_cache);
    ass_cache_empty(priv->cache.metrics_cache);

    if (priv->fontselect)
        ass_fontselect_free(priv->fontselect);
    priv->fontselect = ass_fontselect_init(priv->library, priv->ftlibrary,
            &priv->num_emfonts, default_family, default_font, config, dfp);
}

void ass_set_selective_style_override_enabled(ASS_Renderer *priv, int bits)
{
    if (priv->settings.selective_style_overrides != bits) {
        priv->settings.selective_style_overrides = bits;
        ass_reconfigure(priv);
    }
}

void ass_set_selective_style_override(ASS_Renderer *priv, ASS_Style *style)
{
    ASS_Style *user_style = &priv->user_override_style;
    free(user_style->FontName);
    *user_style = *style;
    user_style->FontName = strdup(user_style->FontName);
    ass_reconfigure(priv);
}

int ass_fonts_update(ASS_Renderer *render_priv)
{
    // This is just a stub now!
    return 1;
}

void ass_set_cache_limits(ASS_Renderer *render_priv, int glyph_max,
                          int bitmap_max)
{
    render_priv->cache.glyph_max = glyph_max ? glyph_max : GLYPH_CACHE_MAX;

    size_t bitmap_cache, composite_cache;
    if (bitmap_max) {
        bitmap_cache = MEGABYTE * (size_t) bitmap_max;
        composite_cache = bitmap_cache / (COMPOSITE_CACHE_RATIO + 1);
        bitmap_cache -= composite_cache;
    } else {
        bitmap_cache = BITMAP_CACHE_MAX_SIZE;
        composite_cache = COMPOSITE_CACHE_MAX_SIZE;
    }
    render_priv->cache.bitmap_max_size = bitmap_cache;
    render_priv->cache.composite_max_size = composite_cache;
}

ASS_FontProvider *
ass_create_font_provider(ASS_Renderer *priv, ASS_FontProviderFuncs *funcs,
                         void *data)
{
    return ass_font_provider_new(priv->fontselect, funcs, data);
}

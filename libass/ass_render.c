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

#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "ass_render.h"
#include "ass_parse.h"
#include "ass_shaper.h"

#define MAX_GLYPHS_INITIAL 1024
#define MAX_LINES_INITIAL 64
#define MAX_BITMAPS_INITIAL 16
#define MAX_STR_LENGTH_INITIAL 64
#define SUBPIXEL_MASK 63
#define SUBPIXEL_ACCURACY 7

#if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM

#include "x86/blend_bitmaps.h"
#include "x86/rasterizer.h"

#endif // ASM

ASS_Renderer *ass_renderer_init(ASS_Library *library)
{
    int error;
    FT_Library ft;
    ASS_Renderer *priv = 0;
    int vmajor, vminor, vpatch;

    error = FT_Init_FreeType(&ft);
    if (error) {
        ass_msg(library, MSGL_FATAL, "%s failed", "FT_Init_FreeType");
        goto ass_init_exit;
    }

    FT_Library_Version(ft, &vmajor, &vminor, &vpatch);
    ass_msg(library, MSGL_V, "Raster: FreeType %d.%d.%d",
           vmajor, vminor, vpatch);

    priv = calloc(1, sizeof(ASS_Renderer));
    if (!priv) {
        FT_Done_FreeType(ft);
        goto ass_init_exit;
    }

    priv->synth_priv = ass_synth_init(BLUR_MAX_RADIUS);

    priv->library = library;
    priv->ftlibrary = ft;
    // images_root and related stuff is zero-filled in calloc

    #if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM
        int sse2 = has_sse2();
        int avx2 = has_avx2();
        priv->add_bitmaps_func = avx2 ? ass_add_bitmaps_avx2 :
            (sse2 ? ass_add_bitmaps_sse2 : ass_add_bitmaps_x86);
        #ifdef __x86_64__
            priv->mul_bitmaps_func = avx2 ? ass_mul_bitmaps_avx2 : ass_mul_bitmaps_sse2;
            priv->sub_bitmaps_func = avx2 ? ass_sub_bitmaps_avx2 : ass_sub_bitmaps_sse2;
        #else
            priv->mul_bitmaps_func = mul_bitmaps_c;
            priv->sub_bitmaps_func = ass_sub_bitmaps_x86;
        #endif
    #else
        priv->add_bitmaps_func = add_bitmaps_c;
        priv->sub_bitmaps_func = sub_bitmaps_c;
        priv->mul_bitmaps_func = mul_bitmaps_c;
    #endif

#if CONFIG_RASTERIZER
#if CONFIG_LARGE_TILES
    priv->rasterizer.tile_order = 5;
    #if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM
        priv->rasterizer.fill_solid = avx2 ? ass_fill_solid_tile32_avx2 :
            (sse2 ? ass_fill_solid_tile32_sse2 : ass_fill_solid_tile32_c);
        priv->rasterizer.fill_halfplane = avx2 ? ass_fill_halfplane_tile32_avx2 :
            (sse2 ? ass_fill_halfplane_tile32_sse2 : ass_fill_halfplane_tile32_c);
        priv->rasterizer.fill_generic = avx2 ? ass_fill_generic_tile32_avx2 :
            (sse2 ? ass_fill_generic_tile32_sse2 : ass_fill_generic_tile32_c);
    #else
        priv->rasterizer.fill_solid = ass_fill_solid_tile32_c;
        priv->rasterizer.fill_halfplane = ass_fill_halfplane_tile32_c;
        priv->rasterizer.fill_generic = ass_fill_generic_tile32_c;
    #endif
#else
    priv->rasterizer.tile_order = 4;
    #if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM
        priv->rasterizer.fill_solid = avx2 ? ass_fill_solid_tile16_avx2 :
            (sse2 ? ass_fill_solid_tile16_sse2 : ass_fill_solid_tile16_c);
        priv->rasterizer.fill_halfplane = avx2 ? ass_fill_halfplane_tile16_avx2 :
            (sse2 ? ass_fill_halfplane_tile16_sse2 : ass_fill_halfplane_tile16_c);
        priv->rasterizer.fill_generic = avx2 ? ass_fill_generic_tile16_avx2 :
            (sse2 ? ass_fill_generic_tile16_sse2 : ass_fill_generic_tile16_c);
    #else
        priv->rasterizer.fill_solid = ass_fill_solid_tile16_c;
        priv->rasterizer.fill_halfplane = ass_fill_halfplane_tile16_c;
        priv->rasterizer.fill_generic = ass_fill_generic_tile16_c;
    #endif
#endif
    priv->rasterizer.outline_error = 16;
    rasterizer_init(&priv->rasterizer);
#endif

    priv->cache.font_cache = ass_font_cache_create();
    priv->cache.bitmap_cache = ass_bitmap_cache_create();
    priv->cache.composite_cache = ass_composite_cache_create();
    priv->cache.outline_cache = ass_outline_cache_create();
    priv->cache.glyph_max = GLYPH_CACHE_MAX;
    priv->cache.bitmap_max_size = BITMAP_CACHE_MAX_SIZE;
    priv->cache.composite_max_size = COMPOSITE_CACHE_MAX_SIZE;

    priv->text_info.max_bitmaps = MAX_BITMAPS_INITIAL;
    priv->text_info.max_glyphs = MAX_GLYPHS_INITIAL;
    priv->text_info.max_lines = MAX_LINES_INITIAL;
    priv->text_info.n_bitmaps = 0;
    priv->text_info.combined_bitmaps = calloc(MAX_BITMAPS_INITIAL, sizeof(CombinedBitmapInfo));
    priv->text_info.glyphs = calloc(MAX_GLYPHS_INITIAL, sizeof(GlyphInfo));
    priv->text_info.lines = calloc(MAX_LINES_INITIAL, sizeof(LineInfo));

    priv->settings.font_size_coeff = 1.;

    priv->shaper = ass_shaper_new(0);
    ass_shaper_info(library);
#ifdef CONFIG_HARFBUZZ
    priv->settings.shaper = ASS_SHAPING_COMPLEX;
#else
    priv->settings.shaper = ASS_SHAPING_SIMPLE;
#endif

  ass_init_exit:
    if (priv)
        ass_msg(library, MSGL_V, "Initialized");
    else
        ass_msg(library, MSGL_ERR, "Initialization failed");

    return priv;
}

static void free_list_clear(ASS_Renderer *render_priv)
{
    if (render_priv->free_head) {
        FreeList *item = render_priv->free_head;
        while(item) {
            FreeList *oi = item;
            ass_aligned_free(item->object);
            item = item->next;
            free(oi);
        }
        render_priv->free_head = NULL;
    }
}

void ass_renderer_done(ASS_Renderer *render_priv)
{
    ass_cache_done(render_priv->cache.font_cache);
    ass_cache_done(render_priv->cache.bitmap_cache);
    ass_cache_done(render_priv->cache.composite_cache);
    ass_cache_done(render_priv->cache.outline_cache);

    ass_free_images(render_priv->images_root);
    ass_free_images(render_priv->prev_images_root);

#if CONFIG_RASTERIZER
    rasterizer_done(&render_priv->rasterizer);
#endif

    if (render_priv->state.stroker) {
        FT_Stroker_Done(render_priv->state.stroker);
        render_priv->state.stroker = 0;
    }
    if (render_priv->ftlibrary)
        FT_Done_FreeType(render_priv->ftlibrary);
    if (render_priv->fontconfig_priv)
        fontconfig_done(render_priv->fontconfig_priv);
    if (render_priv->synth_priv)
        ass_synth_done(render_priv->synth_priv);
    ass_shaper_free(render_priv->shaper);
    free(render_priv->eimg);
    free(render_priv->text_info.glyphs);
    free(render_priv->text_info.lines);

    free(render_priv->text_info.combined_bitmaps);

    free(render_priv->settings.default_font);
    free(render_priv->settings.default_family);

    free(render_priv->user_override_style.FontName);

    free_list_clear(render_priv);
    free(render_priv);
}

/**
 * \brief Create a new ASS_Image
 * Parameters are the same as ASS_Image fields.
 */
static ASS_Image *my_draw_bitmap(unsigned char *bitmap, int bitmap_w,
                                 int bitmap_h, int stride, int dst_x,
                                 int dst_y, uint32_t color)
{
    ASS_Image *img = malloc(sizeof(ASS_Image));

    if (img) {
        img->w = bitmap_w;
        img->h = bitmap_h;
        img->stride = stride;
        img->bitmap = bitmap;
        img->color = color;
        img->dst_x = dst_x;
        img->dst_y = dst_y;
    }

    return img;
}

/**
 * \brief Mapping between script and screen coordinates
 */
static double x2scr(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->orig_width_nocrop / render_priv->font_scale_x /
        render_priv->track->PlayResX +
        FFMAX(render_priv->settings.left_margin, 0);
}
static double x2scr_pos(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->orig_width / render_priv->font_scale_x / render_priv->track->PlayResX +
        render_priv->settings.left_margin;
}
static double x2scr_scaled(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->orig_width_nocrop /
        render_priv->track->PlayResX +
        FFMAX(render_priv->settings.left_margin, 0);
}
static double x2scr_pos_scaled(ASS_Renderer *render_priv, double x)
{
    return x * render_priv->orig_width / render_priv->track->PlayResX +
        render_priv->settings.left_margin;
}
/**
 * \brief Mapping between script and screen coordinates
 */
static double y2scr(ASS_Renderer *render_priv, double y)
{
    return y * render_priv->orig_height_nocrop /
        render_priv->track->PlayResY +
        FFMAX(render_priv->settings.top_margin, 0);
}
static double y2scr_pos(ASS_Renderer *render_priv, double y)
{
    return y * render_priv->orig_height / render_priv->track->PlayResY +
        render_priv->settings.top_margin;
}

// the same for toptitles
static double y2scr_top(ASS_Renderer *render_priv, double y)
{
    if (render_priv->settings.use_margins)
        return y * render_priv->orig_height_nocrop /
            render_priv->track->PlayResY;
    else
        return y * render_priv->orig_height_nocrop /
            render_priv->track->PlayResY +
            FFMAX(render_priv->settings.top_margin, 0);
}
// the same for subtitles
static double y2scr_sub(ASS_Renderer *render_priv, double y)
{
    if (render_priv->settings.use_margins)
        return y * render_priv->orig_height_nocrop /
            render_priv->track->PlayResY +
            FFMAX(render_priv->settings.top_margin, 0)
            + FFMAX(render_priv->settings.bottom_margin, 0);
    else
        return y * render_priv->orig_height_nocrop /
            render_priv->track->PlayResY +
            FFMAX(render_priv->settings.top_margin, 0);
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
static ASS_Image **render_glyph_i(ASS_Renderer *render_priv,
                                  Bitmap *bm, int dst_x, int dst_y,
                                  uint32_t color, uint32_t color2, int brk,
                                  ASS_Image **tail, unsigned int type)
{
    int i, j, x0, y0, x1, y1, cx0, cy0, cx1, cy1, sx, sy, zx, zy;
    Rect r[4];
    ASS_Image *img;

    dst_x += bm->left;
    dst_y += bm->top;

    // we still need to clip against screen boundaries
    zx = x2scr_pos_scaled(render_priv, 0);
    zy = y2scr_pos(render_priv, 0);
    sx = x2scr_pos_scaled(render_priv, render_priv->track->PlayResX);
    sy = y2scr_pos(render_priv, render_priv->track->PlayResY);

    x0 = 0;
    y0 = 0;
    x1 = bm->w;
    y1 = bm->h;
    cx0 = render_priv->state.clip_x0 - dst_x;
    cy0 = render_priv->state.clip_y0 - dst_y;
    cx1 = render_priv->state.clip_x1 - dst_x;
    cy1 = render_priv->state.clip_y1 - dst_y;

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
                lbrk - r[j].x0, r[j].y1 - r[j].y0,
                bm->stride, dst_x + r[j].x0, dst_y + r[j].y0, color);
            if (!img) break;
            img->type = type;
            *tail = img;
            tail = &img->next;
        }
        if (lbrk < r[j].x1) {
            if (lbrk < r[j].x0) lbrk = r[j].x0;
            img = my_draw_bitmap(bm->buffer + r[j].y0 * bm->stride + lbrk,
                r[j].x1 - lbrk, r[j].y1 - r[j].y0,
                bm->stride, dst_x + lbrk, dst_y + r[j].y0, color2);
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
render_glyph(ASS_Renderer *render_priv, Bitmap *bm, int dst_x, int dst_y,
             uint32_t color, uint32_t color2, int brk, ASS_Image **tail, unsigned int type)
{
    // Inverse clipping in use?
    if (render_priv->state.clip_mode)
        return render_glyph_i(render_priv, bm, dst_x, dst_y, color, color2,
                              brk, tail, type);

    // brk is relative to dst_x
    // color = color left of brk
    // color2 = color right of brk
    int b_x0, b_y0, b_x1, b_y1; // visible part of the bitmap
    int clip_x0, clip_y0, clip_x1, clip_y1;
    int tmp;
    ASS_Image *img;

    dst_x += bm->left;
    dst_y += bm->top;
    brk -= bm->left;

    // clipping
    clip_x0 = FFMINMAX(render_priv->state.clip_x0, 0, render_priv->width);
    clip_y0 = FFMINMAX(render_priv->state.clip_y0, 0, render_priv->height);
    clip_x1 = FFMINMAX(render_priv->state.clip_x1, 0, render_priv->width);
    clip_y1 = FFMINMAX(render_priv->state.clip_y1, 0, render_priv->height);
    b_x0 = 0;
    b_y0 = 0;
    b_x1 = bm->w;
    b_y1 = bm->h;

    tmp = dst_x - clip_x0;
    if (tmp < 0) {
        b_x0 = -tmp;
        render_priv->state.has_clips = 1;
    }
    tmp = dst_y - clip_y0;
    if (tmp < 0) {
        b_y0 = -tmp;
        render_priv->state.has_clips = 1;
    }
    tmp = clip_x1 - dst_x - bm->w;
    if (tmp < 0) {
        b_x1 = bm->w + tmp;
        render_priv->state.has_clips = 1;
    }
    tmp = clip_y1 - dst_y - bm->h;
    if (tmp < 0) {
        b_y1 = bm->h + tmp;
        render_priv->state.has_clips = 1;
    }

    if ((b_y0 >= b_y1) || (b_x0 >= b_x1))
        return tail;

    if (brk > b_x0) {           // draw left part
        if (brk > b_x1)
            brk = b_x1;
        img = my_draw_bitmap(bm->buffer + bm->stride * b_y0 + b_x0,
                             brk - b_x0, b_y1 - b_y0, bm->stride,
                             dst_x + b_x0, dst_y + b_y0, color);
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
                             dst_x + brk, dst_y + b_y0, color2);
        if (!img) return tail;
        img->type = type;
        *tail = img;
        tail = &img->next;
    }
    return tail;
}

// Return true if the object could be added, and the object is not NULL.
static bool free_list_add(ASS_Renderer *render_priv, void *object)
{
    if (!object)
        return false;
    FreeList *l = calloc(1, sizeof(FreeList));
    if (!l)
        return false;
    if (!render_priv->free_head) {
        render_priv->free_head = l;
        render_priv->free_head->object = object;
        render_priv->free_tail = render_priv->free_head;
    } else {
        l->object = object;
        render_priv->free_tail->next = l;
        render_priv->free_tail = render_priv->free_tail->next;
    }
    return true;
}

/**
 * Iterate through a list of bitmaps and blend with clip vector, if
 * applicable. The blended bitmaps are added to a free list which is freed
 * at the start of a new frame.
 */
static void blend_vector_clip(ASS_Renderer *render_priv,
                              ASS_Image *head)
{
    Bitmap *clip_bm = NULL;
    ASS_Image *cur;
    ASS_Drawing *drawing = render_priv->state.clip_drawing;
    BitmapHashKey key;
    BitmapHashValue *val;

    if (!drawing)
        return;

    // Try to get mask from cache
    memset(&key, 0, sizeof(key));
    key.type = BITMAP_CLIP;
    key.u.clip.text = drawing->text;
    val = ass_cache_get(render_priv->cache.bitmap_cache, &key);

    if (val) {
        clip_bm = val->bm;
    } else {
        BitmapHashValue v;

        // Not found in cache, parse and rasterize it
        ASS_Outline *outline = ass_drawing_parse(drawing, 1);
        if (!outline) {
            ass_msg(render_priv->library, MSGL_WARN,
                    "Clip vector parsing failed. Skipping.");
            return;
        }

        // We need to translate the clip according to screen borders
        if (render_priv->settings.left_margin != 0 ||
            render_priv->settings.top_margin != 0) {
            FT_Vector trans = {
                .x = int_to_d6(render_priv->settings.left_margin),
                .y = -int_to_d6(render_priv->settings.top_margin),
            };
            outline_translate(outline, trans.x, trans.y);
        }

        clip_bm = outline_to_bitmap(render_priv, outline, 0);

        // Add to cache
        memset(&v, 0, sizeof(v));
        key.u.clip.text = strdup(drawing->text);
        v.bm = clip_bm;
        ass_cache_put(render_priv->cache.bitmap_cache, &key, &v);
    }

    if (!clip_bm) return;

    // Iterate through bitmaps and blend/clip them
    for (cur = head; cur; cur = cur->next) {
        int left, top, right, bottom, w, h;
        int ax, ay, aw, ah, as;
        int bx, by, bw, bh, bs;
        int aleft, atop, bleft, btop;
        unsigned char *abuffer, *bbuffer, *nbuffer;

        render_priv->state.has_clips = 1;

        abuffer = cur->bitmap;
        bbuffer = clip_bm->buffer;
        ax = cur->dst_x;
        ay = cur->dst_y;
        aw = cur->w;
        ah = cur->h;
        as = cur->stride;
        bx = clip_bm->left;
        by = clip_bm->top;
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

        if (render_priv->state.clip_drawing_mode) {
            // Inverse clip
            if (ax + aw < bx || ay + ah < by || ax > bx + bw ||
                ay > by + bh || !h || !w) {
                continue;
            }

            // Allocate new buffer and add to free list
            nbuffer = ass_aligned_alloc(32, as * ah);
            if (!free_list_add(render_priv, nbuffer)) {
                ass_aligned_free(nbuffer);
                return;
            }

            // Blend together
            memcpy(nbuffer, abuffer, ((ah - 1) * as) + aw);
            render_priv->sub_bitmaps_func(nbuffer + atop * as + aleft, as,
                                          bbuffer + btop * bs + bleft, bs,
                                          h, w);
        } else {
            // Regular clip
            if (ax + aw < bx || ay + ah < by || ax > bx + bw ||
                ay > by + bh || !h || !w) {
                cur->w = cur->h = cur->stride = 0;
                continue;
            }

            // Allocate new buffer and add to free list
            unsigned align = (w >= 16) ? 16 : ((w >= 8) ? 8 : 1);
            unsigned ns = ass_align(align, w);
            nbuffer = ass_aligned_alloc(align, ns * h);
            if (!free_list_add(render_priv, nbuffer)) {
                ass_aligned_free(nbuffer);
                return;
            }

            // Blend together
            render_priv->mul_bitmaps_func(nbuffer, ns,
                                          abuffer + atop * as + aleft, as,
                                          bbuffer + btop * bs + bleft, bs,
                                          w, h);
            cur->dst_x += aleft;
            cur->dst_y += atop;
            cur->w = w;
            cur->h = h;
            cur->stride = ns;
        }
        cur->bitmap = nbuffer;
    }
}

static inline int is_skip_symbol(uint32_t x)
{
    return (x == 0 || x == '\n' || x == '\r');
}
/**
 * \brief Convert TextInfo struct to ASS_Image list
 * Splits glyphs in halves when needed (for \kf karaoke).
 */
static ASS_Image *render_text(ASS_Renderer *render_priv, int dst_x, int dst_y)
{
    int pen_x, pen_y;
    int i;
    Bitmap *bm;
    ASS_Image *head;
    ASS_Image **tail = &head;
    TextInfo *text_info = &render_priv->text_info;

    for (i = 0; i < text_info->n_bitmaps; ++i) {
        CombinedBitmapInfo *info = &text_info->combined_bitmaps[i];
        if (!info->bm_s || (info->shadow_x == 0 && info->shadow_y == 0))
            continue;
        if (render_priv->state.border_style == 4)
            continue;

        pen_x = dst_x + info->pos.x;
        pen_y = dst_y + info->pos.y;
        bm = info->bm_s;

        tail =
            render_glyph(render_priv, bm, pen_x, pen_y, info->c[3], 0,
                    1000000, tail, IMAGE_TYPE_SHADOW);
    }

    for (i = 0; i < text_info->n_bitmaps; ++i) {
        CombinedBitmapInfo *info = &text_info->combined_bitmaps[i];
        if (!info->bm_o)
            continue;

        pen_x = dst_x + info->pos.x;
        pen_y = dst_y + info->pos.y;
        bm = info->bm_o;

        if ((info->effect_type == EF_KARAOKE_KO)
                && (info->effect_timing <= info->first_pos_x)) {
            // do nothing
        } else {
            tail =
                render_glyph(render_priv, bm, pen_x, pen_y, info->c[2],
                        0, 1000000, tail, IMAGE_TYPE_OUTLINE);
        }
    }

    for (i = 0; i < text_info->n_bitmaps; ++i) {
        CombinedBitmapInfo *info = &text_info->combined_bitmaps[i];
        if (!info->bm)
            continue;

        pen_x = dst_x + info->pos.x;
        pen_y = dst_y + info->pos.y;
        bm = info->bm;

        if ((info->effect_type == EF_KARAOKE)
                || (info->effect_type == EF_KARAOKE_KO)) {
            if (info->effect_timing > info->first_pos_x)
                tail =
                    render_glyph(render_priv, bm, pen_x, pen_y,
                            info->c[0], 0, 1000000, tail, IMAGE_TYPE_CHARACTER);
            else
                tail =
                    render_glyph(render_priv, bm, pen_x, pen_y,
                            info->c[1], 0, 1000000, tail, IMAGE_TYPE_CHARACTER);
        } else if (info->effect_type == EF_KARAOKE_KF) {
            tail =
                render_glyph(render_priv, bm, pen_x, pen_y, info->c[0],
                        info->c[1], info->effect_timing, tail, IMAGE_TYPE_CHARACTER);
        } else
            tail =
                render_glyph(render_priv, bm, pen_x, pen_y, info->c[0],
                        0, 1000000, tail, IMAGE_TYPE_CHARACTER);
    }

    *tail = 0;
    blend_vector_clip(render_priv, head);

    return head;
}

static void compute_string_bbox(TextInfo *text, DBBox *bbox)
{
    int i;

    if (text->length > 0) {
        bbox->xMin = 32000;
        bbox->xMax = -32000;
        bbox->yMin = -1 * text->lines[0].asc + d6_to_double(text->glyphs[0].pos.y);
        bbox->yMax = text->height - text->lines[0].asc +
                     d6_to_double(text->glyphs[0].pos.y);

        for (i = 0; i < text->length; ++i) {
            GlyphInfo *info = text->glyphs + i;
            if (info->skip) continue;
            double s = d6_to_double(info->pos.x);
            double e = s + d6_to_double(info->cluster_advance.x);
            bbox->xMin = FFMIN(bbox->xMin, s);
            bbox->xMax = FFMAX(bbox->xMax, e);
        }
    } else
        bbox->xMin = bbox->xMax = bbox->yMin = bbox->yMax = 0.;
}

static ASS_Style *handle_selective_style_overrides(ASS_Renderer *render_priv,
                                                   ASS_Style *rstyle)
{
    // The script style is the one the event was declared with.
    // The rstyle is either NULL, or the style used with a \r tag.
    ASS_Style *script = render_priv->track->styles +
                        render_priv->state.event->Style;
    ASS_Style *new = &render_priv->state.override_style_temp_storage;
    int explicit = event_is_positioned(render_priv->state.event->Text);
    int requested = render_priv->settings.selective_style_overrides;
    double scale;

    if (!rstyle)
        rstyle = script;

    render_priv->state.style = script;
    render_priv->state.overrides = ASS_OVERRIDE_BIT_FONT_SIZE; // odd default

    if (explicit && (requested & ASS_OVERRIDE_BIT_FONT_SIZE))
        render_priv->state.overrides &= ~(unsigned)ASS_OVERRIDE_BIT_FONT_SIZE;

    if (!explicit && (requested & ASS_OVERRIDE_BIT_STYLE))
        render_priv->state.overrides |= ASS_OVERRIDE_BIT_STYLE;

    if (!(render_priv->state.overrides & ASS_OVERRIDE_BIT_STYLE))
        return rstyle;

    // Create a new style that contains a mix of the original style and
    // user_style (the user's override style). Copy only fields from the
    // script's style that are deemed necessary.
    *new = render_priv->user_override_style;

    new->StrikeOut = rstyle->StrikeOut;
    new->Underline = rstyle->Underline;
    new->Angle = rstyle->Angle;

    new->MarginL = script->MarginL;
    new->MarginR = script->MarginR;
    new->MarginV = script->MarginV;
    new->Alignment = script->Alignment;
    new->Encoding = script->Encoding;
    new->treat_fontname_as_pattern = script->treat_fontname_as_pattern;

    // The user style is supposed to be independent of the script resolution.
    // Treat the user style's values as if they were specified for a script with
    // PlayResY=288, and rescale the values to the current script.
    scale = render_priv->track->PlayResY / 288.0;
    new->FontSize *= scale;
    new->Spacing *= scale;
    new->Outline *= scale;
    new->Shadow *= scale;

    render_priv->state.style = new;

    return new;
}

static void init_font_scale(ASS_Renderer *render_priv)
{
    ASS_Settings *settings_priv = &render_priv->settings;

    render_priv->font_scale = ((double) render_priv->orig_height) /
                              render_priv->track->PlayResY;
    if (settings_priv->storage_height)
        render_priv->blur_scale = ((double) render_priv->orig_height) /
            settings_priv->storage_height;
    else
        render_priv->blur_scale = 1.;
    if (render_priv->track->ScaledBorderAndShadow)
        render_priv->border_scale =
            ((double) render_priv->orig_height) /
            render_priv->track->PlayResY;
    else
        render_priv->border_scale = render_priv->blur_scale;
    if (!settings_priv->storage_height)
        render_priv->blur_scale = render_priv->border_scale;

    if (render_priv->state.overrides & ASS_OVERRIDE_BIT_FONT_SIZE) {
        render_priv->font_scale *= settings_priv->font_size_coeff;
        render_priv->border_scale *= settings_priv->font_size_coeff;
        render_priv->blur_scale *= settings_priv->font_size_coeff;
    }
}

/**
 * \brief partially reset render_context to style values
 * Works like {\r}: resets some style overrides
 */
void reset_render_context(ASS_Renderer *render_priv, ASS_Style *style)
{
    style = handle_selective_style_overrides(render_priv, style);

    init_font_scale(render_priv);

    render_priv->state.c[0] = style->PrimaryColour;
    render_priv->state.c[1] = style->SecondaryColour;
    render_priv->state.c[2] = style->OutlineColour;
    render_priv->state.c[3] = style->BackColour;
    render_priv->state.flags =
        (style->Underline ? DECO_UNDERLINE : 0) |
        (style->StrikeOut ? DECO_STRIKETHROUGH : 0);
    render_priv->state.font_size = style->FontSize;

    free(render_priv->state.family);
    render_priv->state.family = NULL;
    render_priv->state.family = strdup(style->FontName);
    render_priv->state.treat_family_as_pattern =
        style->treat_fontname_as_pattern;
    render_priv->state.bold = style->Bold;
    render_priv->state.italic = style->Italic;
    update_font(render_priv);

    render_priv->state.border_style = style->BorderStyle;
    render_priv->state.border_x = style->Outline;
    render_priv->state.border_y = style->Outline;
    change_border(render_priv, render_priv->state.border_x, render_priv->state.border_y);
    render_priv->state.scale_x = style->ScaleX;
    render_priv->state.scale_y = style->ScaleY;
    render_priv->state.hspacing = style->Spacing;
    render_priv->state.be = 0;
    render_priv->state.blur = style->Blur;
    render_priv->state.shadow_x = style->Shadow;
    render_priv->state.shadow_y = style->Shadow;
    render_priv->state.frx = render_priv->state.fry = 0.;
    render_priv->state.frz = M_PI * style->Angle / 180.;
    render_priv->state.fax = render_priv->state.fay = 0.;
    render_priv->state.font_encoding = style->Encoding;
}

/**
 * \brief Start new event. Reset render_priv->state.
 */
static void
init_render_context(ASS_Renderer *render_priv, ASS_Event *event)
{
    render_priv->state.event = event;
    render_priv->state.parsed_tags = 0;
    render_priv->state.has_clips = 0;

    reset_render_context(render_priv, NULL);
    render_priv->state.wrap_style = render_priv->track->WrapStyle;

    render_priv->state.evt_type = EVENT_NORMAL;
    render_priv->state.alignment = render_priv->state.style->Alignment;
    render_priv->state.pos_x = 0;
    render_priv->state.pos_y = 0;
    render_priv->state.org_x = 0;
    render_priv->state.org_y = 0;
    render_priv->state.have_origin = 0;
    render_priv->state.clip_x0 = 0;
    render_priv->state.clip_y0 = 0;
    render_priv->state.clip_x1 = render_priv->track->PlayResX;
    render_priv->state.clip_y1 = render_priv->track->PlayResY;
    render_priv->state.clip_mode = 0;
    render_priv->state.detect_collisions = 1;
    render_priv->state.fade = 0;
    render_priv->state.drawing_scale = 0;
    render_priv->state.pbo = 0;
    render_priv->state.effect_type = EF_NONE;
    render_priv->state.effect_timing = 0;
    render_priv->state.effect_skip_timing = 0;
    render_priv->state.bm_run_id = 0;
    ass_drawing_free(render_priv->state.drawing);
    render_priv->state.drawing = ass_drawing_new(render_priv->library,
            render_priv->ftlibrary);

    apply_transition_effects(render_priv, event);
}

static void free_render_context(ASS_Renderer *render_priv)
{
    free(render_priv->state.family);
    ass_drawing_free(render_priv->state.drawing);
    ass_drawing_free(render_priv->state.clip_drawing);

    render_priv->state.family = NULL;
    render_priv->state.drawing = NULL;
    render_priv->state.clip_drawing = NULL;
}

/*
 * Replace the outline of a glyph by a contour which makes up a simple
 * opaque rectangle.
 */
static void draw_opaque_box(ASS_Renderer *render_priv, GlyphInfo *info,
                            int asc, int desc, ASS_Outline *ol,
                            FT_Vector advance, int sx, int sy)
{
    int adv = advance.x;
    double scale_y = info->orig_scale_y;
    double scale_x = info->orig_scale_x;

    // to avoid gaps
    sx = FFMAX(64, sx);
    sy = FFMAX(64, sy);

    // Emulate the WTFish behavior of VSFilter, i.e. double-scale
    // the sizes of the opaque box.
    adv += double_to_d6(info->hspacing * render_priv->font_scale * scale_x);
    adv *= scale_x;
    sx *= scale_x;
    sy *= scale_y;
    desc *= scale_y;
    desc += asc * (scale_y - 1.0);

    FT_Vector points[4] = {
        { .x = -sx,         .y = asc + sy },
        { .x = adv + sx,    .y = asc + sy },
        { .x = adv + sx,    .y = -desc - sy },
        { .x = -sx,         .y = -desc - sy },
    };

    ol->n_points = ol->n_contours = 0;
    if (!outline_alloc(ol, 4, 1))
        return;
    for (int i = 0; i < 4; ++i) {
        ol->points[ol->n_points] = points[i];
        ol->tags[ol->n_points++] = 1;
    }
    ol->contours[ol->n_contours++] = ol->n_points - 1;
}

/*
 * Stroke an outline glyph in x/y direction.  Applies various fixups to get
 * around limitations of the FreeType stroker.
 */
static void stroke_outline(ASS_Renderer *render_priv, ASS_Outline *outline,
                           int sx, int sy)
{
    if (sx <= 0 && sy <= 0)
        return;

    fix_freetype_stroker(outline, sx, sy);

    size_t n_points = outline->n_points;
    if (n_points > SHRT_MAX) {
        ass_msg(render_priv->library, MSGL_WARN, "Too many outline points: %d",
                outline->n_points);
        n_points = SHRT_MAX;
    }

    size_t n_contours = FFMIN(outline->n_contours, SHRT_MAX);
    short contours_small[EFFICIENT_CONTOUR_COUNT];
    short *contours = contours_small;
    short *contours_large = NULL;
    if (n_contours > EFFICIENT_CONTOUR_COUNT) {
        contours_large = malloc(n_contours * sizeof(short));
        if (!contours_large)
            return;
        contours = contours_large;
    }
    for (size_t i = 0; i < n_contours; ++i)
        contours[i] = FFMIN(outline->contours[i], n_points - 1);

    FT_Outline ftol;
    ftol.n_points = n_points;
    ftol.n_contours = n_contours;
    ftol.points = outline->points;
    ftol.tags = outline->tags;
    ftol.contours = contours;
    ftol.flags = 0;

    // Borders are equal; use the regular stroker
    if (sx == sy && render_priv->state.stroker) {
        int error;
        FT_StrokerBorder border = FT_Outline_GetOutsideBorder(&ftol);
        error = FT_Stroker_ParseOutline(render_priv->state.stroker, &ftol, 0);
        if (error) {
            ass_msg(render_priv->library, MSGL_WARN,
                    "FT_Stroker_ParseOutline failed, error: %d", error);
        }
        unsigned new_points, new_contours;
        error = FT_Stroker_GetBorderCounts(render_priv->state.stroker, border,
                &new_points, &new_contours);
        if (error) {
            ass_msg(render_priv->library, MSGL_WARN,
                    "FT_Stroker_GetBorderCounts failed, error: %d", error);
        }
        outline_free(outline);
        outline->n_points = outline->n_contours = 0;
        if (new_contours > FFMAX(EFFICIENT_CONTOUR_COUNT, n_contours)) {
            if (!ASS_REALLOC_ARRAY(contours_large, new_contours)) {
                free(contours_large);
                return;
            }
        }
        n_points = new_points;
        n_contours = new_contours;
        if (!outline_alloc(outline, n_points, n_contours)) {
            ass_msg(render_priv->library, MSGL_WARN,
                    "Not enough memory for border outline");
            free(contours_large);
            return;
        }
        ftol.n_points = ftol.n_contours = 0;
        ftol.points = outline->points;
        ftol.tags = outline->tags;

        FT_Stroker_ExportBorder(render_priv->state.stroker, border, &ftol);

        outline->n_points = n_points;
        outline->n_contours = n_contours;
        for (size_t i = 0; i < n_contours; ++i)
            outline->contours[i] = contours[i];

    // "Stroke" with the outline emboldener (in two passes if needed).
    // The outlines look uglier, but the emboldening never adds any points
    } else {
#if (FREETYPE_MAJOR > 2) || \
    ((FREETYPE_MAJOR == 2) && (FREETYPE_MINOR > 4)) || \
    ((FREETYPE_MAJOR == 2) && (FREETYPE_MINOR == 4) && (FREETYPE_PATCH >= 10))
        FT_Outline_EmboldenXY(&ftol, sx * 2, sy * 2);
        FT_Outline_Translate(&ftol, -sx, -sy);
#else
        int i;
        FT_Outline nol;

        FT_Outline_New(render_priv->ftlibrary, ftol.n_points,
                       ftol.n_contours, &nol);
        FT_Outline_Copy(&ftol, &nol);

        FT_Outline_Embolden(&ftol, sx * 2);
        FT_Outline_Translate(&ftol, -sx, -sx);
        FT_Outline_Embolden(&nol, sy * 2);
        FT_Outline_Translate(&nol, -sy, -sy);

        for (i = 0; i < ftol.n_points; i++)
            ftol.points[i].y = nol.points[i].y;

        FT_Outline_Done(render_priv->ftlibrary, &nol);
#endif
    }

    free(contours_large);
}

/**
 * \brief Prepare glyph hash
 */
static void
fill_glyph_hash(ASS_Renderer *priv, OutlineHashKey *outline_key,
                GlyphInfo *info)
{
    if (info->drawing) {
        DrawingHashKey *key = &outline_key->u.drawing;
        outline_key->type = OUTLINE_DRAWING;
        key->scale_x = double_to_d16(info->scale_x);
        key->scale_y = double_to_d16(info->scale_y);
        key->outline.x = double_to_d16(info->border_x);
        key->outline.y = double_to_d16(info->border_y);
        key->border_style = info->border_style;
        // hpacing only matters for opaque box borders (see draw_opaque_box),
        // so for normal borders, maximize cache utility by ignoring it
        key->hspacing =
            info->border_style == 3 ? double_to_d16(info->hspacing) : 0;
        key->hash = info->drawing->hash;
        key->text = info->drawing->text;
        key->pbo = info->drawing->pbo;
        key->scale = info->drawing->scale;
    } else {
        GlyphHashKey *key = &outline_key->u.glyph;
        outline_key->type = OUTLINE_GLYPH;
        key->font = info->font;
        key->size = info->font_size;
        key->face_index = info->face_index;
        key->glyph_index = info->glyph_index;
        key->bold = info->bold;
        key->italic = info->italic;
        key->scale_x = double_to_d16(info->scale_x);
        key->scale_y = double_to_d16(info->scale_y);
        key->outline.x = double_to_d16(info->border_x);
        key->outline.y = double_to_d16(info->border_y);
        key->flags = info->flags;
        key->border_style = info->border_style;
        key->hspacing =
            info->border_style == 3 ? double_to_d16(info->hspacing) : 0;
    }
}

/**
 * \brief Prepare combined-bitmap hash
 */
static void fill_composite_hash(CompositeHashKey *hk, CombinedBitmapInfo *info)
{
    hk->w = info->w;
    hk->h = info->h;
    hk->o_w = info->o_w;
    hk->o_h = info->o_h;
    hk->be = info->be;
    hk->blur = info->blur;
    hk->border_style = info->border_style;
    hk->has_outline = info->has_outline;
    hk->is_drawing = info->is_drawing;
    hk->str.size = info->str_length;
    hk->str.data = info->str;
    hk->chars = info->chars;
    hk->shadow_x = info->shadow_x;
    hk->shadow_y = info->shadow_y;
    hk->flags = info->flags;
    hk->bold = info->bold;
    hk->italic = info->italic;
    hk->hspacing = info->hspacing;
    hk->scale_x = info->scale_x;
    hk->scale_y = info->scale_y;
    hk->has_border = info->has_border;
    hk->border_x = info->border_x;
    hk->border_y = info->border_y;
    hk->frx = info->frx;
    hk->fry = info->fry;
    hk->frz = info->frz;
    hk->fax = info->fax;
    hk->fay = info->fay;
    hk->shift_x = info->shift_x;
    hk->shift_y = info->shift_y;
    hk->advance = info->advance;
}

/**
 * \brief Get normal and outline (border) glyphs
 * \param info out: struct filled with extracted data
 * Tries to get both glyphs from cache.
 * If they can't be found, gets a glyph from font face, generates outline with FT_Stroker,
 * and add them to cache.
 * The glyphs are returned in info->glyph and info->outline_glyph
 */
static void
get_outline_glyph(ASS_Renderer *priv, GlyphInfo *info)
{
    OutlineHashValue *val;
    OutlineHashKey key;

    memset(&info->hash_key, 0, sizeof(key));

    fill_glyph_hash(priv, &key, info);
    val = ass_cache_get(priv->cache.outline_cache, &key);

    if (!val) {
        OutlineHashValue v;
        memset(&v, 0, sizeof(v));

        if (info->drawing) {
            ASS_Drawing *drawing = info->drawing;
            ass_drawing_hash(drawing);
            if(!ass_drawing_parse(drawing, 0))
                return;
            v.outline = outline_copy(&drawing->outline);
            v.advance.x = drawing->advance.x;
            v.advance.y = drawing->advance.y;
            v.asc = drawing->asc;
            v.desc = drawing->desc;
            key.u.drawing.text = strdup(drawing->text);
        } else {
            ass_face_set_size(info->font->faces[info->face_index],
                              info->font_size);
            ass_font_set_transform(info->font, info->scale_x,
                                   info->scale_y, NULL);
            FT_Glyph glyph =
                ass_font_get_glyph(priv->fontconfig_priv, info->font,
                        info->symbol, info->face_index, info->glyph_index,
                        priv->settings.hinting, info->flags);
            if (glyph != NULL) {
                v.outline = outline_convert(&((FT_OutlineGlyph)glyph)->outline);
                if (priv->settings.shaper == ASS_SHAPING_SIMPLE) {
                    v.advance.x = d16_to_d6(glyph->advance.x);
                    v.advance.y = d16_to_d6(glyph->advance.y);
                }
                FT_Done_Glyph(glyph);
                ass_font_get_asc_desc(info->font, info->symbol,
                        &v.asc, &v.desc);
                v.asc  *= info->scale_y;
                v.desc *= info->scale_y;
            }
        }

        if (!v.outline)
            return;

        outline_get_cbox(v.outline, &v.bbox_scaled);

        if (info->border_style == 3) {
            FT_Vector advance;

            v.border = calloc(1, sizeof(ASS_Outline));

            if (priv->settings.shaper == ASS_SHAPING_SIMPLE || info->drawing)
                advance = v.advance;
            else
                advance = info->advance;

            draw_opaque_box(priv, info, v.asc, v.desc, v.border, advance,
                    double_to_d6(info->border_x * priv->border_scale),
                    double_to_d6(info->border_y * priv->border_scale));

        } else if ((info->border_x > 0 || info->border_y > 0)
                && double_to_d6(info->scale_x) && double_to_d6(info->scale_y)) {

            change_border(priv, info->border_x, info->border_y);
            v.border = outline_copy(v.outline);
            stroke_outline(priv, v.border,
                    double_to_d6(info->border_x * priv->border_scale),
                    double_to_d6(info->border_y * priv->border_scale));
        }

        val = ass_cache_put(priv->cache.outline_cache, &key, &v);
    }

    info->hash_key.u.outline.outline = val;
    info->outline = val->outline;
    info->border = val->border;
    info->bbox = val->bbox_scaled;
    if (info->drawing || priv->settings.shaper == ASS_SHAPING_SIMPLE) {
        info->cluster_advance.x = info->advance.x = val->advance.x;
        info->cluster_advance.y = info->advance.y = val->advance.y;
    }
    info->asc = val->asc;
    info->desc = val->desc;
}

/**
 * \brief Apply transformation to outline points of a glyph
 * Applies rotations given by frx, fry and frz and projects the points back
 * onto the screen plane.
 */
static void
transform_3d_points(FT_Vector shift, ASS_Outline *outline, double frx, double fry,
                    double frz, double fax, double fay, double scale,
                    int yshift)
{
    double sx = sin(frx);
    double sy = sin(fry);
    double sz = sin(frz);
    double cx = cos(frx);
    double cy = cos(fry);
    double cz = cos(frz);
    FT_Vector *p = outline->points;
    double x, y, z, xx, yy, zz;
    int dist;

    dist = 20000 * scale;
    for (size_t i = 0; i < outline->n_points; ++i) {
        x = (double) p[i].x + shift.x + (fax * (yshift - p[i].y));
        y = (double) p[i].y + shift.y + (-fay * p[i].x);
        z = 0.;

        xx = x * cz + y * sz;
        yy = -(x * sz - y * cz);
        zz = z;

        x = xx;
        y = yy * cx + zz * sx;
        z = yy * sx - zz * cx;

        xx = x * cy + z * sy;
        yy = y;
        zz = x * sy - z * cy;

        zz = FFMAX(zz, 1000 - dist);

        x = (xx * dist) / (zz + dist);
        y = (yy * dist) / (zz + dist);
        p[i].x = x - shift.x + 0.5;
        p[i].y = y - shift.y + 0.5;
    }
}

/**
 * \brief Apply 3d transformation to several objects
 * \param shift FreeType vector
 * \param glyph FreeType glyph
 * \param glyph2 FreeType glyph
 * \param frx x-axis rotation angle
 * \param fry y-axis rotation angle
 * \param frz z-axis rotation angle
 * Rotates both glyphs by frx, fry and frz. Shift vector is added before rotation and subtracted after it.
 */
static void
transform_3d(FT_Vector shift, ASS_Outline *outline, ASS_Outline *border,
             double frx, double fry, double frz, double fax, double fay,
             double scale, int yshift)
{
    frx = -frx;
    frz = -frz;
    if (frx != 0. || fry != 0. || frz != 0. || fax != 0. || fay != 0.) {
        if (outline)
            transform_3d_points(shift, outline, frx, fry, frz,
                                fax, fay, scale, yshift);

        if (border)
            transform_3d_points(shift, border, frx, fry, frz,
                                fax, fay, scale, yshift);
    }
}

/**
 * \brief Get bitmaps for a glyph
 * \param info glyph info
 * Tries to get glyph bitmaps from bitmap cache.
 * If they can't be found, they are generated by rotating and rendering the glyph.
 * After that, bitmaps are added to the cache.
 * They are returned in info->bm (glyph), info->bm_o (outline) and info->bm_s (shadow).
 */
static void
get_bitmap_glyph(ASS_Renderer *render_priv, GlyphInfo *info)
{
    BitmapHashValue *val;
    OutlineBitmapHashKey *key = &info->hash_key.u.outline;

    if (!info->outline || info->symbol == '\n' || info->symbol == 0 || info->skip)
        return;

    val = ass_cache_get(render_priv->cache.bitmap_cache, &info->hash_key);

    if (!val) {
        FT_Vector shift;
        BitmapHashValue hash_val;
        int error;
        double fax_scaled, fay_scaled;
        double scale_x = render_priv->font_scale_x;

        hash_val.bm = hash_val.bm_o = hash_val.bm_s = 0;

        ASS_Outline *outline = outline_copy(info->outline);
        ASS_Outline *border  = outline_copy(info->border);

        // calculating rotation shift vector (from rotation origin to the glyph basepoint)
        shift.x = key->shift_x;
        shift.y = key->shift_y;
        fax_scaled = info->fax / info->scale_y * info->scale_x;
        fay_scaled = info->fay / info->scale_x * info->scale_y;

        // apply rotation
        // use blur_scale because, like blurs, VSFilter forgets to scale this
        transform_3d(shift, outline, border,
                info->frx, info->fry, info->frz, fax_scaled,
                fay_scaled, render_priv->blur_scale, info->asc);

        // PAR correction scaling
        FT_Matrix m = { double_to_d16(scale_x), 0,
            0, double_to_d16(1.0) };

        // subpixel shift
        if (outline) {
            if (scale_x != 1.0)
                outline_transform(outline, &m);
            outline_translate(outline, key->advance.x, -key->advance.y);
        }
        if (border) {
            if (scale_x != 1.0)
                outline_transform(border, &m);
            outline_translate(border, key->advance.x, -key->advance.y);
        }

        // render glyph
        error = outline_to_bitmap3(render_priv,
                outline, border,
                &hash_val.bm, &hash_val.bm_o,
                &hash_val.bm_s, info->be,
                info->blur * render_priv->blur_scale,
                key->shadow_offset,
                info->border_style,
                info->border_x || info->border_y);
        if (error)
            info->symbol = 0;

        val = ass_cache_put(render_priv->cache.bitmap_cache, &info->hash_key,
                &hash_val);

        outline_free(outline);
        free(outline);
        outline_free(border);
        free(border);
    }

    info->bm = val->bm;
    info->bm_o = val->bm_o;
}

/**
 * This function goes through text_info and calculates text parameters.
 * The following text_info fields are filled:
 *   height
 *   lines[].height
 *   lines[].asc
 *   lines[].desc
 */
static void measure_text(ASS_Renderer *render_priv)
{
    TextInfo *text_info = &render_priv->text_info;
    int cur_line = 0;
    double max_asc = 0., max_desc = 0.;
    GlyphInfo *last = NULL;
    int i;
    int empty_line = 1;
    text_info->height = 0.;
    for (i = 0; i < text_info->length + 1; ++i) {
        if ((i == text_info->length) || text_info->glyphs[i].linebreak) {
            if (empty_line && cur_line > 0 && last) {
                max_asc = d6_to_double(last->asc) / 2.0;
                max_desc = d6_to_double(last->desc) / 2.0;
            }
            text_info->lines[cur_line].asc = max_asc;
            text_info->lines[cur_line].desc = max_desc;
            text_info->height += max_asc + max_desc;
            cur_line++;
            max_asc = max_desc = 0.;
            empty_line = 1;
        }
        if (i < text_info->length) {
            GlyphInfo *cur = text_info->glyphs + i;
            if (d6_to_double(cur->asc) > max_asc)
                max_asc = d6_to_double(cur->asc);
            if (d6_to_double(cur->desc) > max_desc)
                max_desc = d6_to_double(cur->desc);
            if (cur->symbol != '\n' && cur->symbol != 0) {
                empty_line = 0;
                last = cur;
            }
        }
    }
    text_info->height +=
        (text_info->n_lines -
         1) * render_priv->settings.line_spacing;
}

/**
 * Mark extra whitespace for later removal.
 */
#define IS_WHITESPACE(x) ((x->symbol == ' ' || x->symbol == '\n') \
                          && !x->linebreak)
static void trim_whitespace(ASS_Renderer *render_priv)
{
    int i, j;
    GlyphInfo *cur;
    TextInfo *ti = &render_priv->text_info;

    // Mark trailing spaces
    i = ti->length - 1;
    cur = ti->glyphs + i;
    while (i && IS_WHITESPACE(cur)) {
        cur->skip++;
        cur = ti->glyphs + --i;
    }

    // Mark leading whitespace
    i = 0;
    cur = ti->glyphs;
    while (i < ti->length && IS_WHITESPACE(cur)) {
        cur->skip++;
        cur = ti->glyphs + ++i;
    }

    // Mark all extraneous whitespace inbetween
    for (i = 0; i < ti->length; ++i) {
        cur = ti->glyphs + i;
        if (cur->linebreak) {
            // Mark whitespace before
            j = i - 1;
            cur = ti->glyphs + j;
            while (j && IS_WHITESPACE(cur)) {
                cur->skip++;
                cur = ti->glyphs + --j;
            }
            // A break itself can contain a whitespace, too
            cur = ti->glyphs + i;
            if (cur->symbol == ' ') {
                cur->skip++;
                // Mark whitespace after
                j = i + 1;
                cur = ti->glyphs + j;
                while (j < ti->length && IS_WHITESPACE(cur)) {
                    cur->skip++;
                    cur = ti->glyphs + ++j;
                }
                i = j - 1;
            }
        }
    }
}
#undef IS_WHITESPACE

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
wrap_lines_smart(ASS_Renderer *render_priv, double max_text_width)
{
    int i;
    GlyphInfo *cur, *s1, *e1, *s2, *s3, *w;
    int last_space;
    int break_type;
    int exit;
    double pen_shift_x;
    double pen_shift_y;
    int cur_line;
    int run_offset;
    TextInfo *text_info = &render_priv->text_info;

    last_space = -1;
    text_info->n_lines = 1;
    break_type = 0;
    s1 = text_info->glyphs;     // current line start
    for (i = 0; i < text_info->length; ++i) {
        int break_at = -1;
        double s_offset, len;
        cur = text_info->glyphs + i;
        s_offset = d6_to_double(s1->bbox.xMin + s1->pos.x);
        len = d6_to_double(cur->bbox.xMax + cur->pos.x) - s_offset;

        if (cur->symbol == '\n') {
            break_type = 2;
            break_at = i;
            ass_msg(render_priv->library, MSGL_DBG2,
                    "forced line break at %d", break_at);
        } else if (cur->symbol == ' ') {
            last_space = i;
        } else if (len >= max_text_width
                   && (render_priv->state.wrap_style != 2)) {
            break_type = 1;
            break_at = last_space;
            if (break_at >= 0)
                ass_msg(render_priv->library, MSGL_DBG2, "line break at %d",
                        break_at);
        }

        if (break_at != -1) {
            // need to use one more line
            // marking break_at+1 as start of a new line
            int lead = break_at + 1;    // the first symbol of the new line
            if (text_info->n_lines >= text_info->max_lines) {
                // Raise maximum number of lines
                text_info->max_lines *= 2;
                text_info->lines = realloc(text_info->lines,
                                           sizeof(LineInfo) *
                                           text_info->max_lines);
            }
            if (lead < text_info->length) {
                text_info->glyphs[lead].linebreak = break_type;
                last_space = -1;
                s1 = text_info->glyphs + lead;
                s_offset = d6_to_double(s1->bbox.xMin + s1->pos.x);
                text_info->n_lines++;
            }
        }
    }
#define DIFF(x,y) (((x) < (y)) ? (y - x) : (x - y))
    exit = 0;
    while (!exit && render_priv->state.wrap_style != 1) {
        exit = 1;
        w = s3 = text_info->glyphs;
        s1 = s2 = 0;
        for (i = 0; i <= text_info->length; ++i) {
            cur = text_info->glyphs + i;
            if ((i == text_info->length) || cur->linebreak) {
                s1 = s2;
                s2 = s3;
                s3 = cur;
                if (s1 && (s2->linebreak == 1)) {       // have at least 2 lines, and linebreak is 'soft'
                    double l1, l2, l1_new, l2_new;

                    w = s2;
                    do {
                        --w;
                    } while ((w > s1) && (w->symbol == ' '));
                    while ((w > s1) && (w->symbol != ' ')) {
                        --w;
                    }
                    e1 = w;
                    while ((e1 > s1) && (e1->symbol == ' ')) {
                        --e1;
                    }
                    if (w->symbol == ' ')
                        ++w;

                    l1 = d6_to_double(((s2 - 1)->bbox.xMax + (s2 - 1)->pos.x) -
                        (s1->bbox.xMin + s1->pos.x));
                    l2 = d6_to_double(((s3 - 1)->bbox.xMax + (s3 - 1)->pos.x) -
                        (s2->bbox.xMin + s2->pos.x));
                    l1_new = d6_to_double(
                        (e1->bbox.xMax + e1->pos.x) -
                        (s1->bbox.xMin + s1->pos.x));
                    l2_new = d6_to_double(
                        ((s3 - 1)->bbox.xMax + (s3 - 1)->pos.x) -
                        (w->bbox.xMin + w->pos.x));

                    if (DIFF(l1_new, l2_new) < DIFF(l1, l2)) {
                        w->linebreak = 1;
                        s2->linebreak = 0;
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

    measure_text(render_priv);
    trim_whitespace(render_priv);

    pen_shift_x = 0.;
    pen_shift_y = 0.;
    cur_line = 1;
    run_offset = 0;

    i = 0;
    cur = text_info->glyphs + i;
    while (i < text_info->length && cur->skip)
        cur = text_info->glyphs + ++i;
    pen_shift_x = d6_to_double(-cur->pos.x);

    for (i = 0; i < text_info->length; ++i) {
        cur = text_info->glyphs + i;
        if (cur->linebreak) {
            while (i < text_info->length && cur->skip && cur->symbol != '\n')
                cur = text_info->glyphs + ++i;
            double height =
                text_info->lines[cur_line - 1].desc +
                text_info->lines[cur_line].asc;
            text_info->lines[cur_line - 1].len = i -
                text_info->lines[cur_line - 1].offset;
            text_info->lines[cur_line].offset = i;
            cur_line++;
            run_offset++;
            pen_shift_x = d6_to_double(-cur->pos.x);
            pen_shift_y += height + render_priv->settings.line_spacing;
        }
        cur->pos.x += double_to_d6(pen_shift_x);
        cur->pos.y += double_to_d6(pen_shift_y);
    }
    text_info->lines[cur_line - 1].len =
        text_info->length - text_info->lines[cur_line - 1].offset;

#if 0
    // print line info
    for (i = 0; i < text_info->n_lines; i++) {
        printf("line %d offset %d length %d\n", i, text_info->lines[i].offset,
                text_info->lines[i].len);
    }
#endif
}

/**
 * \brief Calculate base point for positioning and rotation
 * \param bbox text bbox
 * \param alignment alignment
 * \param bx, by out: base point coordinates
 */
static void get_base_point(DBBox *bbox, int alignment, double *bx, double *by)
{
    const int halign = alignment & 3;
    const int valign = alignment & 12;
    if (bx)
        switch (halign) {
        case HALIGN_LEFT:
            *bx = bbox->xMin;
            break;
        case HALIGN_CENTER:
            *bx = (bbox->xMax + bbox->xMin) / 2.0;
            break;
        case HALIGN_RIGHT:
            *bx = bbox->xMax;
            break;
        }
    if (by)
        switch (valign) {
        case VALIGN_TOP:
            *by = bbox->yMin;
            break;
        case VALIGN_CENTER:
            *by = (bbox->yMax + bbox->yMin) / 2.0;
            break;
        case VALIGN_SUB:
            *by = bbox->yMax;
            break;
        }
}

/**
 * Prepare bitmap hash key of a glyph
 */
static void
fill_bitmap_hash(ASS_Renderer *priv, GlyphInfo *info,
                 OutlineBitmapHashKey *hash_key)
{
    hash_key->frx = rot_key(info->frx);
    hash_key->fry = rot_key(info->fry);
    hash_key->frz = rot_key(info->frz);
    hash_key->fax = double_to_d16(info->fax);
    hash_key->fay = double_to_d16(info->fay);
    hash_key->be = info->be;
    hash_key->blur = info->blur;
    hash_key->shadow_offset.x =
        double_to_d6(info->shadow_x * priv->border_scale) & SUBPIXEL_MASK;
    hash_key->shadow_offset.y =
        double_to_d6(info->shadow_y * priv->border_scale) & SUBPIXEL_MASK;
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
    glyph->scale_x = glyph->scale_x * glyph->font_size / ft_size;
    glyph->scale_y = glyph->scale_y * glyph->font_size / ft_size;
    glyph->font_size = ft_size;
}

 /**
  * \brief Checks whether a glyph should start a new bitmap run
  * \param info Pointer to new GlyphInfo to check
  * \param current_info Pointer to CombinedBitmapInfo for current run (may be NULL)
  * \return 1 if a new run should be started
  */
static int is_new_bm_run(GlyphInfo *info, GlyphInfo *last)
{
    if (!last || info->effect || info->drawing || last->drawing) {
        return 1;
    }
    // FIXME: Don't break on glyph substitutions
    if (strcmp(last->font->desc.family, info->font->desc.family) ||
        last->font->desc.vertical != info->font->desc.vertical ||
        last->face_index != info->face_index ||
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
        last->flags != info->flags){
        return 1;
    }
    return 0;
}

static void make_shadow_bitmap(CombinedBitmapInfo *info, ASS_Renderer *render_priv)
{
    // VSFilter compatibility: invisible fill and no border?
    // In this case no shadow is supposed to be rendered.
    if (!info->has_border && (info->c[0] & 0xFF) == 0xFF) {
        return;
    }

    // Create shadow and fix outline as needed
    if (info->bm_o && info->border_style != 3) {
        info->bm_s = copy_bitmap(info->bm_o);
        fix_outline(info->bm, info->bm_o);
    } else if (info->bm_o && (info->border_x || info->border_y)) {
        info->bm_s = copy_bitmap(info->bm_o);
    } else if (info->bm_o) {
        info->bm_s = info->bm_o;
        info->bm_o = 0;
    } else
        info->bm_s = copy_bitmap(info->bm);

    if (!info->bm_s)
        return;

    // Works right even for negative offsets
    // '>>' rounds toward negative infinity, '&' returns correct remainder
    int offset_x = double_to_d6(info->shadow_x * render_priv->border_scale);
    int offset_y = double_to_d6(info->shadow_y * render_priv->border_scale);
    info->bm_s->left += offset_x >> 6;
    info->bm_s->top  += offset_y >> 6;
    shift_bitmap(info->bm_s, offset_x & SUBPIXEL_MASK, offset_y & SUBPIXEL_MASK);
}

// Parse event text.
// Fill render_priv->text_info.
static int parse_events(ASS_Renderer *render_priv, ASS_Event *event)
{
    TextInfo *text_info = &render_priv->text_info;
    ASS_Drawing *drawing;
    unsigned code;
    char *p, *q;
    int i;

    drawing = render_priv->state.drawing;
    p = event->Text;

    // Event parsing.
    while (1) {
        // get next char, executing style override
        // this affects render_context
        code = 0;
        while (*p) {
            if ((*p == '{') && (q = strchr(p, '}'))) {
                while (p < q)
                    p = parse_tag(render_priv, p, q, 1.);
                assert(*p == '}');
                p++;
            } else if (render_priv->state.drawing_scale) {
                q = p;
                if (*p == '{')
                    q++;
                while ((*q != '{') && (*q != 0))
                    q++;
                ass_drawing_set_text(drawing, p, q - p);
                code = 0xfffc; // object replacement character
                p = q;
                break;
            } else {
                code = get_next_char(render_priv, &p);
                break;
            }
        }

        if (code == 0)
            break;

        if (text_info->length >= text_info->max_glyphs) {
            // Raise maximum number of glyphs
            text_info->max_glyphs *= 2;
            text_info->glyphs =
                realloc(text_info->glyphs,
                        sizeof(GlyphInfo) * text_info->max_glyphs);
        }

        GlyphInfo *info = &text_info->glyphs[text_info->length];

        // Clear current GlyphInfo
        memset(info, 0, sizeof(GlyphInfo));

        // Parse drawing
        if (drawing->text) {
            drawing->scale_x = render_priv->state.scale_x *
                                     render_priv->font_scale;
            drawing->scale_y = render_priv->state.scale_y *
                                     render_priv->font_scale;
            drawing->scale = render_priv->state.drawing_scale;
            drawing->pbo = render_priv->state.pbo;
            info->drawing = drawing;
        }

        // face could have been changed in get_next_char
        if (!render_priv->state.font) {
            free_render_context(render_priv);
            return 1;
        }

        // Fill glyph information
        info->symbol = code;
        info->font = render_priv->state.font;
        for (i = 0; i < 4; ++i) {
            uint32_t clr = render_priv->state.c[i];
            change_alpha(&clr,
                         mult_alpha(_a(clr), render_priv->state.fade), 1.);
            info->c[i] = clr;
        }

        info->effect_type = render_priv->state.effect_type;
        info->effect_timing = render_priv->state.effect_timing;
        info->effect_skip_timing = render_priv->state.effect_skip_timing;
        info->font_size =
            render_priv->state.font_size * render_priv->font_scale;
        info->be = render_priv->state.be;
        info->blur = render_priv->state.blur;
        info->shadow_x = render_priv->state.shadow_x;
        info->shadow_y = render_priv->state.shadow_y;
        info->scale_x = info->orig_scale_x = render_priv->state.scale_x;
        info->scale_y = info->orig_scale_y = render_priv->state.scale_y;
        info->border_style = render_priv->state.border_style;
        info->border_x= render_priv->state.border_x;
        info->border_y = render_priv->state.border_y;
        info->hspacing = render_priv->state.hspacing;
        info->bold = render_priv->state.bold;
        info->italic = render_priv->state.italic;
        info->flags = render_priv->state.flags;
        info->frx = render_priv->state.frx;
        info->fry = render_priv->state.fry;
        info->frz = render_priv->state.frz;
        info->fax = render_priv->state.fax;
        info->fay = render_priv->state.fay;

        if (info->drawing) {
            drawing = render_priv->state.drawing =
                ass_drawing_new(render_priv->library, render_priv->ftlibrary);
        } else {
            fix_glyph_scaling(render_priv, info);
        }

        text_info->length++;

        render_priv->state.effect_type = EF_NONE;
        render_priv->state.effect_timing = 0;
        render_priv->state.effect_skip_timing = 0;
    }

    return 0;
}

// Process render_priv->text_info and load glyph outlines.
static void retrieve_glyphs(ASS_Renderer *render_priv)
{
    GlyphInfo *glyphs = render_priv->text_info.glyphs;
    int i;

    for (i = 0; i < render_priv->text_info.length; i++) {
        GlyphInfo *info = glyphs + i;
        while (info) {
            get_outline_glyph(render_priv, info);
            info = info->next;
        }
        info = glyphs + i;

        // Add additional space after italic to non-italic style changes
        if (i && glyphs[i - 1].italic && !info->italic) {
            int back = i - 1;
            GlyphInfo *og = &glyphs[back];
            while (back && og->bbox.xMax - og->bbox.xMin == 0
                    && og->italic)
                og = &glyphs[--back];
            if (og->bbox.xMax > og->cluster_advance.x)
                og->cluster_advance.x = og->bbox.xMax;
        }

        // add horizontal letter spacing
        info->cluster_advance.x += double_to_d6(info->hspacing *
                render_priv->font_scale * info->orig_scale_x);

        // add displacement for vertical shearing
        info->cluster_advance.y += (info->fay / info->scale_x * info->scale_y) * info->cluster_advance.x;
    }
}

// Preliminary layout (for line wrapping)
static void preliminary_layout(ASS_Renderer *render_priv)
{
    FT_Vector pen;
    int i;

    pen.x = 0;
    pen.y = 0;
    for (i = 0; i < render_priv->text_info.length; i++) {
        GlyphInfo *info = render_priv->text_info.glyphs + i;
        FT_Vector cluster_pen = pen;
        while (info) {
            info->pos.x = cluster_pen.x;
            info->pos.y = cluster_pen.y;

            cluster_pen.x += info->advance.x;
            cluster_pen.y += info->advance.y;

            // fill bitmap hash
            info->hash_key.type = BITMAP_OUTLINE;
            fill_bitmap_hash(render_priv, info, &info->hash_key.u.outline);

            info = info->next;
        }
        info = render_priv->text_info.glyphs + i;
        pen.x += info->cluster_advance.x;
        pen.y += info->cluster_advance.y;
    }
}

// Reorder text into visual order
static void reorder_text(ASS_Renderer *render_priv)
{
    TextInfo *text_info = &render_priv->text_info;
    FT_Vector pen;
    int i;

    FriBidiStrIndex *cmap = ass_shaper_reorder(render_priv->shaper, text_info);
    if (!cmap) {
        ass_msg(render_priv->library, MSGL_ERR, "Failed to reorder text");
        ass_shaper_cleanup(render_priv->shaper, text_info);
        free_render_context(render_priv);
        return;
    }

    // Reposition according to the map
    pen.x = 0;
    pen.y = 0;
    int lineno = 1;
    double last_pen_x = 0;
    double last_fay = 0;
    for (i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + cmap[i];
        if (text_info->glyphs[i].linebreak) {
            pen.y -= (last_fay / info->scale_x * info->scale_y) * (pen.x - last_pen_x);
            last_pen_x = pen.x = 0;
            pen.y += double_to_d6(text_info->lines[lineno-1].desc);
            pen.y += double_to_d6(text_info->lines[lineno].asc);
            pen.y += double_to_d6(render_priv->settings.line_spacing);
            lineno++;
        }
        else if (last_fay != info->fay) {
            pen.y -= (last_fay / info->scale_x * info->scale_y) * (pen.x - last_pen_x);
            last_pen_x = pen.x;
        }
        last_fay = info->fay;
        if (info->skip) continue;
        FT_Vector cluster_pen = pen;
        while (info) {
            info->pos.x = info->offset.x + cluster_pen.x;
            info->pos.y = info->offset.y + cluster_pen.y;
            cluster_pen.x += info->advance.x;
            cluster_pen.y += info->advance.y;
            info = info->next;
        }
        info = text_info->glyphs + cmap[i];
        pen.x += info->cluster_advance.x;
        pen.y += info->cluster_advance.y;
    }
}

static void align_lines(ASS_Renderer *render_priv, double max_text_width)
{
    TextInfo *text_info = &render_priv->text_info;
    GlyphInfo *glyphs = text_info->glyphs;
    int i, j;
    double width = 0;
    int last_break = -1;
    int halign = render_priv->state.alignment & 3;

    if (render_priv->state.evt_type == EVENT_HSCROLL)
        return;

    for (i = 0; i <= text_info->length; ++i) {   // (text_info->length + 1) is the end of the last line
        if ((i == text_info->length) || glyphs[i].linebreak) {
            double shift = 0;
            if (halign == HALIGN_LEFT) {    // left aligned, no action
                shift = 0;
            } else if (halign == HALIGN_RIGHT) {    // right aligned
                shift = max_text_width - width;
            } else if (halign == HALIGN_CENTER) {   // centered
                shift = (max_text_width - width) / 2.0;
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

static void calculate_rotation_params(ASS_Renderer *render_priv, DBBox *bbox,
                                      double device_x, double device_y)
{
    TextInfo *text_info = &render_priv->text_info;
    DVector center;
    int i;

    if (render_priv->state.have_origin) {
        center.x = x2scr(render_priv, render_priv->state.org_x);
        center.y = y2scr(render_priv, render_priv->state.org_y);
    } else {
        double bx = 0., by = 0.;
        get_base_point(bbox, render_priv->state.alignment, &bx, &by);
        center.x = device_x + bx;
        center.y = device_y + by;
    }

    for (i = 0; i < text_info->length; ++i) {
        GlyphInfo *info = text_info->glyphs + i;
        while (info) {
            OutlineBitmapHashKey *key = &info->hash_key.u.outline;

            if (key->frx || key->fry || key->frz || key->fax || key->fay) {
                key->shift_x = info->pos.x + double_to_d6(device_x - center.x);
                key->shift_y = -(info->pos.y + double_to_d6(device_y - center.y));
            } else {
                key->shift_x = 0;
                key->shift_y = 0;
            }
            info = info->next;
        }
    }
}


// Convert glyphs to bitmaps, combine them, apply blur, generate shadows.
static void render_and_combine_glyphs(ASS_Renderer *render_priv,
                                      double device_x, double device_y)
{
    int i;
    TextInfo *text_info = &render_priv->text_info;
    int left = render_priv->settings.left_margin;
    device_x = (device_x - left) * render_priv->font_scale_x + left;
    unsigned nb_bitmaps = 0;
    char linebreak = 0;
    CombinedBitmapInfo *combined_info = text_info->combined_bitmaps;
    CombinedBitmapInfo *current_info = NULL;
    GlyphInfo *last_info = NULL;
    for (i = 0; i < text_info->length; ++i) {
        GlyphInfo *info = text_info->glyphs + i;
        if (info->linebreak) linebreak = 1;
        if (info->skip) continue;
        while (info) {
            OutlineBitmapHashKey *key = &info->hash_key.u.outline;
            info->pos.x *= render_priv->font_scale_x;
            key->advance.x =
                double_to_d6(device_x - (int) device_x +
                        d6_to_double(info->pos.x & SUBPIXEL_MASK)) & ~SUBPIXEL_ACCURACY;
            key->advance.y =
                double_to_d6(device_y - (int) device_y +
                        d6_to_double(info->pos.y & SUBPIXEL_MASK)) & ~SUBPIXEL_ACCURACY;
            get_bitmap_glyph(render_priv, info);

            int bm_x = info->pos.x >> 6,
                bm_y = info->pos.y >> 6,
                bm_o_x = bm_x, bm_o_y = bm_y, min_bm_x = bm_x, min_bm_y = bm_y;

            if(info->bm){
                bm_x += info->bm->left;
                bm_y += info->bm->top;
                min_bm_x = bm_x;
                min_bm_y = bm_y;
            }

            if(info->bm_o){
                bm_o_x += info->bm_o->left;
                bm_o_y += info->bm_o->top;
                min_bm_x = FFMIN(min_bm_x, bm_o_x);
                min_bm_y = FFMIN(min_bm_y, bm_o_y);
            }

            if(linebreak || is_new_bm_run(info, last_info)){
                linebreak = 0;
                ++nb_bitmaps;
                if (nb_bitmaps >= text_info->max_bitmaps) {
                    // Raise maximum number of bitmaps
                    text_info->max_bitmaps *= 2;
                    text_info->combined_bitmaps = combined_info =
                        realloc(combined_info,
                                sizeof(CombinedBitmapInfo) * text_info->max_bitmaps);
                }

                current_info = &combined_info[nb_bitmaps - 1];

                current_info->pos.x = min_bm_x;
                current_info->pos.y = min_bm_y;

                current_info->first_pos_x = info->bbox.xMax >> 6;

                memcpy(&current_info->c, &info->c, sizeof(info->c));
                current_info->effect_type = info->effect_type;
                current_info->effect_timing = info->effect_timing;
                current_info->be = info->be;
                current_info->blur = info->blur;
                current_info->shadow_x = info->shadow_x;
                current_info->shadow_y = info->shadow_y;
                current_info->frx = info->frx;
                current_info->fry = info->fry;
                current_info->frz = info->frz;
                current_info->fax = info->fax;
                current_info->fay = info->fay;
                current_info->scale_x = info->scale_x;
                current_info->scale_y = info->scale_y;
                current_info->border_style = info->border_style;
                current_info->border_x = info->border_x;
                current_info->border_y = info->border_y;
                current_info->hspacing = info->hspacing;
                current_info->italic = info->italic;
                current_info->bold = info->bold;
                current_info->flags = info->flags;

                current_info->shift_x = key->shift_x;
                current_info->shift_y = key->shift_y;
                current_info->advance = key->advance;

                current_info->has_border = !!info->border;

                current_info->has_outline = 0;
                current_info->cached = 0;
                current_info->is_drawing = 0;

                current_info->bm = current_info->bm_o = current_info->bm_s = NULL;

                current_info->max_str_length =
                    MAX_STR_LENGTH_INITIAL * sizeof(info->glyph_index);
                current_info->str_length = 0;
                current_info->str = malloc(current_info->max_str_length);
                current_info->chars = 0;

                current_info->w = current_info->h = current_info->o_w = current_info->o_h = 0;

            }

            if(info->drawing){
                free(current_info->str);
                current_info->str = strdup(info->drawing->text);
                current_info->str_length = strlen(info->drawing->text);
                current_info->is_drawing = 1;
                ass_drawing_free(info->drawing);
            }else{
                int length = current_info->str_length;
                current_info->chars++;
                current_info->str_length += sizeof(info->glyph_index);
                if(current_info->str_length > current_info->max_str_length){
                    current_info->max_str_length *= 2;
                    current_info->str = realloc(current_info->str,
                                                current_info->max_str_length);
                }
                *(int *)(current_info->str + length) = info->glyph_index;
            }

            current_info->has_outline = current_info->has_outline || !!info->bm_o;

            if(min_bm_y < current_info->pos.y){
                current_info->h += current_info->pos.y - min_bm_y;
                current_info->o_h += current_info->pos.y - min_bm_y;
                current_info->pos.y = min_bm_y;
            }

            if(min_bm_x < current_info->pos.x){
                current_info->w += current_info->pos.x - min_bm_x;
                current_info->o_w += current_info->pos.x - min_bm_x;
                current_info->pos.x = min_bm_x;
            }

            if(info->bm){
                current_info->w =
                    FFMAX(current_info->w, info->bm->w + bm_x - current_info->pos.x);
                current_info->h =
                    FFMAX(current_info->h, info->bm->h + bm_y - current_info->pos.y);
            }

            if(info->bm_o){
                current_info->o_w =
                    FFMAX(current_info->o_w, info->bm_o->w + bm_o_x - current_info->pos.x);
                current_info->o_h =
                    FFMAX(current_info->o_h, info->bm_o->h + bm_o_y - current_info->pos.y);
            }

            info->bm_run_id = nb_bitmaps - 1;

            last_info = info;
            info = info->next;
        }
    }

    CompositeHashKey hk;
    CompositeHashValue *hv;
    for (i = 0; i < nb_bitmaps; ++i) {
        CombinedBitmapInfo *info = &combined_info[i];

        fill_composite_hash(&hk, info);

        hv = ass_cache_get(render_priv->cache.composite_cache, &hk);

        if(hv){
            info->bm = hv->bm;
            info->bm_o = hv->bm_o;
            info->bm_s = hv->bm_s;
            info->cached = 1;
            free(info->str);
        }else{
            if(info->chars != 1 && !info->is_drawing){
                info->bm = alloc_bitmap(info->w, info->h);
                if(info->has_outline){
                    info->bm_o = alloc_bitmap(info->o_w, info->o_h);
                }
            }
        }
    }

    for (i = 0; i < text_info->length; ++i) {
        GlyphInfo *info = text_info->glyphs + i;
        if (info->skip) continue;
        while (info) {
            current_info = &combined_info[info->bm_run_id];
            if(!current_info->cached && !is_skip_symbol(info->symbol)){
                if(current_info->chars == 1 || current_info->is_drawing){
                    int offset_x = (info->pos.x >> 6) - current_info->pos.x;
                    int offset_y = (info->pos.y >> 6) - current_info->pos.y;
                    if(info->bm){
                        current_info->bm = copy_bitmap(info->bm);
                        if (!current_info->bm)
                            goto cleanup;
                        current_info->bm->left += offset_x;
                        current_info->bm->top += offset_y;
                    }
                    if(info->bm_o){
                        current_info->bm_o = copy_bitmap(info->bm_o);
                        if (!current_info->bm_o)
                            goto cleanup;
                        current_info->bm_o->left += offset_x;
                        current_info->bm_o->top += offset_y;
                    }
                }else{
                    unsigned offset_x, offset_y;
                    if(info->bm && info->bm->w && info->bm->h){
                        offset_x = (info->pos.x >> 6) - current_info->pos.x + info->bm->left;
                        offset_y = (info->pos.y >> 6) - current_info->pos.y + info->bm->top;
                        render_priv->add_bitmaps_func(
                            &current_info->bm->buffer[offset_y * current_info->bm->stride + offset_x],
                            current_info->bm->stride,
                            info->bm->buffer,
                            info->bm->stride,
                            info->bm->h,
                            info->bm->w
                        );
                    }
                    if(info->bm_o && info->bm_o->w && info->bm_o->h){
                        offset_x = (info->pos.x >> 6) - current_info->pos.x + info->bm_o->left;
                        offset_y = (info->pos.y >> 6) - current_info->pos.y + info->bm_o->top;
                        render_priv->add_bitmaps_func(
                            &current_info->bm_o->buffer[offset_y * current_info->bm_o->stride + offset_x],
                            current_info->bm_o->stride,
                            info->bm_o->buffer,
                            info->bm_o->stride,
                            info->bm_o->h,
                            info->bm_o->w
                        );
                    }
                }
            }
            cleanup:
            info = info->next;
        }
    }

    for (i = 0; i < nb_bitmaps; ++i) {
        if(!combined_info[i].cached){
            CompositeHashValue chv;
            CombinedBitmapInfo *info = &combined_info[i];
            if(info->bm || info->bm_o){
                ass_synth_blur(render_priv->synth_priv, info->border_style == 3,
                               info->be, info->blur * render_priv->blur_scale * 2,
                               info->bm, info->bm_o);
                make_shadow_bitmap(info, render_priv);
            }

            fill_composite_hash(&hk, info);

            chv.bm = info->bm;
            chv.bm_o = info->bm_o;
            chv.bm_s = info->bm_s;

            ass_cache_put(render_priv->cache.composite_cache, &hk, &chv);
        }
    }

    text_info->n_bitmaps = nb_bitmaps;
}

static void add_background(ASS_Renderer *render_priv, EventImages *event_images)
{
    void *nbuffer = ass_aligned_alloc(1, event_images->width * event_images->height);
    if (!free_list_add(render_priv, nbuffer)) {
        ass_aligned_free(nbuffer);
    } else {
        memset(nbuffer, 0xFF, event_images->width * event_images->height);
        ASS_Image *img = my_draw_bitmap(nbuffer, event_images->width,
                                        event_images->height,
                                        event_images->width,
                                        event_images->left,
                                        event_images->top,
                                        render_priv->state.c[3]);
        if (img) {
            img->next = event_images->imgs;
            event_images->imgs = img;
        }
    }
}

/**
 * \brief Main ass rendering function, glues everything together
 * \param event event to render
 * \param event_images struct containing resulting images, will also be initialized
 * Process event, appending resulting ASS_Image's to images_root.
 */
static int
ass_render_event(ASS_Renderer *render_priv, ASS_Event *event,
                 EventImages *event_images)
{
    DBBox bbox;
    int MarginL, MarginR, MarginV;
    int valign;
    double device_x = 0;
    double device_y = 0;
    TextInfo *text_info = &render_priv->text_info;

    if (event->Style >= render_priv->track->n_styles) {
        ass_msg(render_priv->library, MSGL_WARN, "No style found");
        return 1;
    }
    if (!event->Text) {
        ass_msg(render_priv->library, MSGL_WARN, "Empty event");
        return 1;
    }

    init_render_context(render_priv, event);
    text_info->length = 0;

    if (parse_events(render_priv, event))
        return 1;

    if (text_info->length == 0) {
        // no valid symbols in the event; this can be smth like {comment}
        free_render_context(render_priv);
        return 1;
    }

    // Find shape runs and shape text
    ass_shaper_set_base_direction(render_priv->shaper,
            resolve_base_direction(render_priv->state.font_encoding));
    ass_shaper_find_runs(render_priv->shaper, render_priv, text_info->glyphs,
            text_info->length);
    if (ass_shaper_shape(render_priv->shaper, text_info) < 0) {
        ass_msg(render_priv->library, MSGL_ERR, "Failed to shape text");
        free_render_context(render_priv);
        return 1;
    }

    retrieve_glyphs(render_priv);

    preliminary_layout(render_priv);

    // depends on glyph x coordinates being monotonous, so it should be done before line wrap
    process_karaoke_effects(render_priv);

    valign = render_priv->state.alignment & 12;

    MarginL =
        (event->MarginL) ? event->MarginL : render_priv->state.style->MarginL;
    MarginR =
        (event->MarginR) ? event->MarginR : render_priv->state.style->MarginR;
    MarginV =
        (event->MarginV) ? event->MarginV : render_priv->state.style->MarginV;

    // calculate max length of a line
    double max_text_width =
        x2scr(render_priv, render_priv->track->PlayResX - MarginR) -
        x2scr(render_priv, MarginL);

    // wrap lines
    if (render_priv->state.evt_type != EVENT_HSCROLL) {
        // rearrange text in several lines
        wrap_lines_smart(render_priv, max_text_width);
    } else {
        // no breaking or wrapping, everything in a single line
        text_info->lines[0].offset = 0;
        text_info->lines[0].len = text_info->length;
        text_info->n_lines = 1;
        measure_text(render_priv);
    }

    reorder_text(render_priv);

    align_lines(render_priv, max_text_width);

    // determing text bounding box
    compute_string_bbox(text_info, &bbox);

    // determine device coordinates for text

    // x coordinate for everything except positioned events
    if (render_priv->state.evt_type == EVENT_NORMAL ||
        render_priv->state.evt_type == EVENT_VSCROLL) {
        device_x = x2scr(render_priv, MarginL);
    } else if (render_priv->state.evt_type == EVENT_HSCROLL) {
        if (render_priv->state.scroll_direction == SCROLL_RL)
            device_x =
                x2scr(render_priv,
                      render_priv->track->PlayResX -
                      render_priv->state.scroll_shift);
        else if (render_priv->state.scroll_direction == SCROLL_LR)
            device_x =
                x2scr(render_priv,
                      render_priv->state.scroll_shift) - (bbox.xMax -
                                                          bbox.xMin);
    }

    // y coordinate for everything except positioned events
    if (render_priv->state.evt_type == EVENT_NORMAL ||
        render_priv->state.evt_type == EVENT_HSCROLL) {
        if (valign == VALIGN_TOP) {     // toptitle
            device_y =
                y2scr_top(render_priv,
                          MarginV) + text_info->lines[0].asc;
        } else if (valign == VALIGN_CENTER) {   // midtitle
            double scr_y =
                y2scr(render_priv, render_priv->track->PlayResY / 2.0);
            device_y = scr_y - (bbox.yMax + bbox.yMin) / 2.0;
        } else {                // subtitle
            double scr_top, scr_bottom, scr_y0;
            if (valign != VALIGN_SUB)
                ass_msg(render_priv->library, MSGL_V,
                       "Invalid valign, assuming 0 (subtitle)");
            scr_bottom =
                y2scr_sub(render_priv,
                          render_priv->track->PlayResY - MarginV);
            scr_top = y2scr_top(render_priv, 0); //xxx not always 0?
            device_y = scr_bottom + (scr_top - scr_bottom) *
                       render_priv->settings.line_position / 100.0;
            device_y -= text_info->height;
            device_y += text_info->lines[0].asc;
            // clip to top to avoid confusion if line_position is very high,
            // turning the subtitle into a toptitle
            // also, don't change behavior if line_position is not used
            scr_y0 = scr_top + text_info->lines[0].asc;
            if (device_y < scr_y0 && render_priv->settings.line_position > 0) {
                device_y = scr_y0;
            }
        }
    } else if (render_priv->state.evt_type == EVENT_VSCROLL) {
        if (render_priv->state.scroll_direction == SCROLL_TB)
            device_y =
                y2scr(render_priv,
                      render_priv->state.clip_y0 +
                      render_priv->state.scroll_shift) - (bbox.yMax -
                                                          bbox.yMin);
        else if (render_priv->state.scroll_direction == SCROLL_BT)
            device_y =
                y2scr(render_priv,
                      render_priv->state.clip_y1 -
                      render_priv->state.scroll_shift);
    }

    // positioned events are totally different
    if (render_priv->state.evt_type == EVENT_POSITIONED) {
        double base_x = 0;
        double base_y = 0;
        get_base_point(&bbox, render_priv->state.alignment, &base_x, &base_y);
        device_x =
            x2scr_pos(render_priv, render_priv->state.pos_x) - base_x;
        device_y =
            y2scr_pos(render_priv, render_priv->state.pos_y) - base_y;
    }

    // fix clip coordinates (they depend on alignment)
    if (render_priv->state.evt_type == EVENT_NORMAL ||
        render_priv->state.evt_type == EVENT_HSCROLL ||
        render_priv->state.evt_type == EVENT_VSCROLL) {
        render_priv->state.clip_x0 =
            x2scr_scaled(render_priv, render_priv->state.clip_x0);
        render_priv->state.clip_x1 =
            x2scr_scaled(render_priv, render_priv->state.clip_x1);
        if (valign == VALIGN_TOP) {
            render_priv->state.clip_y0 =
                y2scr_top(render_priv, render_priv->state.clip_y0);
            render_priv->state.clip_y1 =
                y2scr_top(render_priv, render_priv->state.clip_y1);
        } else if (valign == VALIGN_CENTER) {
            render_priv->state.clip_y0 =
                y2scr(render_priv, render_priv->state.clip_y0);
            render_priv->state.clip_y1 =
                y2scr(render_priv, render_priv->state.clip_y1);
        } else if (valign == VALIGN_SUB) {
            render_priv->state.clip_y0 =
                y2scr_sub(render_priv, render_priv->state.clip_y0);
            render_priv->state.clip_y1 =
                y2scr_sub(render_priv, render_priv->state.clip_y1);
        }
    } else if (render_priv->state.evt_type == EVENT_POSITIONED) {
        render_priv->state.clip_x0 =
            x2scr_pos_scaled(render_priv, render_priv->state.clip_x0);
        render_priv->state.clip_x1 =
            x2scr_pos_scaled(render_priv, render_priv->state.clip_x1);
        render_priv->state.clip_y0 =
            y2scr_pos(render_priv, render_priv->state.clip_y0);
        render_priv->state.clip_y1 =
            y2scr_pos(render_priv, render_priv->state.clip_y1);
    }

    calculate_rotation_params(render_priv, &bbox, device_x, device_y);

    render_and_combine_glyphs(render_priv, device_x, device_y);

    memset(event_images, 0, sizeof(*event_images));
    event_images->top = device_y - text_info->lines[0].asc;
    event_images->height = text_info->height;
    event_images->left =
        (device_x + bbox.xMin * render_priv->font_scale_x) + 0.5;
    event_images->width =
        (bbox.xMax - bbox.xMin) * render_priv->font_scale_x + 0.5;
    event_images->detect_collisions = render_priv->state.detect_collisions;
    event_images->shift_direction = (valign == VALIGN_TOP) ? 1 : -1;
    event_images->event = event;
    event_images->imgs = render_text(render_priv, (int) device_x, (int) device_y);

    if (render_priv->state.border_style == 4)
        add_background(render_priv, event_images);

    ass_shaper_cleanup(render_priv->shaper, text_info);
    free_render_context(render_priv);

    return 0;
}

/**
 * \brief deallocate image list
 * \param img list pointer
 */
void ass_free_images(ASS_Image *img)
{
    while (img) {
        ASS_Image *next = img->next;
        free(img);
        img = next;
    }
}

/**
 * \brief Check cache limits and reset cache if they are exceeded
 */
static void check_cache_limits(ASS_Renderer *priv, CacheStore *cache)
{
    if (ass_cache_empty(cache->bitmap_cache, cache->bitmap_max_size)) {
        ass_free_images(priv->prev_images_root);
        priv->prev_images_root = 0;
        priv->cache_cleared = 1;
    }
    if (ass_cache_empty(cache->outline_cache, cache->glyph_max)) {
        ass_cache_empty(cache->bitmap_cache, 0);
        ass_free_images(priv->prev_images_root);
        priv->prev_images_root = 0;
        priv->cache_cleared = 1;
    }
    if (ass_cache_empty(cache->composite_cache, cache->composite_max_size)) {
        ass_free_images(priv->prev_images_root);
        priv->prev_images_root = 0;
        priv->cache_cleared = 1;
    }
}

/**
 * \brief Start a new frame
 */
static int
ass_start_frame(ASS_Renderer *render_priv, ASS_Track *track,
                long long now)
{
    ASS_Settings *settings_priv = &render_priv->settings;

    if (!render_priv->settings.frame_width
        && !render_priv->settings.frame_height)
        return 1;               // library not initialized

    if (!render_priv->fontconfig_priv)
        return 1;

    free_list_clear(render_priv);

    if (track->n_events == 0)
        return 1;               // nothing to do

    render_priv->track = track;
    render_priv->time = now;

    ass_lazy_track_init(render_priv->library, render_priv->track);

    ass_shaper_set_kerning(render_priv->shaper, track->Kerning);
    ass_shaper_set_language(render_priv->shaper, track->Language);
    ass_shaper_set_level(render_priv->shaper, render_priv->settings.shaper);

    // PAR correction
    double par = render_priv->settings.par;
    if (par == 0.) {
        if (settings_priv->frame_width && settings_priv->frame_height &&
            settings_priv->storage_width && settings_priv->storage_height) {
            double dar = ((double) settings_priv->frame_width) /
                         settings_priv->frame_height;
            double sar = ((double) settings_priv->storage_width) /
                         settings_priv->storage_height;
            par = sar / dar;
        } else
            par = 1.0;
    }
    render_priv->font_scale_x = par;

    render_priv->prev_images_root = render_priv->images_root;
    render_priv->images_root = 0;

    check_cache_limits(render_priv, &render_priv->cache);

    return 0;
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

static int overlap(Segment *s1, Segment *s2)
{
    if (s1->a >= s2->b || s2->a >= s1->b ||
        s1->ha >= s2->hb || s2->ha >= s1->hb)
        return 0;
    return 1;
}

static int cmp_segment(const void *p1, const void *p2)
{
    return ((Segment *) p1)->a - ((Segment *) p2)->a;
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
static int fit_segment(Segment *s, Segment *fixed, int *cnt, int dir)
{
    int i;
    int shift = 0;

    if (dir == 1)               // move down
        for (i = 0; i < *cnt; ++i) {
            if (s->b + shift <= fixed[i].a || s->a + shift >= fixed[i].b ||
                s->hb <= fixed[i].ha || s->ha >= fixed[i].hb)
                continue;
            shift = fixed[i].b - s->a;
    } else                      // dir == -1, move up
        for (i = *cnt - 1; i >= 0; --i) {
            if (s->b + shift <= fixed[i].a || s->a + shift >= fixed[i].b ||
                s->hb <= fixed[i].ha || s->ha >= fixed[i].hb)
                continue;
            shift = fixed[i].a - s->b;
        }

    fixed[*cnt].a = s->a + shift;
    fixed[*cnt].b = s->b + shift;
    fixed[*cnt].ha = s->ha;
    fixed[*cnt].hb = s->hb;
    (*cnt)++;
    qsort(fixed, *cnt, sizeof(Segment), cmp_segment);

    return shift;
}

static void
fix_collisions(ASS_Renderer *render_priv, EventImages *imgs, int cnt)
{
    Segment *used = ass_realloc_array(NULL, cnt, sizeof(*used));
    int cnt_used = 0;
    int i, j;

    if (!used)
        return;

    // fill used[] with fixed events
    for (i = 0; i < cnt; ++i) {
        ASS_RenderPriv *priv;
        if (!imgs[i].detect_collisions)
            continue;
        priv = get_render_priv(render_priv, imgs[i].event);
        if (priv && priv->height > 0) { // it's a fixed event
            Segment s;
            s.a = priv->top;
            s.b = priv->top + priv->height;
            s.ha = priv->left;
            s.hb = priv->left + priv->width;
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
                used[cnt_used].a = priv->top;
                used[cnt_used].b = priv->top + priv->height;
                used[cnt_used].ha = priv->left;
                used[cnt_used].hb = priv->left + priv->width;
                cnt_used++;
                shift_event(render_priv, imgs + i, priv->top - imgs[i].top);
            }
        }
    }
    qsort(used, cnt_used, sizeof(Segment), cmp_segment);

    // try to fit other events in free spaces
    for (i = 0; i < cnt; ++i) {
        ASS_RenderPriv *priv;
        if (!imgs[i].detect_collisions)
            continue;
        priv = get_render_priv(render_priv, imgs[i].event);
        if (priv && priv->height == 0) {        // not a fixed event
            int shift;
            Segment s;
            s.a = imgs[i].top;
            s.b = imgs[i].top + imgs[i].height;
            s.ha = imgs[i].left;
            s.hb = imgs[i].left + imgs[i].width;
            shift = fit_segment(&s, used, &cnt_used, imgs[i].shift_direction);
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

    if (priv->cache_cleared || priv->state.has_clips)
        return 2;

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
    int i, cnt, rc;
    EventImages *last;
    ASS_Image **tail;

    // init frame
    rc = ass_start_frame(priv, track, now);
    if (rc != 0) {
        if (detect_change) {
            *detect_change = 2;
        }
        return 0;
    }

    // render events separately
    cnt = 0;
    for (i = 0; i < track->n_events; ++i) {
        ASS_Event *event = track->events + i;
        if ((event->Start <= now)
            && (now < (event->Start + event->Duration))) {
            if (cnt >= priv->eimg_size) {
                priv->eimg_size += 100;
                priv->eimg =
                    realloc(priv->eimg,
                            priv->eimg_size * sizeof(EventImages));
            }
            rc = ass_render_event(priv, event, priv->eimg + cnt);
            if (!rc)
                ++cnt;
        }
    }

    // sort by layer
    qsort(priv->eimg, cnt, sizeof(EventImages), cmp_event_layer);

    // call fix_collisions for each group of events with the same layer
    last = priv->eimg;
    for (i = 1; i < cnt; ++i)
        if (last->event->Layer != priv->eimg[i].event->Layer) {
            fix_collisions(priv, last, priv->eimg + i - last);
            last = priv->eimg + i;
        }
    if (cnt > 0)
        fix_collisions(priv, last, priv->eimg + cnt - last);

    // concat lists
    tail = &priv->images_root;
    for (i = 0; i < cnt; ++i) {
        ASS_Image *cur = priv->eimg[i].imgs;
        while (cur) {
            *tail = cur;
            tail = &cur->next;
            cur = cur->next;
        }
    }

    if (detect_change)
        *detect_change = ass_detect_change(priv);

    // free the previous image list
    ass_free_images(priv->prev_images_root);
    priv->prev_images_root = 0;
    priv->cache_cleared = 0;

    return priv->images_root;
}

/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
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

#ifndef LIBASS_RENDER_H
#define LIBASS_RENDER_H

#include <inttypes.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H
#ifdef CONFIG_HARFBUZZ
#include "hb.h"
#endif

// XXX: fix the inclusion mess so we can avoid doing this here
typedef struct ass_shaper ASS_Shaper;

#include "ass.h"
#include "ass_font.h"
#include "ass_bitmap.h"
#include "ass_cache.h"
#include "ass_utils.h"
#include "ass_fontconfig.h"
#include "ass_library.h"
#include "ass_drawing.h"
#include "ass_bitmap.h"
#include "ass_rasterizer.h"

#define GLYPH_CACHE_MAX 10000
#define BITMAP_CACHE_MAX_SIZE 500 * 1048576
#define COMPOSITE_CACHE_MAX_SIZE 500 * 1048576

#define PARSED_FADE (1<<0)
#define PARSED_A    (1<<1)

typedef struct {
    double xMin;
    double xMax;
    double yMin;
    double yMax;
} DBBox;

typedef struct {
    double x;
    double y;
} DVector;

typedef struct free_list {
    void *object;
    struct free_list *next;
} FreeList;

typedef struct {
    int frame_width;
    int frame_height;
    int storage_width;          // video width before any rescaling
    int storage_height;         // video height before any rescaling
    double font_size_coeff;     // font size multiplier
    double line_spacing;        // additional line spacing (in frame pixels)
    double line_position;       // vertical position for subtitles, 0-100 (0 = no change)
    int top_margin;             // height of top margin. Everything except toptitles is shifted down by top_margin.
    int bottom_margin;          // height of bottom margin. (frame_height - top_margin - bottom_margin) is original video height.
    int left_margin;
    int right_margin;
    int use_margins;            // 0 - place all subtitles inside original frame
    // 1 - use margins for placing toptitles and subtitles
    double par;                 // user defined pixel aspect ratio (0 = unset)
    ASS_Hinting hinting;
    ASS_ShapingLevel shaper;

    char *default_font;
    char *default_family;
} ASS_Settings;

// a rendered event
typedef struct {
    ASS_Image *imgs;
    int top, height, left, width;
    int detect_collisions;
    int shift_direction;
    ASS_Event *event;
} EventImages;

typedef enum {
    EF_NONE = 0,
    EF_KARAOKE,
    EF_KARAOKE_KF,
    EF_KARAOKE_KO
} Effect;

// describes a combined bitmap
typedef struct {
    Bitmap *bm;                 // glyphs bitmap
    unsigned w;
    unsigned h;
    Bitmap *bm_o;               // outline bitmap
    unsigned o_w;
    unsigned o_h;
    Bitmap *bm_s;               // shadow bitmap
    FT_Vector pos;
    uint32_t c[4];              // colors
    FT_Vector advance;          // 26.6
    Effect effect_type;
    int effect_timing;          // time duration of current karaoke word
    // after process_karaoke_effects: distance in pixels from the glyph origin.
    // part of the glyph to the left of it is displayed in a different color.
    int be;                     // blur edges
    double blur;                // gaussian blur
    double shadow_x;
    double shadow_y;
    double frx, fry, frz;       // rotation
    double fax, fay;            // text shearing
    double scale_x, scale_y;
    int border_style;
    int has_border;
    double border_x, border_y;
    double hspacing;
    unsigned italic;
    unsigned bold;
    int flags;
    int shift_x, shift_y;

    unsigned has_outline;
    unsigned is_drawing;

    int max_str_length;
    int str_length;
    unsigned chars;
    char *str;
    int cached;
    FT_Vector pos_orig;
    int first_pos_x;
} CombinedBitmapInfo;

// describes a glyph
// GlyphInfo and TextInfo are used for text centering and word-wrapping operations
typedef struct glyph_info {
    unsigned symbol;
    unsigned skip;              // skip glyph when layouting text
    ASS_Font *font;
    int face_index;
    int glyph_index;
#ifdef CONFIG_HARFBUZZ
    hb_script_t script;
#else
    int script;
#endif
    double font_size;
    ASS_Drawing *drawing;
    FT_Outline *outline;
    FT_Outline *border;
    Bitmap *bm;                 // glyph bitmap
    Bitmap *bm_o;               // outline bitmap
    Bitmap *bm_s;               // shadow bitmap
    FT_BBox bbox;
    FT_Vector pos;
    FT_Vector offset;
    char linebreak;             // the first (leading) glyph of some line ?
    uint32_t c[4];              // colors
    FT_Vector advance;          // 26.6
    FT_Vector cluster_advance;
    char effect;                // the first (leading) glyph of some effect ?
    Effect effect_type;
    int effect_timing;          // time duration of current karaoke word
    // after process_karaoke_effects: distance in pixels from the glyph origin.
    // part of the glyph to the left of it is displayed in a different color.
    int effect_skip_timing;     // delay after the end of last karaoke word
    int asc, desc;              // font max ascender and descender
    int be;                     // blur edges
    double blur;                // gaussian blur
    double shadow_x;
    double shadow_y;
    double frx, fry, frz;       // rotation
    double fax, fay;            // text shearing
    double scale_x, scale_y;
    double orig_scale_x, orig_scale_y; // scale_x,y before fix_glyph_scaling
    int border_style;
    double border_x, border_y;
    double hspacing;
    unsigned italic;
    unsigned bold;
    int flags;

    int bm_run_id;
    int shape_run_id;

    BitmapHashKey hash_key;

    // next glyph in this cluster
    struct glyph_info *next;
} GlyphInfo;

typedef struct {
    double asc, desc;
    int offset, len;
} LineInfo;

typedef struct {
    GlyphInfo *glyphs;
    int length;
    LineInfo *lines;
    int n_lines;
    CombinedBitmapInfo *combined_bitmaps;
    unsigned n_bitmaps;
    double height;
    int max_glyphs;
    int max_lines;
    unsigned max_bitmaps;
} TextInfo;

// Renderer state.
// Values like current font face, color, screen position, clipping and so on are stored here.
typedef struct {
    ASS_Event *event;
    ASS_Style *style;
    int parsed_tags;
    int has_vector_clip;

    ASS_Font *font;
    double font_size;
    int flags;                  // decoration flags (underline/strike-through)

    FT_Stroker stroker;
    int stroker_radius;         // last stroker radius, for caching stroker objects
    int alignment;              // alignment overrides go here; if zero, style value will be used
    double frx, fry, frz;
    double fax, fay;            // text shearing
    enum {
        EVENT_NORMAL,           // "normal" top-, sub- or mid- title
        EVENT_POSITIONED,       // happens after pos(,), margins are ignored
        EVENT_HSCROLL,          // "Banner" transition effect, text_width is unlimited
        EVENT_VSCROLL           // "Scroll up", "Scroll down" transition effects
    } evt_type;
    double pos_x, pos_y;        // position
    double org_x, org_y;        // origin
    char have_origin;           // origin is explicitly defined; if 0, get_base_point() is used
    double scale_x, scale_y;
    double hspacing;            // distance between letters, in pixels
    int border_style;
    double border_x;            // outline width
    double border_y;
    uint32_t c[4];              // colors(Primary, Secondary, so on) in RGBA
    int clip_x0, clip_y0, clip_x1, clip_y1;
    char clip_mode;             // 1 = iclip
    char detect_collisions;
    uint32_t fade;              // alpha from \fad
    char be;                    // blur edges
    double blur;                // gaussian blur
    double shadow_x;
    double shadow_y;
    int drawing_scale;          // currently reading: regular text if 0, drawing otherwise
    double pbo;                 // drawing baseline offset
    ASS_Drawing *drawing;       // current drawing
    ASS_Drawing *clip_drawing;  // clip vector
    int clip_drawing_mode;      // 0 = regular clip, 1 = inverse clip

    Effect effect_type;
    int effect_timing;
    int effect_skip_timing;

    // bitmap run id (used for final bitmap rendering)
    int bm_run_id;

    enum {
        SCROLL_LR,              // left-to-right
        SCROLL_RL,
        SCROLL_TB,              // top-to-bottom
        SCROLL_BT
    } scroll_direction;         // for EVENT_HSCROLL, EVENT_VSCROLL
    int scroll_shift;

    // face properties
    char *family;
    unsigned bold;
    unsigned italic;
    int treat_family_as_pattern;
    int wrap_style;
    int font_encoding;
} RenderContext;

typedef struct {
    Cache *font_cache;
    Cache *outline_cache;
    Cache *bitmap_cache;
    Cache *composite_cache;
    size_t glyph_max;
    size_t bitmap_max_size;
    size_t composite_max_size;
} CacheStore;

typedef void (*BitmapBlendFunc)(uint8_t *dst, intptr_t dst_stride,
                                uint8_t *src, intptr_t src_stride,
                                intptr_t height, intptr_t width);
typedef void (*BitmapMulFunc)(uint8_t *dst, intptr_t dst_stride,
                              uint8_t *src1, intptr_t src1_stride,
                              uint8_t *src2, intptr_t src2_stride,
                              intptr_t width, intptr_t height);
typedef void (*BEBlurFunc)(uint8_t *buf, intptr_t w,
                           intptr_t h, intptr_t stride,
                           uint16_t *tmp);
typedef void (*RestrideBitmapFunc)(uint8_t *dst, intptr_t dst_stride,
                                   uint8_t *src, intptr_t src_stride,
                                   intptr_t width, intptr_t height);

struct ass_renderer {
    ASS_Library *library;
    FT_Library ftlibrary;
    FCInstance *fontconfig_priv;
    ASS_Settings settings;
    int render_id;
    ASS_SynthPriv *synth_priv;
    ASS_Shaper *shaper;

    ASS_Image *images_root;     // rendering result is stored here
    ASS_Image *prev_images_root;
    int cache_cleared;

    EventImages *eimg;          // temporary buffer for sorting rendered events
    int eimg_size;              // allocated buffer size

    // frame-global data
    int width, height;          // screen dimensions
    int orig_height;            // frame height ( = screen height - margins )
    int orig_width;             // frame width ( = screen width - margins )
    int orig_height_nocrop;     // frame height ( = screen height - margins + cropheight)
    int orig_width_nocrop;      // frame width ( = screen width - margins + cropwidth)
    ASS_Track *track;
    long long time;             // frame's timestamp, ms
    double font_scale;
    double font_scale_x;        // x scale applied to all glyphs to preserve text aspect ratio
    double border_scale;
    double blur_scale;

    RenderContext state;
    TextInfo text_info;
    CacheStore cache;

    ASS_Rasterizer rasterizer;
    BitmapBlendFunc add_bitmaps_func;
    BitmapBlendFunc sub_bitmaps_func;
    BitmapMulFunc mul_bitmaps_func;
    BEBlurFunc be_blur_func;
    RestrideBitmapFunc restride_bitmap_func;

    FreeList *free_head;
    FreeList *free_tail;
};

typedef struct render_priv {
    int top, height, left, width;
    int render_id;
} RenderPriv;

typedef struct {
    int x0;
    int y0;
    int x1;
    int y1;
} Rect;

typedef struct {
    int a, b;                   // top and height
    int ha, hb;                 // left and width
} Segment;

void reset_render_context(ASS_Renderer *render_priv, ASS_Style *style);
void ass_free_images(ASS_Image *img);

// XXX: this is actually in ass.c, includes should be fixed later on
void ass_lazy_track_init(ASS_Library *lib, ASS_Track *track);

#endif /* LIBASS_RENDER_H */

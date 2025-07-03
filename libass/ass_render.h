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
#include <stdbool.h>
#include <fribidi.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H
#include <hb.h>

#include "ass.h"
#include "ass_font.h"
#include "ass_bitmap.h"
#include "ass_cache.h"
#include "ass_utils.h"
#include "ass_fontselect.h"
#include "ass_library.h"
#include "ass_drawing.h"
#include "ass_bitmap.h"
#include "ass_rasterizer.h"

#define GLYPH_CACHE_MAX 10000
#define MEGABYTE (1024 * 1024)
#define BITMAP_CACHE_MAX_SIZE (128 * MEGABYTE)
#define COMPOSITE_CACHE_RATIO 2
#define COMPOSITE_CACHE_MAX_SIZE (BITMAP_CACHE_MAX_SIZE / COMPOSITE_CACHE_RATIO)

#define PARSED_FADE (1<<0)
#define PARSED_A    (1<<1)

typedef struct {
    ASS_Image result;
    CompositeHashValue *source;
    unsigned char *buffer;
    size_t ref_count;
} ASS_ImagePriv;

typedef struct {
    int frame_width;
    int frame_height;
    int storage_width;          // video width before any rescaling
    int storage_height;         // video height before any rescaling
    double font_size_coeff;     // font size multiplier
    double line_spacing;        // additional line spacing (in frame pixels)
    double line_position;       // vertical position for subtitles, 0-100 (0 = no change)
    int top_margin;             // height of top margin. Video frame is shifted down by top_margin.
    int bottom_margin;          // height of bottom margin. (frame_height - top_margin - bottom_margin) is original video height.
    int left_margin;
    int right_margin;
    int use_margins;            // 0 - place all subtitles inside original frame
    // 1 - place subtitles (incl. toptitles) in full display frame incl. margins
    double par;                 // user defined pixel aspect ratio (0 = unset)
    ASS_Hinting hinting;
    ASS_ShapingLevel shaper;
    int selective_style_overrides; // ASS_OVERRIDE_* flags

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
    FilterDesc filter;
    uint32_t c[4];              // colors (with fade applied)
    Effect effect_type;

    // during render_and_combine_glyphs: distance in subpixels from the karaoke origin.
    // after render_and_combine_glyphs: screen coordinate in pixels.
    // part of the glyph to the left of it is displayed in a different color.
    int32_t effect_timing;

    // karaoke origin: screen coordinate of leftmost post-transform control point x in subpixels
    int32_t leftmost_x;

    size_t bitmap_count, max_bitmap_count;
    BitmapRef *bitmaps;

    int x, y;
    Bitmap *bm, *bm_o, *bm_s;   // glyphs, outline, shadow bitmaps
    CompositeHashValue *image;
} CombinedBitmapInfo;

typedef struct {
    ASS_DVector scale, offset;
} ASS_Transform;

// describes a glyph
// GlyphInfo and TextInfo are used for text centering and word-wrapping operations
typedef struct glyph_info {
    unsigned symbol;
    bool skip;                  // skip glyph when layouting text
    bool is_trimmed_whitespace;
    ASS_Font *font;
    int face_index;
    int glyph_index;
    hb_script_t script;
    double font_size;
    ASS_StringView drawing_text;
    int drawing_scale;
    int drawing_pbo;
    OutlineHashValue *outline;
    ASS_Transform transform;
    ASS_Rect bbox;
    ASS_Vector pos;
    ASS_Vector offset;
    char linebreak;             // the first (leading) glyph of some line ?
    bool starts_new_run;
    uint32_t c[4];              // colors
    ASS_Vector advance;         // 26.6
    ASS_Vector cluster_advance;
    Effect effect_type;
    int32_t effect_timing;          // time duration of current karaoke word
    // after ass_process_karaoke_effects: distance in subpixels from the karaoke origin.
    // part of the glyph to the left of it is displayed in a different color.
    int32_t effect_skip_timing;     // delay after the end of last karaoke word
    bool reset_effect;
    int asc, desc;              // font max ascender and descender
    int be;                     // blur edges
    double blur;                // gaussian blur
    double shadow_x;
    double shadow_y;
    double frx, fry, frz;       // rotation
    double fax, fay;            // text shearing
    double scale_x, scale_y;
    // amount of scale_x,y change due to fix_glyph_scaling
    // scale_fix = before / after
    double scale_fix;
    int border_style;
    double border_x, border_y;
    double hspacing;
    int hspacing_scaled;        // 26.6
    unsigned italic;
    unsigned bold;
    int flags;
    int fade;

    int shape_run_id;

    ASS_Vector shift;
    Bitmap *bm, *bm_o;

    // next glyph in this cluster
    struct glyph_info *next;
} GlyphInfo;

typedef struct {
    double asc, desc;
    int offset, len;
} LineInfo;

typedef struct {
    GlyphInfo *glyphs;
    FriBidiChar *event_text;
    char *breaks;
    int length;
    LineInfo *lines;
    int n_lines;
    CombinedBitmapInfo *combined_bitmaps;
    unsigned n_bitmaps;
    double height;
    int border_top;
    int border_bottom;
    int border_x;
    int max_glyphs;
    int max_lines;
    unsigned max_bitmaps;
} TextInfo;

#include "ass_shaper.h"

// Renderer state.
// Values like current font face, color, screen position, clipping and so on are stored here.
struct render_context {
    ASS_Renderer *renderer;
    TextInfo text_info;
    ASS_Shaper *shaper;
    RasterizerData rasterizer;

    ASS_Event *event;
    ASS_Style *style;

    ASS_Font *font;
    double font_size;
    int parsed_tags;
    int flags;                  // decoration flags (underline/strike-through)

    int alignment;              // alignment overrides go here; if zero, style value will be used
    int justify;                // justify instructions
    double frx, fry, frz;
    double fax, fay;            // text shearing
    double pos_x, pos_y;        // position
    double org_x, org_y;        // origin
    double scale_x, scale_y;
    double hspacing;            // distance between letters, in pixels
    double border_x;            // outline width
    double border_y;
    enum {
        EVENT_NORMAL = 0,       // "normal" top-, sub- or mid- title
        EVENT_POSITIONED = 1,   // happens after \pos or \move, margins are ignored
        EVENT_HSCROLL = 2,      // "Banner" transition effect, text_width is unlimited
        EVENT_VSCROLL = 4       // "Scroll up", "Scroll down" transition effects
    } evt_type;
    int border_style;
    uint32_t c[4];              // colors(Primary, Secondary, so on) in RGBA
    int clip_x0, clip_y0, clip_x1, clip_y1;
    char have_origin;           // origin is explicitly defined; if 0, get_base_point() is used
    char clip_mode;             // 1 = iclip
    char detect_collisions;
    char be;                    // blur edges
    int fade;                   // alpha from \fad
    double blur;                // gaussian blur
    double shadow_x;
    double shadow_y;
    double pbo;                 // drawing baseline offset
    ASS_StringView clip_drawing_text;

    // used to store RenderContext.style when doing selective style overrides
    ASS_Style override_style_temp_storage;

    int drawing_scale;          // currently reading: regular text if 0, drawing otherwise
    int clip_drawing_scale;
    int clip_drawing_mode;      // 0 = regular clip, 1 = inverse clip

    Effect effect_type;
    int32_t effect_timing;
    int32_t effect_skip_timing;
    bool reset_effect;

    enum {
        SCROLL_LR,              // left-to-right
        SCROLL_RL,
        SCROLL_TB,              // top-to-bottom
        SCROLL_BT
    } scroll_direction;         // for EVENT_HSCROLL, EVENT_VSCROLL
    double scroll_shift;
    int scroll_y0, scroll_y1;

    // face properties
    ASS_StringView family;
    unsigned bold;
    unsigned italic;
    int treat_family_as_pattern;
    int wrap_style;
    int font_encoding;

    // combination of ASS_OVERRIDE_BIT_* flags that apply right now
    unsigned overrides;
    // whether to apply font_scale
    int apply_font_scale;
    // whether this is assumed to be explicitly positioned
    int explicit;

    double screen_scale_x;
    double screen_scale_y;
    double border_scale_x;
    double border_scale_y;
    double blur_scale_x;
    double blur_scale_y;
};

typedef struct render_context RenderContext;

typedef struct {
    Cache *font_cache;
    Cache *outline_cache;
    Cache *bitmap_cache;
    Cache *composite_cache;
    Cache *face_size_metrics_cache;
    Cache *metrics_cache;
    size_t glyph_max;
    size_t bitmap_max_size;
    size_t composite_max_size;
} CacheStore;

struct ass_renderer {
    ASS_Library *library;
    FT_Library ftlibrary;
    ASS_FontSelector *fontselect;
    size_t num_emfonts;
    ASS_Settings settings;
    int render_id;

    ASS_Image *images_root;     // rendering result is stored here
    ASS_Image *prev_images_root;

    EventImages *eimg;          // temporary buffer for sorting rendered events
    int eimg_size;              // allocated buffer size

    // frame-global data
    int width, height;          // screen dimensions (the whole frame from ass_set_frame_size)
    int frame_content_height;   // content frame height ( = screen height - API margins )
    int frame_content_width;    // content frame width ( = screen width - API margins )
    double fit_height;          // content frame height without zoom & pan (fit to screen & letterboxed)
    double fit_width;           // content frame width without zoom & pan (fit to screen & letterboxed)
    ASS_Track *track;
    long long time;             // frame's timestamp, ms
    double par_scale_x;        // x scale applied to all glyphs to preserve text aspect ratio

    RenderContext state;
    CacheStore cache;

    BitmapEngine engine;

    ASS_Style user_override_style;
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

void ass_reset_render_context(RenderContext *state, ASS_Style *style);
void ass_frame_ref(ASS_Image *img);
void ass_frame_unref(ASS_Image *img);
ASS_Vector ass_layout_res(ASS_Renderer *render_priv);

// XXX: this is actually in ass.c, includes should be fixed later on
void ass_lazy_track_init(ASS_Library *lib, ASS_Track *track);

#endif /* LIBASS_RENDER_H */

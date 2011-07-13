/*
 * Copyright (C) 2011 Grigori Goronzy <greg@chown.ath.cx>
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

#include <fribidi/fribidi.h>
#include <hb-ft.h>

#include "ass_shaper.h"
#include "ass_render.h"
#include "ass_font.h"
#include "ass_parse.h"

#define MAX_RUNS 50

struct ass_shaper {
    // FriBidi log2vis
    int n_glyphs;
    FriBidiChar *event_text;
    FriBidiCharType *ctypes;
    FriBidiLevel *emblevels;
    FriBidiStrIndex *cmap;
};

/**
 * \brief Print version information
 */
void ass_shaper_info(ASS_Library *lib)
{
    ass_msg(lib, MSGL_V, "Complex text layout enabled, using FriBidi "
            FRIBIDI_VERSION " HarfBuzz-ng %s", hb_version_string());
}

/**
 * \brief grow arrays, if needed
 * \param new_size requested size
 */
static void check_allocations(ASS_Shaper *shaper, size_t new_size)
{
    if (new_size > shaper->n_glyphs) {
        shaper->event_text = realloc(shaper->event_text, sizeof(FriBidiChar) * new_size);
        shaper->ctypes     = realloc(shaper->ctypes, sizeof(FriBidiCharType) * new_size);
        shaper->emblevels  = realloc(shaper->emblevels, sizeof(FriBidiLevel) * new_size);
        shaper->cmap       = realloc(shaper->cmap, sizeof(FriBidiStrIndex) * new_size);
    }
}

/**
 * \brief Create a new shaper instance and preallocate data structures
 * \param prealloc preallocation size
 */
ASS_Shaper *ass_shaper_new(size_t prealloc)
{
    ASS_Shaper *shaper = calloc(sizeof(*shaper), 1);

    check_allocations(shaper, prealloc);
    return shaper;
}

/**
 * \brief Free shaper and related data
 */
void ass_shaper_free(ASS_Shaper *shaper)
{
    free(shaper->event_text);
    free(shaper->ctypes);
    free(shaper->emblevels);
    free(shaper->cmap);
    free(shaper);
}

/**
 * \brief Shape event text with HarfBuzz. Full OpenType shaping.
 * \param glyphs glyph clusters
 * \param len number of clusters
 */
static void shape_harfbuzz(ASS_Shaper *shaper, GlyphInfo *glyphs, size_t len)
{
    int i, j;
    int run = 0;
    struct {
        int offset;
        int end;
        hb_buffer_t *buf;
        hb_font_t *font;
    } runs[MAX_RUNS];


    for (i = 0; i < len && run < MAX_RUNS; i++, run++) {
        // get length and level of the current run
        int k = i;
        int level = glyphs[i].shape_run_id;
        int direction = shaper->emblevels[k] % 2;
        while (i < (len - 1) && level == glyphs[i+1].shape_run_id)
            i++;
        //printf("run %d from %d to %d with level %d\n", run, k, i, level);
        FT_Face run_font = glyphs[k].font->faces[glyphs[k].face_index];
        runs[run].offset = k;
        runs[run].end    = i;
        runs[run].buf    = hb_buffer_create(i - k + 1);
        runs[run].font   = hb_ft_font_create(run_font, NULL);
        hb_buffer_set_direction(runs[run].buf, direction ? HB_DIRECTION_RTL :
                HB_DIRECTION_LTR);
        hb_buffer_add_utf32(runs[run].buf, shaper->event_text + k, i - k + 1,
                0, i - k + 1);
        hb_shape(runs[run].font, runs[run].buf, NULL, 0);
    }
    //printf("shaped %d runs\n", run);

    // Initialize: skip all glyphs, this is undone later as needed
    for (i = 0; i < len; i++)
        glyphs[i].skip = 1;

    // Update glyph indexes, positions and advances from the shaped runs
    for (i = 0; i < run; i++) {
        int num_glyphs = hb_buffer_get_length(runs[i].buf);
        hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(runs[i].buf, NULL);
        hb_glyph_position_t *pos    = hb_buffer_get_glyph_positions(runs[i].buf, NULL);
        //printf("run text len %d num_glyphs %d\n", runs[i].end - runs[i].offset + 1,
        //        num_glyphs);
        // Update glyphs
        for (j = 0; j < num_glyphs; j++) {
            int idx = glyph_info[j].cluster + runs[i].offset;
            GlyphInfo *info = glyphs + idx;
            GlyphInfo *root = info;
#if 0
            printf("run %d cluster %d codepoint %d -> '%c'\n", i, idx,
                    glyph_info[j].codepoint, event_text[idx]);
            printf("position %d %d advance %d %d\n",
                    pos[j].x_offset, pos[j].y_offset,
                    pos[j].x_advance, pos[j].y_advance);
#endif

            // if we have more than one glyph per cluster, allocate a new one
            // and attach to the root glyph
            if (info->skip == 0) {
                //printf("duplicate cluster entry, adding glyph\n");
                while (info->next)
                    info = info->next;
                info->next = malloc(sizeof(GlyphInfo));
                memcpy(info->next, info, sizeof(GlyphInfo));
                info = info->next;
                info->next = NULL;
            }

            // set position and advance
            info->skip = 0;
            info->glyph_index = glyph_info[j].codepoint;
            info->offset.x    = pos[j].x_offset * info->scale_x;
            info->offset.y    = -pos[j].y_offset * info->scale_y;
            info->advance.x   = pos[j].x_advance * info->scale_x;
            info->advance.y   = -pos[j].y_advance * info->scale_y;

            // accumulate advance in the root glyph
            root->cluster_advance.x += info->advance.x;
            root->cluster_advance.y += info->advance.y;
        }
    }

    // Free runs and associated data
    for (i = 0; i < run; i++) {
        hb_buffer_destroy(runs[i].buf);
        hb_font_destroy(runs[i].font);
    }

}

/**
 * \brief Shape event text with FriBidi. Does mirroring and simple
 * Arabic shaping.
 * \param len number of clusters
 */
static void shape_fribidi(ASS_Shaper *shaper, size_t len)
{
    FriBidiJoiningType *joins = calloc(sizeof(*joins), len);

    fribidi_get_joining_types(shaper->event_text, len, joins);
    fribidi_join_arabic(shaper->ctypes, len, shaper->emblevels, joins);
    fribidi_shape(FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC,
            shaper->emblevels, len, joins, shaper->event_text);

    free(joins);
}

/**
 * \brief Find shape runs according to the event's selected fonts
 */
void ass_shaper_find_runs(ASS_Shaper *shaper, ASS_Renderer *render_priv,
                          GlyphInfo *glyphs, size_t len)
{
    int i;
    int shape_run = 0;

    for (i = 0; i < len; i++) {
        GlyphInfo *last = glyphs + i - 1;
        GlyphInfo *info = glyphs + i;
        // skip drawings
        if (info->symbol == 0xfffc)
            continue;
        // initialize face_index to continue with the same face, if possible
        // XXX: can be problematic in some cases, for example if a font misses
        // a single glyph, like space (U+0020)
        if (i > 0)
            info->face_index = last->face_index;
        // set size and get glyph index
        double size_scaled = ensure_font_size(render_priv,
                info->font_size * render_priv->font_scale);
        ass_font_set_size(info->font, size_scaled);
        ass_font_get_index(render_priv->fontconfig_priv, info->font,
                info->symbol, &info->face_index, &info->glyph_index);
        // shape runs share the same font face and size
        if (i > 0 && (last->font != info->font ||
                    last->font_size != info->font_size ||
                    last->face_index != info->face_index))
            shape_run++;
        info->shape_run_id = shape_run;
        //printf("glyph '%c' shape run id %d face %d\n", info->symbol, info->shape_run_id,
        //        info->face_index);
    }

}

/**
 * \brief Shape an event's text. Calculates directional runs and shapes them.
 * \param text_info event's text
 */
void ass_shaper_shape(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i, last_break;
    FriBidiParType dir;
    GlyphInfo *glyphs = text_info->glyphs;

    check_allocations(shaper, text_info->length);

    // Get bidi character types and embedding levels
    last_break = 0;
    for (i = 0; i < text_info->length; i++) {
        shaper->event_text[i] = glyphs[i].symbol;
        // embedding levels should be calculated paragraph by paragraph
        if (glyphs[i].symbol == '\n' || i == text_info->length - 1) {
            //printf("paragraph from %d to %d\n", last_break, i);
            dir = FRIBIDI_PAR_ON;
            fribidi_get_bidi_types(shaper->event_text + last_break,
                    i - last_break + 1, shaper->ctypes + last_break);
            fribidi_get_par_embedding_levels(shaper->ctypes + last_break,
                    i - last_break + 1, &dir, shaper->emblevels + last_break);
            last_break = i + 1;
        }
    }

    // add embedding levels to shape runs for final runs
    for (i = 0; i < text_info->length; i++) {
        glyphs[i].shape_run_id += shaper->emblevels[i];
    }

#if 0
    printf("levels ");
    for (i = 0; i < text_info->length; i++) {
        printf("%d ", glyphs[i].shape_run_id);
    }
    printf("\n");
#endif

    //shape_fribidi(shaper, text_info->length);
    shape_harfbuzz(shaper, glyphs, text_info->length);

    // Update glyphs
    for (i = 0; i < text_info->length; i++) {
        glyphs[i].symbol = shaper->event_text[i];
        // Skip direction override control characters
        // NOTE: Behdad said HarfBuzz is supposed to remove these, but this hasn't
        // been implemented yet
        if (glyphs[i].symbol <= 0x202F && glyphs[i].symbol >= 0x202a) {
            glyphs[i].symbol = 0;
            glyphs[i].skip++;
        }
    }
}

/**
 * \brief clean up additional data temporarily needed for shaping and
 * (e.g. additional glyphs allocated)
 */
void ass_shaper_cleanup(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i;

    for (i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + i;
        info = info->next;
        while (info) {
            GlyphInfo *next = info->next;
            free(info);
            info = next;
        }
    }
}

/**
 * \brief Calculate reorder map to render glyphs in visual order
 */
FriBidiStrIndex *ass_shaper_reorder(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i;
    FriBidiParType dir;

    // Initialize reorder map
    for (i = 0; i < text_info->length; i++)
        shaper->cmap[i] = i;

    // Create reorder map line-by-line
    for (i = 0; i < text_info->n_lines; i++) {
        LineInfo *line = text_info->lines + i;
        int level;
        dir = FRIBIDI_PAR_ON;

        // FIXME: we should actually specify
        // the correct paragraph base direction
        level = fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT,
                shaper->ctypes + line->offset, line->len, 0, dir,
                shaper->emblevels + line->offset, NULL,
                shaper->cmap + line->offset);
        //printf("reorder line %d to level %d\n", i, level);
    }

#if 0
    printf("map ");
    for (i = 0; i < text_info->length; i++) {
        printf("%d ", cmap[i]);
    }
    printf("\n");
#endif

    return shaper->cmap;
}

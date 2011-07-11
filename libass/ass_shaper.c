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

#include "ass_render.h"
#include "ass_shaper.h"

#define MAX_RUNS 30

/**
 * \brief Print version information
 */
void ass_shaper_info(ASS_Library *lib)
{
    ass_msg(lib, MSGL_V, "Complex text layout enabled, using FriBidi "
            FRIBIDI_VERSION " HarfBuzz-ng %s", hb_version_string());
}

/**
 * \brief Shape an event's text. Calculates directional runs and shapes them.
 * \param text_info event's text
 * \param ctypes returns character types
 * \param emblevels returns embedding levels (directional runs)
 */
void ass_shaper_shape(TextInfo *text_info, FriBidiCharType *ctypes,
                      FriBidiLevel *emblevels)
{
    int i, j, last_break;
    FriBidiParType dir;
    FriBidiChar *event_text = calloc(sizeof(*event_text), text_info->length);
    FriBidiJoiningType *joins = calloc(sizeof(*joins), text_info->length);
    GlyphInfo *glyphs = text_info->glyphs;
    // XXX: dynamically allocate
    struct {
        int offset;
        int end;
        hb_buffer_t *buf;
        hb_font_t *font;
    } runs[MAX_RUNS];

    // Get bidi character types and embedding levels
    last_break = 0;
    for (i = 0; i < text_info->length; i++) {
        event_text[i] = glyphs[i].symbol;
        // embedding levels should be calculated paragraph by paragraph
        if (glyphs[i].symbol == '\n' || i == text_info->length - 1) {
            //printf("paragraph from %d to %d\n", last_break, i);
            dir = FRIBIDI_PAR_ON;
            fribidi_get_bidi_types(event_text + last_break, i - last_break + 1,
                    ctypes + last_break);
            fribidi_get_par_embedding_levels(ctypes + last_break,
                    i - last_break + 1, &dir, emblevels + last_break);
            last_break = i + 1;
        }
    }

    // add embedding levels to shape runs for final runs
    for (i = 0; i < text_info->length; i++) {
        glyphs[i].shape_run_id += emblevels[i];
    }

#if 0
    printf("levels ");
    for (i = 0; i < text_info->length; i++) {
        printf("%d ", glyphs[i].shape_run_id);
    }
    printf("\n");
#endif

#if 0
    // Use FriBidi's shaper for mirroring and simple Arabic shaping
    fribidi_get_joining_types(event_text, text_info->length, joins);
    fribidi_join_arabic(ctypes, text_info->length, emblevels, joins);
    fribidi_shape(FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC, emblevels,
            text_info->length, joins, event_text);
#endif

    // Shape runs with HarfBuzz-ng
    int run = 0;
    for (i = 0; i < text_info->length && run < MAX_RUNS; i++, run++) {
        // get length and level of the current run
        int k = i;
        int level = glyphs[i].shape_run_id;
        while (i < (text_info->length - 1) && level == glyphs[i+1].shape_run_id)
            i++;
        //printf("run %d from %d to %d with level %d\n", run, k, i, level);
        FT_Face run_font = glyphs[k].font->faces[glyphs[k].face_index];
        runs[run].offset = k;
        runs[run].end    = i;
        runs[run].buf    = hb_buffer_create(i - k + 1);
        runs[run].font   = hb_ft_font_create(run_font, NULL);
        hb_buffer_set_direction(runs[run].buf, (level % 2) ? HB_DIRECTION_RTL :
                HB_DIRECTION_LTR);
        hb_buffer_add_utf32(runs[run].buf, event_text + k, i - k + 1,
                0, i - k + 1);
        hb_shape(runs[run].font, runs[run].buf, NULL, 0);
    }
    //printf("shaped %d runs\n", run);

    // Initialize: skip all glyphs, this is undone later as needed
    for (i = 0; i < text_info->length; i++)
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
            info->offset.y    = pos[j].y_offset * info->scale_y;
            info->advance.x   = pos[j].x_advance * info->scale_x;
            info->advance.y   = pos[j].y_advance * info->scale_y;

            // accumulate maximum advance in the root glyph
            root->advance.x = FFMAX(root->advance.x, info->advance.x);
            root->advance.y = FFMAX(root->advance.y, info->advance.y);
        }
    }

    // Free runs and associated data
    for (i = 0; i < run; i++) {
        hb_buffer_destroy(runs[i].buf);
        hb_font_destroy(runs[i].font);
    }

    // Update glyphs
    for (i = 0; i < text_info->length; i++) {
        glyphs[i].symbol = event_text[i];
        // Skip direction override control characters
        // NOTE: Behdad said HarfBuzz is supposed to remove these, but this hasn't
        // been implemented yet
        if (glyphs[i].symbol <= 0x202F && glyphs[i].symbol >= 0x202a) {
            glyphs[i].symbol = 0;
            glyphs[i].skip++;
        }
    }

    free(joins);
    free(event_text);
}

/**
 * \brief clean up additional data temporarily needed for shaping and
 * (e.g. additional glyphs allocated)
 */
void ass_shaper_cleanup(TextInfo *text_info)
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

void ass_shaper_reorder(TextInfo *text_info, FriBidiCharType *ctypes,
                        FriBidiLevel *emblevels, FriBidiStrIndex *cmap)
{
    int i;
    FriBidiParType dir;

    // Initialize reorder map
    for (i = 0; i < text_info->length; i++)
        cmap[i] = i;

    // Create reorder map line-by-line
    for (i = 0; i < text_info->n_lines; i++) {
        LineInfo *line = text_info->lines + i;
        int level;
        dir = FRIBIDI_PAR_ON;

        // FIXME: we should actually specify
        // the correct paragraph base direction
        level = fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT,
                ctypes + line->offset, line->len, 0, dir,
                emblevels + line->offset, NULL, cmap + line->offset);
        //printf("reorder line %d to level %d\n", i, level);
    }

#if 0
    printf("map ");
    for (i = 0; i < text_info->length; i++) {
        printf("%d ", cmap[i]);
    }
    printf("\n");
#endif

}

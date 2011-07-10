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

#include "ass_render.h"
#include "ass_shaper.h"

/**
 * \brief Print version information
 */
void ass_shaper_info(ASS_Library *lib)
{
    ass_msg(lib, MSGL_V, "Complex text layout enabled, using FriBidi "
            FRIBIDI_VERSION);
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
    int i, last_break;
    FriBidiParType dir;
    FriBidiChar *event_text = calloc(sizeof(*event_text), text_info->length);
    FriBidiJoiningType *joins = calloc(sizeof(*joins), text_info->length);
    GlyphInfo *glyphs = text_info->glyphs;

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

#if 0
    printf("levels ");
    for (i = 0; i < text_info->length; i++) {
        printf("%d:%d ", ctypes[i], emblevels[i]);
    }
    printf("\n");
#endif

    // Use FriBidi's shaper for mirroring and simple Arabic shaping
    fribidi_get_joining_types(event_text, text_info->length, joins);
    fribidi_join_arabic(ctypes, text_info->length, emblevels, joins);
    fribidi_shape(FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC, emblevels,
            text_info->length, joins, event_text);

    // XXX: insert HarfBuzz shaper here

    // Update glyphs
    for (i = 0; i < text_info->length; i++) {
        glyphs[i].symbol = event_text[i];
        // Skip direction override characters
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

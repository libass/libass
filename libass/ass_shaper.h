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

#ifndef LIBASS_SHAPER_H
#define LIBASS_SHAPER_H

typedef struct ass_shaper ASS_Shaper;

#include <fribidi.h>
#include <stdbool.h>
#include "ass_render.h"
#include "ass_cache.h"

#if FRIBIDI_MAJOR_VERSION >= 1
#define USE_FRIBIDI_EX_API
#endif

void ass_shaper_info(ASS_Library *lib);
ASS_Shaper *ass_shaper_new(Cache *metrics_cache);
void ass_shaper_free(ASS_Shaper *shaper);
void ass_shaper_set_kerning(ASS_Shaper *shaper, bool kern);
void ass_shaper_find_runs(ASS_Shaper *shaper, ASS_Renderer *render_priv,
                          GlyphInfo *glyphs, size_t len);
void ass_shaper_set_base_direction(ASS_Shaper *shaper, FriBidiParType dir);
void ass_shaper_set_language(ASS_Shaper *shaper, const char *code);
void ass_shaper_set_level(ASS_Shaper *shaper, ASS_ShapingLevel level);
#ifdef USE_FRIBIDI_EX_API
void ass_shaper_set_bidi_brackets(ASS_Shaper *shaper, bool match_brackets);
#endif
void ass_shaper_set_whole_text_layout(ASS_Shaper *shaper, bool enable);
bool ass_shaper_shape(ASS_Shaper *shaper, TextInfo *text_info);
void ass_shaper_cleanup(ASS_Shaper *shaper, TextInfo *text_info);
FriBidiStrIndex *ass_shaper_reorder(ASS_Shaper *shaper, TextInfo *text_info);
FriBidiStrIndex *ass_shaper_get_reorder_map(ASS_Shaper *shaper);
FriBidiParType ass_resolve_base_direction(int font_encoding);

void ass_shaper_font_data_free(ASS_ShaperFontData *priv);

#endif

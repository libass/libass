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

#ifndef LIBASS_FONT_H
#define LIBASS_FONT_H

#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct ass_font ASS_Font;

#include "ass.h"
#include "ass_types.h"
#include "ass_fontselect.h"
#include "ass_cache.h"
#include "ass_outline.h"

#define VERTICAL_LOWER_BOUND 0x02f1

#define ASS_FONT_MAX_FACES 10
#define DECO_UNDERLINE     1
#define DECO_STRIKETHROUGH 2
#define DECO_ROTATE        4

struct ass_font {
    ASS_FontDesc desc;
    ASS_Library *library;
    FT_Library ftlibrary;
    int faces_uid[ASS_FONT_MAX_FACES];
    FT_Face faces[ASS_FONT_MAX_FACES];
    ASS_ShaperFontData *shaper_priv;
    int n_faces;
    double size;
};

void ass_charmap_magic(ASS_Library *library, FT_Face face);
ASS_Font *ass_font_new(ASS_Renderer *render_priv, ASS_FontDesc *desc);
void ass_face_set_size(FT_Face face, double size);
int ass_face_get_weight(FT_Face face);
void ass_font_get_asc_desc(ASS_Font *font, int face_index,
                           int *asc, int *desc);
int ass_font_get_index(ASS_FontSelector *fontsel, ASS_Font *font,
                       uint32_t symbol, int *face_index, int *glyph_index);
uint32_t ass_font_index_magic(FT_Face face, uint32_t symbol);
bool ass_font_get_glyph(ASS_Font *font, int face_index, int index,
                        ASS_Hinting hinting);
void ass_font_clear(ASS_Font *font);

bool ass_get_glyph_outline(ASS_Outline *outline, int32_t *advance,
                           FT_Face face, unsigned flags);

FT_Face ass_face_open(ASS_Library *lib, FT_Library ftlib, const char *path,
                      const char *postscript_name, int index);
FT_Face ass_face_stream(ASS_Library *lib, FT_Library ftlib, const char *name,
                        const ASS_FontStream *stream, int index);

#endif                          /* LIBASS_FONT_H */

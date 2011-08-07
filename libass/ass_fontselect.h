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

#ifndef LIBASS_FONTCONFIG_H
#define LIBASS_FONTCONFIG_H

#include <stdint.h>
#include "ass_types.h"
#include "ass.h"
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct font_selector ASS_FontSelector;
typedef struct font_provider ASS_FontProvider;
typedef struct font_info ASS_FontInfo;

// get face data
typedef void *(*GetFaceFunc)(void *);

// check for a glyph
typedef int (*CheckGlyphFunc)(void *, uint32_t);

// destroy font_info and related data
typedef void (*DestroyFunc)(void *);
typedef void (*DestroyProviderFunc)(void *);

typedef struct font_provider_funcs {
    GetFaceFunc     get_face;
    CheckGlyphFunc  check_glyph;
    DestroyFunc     destroy_font;
    DestroyProviderFunc destroy_provider;
} ASS_FontProviderFuncs;

typedef struct font_provider_meta_data {
    char *family;
    char **fullnames;
    int n_fullname;
    int slant;
    int weight;
} ASS_FontProviderMetaData;

ASS_FontSelector *
ass_fontselect_init(ASS_Library *library,
                    FT_Library ftlibrary, const char *family,
                    const char *path);
char *ass_font_select(ASS_FontSelector *priv, ASS_Library *lib,
                      const char *family, unsigned bold, unsigned italic,
                      int *index, uint32_t code);
void ass_fontselect_free(ASS_FontSelector *priv);

// Font provider functions
ASS_FontProvider *ass_font_provider_new(ASS_FontSelector *selector,
        ASS_FontProviderFuncs *funcs, void *data);
int ass_font_provider_add_font(ASS_FontProvider *provider,
        ASS_FontProviderMetaData *meta, const char *path, unsigned int index,
        void *data);
void ass_font_provider_free(ASS_FontProvider *provider);

#endif                          /* LIBASS_FONTCONFIG_H */

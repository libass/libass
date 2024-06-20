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

#include "config.h"
#include "ass_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "ass_fontconfig.h"
#include "ass_fontselect.h"
#include "ass_utils.h"

#define MAX_NAME 100

typedef struct fc_private {
    FcConfig *config;
    FcFontSet *fallbacks;
    FcCharSet *fallback_chars;

    Cache *cache;
} ProviderPrivate;

static bool check_glyph(void *priv, uint32_t code)
{
    FcPattern *pat = (FcPattern *)priv;
    FcCharSet *charset;

    if (!pat)
        return true;

    if (code == 0)
        return true;

    FcResult result = FcPatternGetCharSet(pat, FC_CHARSET, 0, &charset);
    if (result != FcResultMatch)
        return false;
    if (FcCharSetHasChar(charset, code) == FcTrue)
        return true;
    return false;
}

static void destroy_font(void *priv)
{
    FcPatternDestroy((FcPattern *) priv);
}

static void destroy(void *priv)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;

    if (fc->cache)
        ass_cache_done(fc->cache);
    if (fc->fallback_chars)
        FcCharSetDestroy(fc->fallback_chars);
    if (fc->fallbacks)
        FcFontSetDestroy(fc->fallbacks);
    FcConfigDestroy(fc->config);
    free(fc);
}

static bool add_name(Cache *cache, FcObjectSet *filter, FcPattern *pat, const char *name)
{
    FontconfigNameHashKey key = {
        .name = {
            .str = name,
            .len = strlen(name),
        },
    };

    FontconfigNameHashValue *value = ass_cache_get(cache, &key, NULL);
    if (!value)
        return false;

    for (size_t i = 0; i < value->size; i++) {
        if (FcPatternEqualSubset(value->patterns[i], pat, filter))
            return true;
    }

    if (value->size >= value->capacity) {
        size_t new_cap = FFMAX(value->capacity, 1) * 2;
        void *resized = ass_realloc_array(value->patterns, new_cap, sizeof(FcPattern*));
        if (!resized)
            return false;

        value->patterns = resized;
        value->capacity = new_cap;
    }

    FcPatternReference(pat);
    value->patterns[value->size++] = pat;

    return true;
}

static bool try_add_name(Cache *cache, FcObjectSet *filter, FcPattern *pat, const char *object, int n)
{
    char *name = NULL;
    if (FcPatternGetString(pat, object, n, (FcChar8 **)&name) != FcResultMatch)
        return false;

    return add_name(cache, filter, pat, name);
}

static bool scan_fonts(ProviderPrivate *priv, ASS_FontProvider *provider)
{
    int i;
    FcFontSet *fonts;
    FcConfig *config = priv->config;
    Cache *cache = priv->cache;

    // get list of fonts;
    // sorting by default pattern prefers regular variants
    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return false;

    FcDefaultSubstitute(pat);
    FcResult res;
    // trim=FcFalse returns all system fonts
    fonts = FcFontSort(config, pat, FcFalse, NULL, &res);
    FcPatternDestroy(pat);
    if (res != FcResultMatch)
        return false;

    FcObjectSet *filter = FcObjectSetBuild(FC_FILE, FC_INDEX, NULL);
    if (!filter) {
        FcFontSetDestroy(fonts);
        return false;
    }

    // fill font_info list
    for (i = 0; i < fonts->nfont; i++) {
        FcPattern *pat = fonts->fonts[i];
        FcBool outline;

        // skip non-outline fonts
        FcResult result = FcPatternGetBool(pat, FC_OUTLINE, 0, &outline);
        if (result != FcResultMatch || outline != FcTrue)
            continue;

        // ignore variants (we'll handle those via the base name)
        int index;
        result = FcPatternGetInteger(pat, FC_INDEX, 0, &index);
        if (result != FcResultMatch || index > 0xFFFF)
            continue;

        // read family names
        for (int j = 0; try_add_name(cache, filter, pat, FC_FAMILY, j); j++);

        // read fullnames
        try_add_name(cache, filter, pat, FC_POSTSCRIPT_NAME, 0);
        for (int j = 0; try_add_name(cache, filter, pat, FC_FULLNAME, j); j++);
    }

    FcObjectSetDestroy(filter);
    FcFontSetDestroy(fonts);
    return true;
}

static void cache_fallbacks(ProviderPrivate *fc)
{
    FcResult result;

    if (fc->fallbacks)
        return;

    // Create a suitable pattern
    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return;
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans-serif");
    FcPatternAddBool(pat, FC_OUTLINE, FcTrue);
    FcConfigSubstitute (fc->config, pat, FcMatchPattern);
    FcDefaultSubstitute (pat);

    // FC_LANG is automatically set according to locale, but this results
    // in strange sorting sometimes, so remove the attribute completely.
    FcPatternDel(pat, FC_LANG);

    // Sort installed fonts and eliminate duplicates; this can be very
    // expensive.
    fc->fallbacks = FcFontSort(fc->config, pat, FcTrue, &fc->fallback_chars,
            &result);

    // If this fails, just add an empty set
    // (if it fails, cache_fallbacks will just be reattempted later)
    if (result != FcResultMatch)
        fc->fallbacks = FcFontSetCreate();

    FcPatternDestroy(pat);
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *family, uint32_t codepoint)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;
    FcResult result;

    cache_fallbacks(fc);

    if (!fc->fallbacks || fc->fallbacks->nfont == 0)
        return NULL;

    if (codepoint == 0) {
        char *family = NULL;
        result = FcPatternGetString(fc->fallbacks->fonts[0], FC_FAMILY, 0,
                (FcChar8 **)&family);
        if (result == FcResultMatch) {
            return strdup(family);
        } else {
            return NULL;
        }
    }

    // fallback_chars is the union of all available charsets, so
    // if we can't find the glyph in there, the system does not
    // have any font to render this glyph.
    if (FcCharSetHasChar(fc->fallback_chars, codepoint) == FcFalse)
        return NULL;

    for (int j = 0; j < fc->fallbacks->nfont; j++) {
        FcPattern *pattern = fc->fallbacks->fonts[j];

        FcCharSet *charset;
        result = FcPatternGetCharSet(pattern, FC_CHARSET, 0, &charset);

        if (result == FcResultMatch && FcCharSetHasChar(charset,
                    codepoint)) {
            char *family = NULL;
            result = FcPatternGetString(pattern, FC_FAMILY, 0,
                    (FcChar8 **)&family);
            if (result == FcResultMatch) {
                return strdup(family);
            } else {
                return NULL;
            }
        }
    }

    // we shouldn't get here
    return NULL;
}

static void get_substitutions(void *priv, const char *name,
                              ASS_FontProviderMetaData *meta)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;

    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return;

    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)name);
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"__libass_delimiter");
    FcPatternAddBool(pat, FC_OUTLINE, FcTrue);
    if (!FcConfigSubstitute(fc->config, pat, FcMatchPattern))
        goto cleanup;

    // read and strdup fullnames
    meta->n_fullname = 0;
    meta->fullnames = calloc(MAX_NAME, sizeof(char *));
    if (!meta->fullnames)
        goto cleanup;

    char *alias = NULL;
    while (FcPatternGetString(pat, FC_FAMILY, meta->n_fullname,
                (FcChar8 **)&alias) == FcResultMatch
                && meta->n_fullname < MAX_NAME
                && strcmp(alias, "__libass_delimiter") != 0) {
        alias = strdup(alias);
        if (!alias)
            goto cleanup;
        meta->fullnames[meta->n_fullname] = alias;
        meta->n_fullname++;
    }

cleanup:
    FcPatternDestroy(pat);
}

static void match_fonts(void *priv, ASS_Library *lib, ASS_FontProvider *provider,
                        const char *name)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;

    FontconfigNameHashKey key = {
        .name = {
            .str = name,
            .len = strlen(name),
        },
    };

    FontconfigNameHashValue *value = ass_cache_get(fc->cache, &key, NULL);
    if (!value || !value->capacity)
        return;

    for (size_t i = 0; i < value->size; i++) {
        ASS_FontProviderMetaData meta = {
            .extended_family = (char*)name,
        };
        FcPattern *pat = value->patterns[i];
        int index;
        char *path;

        // index in ttc file
        FcResult result = FcPatternGetInteger(pat, FC_INDEX, 0, &index);
        if (result != FcResultMatch)
            continue;

        // path
        result = FcPatternGetString(pat, FC_FILE, 0, (FcChar8 **)&path);
        if (result != FcResultMatch)
            continue;

        ass_font_provider_add_font(provider, &meta, path, index, pat);
        value->patterns[i] = NULL;
    }

    value->capacity = 0;
}

static ASS_FontProviderFuncs fontconfig_callbacks = {
    .check_glyph        = check_glyph,
    .destroy_font       = destroy_font,
    .destroy_provider   = destroy,
    .get_substitutions  = get_substitutions,
    .get_fallback       = get_fallback,
    .match_fonts        = match_fonts,
};

ASS_FontProvider *
ass_fontconfig_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                            const char *config, FT_Library ftlib)
{
    int rc = FcResultNoMatch;
    ProviderPrivate *fc = NULL;
    ASS_FontProvider *provider = NULL;

    fc = calloc(1, sizeof(ProviderPrivate));
    if (fc == NULL)
        return NULL;

    fc->cache = ass_fontconfig_name_cache_create();
    if (!fc->cache) {
        free(fc);
        return NULL;
    }

    // build and load fontconfig configuration
    fc->config = FcConfigCreate();

    if (fc->config)
        rc = FcConfigParseAndLoad(fc->config, (unsigned char *) config, FcTrue);
    if (!rc) {
        ass_msg(lib, MSGL_WARN, "No usable fontconfig configuration "
                "file found, using fallback.");
        FcConfigDestroy(fc->config);
        fc->config = FcInitLoadConfig();
    }

    if (fc->config)
        rc = FcConfigBuildFonts(fc->config);

    if (!rc || !fc->config) {
        ass_msg(lib, MSGL_ERR,
                "No valid fontconfig configuration found!");
        FcConfigDestroy(fc->config);
        free(fc);
        return NULL;
    }

    // create font provider
    provider = ass_font_provider_new(selector, &fontconfig_callbacks, fc);

    // build database from system fonts
    if (!scan_fonts(fc, provider))
        ass_msg(lib, MSGL_ERR, "Failed to load fontconfig fonts!");

    return provider;
}

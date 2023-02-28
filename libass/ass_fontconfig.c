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
} ProviderPrivate;

static bool check_postscript(void *priv)
{
    FcPattern *pat = (FcPattern *)priv;
    char *format;

    FcResult result =
        FcPatternGetString(pat, FC_FONTFORMAT, 0, (FcChar8 **)&format);
    if (result != FcResultMatch)
        return false;

    return !strcmp(format, "Type 1") || !strcmp(format, "Type 42") ||
           !strcmp(format, "CID Type 1") || !strcmp(format, "CFF");
}

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

static void destroy(void *priv)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;

    FcConfigDestroy(fc->config);
    free(fc);
}

static void scan_fonts(FcConfig *config, ASS_FontProvider *provider)
{
    int i;
    FcFontSet *fonts;
    ASS_FontProviderMetaData meta = {0};

    // get list of fonts
    fonts = FcConfigGetFonts(config, FcSetSystem);

    // fill font_info list
    for (i = 0; i < fonts->nfont; i++) {
        FcPattern *pat = fonts->fonts[i];
        FcBool outline;
        int index;
        double weight;
        char *path;
        char *fullnames[MAX_NAME];
        char *families[MAX_NAME];

        // skip non-outline fonts
        FcResult result = FcPatternGetBool(pat, FC_OUTLINE, 0, &outline);
        if (result != FcResultMatch || outline != FcTrue)
            continue;

        // simple types
        result  = FcPatternGetInteger(pat, FC_SLANT, 0, &meta.slant);
        result |= FcPatternGetInteger(pat, FC_WIDTH, 0, &meta.width);
        result |= FcPatternGetDouble(pat, FC_WEIGHT, 0, &weight);
        result |= FcPatternGetInteger(pat, FC_INDEX, 0, &index);
        if (result != FcResultMatch)
            continue;

        // fontconfig uses its own weight scale, apparently derived
        // from typographical weight. we're using truetype weights, so
        // convert appropriately.
#if FC_VERSION >= 21292
        meta.weight = FcWeightToOpenTypeDouble(weight) + 0.5;
#elif FC_VERSION >= 21191
        // Versions prior to 2.12.92 only had integer precision.
        meta.weight = FcWeightToOpenType(weight + 0.5) + 0.5;
#else
        // On older fontconfig, FcWeightToOpenType is unavailable, and its inverse was
        // implemented more simply, using an if/else ladder instead of linear interpolation.
        // We implement an inverse of that ladder here.
        // We don't expect actual FC caches from these versions to have intermediate
        // values, so the average checks are only for completeness.
#define AVG(x, y) (((double)x + y) / 2)
#ifndef FC_WEIGHT_SEMILIGHT
#define FC_WEIGHT_SEMILIGHT 55
#endif
        if (weight < AVG(FC_WEIGHT_THIN, FC_WEIGHT_EXTRALIGHT))
            meta.weight = 100;
        else if (weight < AVG(FC_WEIGHT_EXTRALIGHT, FC_WEIGHT_LIGHT))
            meta.weight = 200;
        else if (weight < AVG(FC_WEIGHT_LIGHT, FC_WEIGHT_SEMILIGHT))
            meta.weight = 300;
        else if (weight < AVG(FC_WEIGHT_SEMILIGHT, FC_WEIGHT_BOOK))
            meta.weight = 350;
        else if (weight < AVG(FC_WEIGHT_BOOK, FC_WEIGHT_REGULAR))
            meta.weight = 380;
        else if (weight < AVG(FC_WEIGHT_REGULAR, FC_WEIGHT_MEDIUM))
            meta.weight = 400;
        else if (weight < AVG(FC_WEIGHT_MEDIUM, FC_WEIGHT_SEMIBOLD))
            meta.weight = 500;
        else if (weight < AVG(FC_WEIGHT_SEMIBOLD, FC_WEIGHT_BOLD))
            meta.weight = 600;
        else if (weight < AVG(FC_WEIGHT_BOLD, FC_WEIGHT_EXTRABOLD))
            meta.weight = 700;
        else if (weight < AVG(FC_WEIGHT_EXTRABOLD, FC_WEIGHT_BLACK))
            meta.weight = 800;
        else if (weight < AVG(FC_WEIGHT_BLACK, FC_WEIGHT_EXTRABLACK))
            meta.weight = 900;
        else
            meta.weight = 1000;
#endif

        // path
        result = FcPatternGetString(pat, FC_FILE, 0, (FcChar8 **)&path);
        if (result != FcResultMatch)
            continue;

        // read family names
        meta.n_family = 0;
        while (meta.n_family < MAX_NAME &&
                FcPatternGetString(pat, FC_FAMILY, meta.n_family,
                    (FcChar8 **)&families[meta.n_family]) == FcResultMatch)
            meta.n_family++;
        meta.families = families;

        // read fullnames
        meta.n_fullname = 0;
        while (meta.n_fullname < MAX_NAME &&
                FcPatternGetString(pat, FC_FULLNAME, meta.n_fullname,
                    (FcChar8 **)&fullnames[meta.n_fullname]) == FcResultMatch)
            meta.n_fullname++;
        meta.fullnames = fullnames;

        // read PostScript name
        result = FcPatternGetString(pat, FC_POSTSCRIPT_NAME, 0,
                (FcChar8 **)&meta.postscript_name);
        if (result != FcResultMatch)
            meta.postscript_name = NULL;

        ass_font_provider_add_font(provider, &meta, path, index, (void *)pat);
    }
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *family, uint32_t codepoint, ASS_StringView locale)
{
    ProviderPrivate *fc = (ProviderPrivate *)priv;
    FcResult result;
    char *ret = NULL;

    FcPattern *pat = FcPatternCreate();
    if (!pat)
        return NULL;

    if (locale.len) {
        FcLangSet *ls = FcLangSetCreate();
        if (!ls)
            goto cleanup;

        FcLangSetAdd(ls, (const FcChar8 *)locale.str);
        FcPatternAddLangSet(pat, FC_LANG, ls);

        FcLangSetDestroy(ls);
    }

    if (codepoint) {
        FcCharSet *cs = FcCharSetCreate();
        if (!cs)
            goto cleanup;

        FcCharSetAddChar(cs, codepoint);
        FcPatternAddCharSet(pat, FC_CHARSET, cs);

        FcCharSetDestroy(cs);
    }

    FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddBool(pat, FC_OUTLINE, FcTrue);
    if (!FcConfigSubstitute(fc->config, pat, FcMatchPattern))
        goto cleanup;

    FcDefaultSubstitute(pat);

    if (!locale.len) {
        // FC_LANG is automatically set according to locale, but this results
        // in locale-specific fonts being used for text from other languages,
        // which can look pretty bad (e.g. HannotateTC-W5 being used for English).
        // We remove it here to use language-independent lookup instead if no language
        // was specified by the file or the caller.
        // Callers can pass in the system locale as a fallback behind a user setting
        // if they want to.
        FcPatternDel(pat, FC_LANG);
    }

    FcPattern *match = FcFontMatch(fc->config, pat, &result);
    if (match) {
        FcChar8 *got_family = NULL;
        result = FcPatternGetString(match, FC_FAMILY, 0, &got_family);
        if (got_family)
            ret = strdup((char *)got_family);

        FcPatternDestroy(match);
    }

cleanup:
    if (pat)
        FcPatternDestroy(pat);

    return ret;
}

static ASS_FontProviderFuncs fontconfig_callbacks = {
    .check_postscript   = check_postscript,
    .check_glyph        = check_glyph,
    .destroy_provider   = destroy,
    .get_fallback       = get_fallback,
};

ASS_FontProvider *
ass_fontconfig_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                            const char *config, FT_Library ftlib)
{
    int rc;
    ProviderPrivate *fc = NULL;
    ASS_FontProvider *provider = NULL;

    fc = calloc(1, sizeof(ProviderPrivate));
    if (fc == NULL)
        return NULL;

    // build and load fontconfig configuration
    fc->config = FcConfigCreate();
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
        ass_msg(lib, MSGL_FATAL,
                "No valid fontconfig configuration found!");
        FcConfigDestroy(fc->config);
        free(fc);
        return NULL;
    }

    // create font provider
    provider = ass_font_provider_new(selector, &fontconfig_callbacks, fc);

    // build database from system fonts
    scan_fonts(fc->config, provider);

    return provider;
}

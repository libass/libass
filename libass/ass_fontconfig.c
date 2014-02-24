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

#ifdef CONFIG_FONTCONFIG

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "ass_fontconfig.h"
#include "ass_fontselect.h"
#include "ass_utils.h"

#define MAX_NAME 100

static int check_glyph(void *priv, uint32_t code)
{
    FcPattern *pat = (FcPattern *)priv;
    FcCharSet *charset;

    if (!pat)
        return 1;

    if (code == 0)
        return 1;

    FcResult result = FcPatternGetCharSet(pat, FC_CHARSET, 0, &charset);
    if (result != FcResultMatch)
        return 0;
    if (FcCharSetHasChar(charset, code) == FcTrue)
        return 1;
    return 0;
}

static void destroy(void *priv)
{
    FcConfig *config = (FcConfig *)priv;
    FcConfigDestroy(config);
}

static void scan_fonts(FcConfig *config, ASS_FontProvider *provider)
{
    int i;
    FcFontSet *fonts;
    ASS_FontProviderMetaData meta;

    // get list of fonts
    fonts = FcConfigGetFonts(config, FcSetSystem);

    // fill font_info list
    for (i = 0; i < fonts->nfont; i++) {
        FcPattern *pat = fonts->fonts[i];
        FcBool outline;
        int index, weight;
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
        result |= FcPatternGetInteger(pat, FC_WEIGHT, 0, &weight);
        result |= FcPatternGetInteger(pat, FC_INDEX, 0, &index);
        if (result != FcResultMatch)
            continue;

        // fontconfig uses its own weight scale, apparently derived
        // from typographical weight. we're using truetype weights, so
        // convert appropriately
        if (weight <= FC_WEIGHT_LIGHT)
            meta.weight = FONT_WEIGHT_LIGHT;
        else if (weight <= FC_WEIGHT_MEDIUM)
            meta.weight = FONT_WEIGHT_MEDIUM;
        else
            meta.weight = FONT_WEIGHT_BOLD;

        // path
        result = FcPatternGetString(pat, FC_FILE, 0, (FcChar8 **)&path);
        if (result != FcResultMatch)
            continue;

        // read and strdup fullnames
        meta.n_family = 0;
        while (FcPatternGetString(pat, FC_FAMILY, meta.n_family,
                    (FcChar8 **)&families[meta.n_family]) == FcResultMatch
                    && meta.n_family < MAX_NAME)
            meta.n_family++;
        meta.families = families;

        // read and strdup fullnames
        meta.n_fullname = 0;
        while (FcPatternGetString(pat, FC_FULLNAME, meta.n_fullname,
                    (FcChar8 **)&fullnames[meta.n_fullname]) == FcResultMatch
                    && meta.n_fullname < MAX_NAME)
            meta.n_fullname++;
        meta.fullnames = fullnames;

        ass_font_provider_add_font(provider, &meta, path, index, NULL,
                                   (void *)pat);
    }
}

static ASS_FontProviderFuncs fontconfig_callbacks = {
    NULL,
    check_glyph,
    NULL,
    destroy,
    NULL,
};

ASS_FontProvider *
ass_fontconfig_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                            const char *config)
{
    int rc;
    FcConfig *fc_config;
    ASS_FontProvider *provider = NULL;

    // build and load fontconfig configuration
    fc_config = FcConfigCreate();
    rc = FcConfigParseAndLoad(fc_config, (unsigned char *) config, FcTrue);
    if (!rc) {
        ass_msg(lib, MSGL_WARN, "No usable fontconfig configuration "
                "file found, using fallback.");
        FcConfigDestroy(fc_config);
        fc_config = FcInitLoadConfig();
        rc++;
    }
    if (rc)
        FcConfigBuildFonts(fc_config);

    if (!rc || !fc_config) {
        ass_msg(lib, MSGL_FATAL,
                "No valid fontconfig configuration found!");
        FcConfigDestroy(fc_config);
        goto exit;
    }

    // create font provider
    provider = ass_font_provider_new(selector, &fontconfig_callbacks,
            (void *)fc_config);

    // scan fonts
    scan_fonts(fc_config, provider);

exit:
    return provider;
}

#endif

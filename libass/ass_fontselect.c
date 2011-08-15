/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include <iconv.h>

#include "ass_utils.h"
#include "ass.h"
#include "ass_library.h"
#include "ass_fontselect.h"
#include "ass_fontconfig.h"

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MAX_FULLNAME 100

// proposed structure for holding font data, used for collection
// and matching. strings are utf-8.
struct font_info {
    char *family;       // family name
    char **fullnames;   // list of localized fullnames (e.g. Arial Bold Italic)
    int n_fullname;

    int slant;
    int weight;

    // how to access this face
    char *path;
    int index;

    // similarity score
    unsigned score;

    // callbacks
    ASS_FontProviderFuncs funcs;

    // private data for callbacks
    void *priv;
};

struct font_selector {
    // fallbacks
    char *family_default;
    char *path_default;
    int index_default;

    // font database
    int n_font;
    int alloc_font;
    ASS_FontInfo *font_infos;

    // XXX: for now, manage a single provider
    ASS_FontProvider *provider;
};

struct font_provider {
    ASS_FontSelector *parent;
    ASS_FontProviderFuncs funcs;
    void *priv;
};

ASS_FontProvider *
ass_font_provider_new(ASS_FontSelector *selector, ASS_FontProviderFuncs *funcs,
                      void *priv)
{
    ASS_FontProvider *provider = calloc(1, sizeof(ASS_FontProvider));

    provider->parent   = selector;
    provider->funcs    = *funcs;
    provider->priv     = priv;

    return provider;
}

int
ass_font_provider_add_font(ASS_FontProvider *provider,
                           ASS_FontProviderMetaData *meta, const char *path,
                           unsigned int index, void *data)
{
    int i;
    ASS_FontSelector *selector = provider->parent;
    ASS_FontInfo *info;

    // TODO: sanity checks. do we have a path or valid get_face function?

    // check size
    if (selector->n_font >= selector->alloc_font) {
        selector->alloc_font = FFMAX(1, 2 * selector->alloc_font);
        selector->font_infos = realloc(selector->font_infos,
                selector->alloc_font * sizeof(ASS_FontInfo));
    }

    // copy over metadata
    info = selector->font_infos + selector->n_font;
    memset(info, 0, sizeof(ASS_FontInfo));

    info->slant       = meta->slant;
    info->weight      = meta->weight;
    info->family      = strdup(meta->family);
    info->n_fullname  = meta->n_fullname;
    info->fullnames   = calloc(meta->n_fullname, sizeof(char *));

    for (i = 0; i < info->n_fullname; i++)
        info->fullnames[i] = strdup(meta->fullnames[i]);

    if (path)
        info->path = strdup(path);

    info->index = index;
    info->priv  = data;
    info->funcs = provider->funcs;

    selector->n_font++;

    return 1;
}

void ass_font_provider_free(ASS_FontProvider *provider)
{
    // TODO: this should probably remove all fonts that belong
    // to this provider from the list

    if (provider->funcs.destroy_provider)
        provider->funcs.destroy_provider(provider->priv);
    free(provider);
}



/**
 * \brief Compare a font (a) against a font request (b). Records
 * a matching score - the lower the better.
 * \param a font
 * \param b font request
 * \return matching score
 */
static unsigned font_info_similarity(ASS_FontInfo *a, ASS_FontInfo *b)
{
    int i, j;
    unsigned similarity = 0;

    // compare fullnames
    // a matching fullname is very nice and instantly drops the score to zero
    similarity = 10000;
    for (i = 0; i < a->n_fullname; i++)
        for (j = 0; j < b->n_fullname; j++) {
            if (ABS(strcasecmp(a->fullnames[i], b->fullnames[j])) == 0)
                similarity = 0;
        }

    // if we don't have any match, compare fullnames against family
    // sometimes the family name is used similarly
    if (similarity > 0) {
        for (i = 0; i < b->n_fullname; i++) {
            if (ABS(strcasecmp(a->family, b->fullnames[i])) == 0)
                similarity = 0;
        }
    }

    // compare shortened family, if no fullname matches
    if (similarity > 0 && ABS(strcasecmp(a->family, b->family)) == 0)
        similarity = 1000;

    // nothing found? Try fallback fonts
    // XXX: add more than arial
    if (similarity > 1000 && ABS(strcasecmp(a->family, "Arial")) == 0)
        similarity = 2000;

    // compare slant
    similarity += ABS(a->slant - b->slant);

    // compare weight
    similarity += ABS(a->weight - b->weight);

    return similarity;
}

// calculate scores
static void font_info_req_similarity(ASS_FontInfo *font_infos, size_t len,
                                     ASS_FontInfo *req)
{
    int i;

    for (i = 0; i < len; i++)
        font_infos[i].score = font_info_similarity(&font_infos[i], req);
}

#if 1
// dump font information
static void font_info_dump(ASS_FontInfo *font_infos, size_t len)
{
    int i, j;

    // dump font infos
    for (i = 0; i < len; i++) {
        printf("font %d\n", i);
        printf("  family: '%s'\n", font_infos[i].family);
        printf("  fullnames: ");
        for (j = 0; j < font_infos[i].n_fullname; j++)
            printf("'%s' ", font_infos[i].fullnames[j]);
        printf("\n");
        printf("  slant: %d\n", font_infos[i].slant);
        printf("  weight: %d\n", font_infos[i].weight);
        printf("  path: %s\n", font_infos[i].path);
        printf("  index: %d\n", font_infos[i].index);
        printf("  score: %d\n", font_infos[i].score);

    }
}
#endif

static int font_info_compare(const void *av, const void *bv)
{
    const ASS_FontInfo *a = av;
    const ASS_FontInfo *b = bv;

    return a->score - b->score;
}

static char *select_font(ASS_FontSelector *priv, ASS_Library *library,
                          const char *family, unsigned bold,
                          unsigned italic, int *index, uint32_t code)
{
    int num_fonts = priv->n_font;
    ASS_FontInfo *font_infos = priv->font_infos;
    ASS_FontInfo req;
    char *req_fullname;

    // do we actually have any fonts?
    if (!priv->n_font)
        return NULL;

    // fill font request
    memset(&req, 0, sizeof(ASS_FontInfo));
    req.slant   = italic;
    req.weight  = bold;
    req.n_fullname   = 1;
    req.fullnames    = &req_fullname;
    req.fullnames[0] = trim_space(strdup(family));
    req.family       = trim_space(strdup(family));
    char *p = strchr(req.family, ' ');
    if (p) *p = 0;

    // calculate similarities
    font_info_req_similarity(font_infos, num_fonts, &req);

    // sort
    qsort(font_infos, num_fonts, sizeof(ASS_FontInfo),
            font_info_compare);

    // check glyph coverage
    int info_index = 0;
    while (info_index < priv->n_font && font_infos[info_index].funcs.check_glyph
            && font_infos[info_index].funcs.check_glyph(font_infos[info_index].priv, code) == 0)
        info_index++;

    free(req.fullnames[0]);
    free(req.family);

    // return best match
    if (!font_infos[info_index].path)
        return NULL;
    *index = font_infos[info_index].index;
    return strdup(font_infos[info_index].path);
}


/**
 * \brief Find a font. Use default family or path if necessary.
 * \param library ASS library handle
 * \param family font family
 * \param treat_family_as_pattern treat family as fontconfig pattern
 * \param bold font weight value
 * \param italic font slant value
 * \param index out: font index inside a file
 * \param code: the character that should be present in the font, can be 0
 * \return font file path
*/
char *ass_font_select(ASS_FontSelector *priv, ASS_Library *library,
                      const char *family, unsigned bold, unsigned italic,
                      int *index, uint32_t code)
{
    char *res = 0;

    if (family && *family)
        res = select_font(priv, library, family, bold, italic, index, code);

    if (!res && priv->family_default) {
        res = select_font(priv, library, priv->family_default, bold,
                italic, index, code);
        if (res)
            ass_msg(library, MSGL_WARN, "fontselect: Using default "
                    "font family: (%s, %d, %d) -> %s, %d",
                    family, bold, italic, res, *index);
    }

    if (!res && priv->path_default) {
        res = strdup(priv->path_default);
        *index = priv->index_default;
        ass_msg(library, MSGL_WARN, "fontselect: Using default font: "
                "(%s, %d, %d) -> %s, %d", family, bold, italic,
                res, *index);
    }

    if (!res) {
        res = select_font(priv, library, "Arial", bold, italic,
                           index, code);
        if (res)
            ass_msg(library, MSGL_WARN, "fontselect: Using 'Arial' "
                    "font family: (%s, %d, %d) -> %s, %d", family, bold,
                    italic, res, *index);
    }

    if (res)
        ass_msg(library, MSGL_V,
                "fontselect: (%s, %d, %d) -> %s, %d", family, bold,
                italic, res, *index);

    return res;
}

static int get_font_info(FT_Library lib, FT_Face face, ASS_FontInfo *info)
{
    int i;
    int num_fullname = 0;
    int num_names = FT_Get_Sfnt_Name_Count(face);
    int slant, weight;
    char *fullnames[100];
    char *family = NULL;
    iconv_t utf16to8;

    // we're only interested in outlines
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
        return 0;

    // scan font names
    utf16to8 = iconv_open("UTF-8", "UTF-16BE");
    for (i = 0; i < num_names && num_fullname < 100; i++) {
        FT_SfntName name;
        FT_Get_Sfnt_Name(face, i, &name);
        //printf("name %d pid %d eid %d lid %d nameid %d\n",
        //        i, name.platform_id, name.encoding_id, name.language_id, name.name_id);
        // we add both full names and alternate family names to the list of full names
        if (name.platform_id == 3 && (name.name_id == 4 || name.name_id == 1)) {
            char buf[1024];
            char *bufptr = buf;
            size_t inbytes = name.string_len;
            size_t outbytes = 1024;
            iconv(utf16to8, (char**)&name.string, &inbytes, &bufptr, &outbytes);
            *bufptr = '\0';
            // no primary family name yet - just use the first we encounter as a best guess
            if (family == NULL && name.name_id == 1) {
                family = strdup(buf);
                continue;
            }
            fullnames[num_fullname] = strdup(buf);
            num_fullname++;
        }
    }
    iconv_close(utf16to8);

    // check if we got a valid family - if not use whatever FreeType gives us
    if (family == NULL)
        family = strdup(face->family_name);

    // calculate sensible slant and weight from style attributes
    slant  = 110 * !!(face->style_flags & FT_STYLE_FLAG_ITALIC);
    weight = 120 * !!(face->style_flags & FT_STYLE_FLAG_BOLD) + 80;

    // fill our struct
    info->family = family;
    info->slant  = slant;
    info->weight = weight;
    info->fullnames = calloc(sizeof(char *), num_fullname);
    memcpy(info->fullnames, &fullnames, sizeof(char *) * num_fullname);
    info->n_fullname = num_fullname;

    return 1;
}

/**
 * \brief Process memory font.
 * \param priv private data
 * \param library library object
 * \param ftlibrary freetype library object
 * \param idx index of the processed font in library->fontdata
 *
 * Builds a font pattern in memory via FT_New_Memory_Face/FcFreeTypeQueryFace.
*/
static void process_fontdata(ASS_FontSelector *priv, ASS_Library *library,
                             FT_Library ftlibrary, int idx)
{
    int rc;
    const char *name = library->fontdata[idx].name;
    const char *data = library->fontdata[idx].data;
    int data_size = library->fontdata[idx].size;

    FT_Face face;
    int face_index, num_faces = 1;

    for (face_index = 0; face_index < num_faces; ++face_index) {
        rc = FT_New_Memory_Face(ftlibrary, (unsigned char *) data,
                                data_size, face_index, &face);
        if (rc) {
            ass_msg(library, MSGL_WARN, "Error opening memory font: %s",
                   name);
            return;
        }
        num_faces = face->num_faces;

        // get font metadata and add to list
        ASS_FontInfo info;
        memset(&info, 0, sizeof(ASS_FontInfo));
        if (!get_font_info(ftlibrary, face, &info))
            continue;
        info.index = face_index;
        info.path  = strdup(name);

        priv->font_infos = realloc(priv->font_infos, sizeof(ASS_FontInfo) * (priv->n_font + 1));
        memcpy(priv->font_infos + priv->n_font, &info, sizeof(ASS_FontInfo));
        priv->n_font++;

        FT_Done_Face(face);
    }
}

/**
 * \brief Init font selector.
 * \param library libass library object
 * \param ftlibrary freetype library object
 * \param family default font family
 * \param path default font path
 * \return newly created font selector
 */
ASS_FontSelector *
ass_fontselect_init(ASS_Library *library,
                    FT_Library ftlibrary, const char *family,
                    const char *path)
{
    int i;
    ASS_FontSelector *priv = calloc(1, sizeof(ASS_FontSelector));

    priv->family_default = family ? strdup(family) : NULL;
    priv->path_default = path ? strdup(path) : NULL;
    priv->index_default = 0;

    // XXX: for now, always add the fontconfig provider
    priv->provider = ass_fontconfig_add_provider(library, priv, NULL);

    // XXX: use a real font provider for this
    for (i = 0; i < library->num_fontdata; ++i)
        process_fontdata(priv, library, ftlibrary, i);

    return priv;
}

/**
 * \brief Free font selector and release associated data
 *
 */
void ass_fontselect_free(ASS_FontSelector *priv)
{
    int i;

    if (priv) {
        for (i = 0; i < priv->n_font; i++) {
            ASS_FontInfo *info = priv->font_infos + i;
            int j;
            for (j = 0; j < info->n_fullname; j++)
                free(info->fullnames[j]);
            free(info->fullnames);
            free(info->family);
            if (info->path)
                free(info->path);
            if (info->funcs.destroy_font)
                info->funcs.destroy_font(info->priv);
        }
        free(priv->font_infos);
        free(priv->path_default);
        free(priv->family_default);
    }

    // TODO: we should track all child font providers and
    // free them here
    ass_font_provider_free(priv->provider);

    free(priv);
}
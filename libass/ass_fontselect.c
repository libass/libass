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
#include "ass_coretext.h"
#include "ass_directwrite.h"
#include "ass_font.h"

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MAX_FULLNAME 100

static const char *fallback_fonts[] = {
    // generic latin sans-serif fonts
    "Arial",
    "DejaVu Sans",
    // fonts with lots of coverage
    "Arial Unicode MS",
    "FreeSerif",
    NULL
};

// internal font database element
// all strings are utf-8
struct font_info {
    int uid;            // unique font face id

    char **families;    // family name
    char **fullnames;   // list of localized fullnames (e.g. Arial Bold Italic)
    int n_family;
    int n_fullname;

    int slant;
    int weight;         // TrueType scale, 100-900
    int width;

    // how to access this face
    char *path;            // absolute path
    int index;             // font index inside font collections
    char *postscript_name; // can be used as an alternative to index to
                           // identify a font inside a collection

    // similarity score
    unsigned score;

    // font source
    ASS_FontProvider *provider;

    // private data for callbacks
    void *priv;
};

struct font_selector {
    // uid counter
    int uid;

    // fallbacks
    char *family_default;
    char *path_default;
    int index_default;

    // font database
    int n_font;
    int alloc_font;
    ASS_FontInfo *font_infos;

    ASS_FontProvider *default_provider;
    ASS_FontProvider *embedded_provider;
};

struct font_provider {
    ASS_FontSelector *parent;
    ASS_FontProviderFuncs funcs;
    void *priv;
};

// simple glyph coverage map
typedef struct coverage_map CoverageMap;
struct coverage_map {
    int n_codepoint;
    uint32_t *codepoints;
};

typedef struct font_data_ft FontDataFT;
struct font_data_ft {
    ASS_Library *lib;
    CoverageMap *coverage;
    char *name;
    int idx;
};

static int check_glyph_ft(void *data, uint32_t codepoint)
{
    int i;
    CoverageMap *coverage = ((FontDataFT *)data)->coverage;

    if (!codepoint)
        return 1;

    // XXX: sort at map creation and use bsearch here - is this worth it?
    for (i = 0; i < coverage->n_codepoint; i++)
        if (coverage->codepoints[i] == codepoint)
            return 1;

    return 0;
}

static void coverage_map_destroy(void *data)
{
    CoverageMap *coverage = (CoverageMap *)data;

    free(coverage->codepoints);
    free(coverage);
}

static void destroy_font_ft(void *data)
{
    FontDataFT *fd = (FontDataFT *)data;

    if (fd->coverage)
        coverage_map_destroy(fd->coverage);

    free(fd->name);
    free(fd);
}

/**
 * \brief find a memory font by name
 * \param library ASS library
 * \param name font name
 */
static int find_font(ASS_Library *library, char *name)
{
    int i;
    for (i = 0; i < library->num_fontdata; ++i)
        if (strcasecmp(name, library->fontdata[i].name) == 0)
            return i;
    return -1;
}

static size_t
get_data_embedded(void *data, unsigned char *buf, size_t offset, size_t len)
{
    FontDataFT *ft = (FontDataFT *)data;
    ASS_Fontdata *fd = ft->lib->fontdata;
    int i = ft->idx;

    if (ft->idx < 0)
        ft->idx = i = find_font(ft->lib, ft->name);

    if (buf == NULL)
        return fd[i].size;

    memcpy(buf, fd[i].data + offset, len);
    return len;
}

static ASS_FontProviderFuncs ft_funcs = {
    get_data_embedded,
    check_glyph_ft,
    destroy_font_ft,
    NULL,
    NULL,
};

/**
 * \brief Create a bare font provider.
 * \param selector parent selector. The provider will be attached to it.
 * \param funcs callback/destroy functions
 * \param data private data of the provider
 * \return the font provider
 */
ASS_FontProvider *
ass_font_provider_new(ASS_FontSelector *selector, ASS_FontProviderFuncs *funcs,
                      void *data)
{
    ASS_FontProvider *provider = calloc(1, sizeof(ASS_FontProvider));

    provider->parent   = selector;
    provider->funcs    = *funcs;
    provider->priv     = data;

    return provider;
}

/**
 * \brief Add a font to a font provider.
 * \param provider the font provider
 * \param meta basic metadata of the font
 * \param path path to the font file, or NULL
 * \param index face index inside the file
 * \param psname PostScript name of the face (overrides index if present)
 * \param data private data for the font
 * \return success
 */
int
ass_font_provider_add_font(ASS_FontProvider *provider,
                           ASS_FontProviderMetaData *meta, const char *path,
                           unsigned int index, const char *psname, void *data)
{
    int i;
    int weight, slant, width;
    ASS_FontSelector *selector = provider->parent;
    ASS_FontInfo *info;

#if 0
    int j;
    printf("new font:\n");
    printf("  families: ");
    for (j = 0; j < meta->n_family; j++)
        printf("'%s' ", meta->families[j]);
    printf("\n");
    printf("  fullnames: ");
    for (j = 0; j < meta->n_fullname; j++)
        printf("'%s' ", meta->fullnames[j]);
    printf("\n");
    printf("  slant: %d\n", meta->slant);
    printf("  weight: %d\n", meta->weight);
    printf("  width: %d\n", meta->width);
    printf("  path: %s\n", path);
    printf("  index: %d\n", index);
#endif

    weight = meta->weight;
    slant  = meta->slant;
    width  = meta->width;

    // check slant/weight for validity, use defaults if they're invalid
    if (weight < 100 || weight > 900)
        weight = 400;
    if (slant < 0 || slant > 110)
        slant = 0;
    if (width < 50 || width > 200)
        width = 100;

    // check size
    if (selector->n_font >= selector->alloc_font) {
        selector->alloc_font = FFMAX(1, 2 * selector->alloc_font);
        selector->font_infos = realloc(selector->font_infos,
                selector->alloc_font * sizeof(ASS_FontInfo));
    }

    // copy over metadata
    info = selector->font_infos + selector->n_font;
    memset(info, 0, sizeof(ASS_FontInfo));

    // set uid
    info->uid = selector->uid++;

    info->slant       = slant;
    info->weight      = weight;
    info->width       = width;
    info->n_fullname  = meta->n_fullname;
    info->n_family    = meta->n_family;
    info->fullnames   = calloc(meta->n_fullname, sizeof(char *));
    info->families    = calloc(meta->n_family, sizeof(char *));

    for (i = 0; i < info->n_family; i++)
        info->families[i] = strdup(meta->families[i]);

    for (i = 0; i < info->n_fullname; i++)
        info->fullnames[i] = strdup(meta->fullnames[i]);

    if (path)
        info->path = strdup(path);

    if (psname)
        info->postscript_name = strdup(psname);

    info->index = index;
    info->priv  = data;
    info->provider = provider;

    selector->n_font++;

    return 1;
}

/**
 * \brief Clean up font database. Deletes all fonts that have an invalid
 * font provider (NULL).
 * \param selector the font selector
 */
static void ass_fontselect_cleanup(ASS_FontSelector *selector)
{
    int i, w;

    for (i = 0, w = 0; i < selector->n_font; i++) {
        ASS_FontInfo *info = selector->font_infos + i;

        // update write pointer
        if (info->provider != NULL) {
            // rewrite, if needed
            if (w != i)
                memcpy(selector->font_infos + w, selector->font_infos + i,
                        sizeof(ASS_FontInfo));
            w++;
        }

    }

    selector->n_font = w;
}

void ass_font_provider_free(ASS_FontProvider *provider)
{
    int i, j;
    ASS_FontSelector *selector = provider->parent;

    // free all fonts and mark their entries
    for (i = 0; i < selector->n_font; i++) {
        ASS_FontInfo *info = selector->font_infos + i;

        if (info->provider == provider) {
            for (j = 0; j < info->n_fullname; j++)
                free(info->fullnames[j]);
            for (j = 0; j < info->n_family; j++)
                free(info->families[j]);

            free(info->fullnames);
            free(info->families);

            if (info->path)
                free(info->path);

            if (info->postscript_name)
                free(info->postscript_name);

            if (info->provider->funcs.destroy_font)
                info->provider->funcs.destroy_font(info->priv);

            info->provider = NULL;
        }

    }

    // delete marked entries
    ass_fontselect_cleanup(selector);

    // free private data of the provider
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
static unsigned font_info_similarity(ASS_FontInfo *a, ASS_FontInfo *req)
{
    int i, j;
    unsigned similarity = 0;

    // compare fullnames
    // a matching fullname is very nice and instantly drops the score to zero
    similarity = 10000;
    for (i = 0; i < a->n_fullname; i++)
        for (j = 0; j < req->n_fullname; j++) {
            if (strcasecmp(a->fullnames[i], req->fullnames[j]) == 0)
                similarity = 0;
        }

    // if we don't have any match, compare fullnames against family
    // sometimes the family name is used similarly
    if (similarity > 0) {
        for (i = 0; i < a->n_family; i++)
            for (j = 0; j < req->n_fullname; j++) {
                if (strcasecmp(a->families[i], req->fullnames[j]) == 0)
                    similarity = 0;
            }
    }

    // compare slant
    similarity += ABS(a->slant - req->slant);

    // compare weight
    similarity += ABS(a->weight - req->weight);

    // compare width
    similarity += ABS(a->width - req->width);

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

#if 0
// dump font information
static void font_info_dump(ASS_FontInfo *font_infos, size_t len)
{
    int i, j;

    // dump font infos
    for (i = 0; i < len; i++) {
        printf("font %d\n", i);
        printf("  families: ");
        for (j = 0; j < font_infos[i].n_family; j++)
            printf("'%s' ", font_infos[i].families[j]);
        printf("  fullnames: ");
        for (j = 0; j < font_infos[i].n_fullname; j++)
            printf("'%s' ", font_infos[i].fullnames[j]);
        printf("\n");
        printf("  slant: %d\n", font_infos[i].slant);
        printf("  weight: %d\n", font_infos[i].weight);
        printf("  width: %d\n", font_infos[i].width);
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
                         const char *family, unsigned bold, unsigned italic,
                         int *index, char **postscript_name, int *uid,
                         ASS_FontStream *stream, uint32_t code)
{
    int num_fonts = priv->n_font;
    int idx = 0;
    ASS_FontInfo req;
    char *req_fullname;
    char *tfamily = trim_space(strdup(family));

    ASS_FontProvider *default_provider = priv->default_provider;
    if (default_provider && default_provider->funcs.match_fonts)
        default_provider->funcs.match_fonts(library, default_provider, tfamily);

    ASS_FontInfo *font_infos = priv->font_infos;

    // do we actually have any fonts?
    if (!priv->n_font)
        return NULL;

    // fill font request
    memset(&req, 0, sizeof(ASS_FontInfo));
    req.slant   = italic;
    req.weight  = bold;
    req.width   = 100;
    req.n_fullname   = 1;
    req.fullnames    = &req_fullname;
    req.fullnames[0] = tfamily;

    // calculate similarities
    font_info_req_similarity(font_infos, num_fonts, &req);

    // sort
    qsort(font_infos, num_fonts, sizeof(ASS_FontInfo),
            font_info_compare);

    // check glyph coverage
    while (idx < priv->n_font) {
        ASS_FontProvider *provider = font_infos[idx].provider;
        if (!provider || !provider->funcs.check_glyph) {
            idx++;
            continue;
        }
        if (provider->funcs.check_glyph(font_infos[idx].priv, code) > 0)
            break;
        idx++;
    }

    free(req.fullnames[0]);

    *postscript_name = font_infos[idx].postscript_name;
    *index = font_infos[idx].index;
    *uid   = font_infos[idx].uid;

    // nothing found
    if (idx == priv->n_font)
        return NULL;

    // if there is no valid path, this is a memory stream font
    if (font_infos[idx].path == NULL) {
        ASS_FontProvider *provider = font_infos[idx].provider;
        stream->func = provider->funcs.get_data;
        stream->priv = font_infos[idx].priv;
        // FIXME: we should define a default family name in some way,
        // possibly the first (or last) English name
        return strdup(font_infos[idx].families[0]);
    } else
        return strdup(font_infos[idx].path);
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
                      ASS_Font *font, int *index, char **postscript_name,
                      int *uid, ASS_FontStream *data, uint32_t code)
{
    char *res = 0;
    const char *family = font->desc.family;
    unsigned bold = font->desc.bold;
    unsigned italic = font->desc.italic;

    if (family && *family)
        res = select_font(priv, library, family, bold, italic, index,
                postscript_name, uid, data, code);

    if (!res && priv->family_default) {
        res = select_font(priv, library, priv->family_default, bold,
                italic, index, postscript_name, uid, data, code);
        if (res)
            ass_msg(library, MSGL_WARN, "fontselect: Using default "
                    "font family: (%s, %d, %d) -> %s, %d, %s",
                    family, bold, italic, res, *index, *postscript_name);
    }

    if (!res && priv->path_default) {
        res = strdup(priv->path_default);
        *index = priv->index_default;
        ass_msg(library, MSGL_WARN, "fontselect: Using default font: "
                "(%s, %d, %d) -> %s, %d, %s", family, bold, italic,
                res, *index, *postscript_name);
    }

    if (!res) {
        // This code path is reached when the script uses glyphs not
        // available in the previous fonts (or no font is matched), and
        // the ASS_FontProvider used provides only the MatchFontsFunc callback
        for (int i = 0; fallback_fonts[i] && !res; i++) {
            res = select_font(priv, library, fallback_fonts[i], bold,
                    italic, index, postscript_name, uid, data, code);
            if (res)
                ass_msg(library, MSGL_WARN, "fontselect: Using fallback "
                        "font family: (%s, %d, %d) -> %s, %d, %s",
                        family, bold, italic, res, *index, *postscript_name);
        }
    }

    if (res)
        ass_msg(library, MSGL_V,
                "fontselect: (%s, %d, %d) -> %s, %d, %s", family, bold,
                italic, res, *index, *postscript_name);

    return res;
}


/**
 * \brief Read basic metadata (names, weight, slant) from a FreeType face,
 * as required for the FontSelector for matching and sorting.
 * \param lib FreeType library
 * \param face FreeType face
 * \param info metadata, returned here
 * \return success
 */
static int
get_font_info(FT_Library lib, FT_Face face, ASS_FontProviderMetaData *info)
{
    int i;
    int num_fullname = 0;
    int num_family   = 0;
    int num_names = FT_Get_Sfnt_Name_Count(face);
    int slant, weight;
    char *fullnames[MAX_FULLNAME];
    char *families[MAX_FULLNAME];
    iconv_t utf16to8;

    // we're only interested in outlines
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
        return 0;

    // scan font names
    utf16to8 = iconv_open("UTF-8", "UTF-16BE");
    for (i = 0; i < num_names; i++) {
        FT_SfntName name;

        FT_Get_Sfnt_Name(face, i, &name);

        if (name.platform_id == 3 && (name.name_id == 4 || name.name_id == 1)) {
            char buf[1024];
            char *bufptr = buf;
            size_t inbytes = name.string_len;
            size_t outbytes = 1024;

            iconv(utf16to8, (char**)&name.string, &inbytes, &bufptr, &outbytes);
            *bufptr = '\0';

            if (name.name_id == 4) {
                fullnames[num_fullname] = strdup(buf);
                num_fullname++;
            }

            if (name.name_id == 1) {
                families[num_family] = strdup(buf);
                num_family++;
            }
        }

    }
    iconv_close(utf16to8);

    // check if we got a valid family - if not use whatever FreeType gives us
    if (num_family == 0 && face->family_name) {
        families[0] = strdup(face->family_name);
        num_family++;
    }

    // calculate sensible slant and weight from style attributes
    slant  = 110 * !!(face->style_flags & FT_STYLE_FLAG_ITALIC);
    weight = 300 * !!(face->style_flags & FT_STYLE_FLAG_BOLD) + 400;

    // fill our struct
    info->slant  = slant;
    info->weight = weight;
    info->width  = 100;     // FIXME, should probably query the OS/2 table
    info->families  = calloc(sizeof(char *), num_family);
    info->fullnames = calloc(sizeof(char *), num_fullname);
    memcpy(info->families, &families, sizeof(char *) * num_family);
    memcpy(info->fullnames, &fullnames, sizeof(char *) * num_fullname);
    info->n_family   = num_family;
    info->n_fullname = num_fullname;

    return 1;
}

/**
 * \brief Free the dynamically allocated fields of metadata
 * created by get_font_info.
 * \param meta metadata created by get_font_info
 */
static void free_font_info(ASS_FontProviderMetaData *meta)
{
    int i;

    for (i = 0; i < meta->n_family; i++)
        free(meta->families[i]);

    for (i = 0; i < meta->n_fullname; i++)
        free(meta->fullnames[i]);

    free(meta->families);
    free(meta->fullnames);
}

/**
 * \brief Calculate a coverage map (array with codepoints) from a FreeType
 * face. This can be used to check glyph coverage quickly.
 * \param face FreeType face
 * \return CoverageMap structure
 */
static CoverageMap *get_coverage_map(FT_Face face)
{
    int i = 0;
    int n_codepoint = 0;
    uint32_t codepoint;
    unsigned index;
    CoverageMap *coverage = calloc(1, sizeof(CoverageMap));

    // determine number of codepoints first
    codepoint = FT_Get_First_Char(face, &index);
    while (index) {
        n_codepoint++;
        codepoint = FT_Get_Next_Char(face, codepoint, &index);
    }

    coverage->codepoints = calloc(n_codepoint, sizeof(uint32_t));
    codepoint = FT_Get_First_Char(face, &index);
    while (index) {
        coverage->codepoints[i++] = codepoint;
        codepoint = FT_Get_Next_Char(face, codepoint, &index);
    }

    coverage->n_codepoint = n_codepoint;

    return coverage;
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
static void process_fontdata(ASS_FontProvider *priv, ASS_Library *library,
                             FT_Library ftlibrary, int idx)
{
    int rc;
    const char *name = library->fontdata[idx].name;
    const char *data = library->fontdata[idx].data;
    int data_size = library->fontdata[idx].size;

    FT_Face face;
    int face_index, num_faces = 1;

    for (face_index = 0; face_index < num_faces; ++face_index) {
        ASS_FontProviderMetaData info;
        FontDataFT *ft;

        rc = FT_New_Memory_Face(ftlibrary, (unsigned char *) data,
                                data_size, face_index, &face);
        if (rc) {
            ass_msg(library, MSGL_WARN, "Error opening memory font: %s",
                   name);
            return;
        }

        num_faces = face->num_faces;

        charmap_magic(library, face);

        memset(&info, 0, sizeof(ASS_FontProviderMetaData));
        if (!get_font_info(ftlibrary, face, &info)) {
            FT_Done_Face(face);
            continue;
        }

        ft = calloc(1, sizeof(FontDataFT));
        ft->lib      = library;
        ft->coverage = get_coverage_map(face);
        ft->name     = strdup(name);
        ft->idx      = -1;

        ass_font_provider_add_font(priv, &info, NULL, face_index, NULL, ft);

        free_font_info(&info);
        FT_Done_Face(face);
    }
}

/**
 * \brief Create font provider for embedded fonts. This parses the fonts known
 * to the current ASS_Library and adds them to the selector.
 * \param lib library
 * \param selector font selector
 * \param ftlib FreeType library - used for querying fonts
 * \return font provider
 */
static ASS_FontProvider *
ass_embedded_fonts_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                                FT_Library ftlib)
{
    int i;
    ASS_FontProvider *priv = ass_font_provider_new(selector, &ft_funcs, NULL);

    for (i = 0; i < lib->num_fontdata; ++i)
        process_fontdata(priv, lib, ftlib, i);

    return priv;
}

struct font_constructors {
    ASS_DefaultFontProvider id;
    ASS_FontProvider *(*constructor)(ASS_Library *, ASS_FontSelector *,
                                     const char *);
};

struct font_constructors font_constructors[] = {
#ifdef CONFIG_CORETEXT
    { ASS_FONTPROVIDER_CORETEXT,   &ass_coretext_add_provider },
#endif
#ifdef CONFIG_FONTCONFIG
    { ASS_FONTPROVIDER_FONTCONFIG, &ass_fontconfig_add_provider },
#endif
#ifdef CONFIG_DIRECTWRITE
    { ASS_FONTPROVIDER_DIRECTWRITE, &ass_directwrite_add_provider },
#endif
    { ASS_FONTPROVIDER_NONE, NULL },
};

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
                    const char *path, const char *config,
                    ASS_DefaultFontProvider dfp)
{
    ASS_FontSelector *priv = calloc(1, sizeof(ASS_FontSelector));

    priv->uid = 1;
    priv->family_default = family ? strdup(family) : NULL;
    priv->path_default = path ? strdup(path) : NULL;
    priv->index_default = 0;

    priv->embedded_provider = ass_embedded_fonts_add_provider(library, priv,
            ftlibrary);

    if (dfp >= ASS_FONTPROVIDER_AUTODETECT) {
        int found = 0;
        for (int i = 0; !found && font_constructors[i].constructor; i++ )
            if (dfp == font_constructors[i].id ||
                dfp == ASS_FONTPROVIDER_AUTODETECT) {
                priv->default_provider =
                    font_constructors[i].constructor(library, priv, config);
                found = 1;
            }

        if (!found)
            ass_msg(library, MSGL_WARN, "can't find selected font provider");

    }

    return priv;
}

void ass_get_available_font_providers(ASS_Library *priv,
                                      ASS_DefaultFontProvider **providers,
                                      size_t *size)
{
    size_t offset = 2;
    *size = offset;
    for (int i = 0; font_constructors[i].constructor; i++)
        (*size)++;
    *providers = calloc(*size, sizeof(ASS_DefaultFontProvider));
    (*providers)[0] = ASS_FONTPROVIDER_NONE;
    (*providers)[1] = ASS_FONTPROVIDER_AUTODETECT;
    for (int i = offset; i < *size; i++)
        (*providers)[i] = font_constructors[i-offset].id;
}

/**
 * \brief Free font selector and release associated data
 * \param the font selector
 */
void ass_fontselect_free(ASS_FontSelector *priv)
{
    if (priv->default_provider)
        ass_font_provider_free(priv->default_provider);
    ass_font_provider_free(priv->embedded_provider);

    // XXX: not quite sure, maybe we should track all registered
    // providers and free them right here. or should that be the
    // responsibility of the library user?

    free(priv->font_infos);
    free(priv->path_default);
    free(priv->family_default);

    free(priv);
}

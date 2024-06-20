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
#include "ass_compat.h"

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <limits.h>
#include <ft2build.h>
#include <sys/types.h>
#include FT_FREETYPE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#include "ass_utils.h"
#include "ass.h"
#include "ass_library.h"
#include "ass_filesystem.h"
#include "ass_fontselect.h"
#include "ass_fontconfig.h"
#include "ass_coretext.h"
#include "ass_directwrite.h"
#include "ass_font.h"
#include "ass_string.h"

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MAX_FULLNAME 100

// internal font database element
// all strings are utf-8
struct font_info {
    int uid;            // unique font face id

    char **families;    // family name
    int n_family;

    FT_Long style_flags;
    int weight;           // TrueType scale, 100-900

    // how to access this face
    char *path;            // absolute path
    int index;             // font index inside font collections

    char *postscript_name; // can be used as an alternative to index to
                           // identify a font inside a collection

    char *extended_family;

    // font source
    ASS_FontProvider *provider;

    // private data for callbacks
    void *priv;
};

struct font_selector {
    ASS_Library *library;
    FT_Library ftlibrary;

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

typedef struct font_data_ft FontDataFT;
struct font_data_ft {
    ASS_Library *lib;
    FT_Face face;
    int idx;
};

static bool check_glyph_ft(void *data, uint32_t codepoint)
{
    FontDataFT *fd = (FontDataFT *)data;

    if (!codepoint)
        return true;

    return !!FT_Get_Char_Index(fd->face, codepoint);
}

static void destroy_font_ft(void *data)
{
    FontDataFT *fd = (FontDataFT *)data;

    FT_Done_Face(fd->face);
    free(fd);
}

static size_t
get_data_embedded(void *data, unsigned char *buf, size_t offset, size_t len)
{
    FontDataFT *ft = (FontDataFT *)data;
    ASS_Fontdata *fd = ft->lib->fontdata;
    int i = ft->idx;

    if (buf == NULL)
        return fd[i].size;

    if (offset >= fd[i].size)
        return 0;

    if (len > fd[i].size - offset)
        len = fd[i].size - offset;

    memcpy(buf, fd[i].data + offset, len);
    return len;
}

static ASS_FontProviderFuncs ft_funcs = {
    .get_data          = get_data_embedded,
    .check_glyph       = check_glyph_ft,
    .destroy_font      = destroy_font_ft,
};

static void load_fonts_from_dir(ASS_Library *library, const char *dir)
{
    ASS_Dir d;
    if (!ass_open_dir(&d, dir))
        return;
    while (true) {
        const char *name = ass_read_dir(&d);
        if (!name)
            break;
        if (name[0] == '.')
            continue;
        const char *path = ass_current_file_path(&d);
        if (!path)
            continue;
        ass_msg(library, MSGL_INFO, "Loading font file '%s'", path);
        size_t size = 0;
        void *data = ass_load_file(library, path, FN_DIR_LIST, &size);
        if (data) {
            ass_add_font(library, name, data, size);
            free(data);
        }
    }
    ass_close_dir(&d);
}

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
    assert(funcs->check_glyph && funcs->destroy_font);

    ASS_FontProvider *provider = calloc(1, sizeof(ASS_FontProvider));
    if (provider == NULL)
        return NULL;

    provider->parent   = selector;
    provider->funcs    = *funcs;
    provider->priv     = data;

    return provider;
}

/**
 * Free all data associated with a FontInfo struct. Handles FontInfo structs
 * with incomplete allocations well.
 *
 * \param info FontInfo struct to free associated data from
 */
static void ass_font_provider_free_fontinfo(ASS_FontInfo *info)
{
    int j;

    if (info->families) {
        for (j = 0; j < info->n_family; j++)
            free(info->families[j]);
        free(info->families);
    }

    if (info->path)
        free(info->path);

    if (info->postscript_name)
        free(info->postscript_name);

    if (info->extended_family)
        free(info->extended_family);
}

/**
 * \brief Ensure space is available for a new FontInfo struct to be inserted,
 * and zero its contents.
 * The new item is only actually considered inserted when n_font is incremented.
 * \param selector font selector to insert into
 * \return zeroed FontInfo pointer, or NULL if allocation failed
 */
static ASS_FontInfo *allocate_font_info(ASS_FontSelector *selector)
{
    // check size
    if (selector->n_font >= selector->alloc_font) {
        size_t new_alloc = FFMAX(1, 2 * selector->alloc_font);
        ASS_FontInfo *new_infos = realloc(selector->font_infos, new_alloc * sizeof(ASS_FontInfo));
        if (!new_infos)
            return NULL;

        selector->alloc_font = new_alloc;
        selector->font_infos = new_infos;
    }

    ASS_FontInfo *info = selector->font_infos + selector->n_font;
    memset(info, 0, sizeof(ASS_FontInfo));

    return info;
}

/**
 * \brief Initialize ASS_FontInfo with string metadata (names, path) from a FreeType face.
 * \param info the FontInfo struct to set up
 * \param face FreeType face
 * \param fallback_family_name family name from outside source, used as last resort
 * \param path path to the font file, or NULL
 * \return success
 */
static bool
init_font_info(ASS_FontInfo *info, FT_Face face, const char *fallback_family_name,
               const char *path)
{
    int i, j;
    int num_names = FT_Get_Sfnt_Name_Count(face);
    bool is_ps = ass_face_is_postscript(face);

    for (i = 0; i < num_names; i++) {
        FT_SfntName name;

        if (FT_Get_Sfnt_Name(face, i, &name))
            continue;

        if (name.platform_id == TT_PLATFORM_MICROSOFT &&
            (name.name_id == TT_NAME_ID_FONT_FAMILY ||
             name.name_id == (is_ps ? TT_NAME_ID_PS_NAME : TT_NAME_ID_FULL_NAME)))
            info->n_family++;
    }

    if (info->n_family) {
        info->families = calloc(info->n_family, sizeof(char*));
        if (!info->families)
            goto error;
    } else if (!fallback_family_name) {
        goto error; // If we don't have any names at all, this is useless
    }

    for (i = 0, j = 0; i < num_names && j < info->n_family; i++) {
        FT_SfntName name;

        if (FT_Get_Sfnt_Name(face, i, &name))
            continue;

        if (name.platform_id == TT_PLATFORM_MICROSOFT &&
            (name.name_id == TT_NAME_ID_FONT_FAMILY ||
             name.name_id == (is_ps ? TT_NAME_ID_PS_NAME : TT_NAME_ID_FULL_NAME))) {
            size_t strsize = name.string_len * 3 + 1;
            info->families[j] = malloc(strsize);
            if (!info->families[j])
                goto error;

            ass_utf16be_to_utf8(info->families[j], strsize, (uint8_t *)name.string,
                                name.string_len);
            j++;
        }
    }

    if (j != info->n_family)
        goto error;

    if (fallback_family_name)
        info->extended_family = strdup(fallback_family_name);

    if (path) {
        info->path = strdup(path);
        if (!info->path)
            goto error;
    }

    return true;

error:
    for (i = 0; i < info->n_family; i++)
        free(info->families[i]);

    free(info->families);
    free(info->extended_family);
    free(info->path);

    return false;
}

/**
 * \brief Read basic metadata (names, weight, slant) from a FreeType face
 * and insert them into a provider's FontInfo list.
 * \param provider the font provider
 * \param face FreeType face
 * \param fallback_family_name family name from outside source, used as last resort
 * \param path path to the font file, or NULL
 * \param data private data for the font
 * \return success
 */
static bool
insert_ft_font(ASS_FontProvider *provider, FT_Face face, const char *fallback_family_name,
               const char *path, void *data)
{
    bool success = false;

    // we're only interested in outlines
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
        return false;

    ASS_FontInfo *info = allocate_font_info(provider->parent);
    if (!info)
        goto cleanup;

    if (!init_font_info(info, face, fallback_family_name, path))
        goto cleanup;

    // set non-allocated metadata
    info->weight = ass_face_get_weight(face);
    info->style_flags = ass_face_get_style_flags(face);
    info->index = (face->face_index & 0xFFFF);
    info->priv = data;
    info->provider = provider;

    // set uid
    info->uid = provider->parent->uid++;
    provider->parent->n_font++;

    success = true;

cleanup:
    if (!success)
        provider->funcs.destroy_font(data);

    return success;
}

/**
 * \brief Add a font to a font provider.
 * \param provider the font provider
 * \param meta basic metadata of the font
 * \param path path to the font file, or NULL
 * \param index face index inside the file (-1 to look up by PostScript name)
 * \param data private data for the font
 * \return success
 */
bool
ass_font_provider_add_font(ASS_FontProvider *provider,
                           ASS_FontProviderMetaData *meta, const char *path,
                           int index, void *data)
{
    ASS_FontSelector *selector = provider->parent;
    FT_Face face;

    if (!path) {
        assert(provider->funcs.get_data);
        ASS_FontStream stream = {
            .func = provider->funcs.get_data,
            .priv = data,
        };
        // This name is only used in an error message, so use
        // our best name but don't panic if we don't have any.
        // Prefer PostScript name because it is unique.
        const char *name = meta->postscript_name ?
            meta->postscript_name : meta->extended_family;
        face = ass_face_stream(selector->library, selector->ftlibrary,
                               name, &stream, index);
    } else {
        face = ass_face_open(selector->library, selector->ftlibrary,
                             path, meta->postscript_name, index);
    }

    if (!face)
        return false;

    bool ret = insert_ft_font(provider, face, meta->extended_family, path, data);

    FT_Done_Face(face);

    return ret;
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
    int i;
    ASS_FontSelector *selector = provider->parent;

    // free all fonts and mark their entries
    for (i = 0; i < selector->n_font; i++) {
        ASS_FontInfo *info = selector->font_infos + i;

        if (info->provider == provider) {
            ass_font_provider_free_fontinfo(info);
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
 * \brief Return whether the given font is in the given family.
 */
static bool matches_family_name(ASS_FontInfo *f, const char *family,
                                bool match_extended_family)
{
    for (int i = 0; i < f->n_family; i++) {
        if (ass_strcasecmp(f->families[i], family) == 0)
            return true;
    }
    if (match_extended_family && f->extended_family) {
        if (ass_strcasecmp(f->extended_family, family) == 0)
            return true;
    }
    return false;
}

/**
 * \brief Compare attributes of font (a) against a font request (req). Returns
 * a matching score - the lower the better.
 * Ignores font names/families!
 * \param a font
 * \param b font request
 * \return matching score
 */
static unsigned font_attributes_similarity(ASS_FontInfo *a, ASS_FontInfo *req)
{
    unsigned score = 0;

    // Assign score for italics mismatch
    if ((req->style_flags & FT_STYLE_FLAG_ITALIC) &&
        !(a->style_flags & FT_STYLE_FLAG_ITALIC))
        score += 1;
    else if (!(req->style_flags & FT_STYLE_FLAG_ITALIC) &&
             (a->style_flags & FT_STYLE_FLAG_ITALIC))
        score += 4;

    int a_weight = a->weight;

    // Offset effective weight for faux-bold (only if font isn't flagged as bold)
    if ((req->weight > a->weight + 150) && !(a->style_flags & FT_STYLE_FLAG_BOLD))
        a_weight += 120;

    // Assign score for weight mismatch
    score += (73 * ABS(a_weight - req->weight)) / 256;

    return score;
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
        printf("  path: %s\n", font_infos[i].path);
        printf("  index: %d\n", font_infos[i].index);
        printf("  score: %d\n", font_infos[i].score);

    }
}
#endif

static bool check_glyph(ASS_FontInfo *fi, uint32_t code)
{
    ASS_FontProvider *provider = fi->provider;
    assert(provider && provider->funcs.check_glyph);

    return provider->funcs.check_glyph(fi->priv, code);
}

static char *
find_font(ASS_FontSelector *priv,
          ASS_FontProviderMetaData meta, bool match_extended_family,
          unsigned bold, unsigned italic,
          int *index, char **postscript_name, int *uid, ASS_FontStream *stream,
          uint32_t code, bool *name_match)
{
    ASS_FontInfo req = {0};
    ASS_FontInfo *selected = NULL;

    // do we actually have any fonts?
    if (!priv->n_font)
        return NULL;

    // fill font request
    req.style_flags = (italic ? FT_STYLE_FLAG_ITALIC : 0);
    req.weight      = bold;

    // Match font family name against font list
    unsigned score_min = UINT_MAX;
    for (int i = 0; i < meta.n_fullname; i++) {
        const char *fullname = meta.fullnames[i];

        for (int x = 0; x < priv->n_font; x++) {
            ASS_FontInfo *font = &priv->font_infos[x];
            unsigned score = UINT_MAX;

            if (matches_family_name(font, fullname, match_extended_family)) {
                // If there's a family match, compare font attributes
                // to determine best match in that particular family
                score = font_attributes_similarity(font, &req);
                *name_match = true;
            }

            // Consider updating idx if score is better than current minimum
            if (score < score_min) {
                // Check if the font has the requested glyph.
                // We are doing this here, for every font face, because
                // coverage might differ between the variants of a font
                // family. In practice, it is common that the regular
                // style has the best coverage while bold/italic/etc
                // variants cover less (e.g. FreeSans family).
                // We want to be able to match even if the closest variant
                // does not have the requested glyph, but another member
                // of the family has the glyph.
                if (!check_glyph(font, code))
                    continue;

                score_min = score;
                selected = font;
            }

            // Lowest possible score instantly matches; this is typical
            // for fullname matches, but can also occur with family matches.
            if (score == 0)
                break;
        }

        // The list of names is sorted by priority. If we matched anything,
        // we can and should stop.
        if (selected != NULL)
            break;
    }

    // found anything?
    char *result = NULL;
    if (selected) {
        ASS_FontProvider *provider = selected->provider;

        // successfully matched, set up return values
        *postscript_name = selected->postscript_name;
        *uid   = selected->uid;
        *index = selected->index;

        // set up memory stream if there is no path
        if (selected->path == NULL) {
            stream->func = provider->funcs.get_data;
            stream->priv = selected->priv;
            // Prefer PostScript name because it is unique. This is only
            // used for display purposes so it doesn't matter that much,
            // though.
            if (selected->postscript_name)
                result = selected->postscript_name;
            else if (selected->n_family)
                result = selected->families[0];
            else
                result = selected->extended_family;
        } else
            result = selected->path;

    }

    return result;
}

static char *select_font(ASS_FontSelector *priv,
                         const char *family, bool match_extended_family,
                         unsigned bold, unsigned italic,
                         int *index, char **postscript_name, int *uid,
                         ASS_FontStream *stream, uint32_t code)
{
    ASS_FontProvider *default_provider = priv->default_provider;
    ASS_FontProviderMetaData meta = {0};
    char *result = NULL;
    bool name_match = false;

    if (family == NULL)
        return NULL;

    ASS_FontProviderMetaData default_meta = {
        .n_fullname = 1,
        .fullnames  = (char **)&family,
    };

    result = find_font(priv, meta, match_extended_family,
                       bold, italic, index, postscript_name, uid,
                       stream, code, &name_match);
    if (result && name_match)
        return result;

    if (default_provider && default_provider->funcs.match_fonts) {
        default_provider->funcs.match_fonts(default_provider->priv,
                                            priv->library, default_provider,
                                            family);

        result = find_font(priv, meta, match_extended_family,
                           bold, italic, index, postscript_name, uid,
                           stream, code, &name_match);

        if (result && name_match)
            return result;
    }

    // Get a list of substitutes if applicable, and use it for matching.
    if (default_provider && default_provider->funcs.get_substitutions) {
        default_provider->funcs.get_substitutions(default_provider->priv,
                                                  family, &meta);
    }

    if (!meta.n_fullname) {
        free(meta.fullnames);
        meta = default_meta;
    }

    result = find_font(priv, meta, true,
                       bold, italic, index, postscript_name, uid,
                       stream, code, &name_match);

    // If no matching font was found, it might not exist in the font list
    // yet. Call the match_fonts callback to fill in the missing fonts
    // on demand, and retry the search for a match.
    if (result == NULL && name_match == false && default_provider &&
            default_provider->funcs.match_fonts) {
        // TODO: consider changing the API to make more efficient
        // implementations possible.
        for (int i = 0; i < meta.n_fullname; i++) {
            default_provider->funcs.match_fonts(default_provider->priv,
                                                priv->library, default_provider,
                                                meta.fullnames[i]);
        }
        result = find_font(priv, meta, true,
                           bold, italic, index, postscript_name, uid,
                           stream, code, &name_match);
    }

    // cleanup
    if (meta.fullnames != default_meta.fullnames) {
        for (int i = 0; i < meta.n_fullname; i++)
            free(meta.fullnames[i]);
        free(meta.fullnames);
    }

    return result;
}


/**
 * \brief Find a font. Use default family or path if necessary.
 * \param family font family
 * \param treat_family_as_pattern treat family as fontconfig pattern
 * \param bold font weight value
 * \param italic font slant value
 * \param index out: font index inside a file
 * \param code: the character that should be present in the font, can be 0
 * \return font file path
*/
char *ass_font_select(ASS_FontSelector *priv,
                      const ASS_Font *font, int *index, char **postscript_name,
                      int *uid, ASS_FontStream *data, uint32_t code)
{
    char *res = 0;
    const char *family = font->desc.family.str;  // always zero-terminated
    unsigned bold = font->desc.bold;
    unsigned italic = font->desc.italic;
    ASS_FontProvider *default_provider = priv->default_provider;

    if (family && *family)
        res = select_font(priv, family, false, bold, italic, index,
                postscript_name, uid, data, code);

    if (!res && priv->family_default) {
        res = select_font(priv, priv->family_default, false, bold,
                italic, index, postscript_name, uid, data, code);
        if (res)
            ass_msg(priv->library, MSGL_WARN, "fontselect: Using default "
                    "font family: (%s, %d, %d) -> %s, %d, %s",
                    family, bold, italic, res, *index,
                    *postscript_name ? *postscript_name : "(none)");
    }

    if (!res && default_provider && default_provider->funcs.get_fallback) {
        const char *search_family = family;
        if (!search_family || !*search_family)
            search_family = "Arial";
        char *fallback_family = default_provider->funcs.get_fallback(
                default_provider->priv, priv->library, search_family, code);

        if (fallback_family) {
            res = select_font(priv, fallback_family, true, bold, italic,
                    index, postscript_name, uid, data, code);
            free(fallback_family);
        }
    }

    if (!res && priv->path_default) {
        res = priv->path_default;
        *index = priv->index_default;
        ass_msg(priv->library, MSGL_WARN, "fontselect: Using default font: "
                "(%s, %d, %d) -> %s, %d, %s", family, bold, italic,
                priv->path_default, *index,
                *postscript_name ? *postscript_name : "(none)");
    }

    if (res)
        ass_msg(priv->library, MSGL_INFO,
                "fontselect: (%s, %d, %d) -> %s, %d, %s", family, bold,
                italic, res, *index, *postscript_name ? *postscript_name : "(none)");
    else
        ass_msg(priv->library, MSGL_WARN,
                "fontselect: failed to find any fallback with glyph 0x%X for font: "
                "(%s, %d, %d)", code, family, bold, italic);

    return res;
}


/**
 * \brief Process memory font.
 * \param priv private data
 * \param idx index of the processed font in priv->library->fontdata
 *
 * Builds a FontInfo with FreeType and some table reading.
*/
static void process_fontdata(ASS_FontProvider *priv, int idx)
{
    ASS_FontSelector *selector = priv->parent;
    ASS_Library *library = selector->library;

    int rc;
    const char *name = library->fontdata[idx].name;
    const char *data = library->fontdata[idx].data;
    int data_size = library->fontdata[idx].size;

    FT_Face face;
    int face_index, num_faces = 1;

    for (face_index = 0; face_index < num_faces; ++face_index) {
        FontDataFT *ft;

        rc = FT_New_Memory_Face(selector->ftlibrary, (unsigned char *) data,
                                data_size, face_index, &face);
        if (rc) {
            ass_msg(library, MSGL_WARN, "Error opening memory font '%s'",
                   name);
            continue;
        }

        num_faces = face->num_faces;

        ass_charmap_magic(library, face);

        ft = calloc(1, sizeof(FontDataFT));

        if (ft == NULL) {
            FT_Done_Face(face);
            continue;
        }

        ft->lib  = library;
        ft->face = face;
        ft->idx  = idx;

        if (!insert_ft_font(priv, face, NULL, NULL, ft)) {
            ass_msg(library, MSGL_WARN, "Error loading embedded font '%s'", name);
        }
    }
}

/**
 * \brief Create font provider for embedded fonts. This parses the fonts known
 * to the current ASS_Library and adds them to the selector.
 * \param selector font selector
 * \return font provider
 */
static ASS_FontProvider *
ass_embedded_fonts_add_provider(ASS_FontSelector *selector, size_t *num_emfonts)
{
    ASS_FontProvider *priv = ass_font_provider_new(selector, &ft_funcs, NULL);
    if (priv == NULL)
        return NULL;

    ASS_Library *lib = selector->library;

    if (lib->fonts_dir && lib->fonts_dir[0]) {
        load_fonts_from_dir(lib, lib->fonts_dir);
    }

    for (size_t i = 0; i < lib->num_fontdata; i++)
        process_fontdata(priv, i);
    *num_emfonts = lib->num_fontdata;

    return priv;
}

struct font_constructors {
    ASS_DefaultFontProvider id;
    ASS_FontProvider *(*constructor)(ASS_Library *, ASS_FontSelector *,
                                     const char *, FT_Library);
    const char *name;
};

struct font_constructors font_constructors[] = {
#ifdef CONFIG_CORETEXT
    { ASS_FONTPROVIDER_CORETEXT,        &ass_coretext_add_provider,     "coretext"},
#endif
#ifdef CONFIG_DIRECTWRITE
    { ASS_FONTPROVIDER_DIRECTWRITE,     &ass_directwrite_add_provider,  "directwrite"
#if ASS_WINAPI_DESKTOP
        " (with GDI)"
#else
        " (without GDI)"
#endif
    },
#endif
#ifdef CONFIG_FONTCONFIG
    { ASS_FONTPROVIDER_FONTCONFIG,      &ass_fontconfig_add_provider,   "fontconfig"},
#endif
    { ASS_FONTPROVIDER_NONE, NULL, NULL },
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
ass_fontselect_init(ASS_Library *library, FT_Library ftlibrary, size_t *num_emfonts,
                    const char *family, const char *path, const char *config,
                    ASS_DefaultFontProvider dfp)
{
    ASS_FontSelector *priv = calloc(1, sizeof(ASS_FontSelector));
    if (priv == NULL)
        return NULL;

    priv->library = library;
    priv->ftlibrary = ftlibrary;
    priv->uid = 1;
    priv->family_default = family ? strdup(family) : NULL;
    priv->path_default = path ? strdup(path) : NULL;
    priv->index_default = 0;

    if (family && !priv->family_default)
        goto fail;
    if (path && !priv->path_default)
        goto fail;

    priv->embedded_provider = ass_embedded_fonts_add_provider(priv, num_emfonts);

    if (priv->embedded_provider == NULL) {
        ass_msg(library, MSGL_WARN, "failed to create embedded font provider");
        goto fail;
    }

    if (dfp >= ASS_FONTPROVIDER_AUTODETECT) {
        for (int i = 0; font_constructors[i].constructor; i++ )
            if (dfp == font_constructors[i].id ||
                dfp == ASS_FONTPROVIDER_AUTODETECT) {
                priv->default_provider =
                    font_constructors[i].constructor(library, priv,
                                                     config, ftlibrary);
                if (priv->default_provider) {
                    ass_msg(library, MSGL_INFO, "Using font provider %s",
                            font_constructors[i].name);
                    break;
                }
            }

        if (!priv->default_provider)
            ass_msg(library, MSGL_WARN, "can't find selected font provider");

    }

    return priv;

fail:
    if (priv->default_provider)
        ass_font_provider_free(priv->default_provider);
    if (priv->embedded_provider)
        ass_font_provider_free(priv->embedded_provider);

    free(priv->family_default);
    free(priv->path_default);

    free(priv);

    return NULL;
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

    if (*providers == NULL) {
        *size = (size_t)-1;
        return;
    }

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
    if (priv->embedded_provider)
        ass_font_provider_free(priv->embedded_provider);

    free(priv->font_infos);
    free(priv->path_default);
    free(priv->family_default);

    free(priv);
}

void ass_map_font(const ASS_FontMapping *map, int len, const char *name,
                  ASS_FontProviderMetaData *meta)
{
    for (int i = 0; i < len; i++) {
        if (ass_strcasecmp(map[i].from, name) == 0) {
            meta->fullnames = calloc(1, sizeof(char *));
            if (meta->fullnames) {
                meta->fullnames[0] = strdup(map[i].to);
                if (meta->fullnames[0])
                    meta->n_fullname = 1;
            }
            return;
        }
    }
}

size_t ass_update_embedded_fonts(ASS_FontSelector *selector, size_t num_loaded)
{
    if (!selector->embedded_provider)
        return num_loaded;

    size_t num_fontdata = selector->library->num_fontdata;
    for (size_t i = num_loaded; i < num_fontdata; i++)
        process_fontdata(selector->embedded_provider, i);
    return num_fontdata;
}

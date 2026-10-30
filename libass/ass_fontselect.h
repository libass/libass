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

#ifndef LIBASS_FONTSELECT_H
#define LIBASS_FONTSELECT_H

#include <stdbool.h>
#include <stdint.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct font_selector ASS_FontSelector;
typedef struct font_info ASS_FontInfo;
typedef struct ass_font_stream ASS_FontStream;

#include "ass_types.h"
#include "ass.h"
#include "ass_font.h"

typedef struct font_provider ASS_FontProvider;

/* Font Provider */
typedef struct ass_font_provider_meta_data ASS_FontProviderMetaData;

/**
 * Get font data. This is a stream interface which can be used as an
 * alternative to providing a font path (which may not be available).
 *
 * This is called by fontselect if a given font was added without a
 * font path (i.e. the path was set to NULL).
 *
 * \param font_priv font private data
 * \param data output buffer; set to NULL to query stream size
 * \param offset stream offset
 * \param len bytes to read into output buffer from stream
 * \return actual number of bytes read, or stream size if data == NULL
 */
typedef size_t  (*GetDataFunc)(void *font_priv, unsigned char *data,
                               size_t offset, size_t len);

/**
 * Check whether the font contains PostScript outlines.
 *
 * \param font_priv font private data
 * \return true if the font contains PostScript outlines
 */
typedef bool    (*CheckPostscriptFunc)(void *font_priv);

/**
 * Check if a glyph is supported by a font.
 *
 * \param font_priv font private data
 * \param codepoint Unicode codepoint (UTF-32)
 * \return true if codepoint is supported by the font
 */
typedef bool    (*CheckGlyphFunc)(void *font_priv, uint32_t codepoint);

/**
* Get index of a font in context of a font collection.
* This function is optional and may be needed to initialize the font index
* lazily.
*
* \param font_priv font private data
* \return font index inside the collection, or 0 in case of a single font
*/
typedef unsigned    (*GetFontIndex)(void *font_priv);

/**
 * Destroy a font's private data.
 *
 *  \param font_priv font private data
 */
typedef void    (*DestroyFontFunc)(void *font_priv);

/**
 * Destroy a font provider's private data.
 *
 * \param priv font provider private data
 */
typedef void    (*DestroyProviderFunc)(void *priv);

/**
 * Add fonts for a given font name; this should add all fonts matching the
 * given name to the fontselect database.
 *
 * This is called by fontselect whenever a new logical font is created. The
 * font provider set as default is used.
 *
 * \param priv font provider private data
 * \param lib ASS_Library instance
 * \param provider font provider instance
 * \param name font name (as specified in script)
 * \param match_extended_family If true, the function allows matching against
 *                              extended family names as defined by the provider.
 *                              This behavior is provider-dependent.
 * \param bold The requested boldness level.
 * \param italic The requested italic style.
 * \param code The character that should be present in the font, can be 0.
 * \return The font selected by the provider.
 */
typedef ASS_FontInfo   *(*MatchFontsFunc)(void *priv,
                                          ASS_Library *lib,
                                          ASS_FontProvider *provider,
                                          char *name,
                                          bool match_extended_family,
                                          unsigned bold, unsigned italic,
                                          uint32_t code);

/**
 * Substitute font name by another. This implements generic font family
 * substitutions (e.g. sans-serif, serif, monospace) as well as font aliases.
 *
 * The generic families should map to sensible platform-specific font families.
 * Aliases are sometimes used to map from common fonts that don't exist on
 * a particular platform to similar alternatives. For example, a Linux
 * system with fontconfig may map "Arial" to "Liberation Sans" and Windows
 * maps "Helvetica" to "Arial".
 *
 * This is called by fontselect when a new logical font is created. The font
 * provider set as default is used.
 *
 * \param priv font provider private data
 * \param name input string for substitution, as specified in the script
 * \param meta metadata (fullnames and n_fullname) to be filled in
 */
typedef void    (*SubstituteFontFunc)(void *priv, const char *name,
                                      ASS_FontProviderMetaData *meta);

/**
 * Get an appropriate fallback extended font family for a given codepoint.
 *
 * This is called by fontselect whenever a glyph is not found in the
 * physical font list of a logical font. fontselect will try to add the
 * font family with match_fonts if it does not exist in the font list
 * add match_fonts is not NULL. Note that the returned font family should
 * contain the requested codepoint.
 *
 * Note that fontselect uses the font provider set as default to determine
 * fallbacks.
 *
 * \param priv font provider private data
 * \param lib ASS_Library instance
 * \param family original font family name (try matching a similar font) (never NULL)
 * \param codepoint Unicode codepoint (UTF-32)
 * \return output extended font family, allocated with malloc(), must be freed
 *         by caller.
 */
typedef char   *(*GetFallbackFunc)(void *priv,
                                   ASS_Library *lib,
                                   const char *family,
                                   uint32_t codepoint);

typedef struct font_provider_funcs {
    GetDataFunc         get_data;               /* optional/mandatory */
    CheckPostscriptFunc check_postscript;       /* optional */
    CheckGlyphFunc      check_glyph;            /* mandatory */
    DestroyFontFunc     destroy_font;           /* mandatory */
    DestroyProviderFunc destroy_provider;       /* optional */
    MatchFontsFunc      match_fonts;            /* optional */
    SubstituteFontFunc  get_substitutions;      /* optional */
    GetFallbackFunc     get_fallback;           /* optional */
    GetFontIndex        get_font_index;         /* optional */
} ASS_FontProviderFuncs;

/*
 * Basic font metadata. All strings must be encoded with UTF-8.
 * At minimum one family is required.
 * If no family names are present, ass_font_provider_add_font
 * will open the font file and read metadata from there,
 * replacing everything but extended_family.
 */
struct ass_font_provider_meta_data {
    /**
     * List of localized font family names,
     * e.g. "Arial", "Arial Narrow" or "Arial Black".
     */
    char **families;

    /**
     * List of localized full names, e.g. "Arial Bold",
     * "Arial Narrow Bold", "Arial Black" or "Arial Black Normal".
     * The English name should be listed first to speed up typical matching.
     */
    char **fullnames;

    /**
     * The PostScript name, e.g. "Arial-BoldMT",
     * "ArialNarrow-Bold" or "Arial-Black".
     */
    char *postscript_name;

    /**
     * Any name that identifies an extended font family, e.g. "Arial".
     * This could be the full designer-named typographic family or (perhaps
     * even better) a (sub)family limited to weight/width/slant variations.
     * Names returned by get_fallback are matched against this field.
     */
    char *extended_family;

    int n_family;       // Number of localized family names
    int n_fullname;     // Number of localized full names

    FT_Long style_flags; // Computed from OS/2 table, or equivalent
    int weight;         // Font weight in TrueType scale, 100-900
                        // See FONT_WEIGHT_*

    /**
     * Whether the font contains PostScript outlines.
     * Unused if the font provider has a check_postscript function.
     */
    bool is_postscript;
};

struct ass_font_stream {
    // GetDataFunc
    size_t  (*func)(void *font_priv, unsigned char *data,
                    size_t offset, size_t len);
    void *priv;
};


typedef struct ass_font_mapping ASS_FontMapping;

struct ass_font_mapping {
    const char *from;
    const char *to;
};

/**
 * Simple font substitution helper. This can be used to implement basic
 * mappings from one name to another. This is useful for supporting
 * generic font families in font providers.
 *
 * \param map list of mappings
 * \param len length of list of mappings
 * \param name font name to map from
 * \param meta metadata struct, mapped fonts will be stored into this
 */
void ass_map_font(const ASS_FontMapping *map, int len, const char *name,
                  ASS_FontProviderMetaData *meta);

ASS_FontSelector *
ass_fontselect_init(ASS_Library *library, FT_Library ftlibrary, size_t *num_emfonts,
                    const char *family, const char *path, const char *config,
                    ASS_DefaultFontProvider dfp);
char *ass_font_select(ASS_FontSelector *priv,
                      const ASS_Font *font, int *index, char **postscript_name,
                      int *uid, ASS_FontStream *data, uint32_t code);
void ass_fontselect_free(ASS_FontSelector *priv);

// Font provider functions
ASS_FontProvider *ass_font_provider_new(ASS_FontSelector *selector,
        ASS_FontProviderFuncs *funcs, void *data);

/**
 * \brief Create an empty font provider. A font provider can be used to
 * provide additional fonts to libass.
 * \param priv parent renderer
 * \param funcs callback functions
 * \param data private data for provider callbacks
 *
 */
ASS_FontProvider *
ass_create_font_provider(ASS_Renderer *priv, ASS_FontProviderFuncs *funcs,
                         void *data);

/**
 * \brief Add a font to a font provider.
 * \param provider the font provider
 * \param info The font to add to the database.
 * \param is_embedded If true, the font will be added to the embedded db.
 *                    If false, it will be added to the provider db.
 * \return True if the font has been successfully added to the provider. Otherwise, false.
 * \note After calling this function, **do not** call `ass_font_provider_free_fontinfo`
 *       on the `info` parameter, as its contents have been copied.
 */
bool
ass_font_provider_add_font(ASS_FontProvider *provider,
                           ASS_FontInfo* info, bool is_embedded);

/**
 * \brief Get the font info from a provider's font.
 * \param provider the font provider
 * \param meta the font metadata. See struct definition for more information.
 * \param path absolute path to font, or NULL for memory-based fonts
 * \param index index inside a font collection file
 *              (-1 to look up by PostScript name)
 * \param data private data for font callbacks
 * \return A pointer to an ASS_FontInfo corresponding to the given parameters.
 */
ASS_FontInfo *
ass_font_provider_get_font_info(ASS_FontProvider *provider,
                                ASS_FontProviderMetaData *meta, const char *path,
                                int index, void *data);

/**
 * \brief Updates the best matching font based on the given criteria.
 * \param info The font to be evaluated.
 * \param requested_font The requested font metadata used for comparison. Only the
 *             `fullnames` and `n_fullname` fields are considered for matching.
 * \param match_extended_family If true, the function allows matching against
 *                              extended family names as defined by the provider.
 *                              This behavior is provider-dependent.
 * \param bold The requested boldness level.
 * \param italic The requested italic style.
 * \param code The character that should be present in the font, can be 0.
 * \param name_match Set to true if the font matches the metadata,
 *                   otherwise set to false.
 * \param best_font_score Score representing the match. The lower, the better.
 *                        It will be updated if the font has a better score.
 * \return True if `info` provides a better match than the previously recorded
 *         best font score. Otherwise, false.
 */
bool ass_update_best_matching_font(ASS_FontInfo *info,
    ASS_FontProviderMetaData requested_font,
    bool match_extended_family,
    unsigned bold, unsigned italic,
    uint32_t code, bool *name_match,
    unsigned *best_font_score);

/**
 * Free all data associated with a FontInfo struct. Handles FontInfo structs
 * with incomplete allocations well.
 *
 * \param info FontInfo struct to free associated data from
 */
void ass_font_provider_free_fontinfo(ASS_FontInfo *info);

/**
 * Free the provider-specific private data.
 *
 * \param info FontInfo struct to free private data from.
 */
void ass_font_provider_destroy_private_fontinfo(ASS_FontInfo *info);

/**
 * \brief Free font provider and associated fonts.
 * \param provider the font provider
 *
 */
void ass_font_provider_free(ASS_FontProvider *provider);

/**
 * \brief Update embedded and memory fonts
 */
size_t ass_update_embedded_fonts(ASS_FontSelector *selector, size_t num_loaded);

#endif                          /* LIBASS_FONTSELECT_H */

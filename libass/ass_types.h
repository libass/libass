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

#ifndef LIBASS_TYPES_H
#define LIBASS_TYPES_H

#include <stdint.h>

#define VALIGN_SUB 0
#define VALIGN_CENTER 8
#define VALIGN_TOP 4
#define HALIGN_LEFT 1
#define HALIGN_CENTER 2
#define HALIGN_RIGHT 3

#define FONT_WEIGHT_LIGHT  300
#define FONT_WEIGHT_MEDIUM 400
#define FONT_WEIGHT_BOLD   700
#define FONT_SLANT_NONE    0
#define FONT_SLANT_ITALIC  100
#define FONT_SLANT_OBLIQUE 110
#define FONT_WIDTH_CONDENSED 75
#define FONT_WIDTH_NORMAL    100
#define FONT_WIDTH_EXPANDED  125


/* Opaque objects internally used by libass.  Contents are private. */
typedef struct ass_renderer ASS_Renderer;
typedef struct render_priv ASS_RenderPriv;
typedef struct parser_priv ASS_ParserPriv;
typedef struct ass_library ASS_Library;
typedef struct font_provider ASS_FontProvider;

/* Font Provider */
typedef struct font_provider_meta_data ASS_FontProviderMetaData;

/**
 * Get font data. This is a stream interface which can be used as an
 * alternative to providing a font path (which may not be available).
 *
 * This is called by fontselect if a given font was added without a
 * font path (i.e. the path was set to NULL).
 *
 * \param font_priv font private data
 * \param output buffer; set to NULL to query stream size
 * \param offset stream offset
 * \param len bytes to read into output buffer from stream
 * \return actual number of bytes read, or stream size if data == NULL
 */
typedef size_t  (*GetDataFunc)(void *font_priv, unsigned char *data,
                               size_t offset, size_t len);

/**
 * Check if a glyph is supported by a font.
 *
 * \param font_priv font private data
 * \param codepont Unicode codepoint (UTF-32)
 * \return non-zero value if codepoint is supported by the font
 */
typedef int     (*CheckGlyphFunc)(void *font_priv, uint32_t codepoint);

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
 * \param lib ASS_Library instance
 * \param provider font provider instance
 * \param name font name (as specified in script)
 */
typedef void    (*MatchFontsFunc)(ASS_Library *lib,
                                  ASS_FontProvider *provider,
                                  char *name);

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
 * \return output string for substitution, allocated with malloc(), must be
 *         freed by caller, can be NULL if no substitution was done.
 */
typedef char   *(*SubstituteFontFunc)(void *priv, const char *name);

/**
 * Get an appropriate fallback font for a given codepoint.
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
 * \param font_priv font private data
 * \param codepoint Unicode codepoint (UTF-32)
 * \return output font family, allocated with malloc(), must be freed
 *         by caller.
 */
typedef char   *(*GetFallbackFunc)(void *font_priv,
                                   ASS_FontProviderMetaData *meta,
                                   uint32_t codepoint);

typedef struct font_provider_funcs {
    GetDataFunc     get_data;       /* optional/mandatory */
    CheckGlyphFunc  check_glyph;    /* mandatory */
    DestroyFontFunc destroy_font;   /* optional */
    DestroyProviderFunc destroy_provider; /* optional */
    MatchFontsFunc  match_fonts;    /* optional */
    SubstituteFontFunc subst_font;  /* optional */
    GetFallbackFunc fallback_font;  /* optional */
} ASS_FontProviderFuncs;

/*
 * Basic font metadata. All strings must be encoded with UTF-8.
 * At minimum one family is required.
 */
typedef struct font_provider_meta_data {

    /**
     * List of localized font family names, e.g. "Arial".
     */
    char **families;

    /**
     * List of localized full names, e.g. "Arial Bold".
     * The English name should be listed first to speed up typical matching.
     */
    char **fullnames;
    int n_family;       // Number of localized family names
    int n_fullname;     // Number of localized full names

    int slant;          // Font slant value from FONT_SLANT_*
    int weight;         // Font weight in TrueType scale, 100-900
                        // See FONT_WEIGHT_*
    int width;          // Font weight in percent, normally 100
                        // See FONT_WIDTH_*
} ASS_FontProviderMetaData;


/* ASS Style: line */
typedef struct ass_style {
    char *Name;
    char *FontName;
    double FontSize;
    uint32_t PrimaryColour;
    uint32_t SecondaryColour;
    uint32_t OutlineColour;
    uint32_t BackColour;
    int Bold;
    int Italic;
    int Underline;
    int StrikeOut;
    double ScaleX;
    double ScaleY;
    double Spacing;
    double Angle;
    int BorderStyle;
    double Outline;
    double Shadow;
    int Alignment;
    int MarginL;
    int MarginR;
    int MarginV;
    int Encoding;
    int treat_fontname_as_pattern;
    double Blur;
} ASS_Style;


/*
 * ASS_Event corresponds to a single Dialogue line;
 * text is stored as-is, style overrides will be parsed later.
 */
typedef struct ass_event {
    long long Start;            // ms
    long long Duration;         // ms

    int ReadOrder;
    int Layer;
    int Style;
    char *Name;
    int MarginL;
    int MarginR;
    int MarginV;
    char *Effect;
    char *Text;

    ASS_RenderPriv *render_priv;
} ASS_Event;

/**
 * Support for (xy-)vsfilter mangled colors
 *
 * Generally, xy-vsfilter emulates the classic vsfilter behavior of
 * rendering directly into the (usually YCbCr) video. vsfilter is
 * hardcoded to use BT.601(TV) as target colorspace when converting
 * the subtitle RGB color to the video colorspace. This led to major
 * breakage when HDTV video was introduced: HDTV typically uses
 * BT.709(TV), but vsfilter still used BT.601(TV) for conversion.
 *
 * This means classic vsfilter will mangle colors as follows:
 *
 *    screen_rgb = bt_709tv_to_rgb(rgb_to_bt601tv(ass_rgb))
 *
 * Or in general:
 *
 *    screen_rgb = video_csp_to_rgb(rgb_to_bt601tv(ass_rgb))
 *
 * where video_csp is the colorspace of the video with which the
 * subtitle was muxed.
 *
 * xy-vsfilter did not fix this, but instead introduced explicit
 * rules how colors were mangled by adding a "YCbCr Matrix" header.
 * If this header specifies a real colorspace (like BT.601(TV) etc.),
 * xy-vsfilter behaves exactly like vsfilter, but using the specified
 * colorspace for conversion of ASS input RGB to screen RGB:
 *
 *    screen_rgb = video_csp_to_rgb(rgb_to_ycbcr_header_csp(ass_rgb))
 *
 * Further, xy-vsfilter behaves like vsfilter with no changes if the header
 * is missing.
 *
 * The special value "None" means untouched RGB values. Keep in mind that
 * some version of xy-vsfilter are buggy and don't interpret this correctly.
 * It appears some people are advocating that this header value is
 * intended for situations where exact colors do not matter.
 *
 * Note that newer Aegisub versions (the main application to produce ASS
 * subtitle scripts) have an option that tries not to mangle the colors. It
 * is said that if the header is not set to BT.601(TV), the colors are
 * supposed not to be mangled, even if the "YCbCr Matrix" header is not
 * set to "None". In other words, the video colorspace as detected by
 * Aegisub is the same as identified in the file header.
 *
 * In general, misinterpreting this header or not using it will lead to
 * slightly different subtitle colors, which can matter if the subtitle
 * attempts to match solid colored areas in the video.
 *
 * Note that libass doesn't change colors based on this header. It
 * absolutely can't do that, because the video colorspace is required
 * in order to handle this as intended by xy-vsfilter.
 */
typedef enum ASS_YCbCrMatrix {
    YCBCR_DEFAULT = 0,  // Header missing
    YCBCR_UNKNOWN,      // Header could not be parsed correctly
    YCBCR_NONE,         // "None" special value
    YCBCR_BT601_TV,
    YCBCR_BT601_PC,
    YCBCR_BT709_TV,
    YCBCR_BT709_PC,
    YCBCR_SMPTE240M_TV,
    YCBCR_SMPTE240M_PC,
    YCBCR_FCC_TV,
    YCBCR_FCC_PC
} ASS_YCbCrMatrix;

/*
 * ass track represent either an external script or a matroska subtitle stream
 * (no real difference between them); it can be used in rendering after the
 * headers are parsed (i.e. events format line read).
 */
typedef struct ass_track {
    int n_styles;           // amount used
    int max_styles;         // amount allocated
    int n_events;
    int max_events;
    ASS_Style *styles;    // array of styles, max_styles length, n_styles used
    ASS_Event *events;    // the same as styles

    char *style_format;     // style format line (everything after "Format: ")
    char *event_format;     // event format line

    enum {
        TRACK_TYPE_UNKNOWN = 0,
        TRACK_TYPE_ASS,
        TRACK_TYPE_SSA
    } track_type;

    // Script header fields
    int PlayResX;
    int PlayResY;
    double Timer;
    int WrapStyle;
    int ScaledBorderAndShadow;
    int Kerning;
    char *Language;
    ASS_YCbCrMatrix YCbCrMatrix;

    int default_style;      // index of default style
    char *name;             // file name in case of external subs, 0 for streams

    ASS_Library *library;
    ASS_ParserPriv *parser_priv;
} ASS_Track;

#endif /* LIBASS_TYPES_H */

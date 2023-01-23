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

#include "ass_shaper.h"
#include "ass_render.h"
#include "ass_font.h"
#include "ass_parse.h"
#include "ass_cache.h"
#include <limits.h>
#include <stdbool.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
enum {
    VERT = 0,
    VKNA,
    KERN,
    LIGA,
    CLIG
};
#define NUM_FEATURES 5

enum {
    WHOLE_TEXT_LAYOUT_OFF,
    WHOLE_TEXT_LAYOUT_IMPLICIT,
    WHOLE_TEXT_LAYOUT_EXPLICIT,
};

struct ass_shaper {
    ASS_ShapingLevel shaping_level;

    // FriBidi log2vis
    int n_codepoints, n_pars;
    FriBidiChar *event_text; // just a reference, owned by text_info
    FriBidiCharType *ctypes;
    FriBidiLevel *emblevels;
    FriBidiStrIndex *cmap;
    FriBidiParType *pbase_dir;
    FriBidiParType base_direction;

    // OpenType features
    int n_features;
    hb_feature_t *features;
    hb_language_t language;

    // Glyph metrics cache, to speed up shaping
    Cache *metrics_cache;

#ifdef USE_FRIBIDI_EX_API
    FriBidiBracketType *btypes;
    bool bidi_brackets;
#endif

    char whole_text_layout;
};

struct ass_shaper_metrics_data {
    Cache *metrics_cache;
    GlyphMetricsHashKey hash_key;
    int vertical;
};

struct ass_shaper_font_data {
    hb_font_t *fonts[ASS_FONT_MAX_FACES];
    hb_font_funcs_t *font_funcs[ASS_FONT_MAX_FACES];
    struct ass_shaper_metrics_data *metrics_data[ASS_FONT_MAX_FACES];
};

/**
 * \brief Print version information
 */
void ass_shaper_info(ASS_Library *lib)
{
    ass_msg(lib, MSGL_INFO, "Shaper: FriBidi "
            FRIBIDI_VERSION " (SIMPLE)"
            " HarfBuzz-ng %s (COMPLEX)", hb_version_string()
           );
}

/**
 * \brief grow per-codepoint arrays, if needed
 * \param new_size requested size
 */
static bool check_codepoint_allocations(ASS_Shaper *shaper, size_t new_size)
{
    if (new_size > shaper->n_codepoints) {
        if (!ASS_REALLOC_ARRAY(shaper->ctypes, new_size) ||
#ifdef USE_FRIBIDI_EX_API
            (shaper->bidi_brackets && !ASS_REALLOC_ARRAY(shaper->btypes, new_size)) ||
#endif
            !ASS_REALLOC_ARRAY(shaper->emblevels, new_size) ||
            !ASS_REALLOC_ARRAY(shaper->cmap, new_size))
            return false;
        shaper->n_codepoints = new_size;
    }
    return true;
}

/**
 * \brief grow per-bidi-paragraph arrays, if needed
 * \param n_pars requested size
 */
static bool check_par_allocations(ASS_Shaper *shaper, size_t n_pars)
{
    if (shaper->whole_text_layout && n_pars > shaper->n_pars) {
        if (!ASS_REALLOC_ARRAY(shaper->pbase_dir, n_pars))
            return false;
        shaper->n_pars = n_pars;
    }
    return true;
}

/**
 * \brief Free shaper and related data
 */
void ass_shaper_free(ASS_Shaper *shaper)
{
    free(shaper->features);
    free(shaper->ctypes);
#ifdef USE_FRIBIDI_EX_API
    free(shaper->btypes);
#endif
    free(shaper->emblevels);
    free(shaper->cmap);
    free(shaper->pbase_dir);
    free(shaper);
}

void ass_shaper_font_data_free(ASS_ShaperFontData *priv)
{
    int i;
    for (i = 0; i < ASS_FONT_MAX_FACES; i++)
        if (priv->fonts[i]) {
            free(priv->metrics_data[i]);
            hb_font_destroy(priv->fonts[i]);
            hb_font_funcs_destroy(priv->font_funcs[i]);
        }
    free(priv);
}

/**
 * \brief set up the HarfBuzz OpenType feature list with some
 * standard features.
 */
static bool init_features(ASS_Shaper *shaper)
{
    shaper->features = calloc(sizeof(hb_feature_t), NUM_FEATURES);
    if (!shaper->features)
        return false;

    shaper->n_features = NUM_FEATURES;
    shaper->features[VERT].tag = HB_TAG('v', 'e', 'r', 't');
    shaper->features[VERT].end = UINT_MAX;
    shaper->features[VKNA].tag = HB_TAG('v', 'k', 'n', 'a');
    shaper->features[VKNA].end = UINT_MAX;
    shaper->features[KERN].tag = HB_TAG('k', 'e', 'r', 'n');
    shaper->features[KERN].end = UINT_MAX;
    shaper->features[LIGA].tag = HB_TAG('l', 'i', 'g', 'a');
    shaper->features[LIGA].end = UINT_MAX;
    shaper->features[CLIG].tag = HB_TAG('c', 'l', 'i', 'g');
    shaper->features[CLIG].end = UINT_MAX;

    return true;
}

/**
 * \brief Set features depending on properties of the run
 */
static void set_run_features(ASS_Shaper *shaper, GlyphInfo *info)
{
    // enable vertical substitutions for @font runs
    if (info->font->desc.vertical)
        shaper->features[VERT].value = shaper->features[VKNA].value = 1;
    else
        shaper->features[VERT].value = shaper->features[VKNA].value = 0;

    // disable ligatures if horizontal spacing is non-standard
    if (info->hspacing)
        shaper->features[LIGA].value = shaper->features[CLIG].value = 0;
    else
        shaper->features[LIGA].value = shaper->features[CLIG].value = 1;
}

/**
 * \brief Update HarfBuzz's idea of font metrics
 * \param hb_font HarfBuzz font
 * \param face associated FreeType font face
 */
static void update_hb_size(hb_font_t *hb_font, FT_Face face)
{
    hb_font_set_scale (hb_font,
            ((uint64_t) face->size->metrics.x_scale * (uint64_t) face->units_per_EM) >> 16,
            ((uint64_t) face->size->metrics.y_scale * (uint64_t) face->units_per_EM) >> 16);
    hb_font_set_ppem (hb_font, face->size->metrics.x_ppem,
            face->size->metrics.y_ppem);
}


/*
 * Cached glyph metrics getters follow
 *
 * These functions replace HarfBuzz' standard FreeType font functions
 * and provide cached access to essential glyph metrics. This usually
 * speeds up shaping a lot. It also allows us to use custom load flags.
 *
 */

static FT_Glyph_Metrics *
get_cached_metrics(struct ass_shaper_metrics_data *metrics,
                   hb_codepoint_t unicode, hb_codepoint_t glyph)
{
    bool rotate = false;
    // if @font rendering is enabled and the glyph should be rotated,
    // make cached_h_advance pick up the right advance later
    if (metrics->vertical && unicode >= VERTICAL_LOWER_BOUND)
        rotate = true;

    metrics->hash_key.glyph_index = glyph;
    FT_Glyph_Metrics *val = ass_cache_get(metrics->metrics_cache, &metrics->hash_key,
                                          rotate ? metrics : NULL);
    if (!val)
        return NULL;
    if (val->width >= 0)
        return val;
    ass_cache_dec_ref(val);
    return NULL;
}

size_t ass_glyph_metrics_construct(void *key, void *value, void *priv)
{
    GlyphMetricsHashKey *k = key;
    FT_Glyph_Metrics *v = value;

    int load_flags = FT_LOAD_DEFAULT | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH
        | FT_LOAD_IGNORE_TRANSFORM;

    FT_Face face = k->font->faces[k->face_index];
    if (FT_Load_Glyph(face, k->glyph_index, load_flags)) {
        v->width = -1;
        return 1;
    }

    memcpy(v, &face->glyph->metrics, sizeof(FT_Glyph_Metrics));

    if (priv)  // rotate
        v->horiAdvance = v->vertAdvance;

    return 1;
}

static hb_blob_t*
get_reference_table(hb_face_t *hbface, hb_tag_t tag, void *font_data)
{
  FT_Face face = font_data;
  FT_ULong len = 0;

  if (FT_Load_Sfnt_Table(face, tag, 0, NULL, &len) != FT_Err_Ok)
    return NULL;

  char *buf = malloc(len);
  if (!buf)
    return NULL;

  if (FT_Load_Sfnt_Table(face, tag, 0, (FT_Byte*)buf, &len) != FT_Err_Ok) {
    free(buf);
    return NULL;
  }

  hb_blob_t *blob = hb_blob_create(buf, len, HB_MEMORY_MODE_WRITABLE, buf, free);
  if (!blob)
      free(buf);

  return blob;
}

static hb_bool_t
get_glyph_nominal(hb_font_t *font, void *font_data, hb_codepoint_t unicode,
                  hb_codepoint_t *glyph, void *user_data)
{
    FT_Face face = font_data;
    struct ass_shaper_metrics_data *metrics_priv = user_data;

    *glyph = ass_font_index_magic(face, unicode);
    if (*glyph)
        *glyph = FT_Get_Char_Index(face, *glyph);
    if (!*glyph)
        return false;

    // rotate glyph advances for @fonts while we still know the Unicode codepoints
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, unicode, *glyph);
    ass_cache_dec_ref(metrics);
    return true;
}

static hb_bool_t
get_glyph_variation(hb_font_t *font, void *font_data, hb_codepoint_t unicode,
                    hb_codepoint_t variation, hb_codepoint_t *glyph, void *user_data)
{
    FT_Face face = font_data;
    struct ass_shaper_metrics_data *metrics_priv = user_data;

    *glyph = ass_font_index_magic(face, unicode);
    if (*glyph)
        *glyph = FT_Face_GetCharVariantIndex(face, *glyph, variation);
    if (!*glyph)
        return false;

    // rotate glyph advances for @fonts while we still know the Unicode codepoints
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, unicode, *glyph);
    ass_cache_dec_ref(metrics);
    return true;
}

static hb_position_t
cached_h_advance(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
                 void *user_data)
{
    struct ass_shaper_metrics_data *metrics_priv = user_data;
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, 0, glyph);
    if (!metrics)
        return 0;

    hb_position_t advance = metrics->horiAdvance;
    ass_cache_dec_ref(metrics);
    return advance;
}

static hb_position_t
cached_v_advance(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
                 void *user_data)
{
    struct ass_shaper_metrics_data *metrics_priv = user_data;
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, 0, glyph);
    if (!metrics)
        return 0;

    hb_position_t advance = metrics->vertAdvance;
    ass_cache_dec_ref(metrics);
    return advance;
}

static hb_bool_t
cached_h_origin(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
                hb_position_t *x, hb_position_t *y, void *user_data)
{
    return true;
}

static hb_bool_t
cached_v_origin(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
                hb_position_t *x, hb_position_t *y, void *user_data)
{
    struct ass_shaper_metrics_data *metrics_priv = user_data;
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, 0, glyph);
    if (!metrics)
        return false;

    *x = metrics->horiBearingX - metrics->vertBearingX;
    *y = metrics->horiBearingY + metrics->vertBearingY;
    ass_cache_dec_ref(metrics);
    return true;
}

static hb_position_t
get_h_kerning(hb_font_t *font, void *font_data, hb_codepoint_t first,
                 hb_codepoint_t second, void *user_data)
{
    FT_Face face = font_data;
    FT_Vector kern;

    if (FT_Get_Kerning(face, first, second, FT_KERNING_DEFAULT, &kern))
        return 0;

    return kern.x;
}

static hb_position_t
get_v_kerning(hb_font_t *font, void *font_data, hb_codepoint_t first,
                 hb_codepoint_t second, void *user_data)
{
    return 0;
}

static hb_bool_t
cached_extents(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
               hb_glyph_extents_t *extents, void *user_data)
{
    struct ass_shaper_metrics_data *metrics_priv = user_data;
    FT_Glyph_Metrics *metrics = get_cached_metrics(metrics_priv, 0, glyph);
    if (!metrics)
        return false;

    extents->x_bearing =  metrics->horiBearingX;
    extents->y_bearing =  metrics->horiBearingY;
    extents->width     =  metrics->width;
    extents->height    = -metrics->height;
    ass_cache_dec_ref(metrics);
    return true;
}

static hb_bool_t
get_contour_point(hb_font_t *font, void *font_data, hb_codepoint_t glyph,
                     unsigned int point_index, hb_position_t *x,
                     hb_position_t *y, void *user_data)
{
    FT_Face face = font_data;
    int load_flags = FT_LOAD_DEFAULT | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH
        | FT_LOAD_IGNORE_TRANSFORM;

    if (FT_Load_Glyph(face, glyph, load_flags))
        return false;

    if (point_index >= (unsigned)face->glyph->outline.n_points)
        return false;

    *x = face->glyph->outline.points[point_index].x;
    *y = face->glyph->outline.points[point_index].y;
    return true;
}

/**
 * \brief Retrieve HarfBuzz font from cache.
 * Create it from FreeType font, if needed.
 * \param info glyph cluster
 * \return HarfBuzz font
 */
static hb_font_t *get_hb_font(ASS_Shaper *shaper, GlyphInfo *info)
{
    ASS_Font *font = info->font;
    hb_font_t **hb_fonts;

    if (!font->shaper_priv)
        font->shaper_priv = calloc(sizeof(ASS_ShaperFontData), 1);
    if (!font->shaper_priv)
        return NULL;

    hb_fonts = font->shaper_priv->fonts;
    if (!hb_fonts[info->face_index]) {
        FT_Face face = font->faces[info->face_index];
        hb_face_t *hb_face = hb_face_create_for_tables(get_reference_table, face, NULL);
        if (!hb_face)
            return NULL;
        hb_face_set_index(hb_face, face->face_index);
        hb_face_set_upem(hb_face, face->units_per_EM);

        hb_font_t *hb_font = hb_fonts[info->face_index] = hb_font_create(hb_face);
        hb_face_destroy(hb_face);
        if (!hb_font)
            return NULL;

        hb_font_set_scale(hb_font,
            (int)(((uint64_t)face->size->metrics.x_scale * face->units_per_EM + (1<<15)) >> 16),
            (int)(((uint64_t)face->size->metrics.y_scale * face->units_per_EM + (1<<15)) >> 16));

        // set up cached metrics access
        struct ass_shaper_metrics_data *metrics =
            font->shaper_priv->metrics_data[info->face_index] =
                calloc(sizeof(struct ass_shaper_metrics_data), 1);
        if (!metrics)
            return NULL;
        metrics->metrics_cache = shaper->metrics_cache;
        metrics->vertical = info->font->desc.vertical;

        hb_font_funcs_t *funcs = hb_font_funcs_create();
        if (!funcs)
            return NULL;
        font->shaper_priv->font_funcs[info->face_index] = funcs;
        hb_font_funcs_set_nominal_glyph_func(funcs, get_glyph_nominal,
                metrics, NULL);
        hb_font_funcs_set_variation_glyph_func(funcs, get_glyph_variation,
                metrics, NULL);
        hb_font_funcs_set_glyph_h_advance_func(funcs, cached_h_advance,
                metrics, NULL);
        hb_font_funcs_set_glyph_v_advance_func(funcs, cached_v_advance,
                metrics, NULL);
        hb_font_funcs_set_glyph_h_origin_func(funcs, cached_h_origin,
                metrics, NULL);
        hb_font_funcs_set_glyph_v_origin_func(funcs, cached_v_origin,
                metrics, NULL);
        hb_font_funcs_set_glyph_h_kerning_func(funcs, get_h_kerning,
                metrics, NULL);
        hb_font_funcs_set_glyph_v_kerning_func(funcs, get_v_kerning,
                metrics, NULL);
        hb_font_funcs_set_glyph_extents_func(funcs, cached_extents,
                metrics, NULL);
        hb_font_funcs_set_glyph_contour_point_func(funcs, get_contour_point,
                metrics, NULL);
        hb_font_set_funcs(hb_font, funcs, face, NULL);
    }

    ass_face_set_size(font->faces[info->face_index], info->font_size);
    update_hb_size(hb_fonts[info->face_index], font->faces[info->face_index]);

    // update hash key for cached metrics
    struct ass_shaper_metrics_data *metrics =
        font->shaper_priv->metrics_data[info->face_index];
    metrics->hash_key.font = info->font;
    metrics->hash_key.face_index = info->face_index;
    metrics->hash_key.size = info->font_size;

    return hb_fonts[info->face_index];
}

/**
 * \brief Determine whether this Unicode codepoint affects shaping
 * of neighbors even if they are in separate shape runs due to bidi,
 * script or font splitting, using VSFilter as the reference.
 */
static inline bool is_shaping_control(unsigned symbol) {
    return symbol == 0x200C /* ZWNJ */ || symbol == 0x200D /* ZWJ */;
}

/**
 * \brief Map script to default language.
 *
 * This maps a script to a language, if a script has a representative
 * language it is typically used with. Otherwise, the invalid language
 * is returned.
 *
 * The mapping is similar to Pango's pango-language.c.
 *
 * \param script script tag
 * \return language tag
 */
static hb_language_t script_to_language(hb_script_t script)
{
    switch (script) {
        // Unicode 1.1
        case HB_SCRIPT_ARABIC: return hb_language_from_string("ar", -1); break;
        case HB_SCRIPT_ARMENIAN: return hb_language_from_string("hy", -1); break;
        case HB_SCRIPT_BENGALI: return hb_language_from_string("bn", -1); break;
        case HB_SCRIPT_CANADIAN_ABORIGINAL: return hb_language_from_string("iu", -1); break;
        case HB_SCRIPT_CHEROKEE: return hb_language_from_string("chr", -1); break;
        case HB_SCRIPT_COPTIC: return hb_language_from_string("cop", -1); break;
        case HB_SCRIPT_CYRILLIC: return hb_language_from_string("ru", -1); break;
        case HB_SCRIPT_DEVANAGARI: return hb_language_from_string("hi", -1); break;
        case HB_SCRIPT_GEORGIAN: return hb_language_from_string("ka", -1); break;
        case HB_SCRIPT_GREEK: return hb_language_from_string("el", -1); break;
        case HB_SCRIPT_GUJARATI: return hb_language_from_string("gu", -1); break;
        case HB_SCRIPT_GURMUKHI: return hb_language_from_string("pa", -1); break;
        case HB_SCRIPT_HANGUL: return hb_language_from_string("ko", -1); break;
        case HB_SCRIPT_HEBREW: return hb_language_from_string("he", -1); break;
        case HB_SCRIPT_HIRAGANA: return hb_language_from_string("ja", -1); break;
        case HB_SCRIPT_KANNADA: return hb_language_from_string("kn", -1); break;
        case HB_SCRIPT_KATAKANA: return hb_language_from_string("ja", -1); break;
        case HB_SCRIPT_LAO: return hb_language_from_string("lo", -1); break;
        case HB_SCRIPT_LATIN: return hb_language_from_string("en", -1); break;
        case HB_SCRIPT_MALAYALAM: return hb_language_from_string("ml", -1); break;
        case HB_SCRIPT_MONGOLIAN: return hb_language_from_string("mn", -1); break;
        case HB_SCRIPT_ORIYA: return hb_language_from_string("or", -1); break;
        case HB_SCRIPT_SYRIAC: return hb_language_from_string("syr", -1); break;
        case HB_SCRIPT_TAMIL: return hb_language_from_string("ta", -1); break;
        case HB_SCRIPT_TELUGU: return hb_language_from_string("te", -1); break;
        case HB_SCRIPT_THAI: return hb_language_from_string("th", -1); break;

        // Unicode 2.0
        case HB_SCRIPT_TIBETAN: return hb_language_from_string("bo", -1); break;

        // Unicode 3.0
        case HB_SCRIPT_ETHIOPIC: return hb_language_from_string("am", -1); break;
        case HB_SCRIPT_KHMER: return hb_language_from_string("km", -1); break;
        case HB_SCRIPT_MYANMAR: return hb_language_from_string("my", -1); break;
        case HB_SCRIPT_SINHALA: return hb_language_from_string("si", -1); break;
        case HB_SCRIPT_THAANA: return hb_language_from_string("dv", -1); break;

        // Unicode 3.2
        case HB_SCRIPT_BUHID: return hb_language_from_string("bku", -1); break;
        case HB_SCRIPT_HANUNOO: return hb_language_from_string("hnn", -1); break;
        case HB_SCRIPT_TAGALOG: return hb_language_from_string("tl", -1); break;
        case HB_SCRIPT_TAGBANWA: return hb_language_from_string("tbw", -1); break;

        // Unicode 4.0
        case HB_SCRIPT_UGARITIC: return hb_language_from_string("uga", -1); break;

        // Unicode 4.1
        case HB_SCRIPT_BUGINESE: return hb_language_from_string("bug", -1); break;
        case HB_SCRIPT_OLD_PERSIAN: return hb_language_from_string("peo", -1); break;
        case HB_SCRIPT_SYLOTI_NAGRI: return hb_language_from_string("syl", -1); break;

        // Unicode 5.0
        case HB_SCRIPT_NKO: return hb_language_from_string("nko", -1); break;

        // no representative language exists
        default: return HB_LANGUAGE_INVALID; break;
    }
}

/**
 * \brief Determine language to be used for shaping a run.
 *
 * \param shaper shaper instance
 * \param script script tag associated with run
 * \return language tag
 */
static hb_language_t
hb_shaper_get_run_language(ASS_Shaper *shaper, hb_script_t script)
{
    hb_language_t lang;

    // override set, use it
    if (shaper->language != HB_LANGUAGE_INVALID)
        return shaper->language;

    // get default language for given script
    lang = script_to_language(script);

    // no dice, use system default
    if (lang == HB_LANGUAGE_INVALID)
        lang = hb_language_get_default();

    return lang;
}

/**
 * \brief Feed a run of shaped characters into the GlyphInfo array.
 *
 * \param glyphs GlyphInfo array
 * \param buf buffer of shaped run
 * \param offset offset into GlyphInfo array
 */
static void
shape_harfbuzz_process_run(GlyphInfo *glyphs, hb_buffer_t *buf, int offset)
{
    int j;
    int num_glyphs = hb_buffer_get_length(buf);
    hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, NULL);
    hb_glyph_position_t *pos    = hb_buffer_get_glyph_positions(buf, NULL);

    for (j = 0; j < num_glyphs; j++) {
        unsigned idx = glyph_info[j].cluster + offset;
        GlyphInfo *info = glyphs + idx;
        GlyphInfo *root = info;

        // if we have more than one glyph per cluster, allocate a new one
        // and attach to the root glyph
        if (!info->skip) {
            while (info->next)
                info = info->next;
            info->next = malloc(sizeof(GlyphInfo));
            if (info->next) {
                memcpy(info->next, info, sizeof(GlyphInfo));
                ass_cache_inc_ref(info->font);
                info = info->next;
                info->next = NULL;
            }
        }

        // set position and advance
        info->skip = false;
        info->glyph_index = glyph_info[j].codepoint;
        info->offset.x    = lrint(pos[j].x_offset * info->scale_x);
        info->offset.y    = lrint(-pos[j].y_offset * info->scale_y);
        info->advance.x   = lrint(pos[j].x_advance * info->scale_x);
        info->advance.y   = lrint(-pos[j].y_advance * info->scale_y);

        // accumulate advance in the root glyph
        root->cluster_advance.x += info->advance.x;
        root->cluster_advance.y += info->advance.y;
    }
}

/**
 * \brief Shape event text with HarfBuzz. Full OpenType shaping.
 * \param glyphs glyph clusters
 * \param len number of clusters
 */
static bool shape_harfbuzz(ASS_Shaper *shaper, GlyphInfo *glyphs, size_t len)
{
    int i;
    hb_buffer_t *buf = hb_buffer_create();
    hb_segment_properties_t props = HB_SEGMENT_PROPERTIES_DEFAULT;

    // Initialize: skip all glyphs, this is undone later as needed
    for (i = 0; i < len; i++)
        glyphs[i].skip = true;

    for (i = 0; i < len; i++) {
        if (glyphs[i].drawing_text.str) {
            glyphs[i].skip = false;
            continue;
        }

        int offset = i;
        hb_font_t *font = get_hb_font(shaper, glyphs + offset);
        if (!font)
            return false;
        int run_id = glyphs[offset].shape_run_id;
        int level = shaper->emblevels[offset];

        // advance in text until end of run
        while (i < (len - 1) && run_id == glyphs[i + 1].shape_run_id &&
                level == shaper->emblevels[i + 1])
            i++;

        hb_buffer_pre_allocate(buf, i - offset + 1);

        int lead_context = 0, trail_context = 0;
        if (shaper->whole_text_layout) {
            hb_buffer_add_utf32(buf, shaper->event_text, len,
                    offset, i - offset + 1);
        } else {
            if (offset > 0 && !glyphs[offset].starts_new_run &&
                    is_shaping_control(glyphs[offset - 1].symbol))
                lead_context = 1;
            if (i < (len - 1) && !glyphs[i + 1].starts_new_run &&
                    is_shaping_control(glyphs[i + 1].symbol))
                trail_context = 1;

            hb_buffer_add_utf32(buf,
                    shaper->event_text + offset - lead_context,
                    i - offset + 1 + lead_context + trail_context,
                    lead_context, i - offset + 1);
        }

        props.direction = FRIBIDI_LEVEL_IS_RTL(level) ?
            HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        props.script = glyphs[offset].script;
        props.language  = hb_shaper_get_run_language(shaper, props.script);
        hb_buffer_set_segment_properties(buf, &props);

        set_run_features(shaper, glyphs + offset);
        hb_shape(font, buf, shaper->features, shaper->n_features);

        shape_harfbuzz_process_run(glyphs, buf,
                shaper->whole_text_layout ? 0 : offset - lead_context);
        hb_buffer_reset(buf);
    }

    hb_buffer_destroy(buf);

    return true;
}

/**
 * \brief Determine script property of all characters. Characters of script
 * common and inherited get their script from their context.
 *
 */
void ass_shaper_determine_script(ASS_Shaper *shaper, GlyphInfo *glyphs,
                                  size_t len)
{
    int i;
    int backwards_scan = 0;
    hb_unicode_funcs_t *ufuncs = hb_unicode_funcs_get_default();
    hb_script_t last_script = HB_SCRIPT_UNKNOWN;

    // determine script (forward scan)
    for (i = 0; i < len; i++) {
        GlyphInfo *info = glyphs + i;
        info->script = hb_unicode_script(ufuncs, info->symbol);

        // common/inherit codepoints inherit script from context
        if (info->script == HB_SCRIPT_COMMON ||
                info->script == HB_SCRIPT_INHERITED) {
            // unknown is not a valid context
            if (last_script != HB_SCRIPT_UNKNOWN)
                info->script = last_script;
            else
                // do a backwards scan to check if next codepoint
                // contains a valid script for context
                backwards_scan = 1;
        } else {
            last_script = info->script;
        }
    }

    // determine script (backwards scan, if needed)
    last_script = HB_SCRIPT_UNKNOWN;
    for (i = len - 1; i >= 0 && backwards_scan; i--) {
        GlyphInfo *info = glyphs + i;

        // common/inherit codepoints inherit script from context
        if (info->script == HB_SCRIPT_COMMON ||
                info->script == HB_SCRIPT_INHERITED) {
            // unknown script is not a valid context
            if (last_script != HB_SCRIPT_UNKNOWN)
                info->script = last_script;
        } else {
            last_script = info->script;
        }
    }
}

/**
 * \brief Shape event text with FriBidi. Does mirroring and simple
 * Arabic shaping.
 * \param len number of clusters
 */
static void shape_fribidi(ASS_Shaper *shaper, GlyphInfo *glyphs, size_t len)
{
    int i;
    FriBidiJoiningType *joins = calloc(sizeof(*joins), len);

    // shape on codepoint level
    fribidi_get_joining_types(shaper->event_text, len, joins);
    fribidi_join_arabic(shaper->ctypes, len, shaper->emblevels, joins);
    fribidi_shape(FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC,
            shaper->emblevels, len, joins, shaper->event_text);

    // update indexes
    for (i = 0; i < len; i++) {
        GlyphInfo *info = glyphs + i;
        FT_Face face = info->font->faces[info->face_index];
        info->symbol = shaper->event_text[i];
        info->glyph_index = ass_font_index_magic(face, shaper->event_text[i]);
        if (info->glyph_index)
            info->glyph_index = FT_Get_Char_Index(face, info->glyph_index);
    }

    free(joins);
}

/**
 * \brief Toggle kerning for HarfBuzz shaping.
 * \param shaper shaper instance
 * \param kern toggle kerning
 */
void ass_shaper_set_kerning(ASS_Shaper *shaper, bool kern)
{
    shaper->features[KERN].value = kern;
}

/**
 * \brief Determine whether HarfBuzz ignores any font-provided glyph
 * for this Unicode codepoint and replaces it with a zero-width glyph.
 * Matches hb_unicode_funcs_t::is_default_ignorable in hb-unicode.hh.
 * The affected codepoints are a subset of Unicode's Default_Ignorable list.
 */
static inline bool is_harfbuzz_ignorable(unsigned symbol) {
    switch (symbol >> 8) {
        case 0x00: return symbol == 0x00AD;
        case 0x03: return symbol == 0x034F;
        case 0x06: return symbol == 0x061C;
        case 0x17: return symbol >= 0x17B4 && symbol <= 0x17B5;
        case 0x18: return symbol >= 0x180B && symbol <= 0x180E;
        case 0x20: return (symbol >= 0x200B && symbol <= 0x200F) ||
                          (symbol >= 0x202A && symbol <= 0x202E) ||
                          (symbol >= 0x2060 && symbol <= 0x206F);
        case 0xFE: return (symbol >= 0xFE00 && symbol <= 0xFE0F) ||
                          symbol == 0xFEFF;
        case 0xFF: return symbol >= 0xFFF0 && symbol <= 0xFFF8;
        case 0x1D1: return symbol >= 0x1D173 && symbol <= 0x1D17A;
        default: return symbol >= 0xE0000 && symbol <= 0xE0FFF;
    }
}

/**
  * \brief Remove all zero-width invisible characters from the text.
  */
static void ass_shaper_skip_characters(GlyphInfo *glyphs, size_t len)
{
    for (int i = 0; i < len; i++) {
        if (is_harfbuzz_ignorable(glyphs[i].symbol)) {
            glyphs[i].skip = true;
        }
    }
}

/**
 * \brief Find shape runs according to the event's selected fonts
 */
void ass_shaper_find_runs(ASS_Shaper *shaper, ASS_Renderer *render_priv,
                          GlyphInfo *glyphs, size_t len)
{
    int i;
    int shape_run = 0;

    ass_shaper_determine_script(shaper, glyphs, len);
    ass_shaper_skip_characters(glyphs, len);

    // find appropriate fonts for the shape runs
    for (i = 0; i < len; i++) {
        GlyphInfo *info = glyphs + i;
        if (!info->drawing_text.str && !info->skip) {
            // get font face and glyph index
            ass_font_get_index(render_priv->fontselect, info->font,
                    info->symbol, &info->face_index, &info->glyph_index);
        }
        if (i > 0) {
            GlyphInfo *last = glyphs + i - 1;
            if ((last->font != info->font ||
                    (!info->skip &&
                        last->face_index != info->face_index) ||
                    last->script != info->script ||
                    info->starts_new_run ||
                    (!shaper->whole_text_layout && info->hspacing) ||
                    last->flags != info->flags))
                shape_run++;
            else if (info->skip)
                info->face_index = last->face_index;
        }
        info->shape_run_id = shape_run;
    }
}

/**
 * \brief Set base direction (paragraph direction) of the text.
 * \param dir base direction
 */
void ass_shaper_set_base_direction(ASS_Shaper *shaper, FriBidiParType dir)
{
    shaper->base_direction = dir;

    if (shaper->whole_text_layout != WHOLE_TEXT_LAYOUT_EXPLICIT)
        shaper->whole_text_layout = dir == FRIBIDI_PAR_ON ?
            WHOLE_TEXT_LAYOUT_IMPLICIT : WHOLE_TEXT_LAYOUT_OFF;
}

/**
 * \brief Set language hint. Some languages have specific character variants,
 * like Serbian Cyrillic.
 * \param lang ISO 639-1 two-letter language code
 */
void ass_shaper_set_language(ASS_Shaper *shaper, const char *code)
{
    hb_language_t lang;

    if (code)
        lang = hb_language_from_string(code, -1);
    else
        lang = HB_LANGUAGE_INVALID;

    shaper->language = lang;
}

/**
 * Set shaping level. Essentially switches between FriBidi and HarfBuzz.
 */
void ass_shaper_set_level(ASS_Shaper *shaper, ASS_ShapingLevel level)
{
    shaper->shaping_level = level;
}

#ifdef USE_FRIBIDI_EX_API
void ass_shaper_set_bidi_brackets(ASS_Shaper *shaper, bool match_brackets)
{
    shaper->bidi_brackets = match_brackets;
}
#endif

void ass_shaper_set_whole_text_layout(ASS_Shaper *shaper, bool enable)
{
    shaper->whole_text_layout = enable ?
        WHOLE_TEXT_LAYOUT_EXPLICIT :
        shaper->base_direction == FRIBIDI_PAR_ON ?
            WHOLE_TEXT_LAYOUT_IMPLICIT : WHOLE_TEXT_LAYOUT_OFF;
}

/**
 * \brief Shape an event's text. Calculates directional runs and shapes them.
 * \param text_info event's text
 * \return success, when 0
 */
bool ass_shaper_shape(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i, ret, last_break;
    FriBidiParType dir, *pdir;
    GlyphInfo *glyphs = text_info->glyphs;
    shaper->event_text = text_info->event_text;

    if (!check_codepoint_allocations(shaper, text_info->length))
        return false;

    for (i = 0; i < text_info->length; i++)
        shaper->event_text[i] = glyphs[i].symbol;

    fribidi_get_bidi_types(shaper->event_text,
            text_info->length, shaper->ctypes);

    int n_pars = 1;
    for (i = 0; i < text_info->length - 1; i++)
        if (shaper->ctypes[i] == FRIBIDI_TYPE_BS)
            n_pars++;

    if (!check_par_allocations(shaper, n_pars))
        return false;

#ifdef USE_FRIBIDI_EX_API
    if (shaper->bidi_brackets) {
        fribidi_get_bracket_types(shaper->event_text,
                text_info->length, shaper->ctypes, shaper->btypes);
    }
#endif

    // Get bidi embedding levels
    last_break = 0;
    pdir = shaper->pbase_dir;
    for (i = 0; i < text_info->length; i++) {
        // Embedding levels must be calculated one bidi "paragraph" at a time
        if (i == text_info->length - 1 ||
                shaper->ctypes[i] == FRIBIDI_TYPE_BS ||
                (!shaper->whole_text_layout &&
                    (glyphs[i + 1].starts_new_run || glyphs[i].hspacing))) {
            dir = shaper->base_direction;
#ifdef USE_FRIBIDI_EX_API
            FriBidiBracketType *btypes = NULL;
            if (shaper->bidi_brackets)
                btypes = shaper->btypes + last_break;
            ret = fribidi_get_par_embedding_levels_ex(
                    shaper->ctypes + last_break, btypes,
                    i - last_break + 1, &dir, shaper->emblevels + last_break);
#else
            ret = fribidi_get_par_embedding_levels(shaper->ctypes + last_break,
                    i - last_break + 1, &dir, shaper->emblevels + last_break);
#endif
            if (ret == 0)
                return false;
            last_break = i + 1;
            if (shaper->whole_text_layout)
                *pdir++ = dir;
        }
    }

    switch (shaper->shaping_level) {
    case ASS_SHAPING_SIMPLE:
        shape_fribidi(shaper, glyphs, text_info->length);
        return true;
    case ASS_SHAPING_COMPLEX:
    default:
        return shape_harfbuzz(shaper, glyphs, text_info->length);
    }
}

/**
 * \brief Create a new shaper instance
 */
ASS_Shaper *ass_shaper_new(Cache *metrics_cache)
{
    assert(metrics_cache);

    ASS_Shaper *shaper = calloc(sizeof(*shaper), 1);
    if (!shaper)
        return NULL;

    shaper->base_direction = FRIBIDI_PAR_ON;

    if (!init_features(shaper))
        goto error;
    shaper->metrics_cache = metrics_cache;

    return shaper;

error:
    ass_shaper_free(shaper);
    return NULL;
}


/**
 * \brief clean up additional data temporarily needed for shaping and
 * (e.g. additional glyphs allocated)
 */
void ass_shaper_cleanup(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i;

    for (i = 0; i < text_info->length; i++) {
        GlyphInfo *info = text_info->glyphs + i;
        info = info->next;
        while (info) {
            GlyphInfo *next = info->next;
            free(info);
            info = next;
        }
    }
}

/**
 * \brief Calculate reorder map to render glyphs in visual order
 * \param shaper shaper instance
 * \param text_info text to be reordered
 * \return map of reordered characters, or NULL
 */
FriBidiStrIndex *ass_shaper_reorder(ASS_Shaper *shaper, TextInfo *text_info)
{
    int i, ret;

    // Initialize reorder map
    for (i = 0; i < text_info->length; i++)
        shaper->cmap[i] = i;

    // Create reorder map line-by-line or run-by-run
    int last_break = 0;
    FriBidiParType *pdir = shaper->whole_text_layout ?
        shaper->pbase_dir : &shaper->base_direction;
    GlyphInfo *glyphs = text_info->glyphs;
    for (i = 0; i < text_info->length; i++) {
        // Bidi "paragraph separators" may occur between line breaks:
        // U+001C..1E even with ASS_FEATURE_WRAP_UNICODE,
        // or U+000D, U+0085, U+2029 only without it
        if (i == text_info->length - 1 || glyphs[i + 1].linebreak ||
                shaper->ctypes[i] == FRIBIDI_TYPE_BS ||
                (!shaper->whole_text_layout &&
                    (glyphs[i + 1].starts_new_run || glyphs[i].hspacing))) {
            ret = fribidi_reorder_line(0,
                    shaper->ctypes, i - last_break + 1, last_break, *pdir,
                    shaper->emblevels, NULL,
                    shaper->cmap);
            if (ret == 0)
                return NULL;

            last_break = i + 1;
            if (shaper->whole_text_layout && shaper->ctypes[i] == FRIBIDI_TYPE_BS)
                pdir++;
        }
    }

    return shaper->cmap;
}

FriBidiStrIndex *ass_shaper_get_reorder_map(ASS_Shaper *shaper)
{
    return shaper->cmap;
}

/**
 * \brief Resolve a Windows font charset number to a suitable base
 * direction. Generally, use LTR for compatibility with VSFilter. The
 * special value -1, which is not a legal Windows font charset number,
 * can be used for autodetection.
 * \param enc Windows font encoding
 */
FriBidiParType ass_resolve_base_direction(int enc)
{
    switch (enc) {
        case -1:
            return FRIBIDI_PAR_ON;
        default:
            return FRIBIDI_PAR_LTR;
    }
}

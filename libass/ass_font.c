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

#include "config.h"
#include "ass_compat.h"

#include <inttypes.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_GLYPH_H
#include FT_TRUETYPE_TABLES_H
#include FT_OUTLINE_H
#include <limits.h>

#include "ass.h"
#include "ass_library.h"
#include "ass_font.h"
#include "ass_fontselect.h"
#include "ass_utils.h"
#include "ass_shaper.h"

/**
 * Select a good charmap, prefer Microsoft Unicode charmaps.
 * Otherwise, let FreeType decide.
 */
void charmap_magic(ASS_Library *library, FT_Face face)
{
    int i;
    int ms_cmap = -1;

    // Search for a Microsoft Unicode cmap
    for (i = 0; i < face->num_charmaps; ++i) {
        FT_CharMap cmap = face->charmaps[i];
        unsigned pid = cmap->platform_id;
        unsigned eid = cmap->encoding_id;
        if (pid == 3 /*microsoft */
            && (eid == 1 /*unicode bmp */
                || eid == 10 /*full unicode */ )) {
            FT_Set_Charmap(face, cmap);
            return;
        } else if (pid == 3 && ms_cmap < 0)
            ms_cmap = i;
    }

    // Try the first Microsoft cmap if no Microsoft Unicode cmap was found
    if (ms_cmap >= 0) {
        FT_CharMap cmap = face->charmaps[ms_cmap];
        FT_Set_Charmap(face, cmap);
        return;
    }

    if (!face->charmap) {
        if (face->num_charmaps == 0) {
            ass_msg(library, MSGL_WARN, "Font face with no charmaps");
            return;
        }
        ass_msg(library, MSGL_WARN,
                "No charmap autodetected, trying the first one");
        FT_Set_Charmap(face, face->charmaps[0]);
        return;
    }
}

/**
 * Adjust char index if the charmap is weird
 * (currently just MS Symbol)
 */

uint32_t ass_font_index_magic(FT_Face face, uint32_t symbol)
{
    if (!face->charmap)
        return symbol;

    switch (face->charmap->encoding) {
    case FT_ENCODING_MS_SYMBOL:
        return 0xF000 | symbol;
    default:
        return symbol;
    }
}

static void set_font_metrics(FT_Face face)
{
    // Mimicking GDI's behavior for asc/desc/height.
    // These fields are (apparently) sometimes used for signed values,
    // despite being unsigned in the spec.
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
    if (os2 && ((short)os2->usWinAscent + (short)os2->usWinDescent != 0)) {
        face->ascender  =  (short)os2->usWinAscent;
        face->descender = -(short)os2->usWinDescent;
        face->height    = face->ascender - face->descender;
    }

    // If we didn't have usable Win values in the OS/2 table,
    // then the values from FreeType will still be in these fields.
    // It'll use either the OS/2 typo metrics or the hhea ones.
    // If the font has typo metrics but FreeType didn't use them
    // (either old FT or USE_TYPO_METRICS not set), we'll try those.
    // In the case of a very broken font that has none of those options,
    // we fall back on using face.bbox.
    // Anything without valid OS/2 Win values isn't supported by VSFilter,
    // so at this point compatibility's out the window and we're just
    // trying to render _something_ readable.
    if (face->ascender - face->descender == 0 || face->height == 0) {
        if (os2 && (os2->sTypoAscender - os2->sTypoDescender) != 0) {
            face->ascender = os2->sTypoAscender;
            face->descender = os2->sTypoDescender;
            face->height = face->ascender - face->descender;
        } else {
            face->ascender = face->bbox.yMax;
            face->descender = face->bbox.yMin;
            face->height = face->ascender - face->descender;
        }
    }
}

static unsigned long
read_stream_font(FT_Stream stream, unsigned long offset, unsigned char *buffer,
                 unsigned long count)
{
    ASS_FontStream *font = (ASS_FontStream *)stream->descriptor.pointer;

    font->func(font->priv, buffer, offset, count);
    return count;
}

static void
close_stream_font(FT_Stream stream)
{
    free(stream->descriptor.pointer);
    free(stream);
}

/**
 * \brief Select a face with the given charcode and add it to ASS_Font
 * \return index of the new face in font->faces, -1 if failed
 */
static int add_face(ASS_FontSelector *fontsel, ASS_Font *font, uint32_t ch)
{
    char *path;
    char *postscript_name = NULL;
    int i, index, uid, error;
    ASS_FontStream stream = { NULL, NULL };
    FT_Face face;

    if (font->n_faces == ASS_FONT_MAX_FACES)
        return -1;

    path = ass_font_select(fontsel, font->library, font , &index,
            &postscript_name, &uid, &stream, ch);

    if (!path)
        return -1;

    for (i = 0; i < font->n_faces; i++) {
        if (font->faces_uid[i] == uid) {
            ass_msg(font->library, MSGL_INFO,
                    "Got a font face that already is available! Skipping.");
            return i;
        }
    }

    if (stream.func) {
        FT_Open_Args args;
        FT_Stream ftstream = calloc(1, sizeof(FT_StreamRec));
        ASS_FontStream *fs  = calloc(1, sizeof(ASS_FontStream));

        *fs = stream;
        ftstream->size  = stream.func(stream.priv, NULL, 0, 0);
        ftstream->read  = read_stream_font;
        ftstream->close = close_stream_font;
        ftstream->descriptor.pointer = (void *)fs;

        memset(&args, 0, sizeof(FT_Open_Args));
        args.flags  = FT_OPEN_STREAM;
        args.stream = ftstream;

        error = FT_Open_Face(font->ftlibrary, &args, index, &face);

        if (error) {
            ass_msg(font->library, MSGL_WARN,
                    "Error opening memory font: '%s'", path);
            return -1;
        }

    } else {
        error = FT_New_Face(font->ftlibrary, path, index, &face);
        if (error) {
            ass_msg(font->library, MSGL_WARN,
                    "Error opening font: '%s', %d", path, index);
            return -1;
        }

        if (postscript_name && index < 0 && face->num_faces > 0) {
            // The font provider gave us a post_script name and is not sure
            // about the face index.. so use the postscript name to find the
            // correct face_index in the collection!
            for (int i = 0; i < face->num_faces; i++) {
                FT_Done_Face(face);
                error = FT_New_Face(font->ftlibrary, path, i, &face);
                if (error) {
                    ass_msg(font->library, MSGL_WARN,
                            "Error opening font: '%s', %d", path, i);
                    return -1;
                }

                const char *face_psname = FT_Get_Postscript_Name(face);
                if (face_psname != NULL &&
                    strcmp(face_psname, postscript_name) == 0)
                    break;
            }
        }
    }

    charmap_magic(font->library, face);
    set_font_metrics(face);

    font->faces[font->n_faces] = face;
    font->faces_uid[font->n_faces++] = uid;
    ass_face_set_size(face, font->size);
    return font->n_faces - 1;
}

/**
 * \brief Create a new ASS_Font according to "desc" argument
 */
ASS_Font *ass_font_new(ASS_Renderer *render_priv, ASS_FontDesc *desc)
{
    ASS_Font *font = ass_cache_get(render_priv->cache.font_cache, desc, render_priv);
    if (!font)
        return NULL;
    if (font->library)
        return font;
    ass_cache_dec_ref(font);
    return NULL;
}

size_t ass_font_construct(void *key, void *value, void *priv)
{
    ASS_Renderer *render_priv = priv;
    ASS_FontDesc *desc = key;
    ASS_Font *font = value;

    font->library = render_priv->library;
    font->ftlibrary = render_priv->ftlibrary;
    font->shaper_priv = NULL;
    font->n_faces = 0;
    font->desc.family = desc->family;
    font->desc.bold = desc->bold;
    font->desc.italic = desc->italic;
    font->desc.vertical = desc->vertical;

    font->size = 0.;

    int error = add_face(render_priv->fontselect, font, 0);
    if (error == -1)
        font->library = NULL;
    return 1;
}

void ass_face_set_size(FT_Face face, double size)
{
    FT_Size_RequestRec rq;
    memset(&rq, 0, sizeof(rq));
    rq.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
    rq.width = 0;
    rq.height = double_to_d6(size);
    rq.horiResolution = rq.vertResolution = 0;
    FT_Request_Size(face, &rq);
}

/**
 * \brief Set font size
 **/
void ass_font_set_size(ASS_Font *font, double size)
{
    int i;
    if (font->size != size) {
        font->size = size;
        for (i = 0; i < font->n_faces; ++i)
            ass_face_set_size(font->faces[i], size);
    }
}

/**
 * \brief Get face weight
 **/
int ass_face_get_weight(FT_Face face)
{
#if FREETYPE_MAJOR > 2 || (FREETYPE_MAJOR == 2 && FREETYPE_MINOR >= 6)
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
#else
    // This old name is still included (as a macro), but deprecated as of 2.6, so avoid using it if we can
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
#endif
    if (os2 && os2->version != 0xffff && os2->usWeightClass)
        return os2->usWeightClass;
    else
        return 300 * !!(face->style_flags & FT_STYLE_FLAG_BOLD) + 400;
}

/**
 * \brief Get maximal font ascender and descender.
 **/
void ass_font_get_asc_desc(ASS_Font *font, int face_index,
                           int *asc, int *desc)
{
    FT_Face face = font->faces[face_index];
    int y_scale = face->size->metrics.y_scale;
    *asc  = FT_MulFix(face->ascender, y_scale);
    *desc = FT_MulFix(-face->descender, y_scale);
}

static void add_line(FT_Outline *ol, int bear, int advance, int dir, int pos, int size) {
    FT_Vector points[4] = {
        {.x = bear,      .y = pos + size},
        {.x = advance,   .y = pos + size},
        {.x = advance,   .y = pos - size},
        {.x = bear,      .y = pos - size},
    };

    if (dir == FT_ORIENTATION_TRUETYPE) {
        int i;
        for (i = 0; i < 4; i++) {
            ol->points[ol->n_points] = points[i];
            ol->tags[ol->n_points++] = 1;
        }
    } else {
        int i;
        for (i = 3; i >= 0; i--) {
            ol->points[ol->n_points] = points[i];
            ol->tags[ol->n_points++] = 1;
        }
    }

    ol->contours[ol->n_contours++] = ol->n_points - 1;
}

/*
 * Strike a glyph with a horizontal line; it's possible to underline it
 * and/or strike through it.  For the line's position and size, truetype
 * tables are consulted.  Obviously this relies on the data in the tables
 * being accurate.
 *
 */
static int ass_strike_outline_glyph(FT_Face face, ASS_Font *font,
                                    FT_Glyph glyph, int under, int through)
{
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
    TT_Postscript *ps = FT_Get_Sfnt_Table(face, ft_sfnt_post);
    FT_Outline *ol = &((FT_OutlineGlyph) glyph)->outline;
    int advance, y_scale, i, dir;

    if (!under && !through)
        return 0;

    // Grow outline
    i = (under ? 4 : 0) + (through ? 4 : 0);
    if (ol->n_points > SHRT_MAX - i)
        return 0;
    if (!ASS_REALLOC_ARRAY(ol->points, ol->n_points + i))
        return 0;
    if (!ASS_REALLOC_ARRAY(ol->tags, ol->n_points + i))
        return 0;
    i = !!under + !!through;
    if (ol->n_contours > SHRT_MAX - i)
        return 0;
    if (!ASS_REALLOC_ARRAY(ol->contours, ol->n_contours + i))
        return 0;

    advance = d16_to_d6(glyph->advance.x);
    y_scale = face->size->metrics.y_scale;

    // Reverse drawing direction for non-truetype fonts
    dir = FT_Outline_Get_Orientation(ol);

    // Add points to the outline
    if (under && ps) {
        int pos = FT_MulFix(ps->underlinePosition, y_scale);
        int size = FT_MulFix(ps->underlineThickness, y_scale / 2);

        if (pos > 0 || size <= 0)
            return 1;

        add_line(ol, 0, advance, dir, pos, size);
    }

    if (through && os2) {
        int pos = FT_MulFix(os2->yStrikeoutPosition, y_scale);
        int size = FT_MulFix(os2->yStrikeoutSize, y_scale / 2);

        if (pos < 0 || size <= 0)
            return 1;

        add_line(ol, 0, advance, dir, pos, size);
    }

    return 0;
}

/**
 * Slightly embold a glyph without touching its metrics
 */
static void ass_glyph_embolden(FT_GlyphSlot slot)
{
    int str;

    if (slot->format != FT_GLYPH_FORMAT_OUTLINE)
        return;

    str = FT_MulFix(slot->face->units_per_EM,
                    slot->face->size->metrics.y_scale) / 64;

    FT_Outline_Embolden(&slot->outline, str);
}

/**
 * \brief Get glyph and face index
 * Finds a face that has the requested codepoint and returns both face
 * and glyph index.
 */
int ass_font_get_index(ASS_FontSelector *fontsel, ASS_Font *font,
                       uint32_t symbol, int *face_index, int *glyph_index)
{
    int index = 0;
    int i;
    FT_Face face = 0;

    *glyph_index = 0;

    if (symbol < 0x20) {
        *face_index = 0;
        return 0;
    }
    // Handle NBSP like a regular space when rendering the glyph
    if (symbol == 0xa0)
        symbol = ' ';
    if (font->n_faces == 0) {
        *face_index = 0;
        return 0;
    }

    // try with the requested face
    if (*face_index < font->n_faces) {
        face = font->faces[*face_index];
        index = FT_Get_Char_Index(face, ass_font_index_magic(face, symbol));
    }

    // not found in requested face, try all others
    for (i = 0; i < font->n_faces && index == 0; ++i) {
        face = font->faces[i];
        index = FT_Get_Char_Index(face, ass_font_index_magic(face, symbol));
        if (index)
            *face_index = i;
    }

    if (index == 0) {
        int face_idx;
        ass_msg(font->library, MSGL_INFO,
                "Glyph 0x%X not found, selecting one more "
                "font for (%.*s, %d, %d)", symbol, (int)font->desc.family.len, font->desc.family.str,
                font->desc.bold, font->desc.italic);
        face_idx = *face_index = add_face(fontsel, font, symbol);
        if (face_idx >= 0) {
            face = font->faces[face_idx];
            index = FT_Get_Char_Index(face, ass_font_index_magic(face, symbol));
            if (index == 0 && face->num_charmaps > 0) {
                int i;
                ass_msg(font->library, MSGL_WARN,
                    "Glyph 0x%X not found, broken font? Trying all charmaps", symbol);
                for (i = 0; i < face->num_charmaps; i++) {
                    FT_Set_Charmap(face, face->charmaps[i]);
                    if ((index = FT_Get_Char_Index(face, ass_font_index_magic(face, symbol))) != 0) break;
                }
            }
            if (index == 0) {
                ass_msg(font->library, MSGL_ERR,
                        "Glyph 0x%X not found in font for (%.*s, %d, %d)",
                        symbol, (int)font->desc.family.len, font->desc.family.str, font->desc.bold,
                        font->desc.italic);
            }
        }
    }

    // FIXME: make sure we have a valid face_index. this is a HACK.
    *face_index  = FFMAX(*face_index, 0);
    *glyph_index = index;

    return 1;
}

/**
 * \brief Get a glyph
 * \param ch character code
 **/
FT_Glyph ass_font_get_glyph(ASS_Font *font, int face_index, int index,
                            ASS_Hinting hinting, int deco)
{
    int error;
    FT_Glyph glyph;
    FT_Face face = font->faces[face_index];
    int flags = 0;

    flags = FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH
            | FT_LOAD_IGNORE_TRANSFORM;
    switch (hinting) {
    case ASS_HINTING_NONE:
        flags |= FT_LOAD_NO_HINTING;
        break;
    case ASS_HINTING_LIGHT:
        flags |= FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT;
        break;
    case ASS_HINTING_NORMAL:
        flags |= FT_LOAD_FORCE_AUTOHINT;
        break;
    case ASS_HINTING_NATIVE:
        break;
    }

    error = FT_Load_Glyph(face, index, flags);
    if (error) {
        ass_msg(font->library, MSGL_WARN, "Error loading glyph, index %d",
                index);
        return 0;
    }
    if (!(face->style_flags & FT_STYLE_FLAG_ITALIC) &&
        (font->desc.italic > 55)) {
        FT_GlyphSlot_Oblique(face->glyph);
    }

    if (font->desc.bold > ass_face_get_weight(face) + 150) {
        ass_glyph_embolden(face->glyph);
    }
    error = FT_Get_Glyph(face->glyph, &glyph);
    if (error) {
        ass_msg(font->library, MSGL_WARN, "Error loading glyph, index %d",
                index);
        return 0;
    }

    // Rotate glyph, if needed
    if (deco & DECO_ROTATE) {
        FT_Matrix m = { 0, double_to_d16(-1.0), double_to_d16(1.0), 0 };
        TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        int desc = 0;

        if (os2)
            desc = FT_MulFix(os2->sTypoDescender, face->size->metrics.y_scale);

        FT_Outline_Translate(&((FT_OutlineGlyph) glyph)->outline, 0, -desc);
        FT_Outline_Transform(&((FT_OutlineGlyph) glyph)->outline, &m);
        FT_Outline_Translate(&((FT_OutlineGlyph) glyph)->outline,
                             face->glyph->metrics.vertAdvance, desc);
        glyph->advance.x = face->glyph->linearVertAdvance;
    }

    ass_strike_outline_glyph(face, font, glyph, deco & DECO_UNDERLINE,
                             deco & DECO_STRIKETHROUGH);

    return glyph;
}

/**
 * \brief Deallocate ASS_Font internals
 **/
void ass_font_clear(ASS_Font *font)
{
    int i;
    if (font->shaper_priv)
        ass_shaper_font_data_free(font->shaper_priv);
    for (i = 0; i < font->n_faces; ++i) {
        if (font->faces[i])
            FT_Done_Face(font->faces[i]);
    }
    free((char *) font->desc.family.str);
}

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

    switch(face->charmap->encoding){
    case FT_ENCODING_MS_SYMBOL:
        return 0xF000 | symbol;
    default:
        return symbol;
    }
}

static void buggy_font_workaround(FT_Face face)
{
    // Some fonts have zero Ascender/Descender fields in 'hhea' table.
    // In this case, get the information from 'os2' table or, as
    // a last resort, from face.bbox.
    if (face->ascender + face->descender == 0 || face->height == 0) {
        TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        if (os2) {
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
    buggy_font_workaround(face);

    font->faces[font->n_faces] = face;
    font->faces_uid[font->n_faces++] = uid;
    ass_face_set_size(face, font->size);
    return font->n_faces - 1;
}

/**
 * \brief Create a new ASS_Font according to "desc" argument
 */
ASS_Font *ass_font_new(Cache *font_cache, ASS_Library *library,
                       FT_Library ftlibrary, ASS_FontSelector *fontsel,
                       ASS_FontDesc *desc)
{
    ASS_Font *font;
    if (ass_cache_get(font_cache, desc, &font)) {
        if (font->desc.family)
            return font;
        ass_cache_dec_ref(font);
        return NULL;
    }
    if (!font)
        return NULL;

    font->library = library;
    font->ftlibrary = ftlibrary;
    font->shaper_priv = NULL;
    font->n_faces = 0;
    ASS_FontDesc *new_desc = ass_cache_key(font);
    font->desc.family = new_desc->family;
    font->desc.bold = desc->bold;
    font->desc.italic = desc->italic;
    font->desc.vertical = desc->vertical;

    font->scale_x = font->scale_y = 1.;
    font->v.x = font->v.y = 0;
    font->size = 0.;

    int error = add_face(fontsel, font, 0);
    if (error == -1) {
        font->desc.family = NULL;
        ass_cache_commit(font, 1);
        ass_cache_dec_ref(font);
        return NULL;
    }
    ass_cache_commit(font, 1);
    return font;
}

/**
 * \brief Set font transformation matrix and shift vector
 **/
void ass_font_set_transform(ASS_Font *font, double scale_x,
                            double scale_y, FT_Vector *v)
{
    font->scale_x = scale_x;
    font->scale_y = scale_y;
    if (v) {
        font->v.x = v->x;
        font->v.y = v->y;
    }
}

void ass_face_set_size(FT_Face face, double size)
{
    TT_HoriHeader *hori = FT_Get_Sfnt_Table(face, ft_sfnt_hhea);
    TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
    double mscale = 1.;
    FT_Size_RequestRec rq;
    FT_Size_Metrics *m = &face->size->metrics;
    // VSFilter uses metrics from TrueType OS/2 table
    // The idea was borrowed from asa (http://asa.diac24.net)
    if (os2) {
        int ft_height = 0;
        if (hori)
            ft_height = hori->Ascender - hori->Descender;
        if (!ft_height)
            ft_height = os2->sTypoAscender - os2->sTypoDescender;
        /* sometimes used for signed values despite unsigned in spec */
        int os2_height = (short)os2->usWinAscent + (short)os2->usWinDescent;
        if (ft_height && os2_height)
            mscale = (double) ft_height / os2_height;
    }
    memset(&rq, 0, sizeof(rq));
    rq.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
    rq.width = 0;
    rq.height = double_to_d6(size * mscale);
    rq.horiResolution = rq.vertResolution = 0;
    FT_Request_Size(face, &rq);
    m->ascender /= mscale;
    m->descender /= mscale;
    m->height /= mscale;
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
 * \brief Get maximal font ascender and descender.
 * \param ch character code
 * The values are extracted from the font face that provides glyphs for the given character
 **/
void ass_font_get_asc_desc(ASS_Font *font, uint32_t ch, FT_F26Dot6 *asc,
                           FT_F26Dot6 *desc)
{
    int i;
    for (i = 0; i < font->n_faces; ++i) {
        FT_Face face = font->faces[i];
        TT_OS2 *os2 = FT_Get_Sfnt_Table(face, ft_sfnt_os2);
        if (FT_Get_Char_Index(face, ass_font_index_magic(face, ch))) {
            /* 16.16 scaling value that converts from font units to 26.6 */
            FT_Fixed y_scale = face->size->metrics.y_scale;
            if (os2) {
                *asc = FT_MulFix((short)os2->usWinAscent, y_scale);
                *desc = FT_MulFix((short)os2->usWinDescent, y_scale);
            } else {
                /* could also use face->size->metrics.ascender w/o MulFix */
                *asc = FT_MulFix(face->ascender, y_scale);
                *desc = FT_MulFix(-face->descender, y_scale);
            }
            return;
        }
    }

    *asc = *desc = 0;
}

static void add_line(FT_Outline *ol, FT_F26Dot6 bear, FT_F26Dot6 advance,
                     FT_Orientation dir, FT_F26Dot6 pos, FT_F26Dot6 size)
{
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
    FT_F26Dot6 advance;
    FT_Fixed y_scale;
    FT_Orientation dir;
    int i;

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
        FT_F26Dot6 pos = FT_MulFix(ps->underlinePosition, y_scale);
        FT_F26Dot6 size = FT_MulFix(ps->underlineThickness, y_scale / 2);

        if (pos > 0 || size <= 0)
            return 1;

        add_line(ol, 0, advance, dir, pos, size);
    }

    if (through && os2) {
        FT_Fixed pos = FT_MulFix(os2->yStrikeoutPosition, y_scale);
        FT_Fixed size = FT_MulFix(os2->yStrikeoutSize, y_scale / 2);

        if (pos < 0 || size <= 0)
            return 1;

        add_line(ol, 0, advance, dir, pos, size);
    }

    return 0;
}


int outline_alloc(ASS_Outline *outline, size_t n_points, size_t n_contours)
{
    outline->contours = malloc(sizeof(size_t) * n_contours);
    outline->points = malloc(sizeof(FT_Vector) * n_points);
    outline->tags = malloc(n_points);
    if (!outline->contours || !outline->points || !outline->tags)
        return 0;

    outline->max_contours = n_contours;
    outline->max_points = n_points;
    return 1;
}

ASS_Outline *outline_convert(const FT_Outline *source)
{
    if (!source)
        return NULL;

    ASS_Outline *ol = calloc(1, sizeof(*ol));
    if (!ol)
        return NULL;

    if (!outline_alloc(ol, source->n_points, source->n_contours)) {
        outline_free(ol);
        free(ol);
        return NULL;
    }

    for (int i = 0; i < source->n_contours; ++i)
        ol->contours[i] = source->contours[i];
    memcpy(ol->points, source->points, sizeof(FT_Vector) * source->n_points);
    memcpy(ol->tags, source->tags, source->n_points);
    ol->n_contours = source->n_contours;
    ol->n_points = source->n_points;
    return ol;
}

ASS_Outline *outline_copy(const ASS_Outline *source)
{
    if (!source)
        return NULL;

    ASS_Outline *ol = calloc(1, sizeof(*ol));
    if (!ol)
        return NULL;

    if (!outline_alloc(ol, source->n_points, source->n_contours)) {
        outline_free(ol);
        free(ol);
        return NULL;
    }

    memcpy(ol->contours, source->contours, sizeof(size_t) * source->n_contours);
    memcpy(ol->points, source->points, sizeof(FT_Vector) * source->n_points);
    memcpy(ol->tags, source->tags, source->n_points);
    ol->n_contours = source->n_contours;
    ol->n_points = source->n_points;
    return ol;
}

void outline_free(ASS_Outline *outline)
{
    if (!outline)
        return;

    free(outline->contours);
    free(outline->points);
    free(outline->tags);
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
                "font for (%s, %d, %d)", symbol, font->desc.family,
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
                        "Glyph 0x%X not found in font for (%s, %d, %d)",
                        symbol, font->desc.family, font->desc.bold,
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
FT_Glyph ass_font_get_glyph(ASS_Font *font, uint32_t ch, int face_index,
                            int index, ASS_Hinting hinting, int deco)
{
    int error;
    FT_Glyph glyph;
    FT_Face face = font->faces[face_index];
    int flags = 0;
    int vertical = font->desc.vertical;

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

    if (!(face->style_flags & FT_STYLE_FLAG_BOLD) &&
        (font->desc.bold > 400)) {
        ass_glyph_embolden(face->glyph);
    }
    error = FT_Get_Glyph(face->glyph, &glyph);
    if (error) {
        ass_msg(font->library, MSGL_WARN, "Error loading glyph, index %d",
                index);
        return 0;
    }

    // Rotate glyph, if needed
    if (vertical && ch >= VERTICAL_LOWER_BOUND) {
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

    // Apply scaling and shift
    FT_Matrix scale = { double_to_d16(font->scale_x), 0, 0,
                        double_to_d16(font->scale_y) };
    FT_Outline *outl = &((FT_OutlineGlyph) glyph)->outline;
    FT_Outline_Transform(outl, &scale);
    FT_Outline_Translate(outl, font->v.x, font->v.y);
    glyph->advance.x *= font->scale_x;

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
    free(font->desc.family);
}

/**
 * \brief Calculate the cbox of a series of points
 */
static void
get_contour_cbox(FT_BBox *box, FT_Vector *points, int start, int end)
{
    box->xMin = box->yMin = INT_MAX;
    box->xMax = box->yMax = INT_MIN;
    int i;

    for (i = start; i <= end; i++) {
        box->xMin = (points[i].x < box->xMin) ? points[i].x : box->xMin;
        box->xMax = (points[i].x > box->xMax) ? points[i].x : box->xMax;
        box->yMin = (points[i].y < box->yMin) ? points[i].y : box->yMin;
        box->yMax = (points[i].y > box->yMax) ? points[i].y : box->yMax;
    }
}

/**
 * \brief Determine signed area of a contour
 * \return area doubled
 */
static long long get_contour_area(FT_Vector *points, int start, int end)
{
    long long area = 0;
    FT_Pos x = points[end].x;
    FT_Pos y = points[end].y;
    for (int i = start; i <= end; i++) {
        area += (long long)(points[i].x + x) * (points[i].y - y);
        x = points[i].x;
        y = points[i].y;
    }
    return area;
}

void outline_translate(const ASS_Outline *outline, FT_Pos dx, FT_Pos dy)
{
    for (size_t i = 0; i < outline->n_points; ++i) {
        outline->points[i].x += dx;
        outline->points[i].y += dy;
    }
}

void outline_transform(const ASS_Outline *outline, const FT_Matrix *matrix)
{
    for (size_t i = 0; i < outline->n_points; ++i) {
        FT_Pos x = FT_MulFix(outline->points[i].x, matrix->xx) +
                   FT_MulFix(outline->points[i].y, matrix->xy);
        FT_Pos y = FT_MulFix(outline->points[i].x, matrix->yx) +
                   FT_MulFix(outline->points[i].y, matrix->yy);
        outline->points[i].x = x;
        outline->points[i].y = y;
    }
}

void outline_get_cbox(const ASS_Outline *outline, FT_BBox *cbox)
{
    if (!outline->n_points) {
        cbox->xMin = cbox->xMax = 0;
        cbox->yMin = cbox->yMax = 0;
        return;
    }
    cbox->xMin = cbox->xMax = outline->points[0].x;
    cbox->yMin = cbox->yMax = outline->points[0].y;
    for (size_t i = 1; i < outline->n_points; ++i) {
        cbox->xMin = FFMIN(cbox->xMin, outline->points[i].x);
        cbox->xMax = FFMAX(cbox->xMax, outline->points[i].x);
        cbox->yMin = FFMIN(cbox->yMin, outline->points[i].y);
        cbox->yMax = FFMAX(cbox->yMax, outline->points[i].y);
    }
}

/**
 * \brief Apply fixups to please the FreeType stroker and improve the
 * rendering result, especially in case the outline has some anomalies.
 * At the moment, the following fixes are done:
 *
 * 1. Reverse contours that have "inside" winding direction but are not
 *    contained in any other contours' cbox.
 * 2. Remove "inside" contours depending on border size, so that large
 *    borders do not reverse the winding direction, which leads to "holes"
 *    inside the border. The inside will be filled by the border of the
 *    outside contour anyway in this case.
 *
 * \param outline FreeType outline, modified in-place
 * \param border_x border size, x direction, d6 format
 * \param border_x border size, y direction, d6 format
 */
void fix_freetype_stroker(ASS_Outline *outline, int border_x, int border_y)
{
    int nc = outline->n_contours;
    int begin, stop;
    char modified = 0;
    char *valid_cont = malloc(nc);
    int start = 0;
    int end = -1;
    FT_BBox *boxes = malloc(nc * sizeof(FT_BBox));
    int i, j;

    long long area = 0;
    // create a list of cboxes of the contours
    for (i = 0; i < nc; i++) {
        start = end + 1;
        end = outline->contours[i];
        get_contour_cbox(&boxes[i], outline->points, start, end);
        area += get_contour_area(outline->points, start, end);
    }
    int inside_direction = area < 0;

    // for each contour, check direction and whether it's "outside"
    // or contained in another contour
    end = -1;
    for (i = 0; i < nc; i++) {
        start = end + 1;
        end = outline->contours[i];
        int dir = get_contour_area(outline->points, start, end) > 0;
        valid_cont[i] = 1;
        if (dir == inside_direction) {
            for (j = 0; j < nc; j++) {
                if (i == j)
                    continue;
                if (boxes[i].xMin >= boxes[j].xMin &&
                    boxes[i].xMax <= boxes[j].xMax &&
                    boxes[i].yMin >= boxes[j].yMin &&
                    boxes[i].yMax <= boxes[j].yMax)
                    goto check_inside;
            }
            /* "inside" contour but we can't find anything it could be
             * inside of - assume the font is buggy and it should be
             * an "outside" contour, and reverse it */
            for (j = 0; j < (end - start) / 2; j++) {
                FT_Vector temp = outline->points[start + 1 + j];
                char temp2 = outline->tags[start + 1 + j];
                outline->points[start + 1 + j] = outline->points[end - j];
                outline->points[end - j] = temp;
                outline->tags[start + 1 + j] = outline->tags[end - j];
                outline->tags[end - j] = temp2;
            }
            dir ^= 1;
        }
        check_inside:
        if (dir == inside_direction) {
            FT_BBox box;
            get_contour_cbox(&box, outline->points, start, end);
            FT_Pos width = box.xMax - box.xMin;
            FT_Pos height = box.yMax - box.yMin;
            if (width < border_x * 2 || height < border_y * 2) {
                valid_cont[i] = 0;
                modified = 1;
            }
        }
    }

    // if we need to modify the outline, rewrite it and skip
    // the contours that we determined should be removed.
    if (modified) {
        int p = 0, c = 0;
        for (i = 0; i < nc; i++) {
            if (!valid_cont[i])
                continue;
            begin = (i == 0) ? 0 : outline->contours[i - 1] + 1;
            stop = outline->contours[i];
            for (j = begin; j <= stop; j++) {
                outline->points[p].x = outline->points[j].x;
                outline->points[p].y = outline->points[j].y;
                outline->tags[p] = outline->tags[j];
                p++;
            }
            outline->contours[c] = p - 1;
            c++;
        }
        outline->n_points = p;
        outline->n_contours = c;
    }

    free(boxes);
    free(valid_cont);
}


/*
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
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

#include <ft2build.h>
#include FT_OUTLINE_H
#include FT_BBOX_H
#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "ass_utils.h"
#include "ass_drawing.h"
#include "ass_font.h"

#define CURVE_ACCURACY 64.0
#define GLYPH_INITIAL_POINTS 100
#define GLYPH_INITIAL_CONTOURS 5

/*
 * \brief Add a single point to a contour.
 */
static inline bool drawing_add_point(ASS_Drawing *drawing,
                                     const FT_Vector *point, char tags)
{
    ASS_Outline *ol = &drawing->outline;
    if (ol->n_points >= ol->max_points) {
        size_t new_size = 2 * ol->max_points;
        if (!ASS_REALLOC_ARRAY(ol->points, new_size))
            return false;
        if (!ASS_REALLOC_ARRAY(ol->tags, new_size))
            return false;
        ol->max_points = new_size;
    }

    ol->points[ol->n_points].x = point->x;
    ol->points[ol->n_points].y = point->y;
    ol->tags[ol->n_points] = tags;
    ol->n_points++;
    return true;
}

/*
 * \brief Close a contour and check outline size overflow.
 */
static inline bool drawing_close_shape(ASS_Drawing *drawing)
{
    ASS_Outline *ol = &drawing->outline;
    if (ol->n_contours >= ol->max_contours) {
        size_t new_size = 2 * ol->max_contours;
        if (!ASS_REALLOC_ARRAY(ol->contours, new_size))
            return false;
        ol->max_contours = new_size;
    }

    ol->contours[ol->n_contours] = ol->n_points - 1;
    ol->n_contours++;
    return true;
}

/*
 * \brief Prepare drawing for parsing.  This just sets a few parameters.
 */
static void drawing_prepare(ASS_Drawing *drawing)
{
    // Scaling parameters
    drawing->point_scale_x = drawing->scale_x / (1 << (drawing->scale - 1));
    drawing->point_scale_y = drawing->scale_y / (1 << (drawing->scale - 1));
}

/*
 * \brief Finish a drawing.  This only sets the horizontal advance according
 * to the outline's bbox at the moment.
 */
static void drawing_finish(ASS_Drawing *drawing, int raw_mode)
{
    int i;
    double pbo;
    FT_BBox bbox = drawing->cbox;
    ASS_Outline *ol = &drawing->outline;

    if (drawing->library)
        ass_msg(drawing->library, MSGL_V,
                "Parsed drawing with %d points and %d contours", ol->n_points,
                ol->n_contours);

    if (raw_mode)
        return;

    drawing->advance.x = bbox.xMax - bbox.xMin;

    pbo = drawing->pbo / (1 << (drawing->scale - 1));
    drawing->desc = double_to_d6(pbo * drawing->scale_y);
    drawing->asc = bbox.yMax - bbox.yMin - drawing->desc;

    // Place it onto the baseline
    for (i = 0; i < ol->n_points; i++)
        ol->points[i].y += drawing->asc;
}

/*
 * \brief Check whether a number of items on the list is available
 */
static int token_check_values(ASS_DrawingToken *token, int i, int type)
{
    int j;
    for (j = 0; j < i; j++) {
        if (!token || token->type != type) return 0;
        token = token->next;
    }

    return 1;
}

/*
 * \brief Tokenize a drawing string into a list of ASS_DrawingToken
 * This also expands points for closing b-splines
 */
static ASS_DrawingToken *drawing_tokenize(char *str)
{
    char *p = str;
    int i, type = -1, is_set = 0;
    double val;
    FT_Vector point = {0, 0};

    ASS_DrawingToken *root = NULL, *tail = NULL, *spline_start = NULL;

    while (p && *p) {
        if (*p == 'c' && spline_start) {
            // Close b-splines: add the first three points of the b-spline
            // back to the end
            if (token_check_values(spline_start->next, 2, TOKEN_B_SPLINE)) {
                for (i = 0; i < 3; i++) {
                    tail->next = calloc(1, sizeof(ASS_DrawingToken));
                    tail->next->prev = tail;
                    tail = tail->next;
                    tail->type = TOKEN_B_SPLINE;
                    tail->point = spline_start->point;
                    spline_start = spline_start->next;
                }
                spline_start = NULL;
            }
        } else if (!is_set && mystrtod(&p, &val)) {
            point.x = double_to_d6(val);
            is_set = 1;
            p--;
        } else if (is_set == 1 && mystrtod(&p, &val)) {
            point.y = double_to_d6(val);
            is_set = 2;
            p--;
        } else if (*p == 'm')
            type = TOKEN_MOVE;
        else if (*p == 'n')
            type = TOKEN_MOVE_NC;
        else if (*p == 'l')
            type = TOKEN_LINE;
        else if (*p == 'b')
            type = TOKEN_CUBIC_BEZIER;
        else if (*p == 'q')
            type = TOKEN_CONIC_BEZIER;
        else if (*p == 's')
            type = TOKEN_B_SPLINE;
        // We're simply ignoring TOKEN_EXTEND_B_SPLINE here.
        // This is not harmful at all, since it can be ommitted with
        // similar result (the spline is extended anyway).

        if (type != -1 && is_set == 2) {
            if (root) {
                tail->next = calloc(1, sizeof(ASS_DrawingToken));
                tail->next->prev = tail;
                tail = tail->next;
            } else
                root = tail = calloc(1, sizeof(ASS_DrawingToken));
            tail->type = type;
            tail->point = point;
            is_set = 0;
            if (type == TOKEN_B_SPLINE && !spline_start)
                spline_start = tail->prev;
        }
        p++;
    }

    return root;
}

/*
 * \brief Free a list of tokens
 */
static void drawing_free_tokens(ASS_DrawingToken *token)
{
    while (token) {
        ASS_DrawingToken *at = token;
        token = token->next;
        free(at);
    }
}

/*
 * \brief Update drawing cbox
 */
static inline void update_cbox(ASS_Drawing *drawing, FT_Vector *point)
{
    FT_BBox *box = &drawing->cbox;

    box->xMin = FFMIN(box->xMin, point->x);
    box->xMax = FFMAX(box->xMax, point->x);
    box->yMin = FFMIN(box->yMin, point->y);
    box->yMax = FFMAX(box->yMax, point->y);
}

/*
 * \brief Translate and scale a point coordinate according to baseline
 * offset and scale.
 */
static inline void translate_point(ASS_Drawing *drawing, FT_Vector *point)
{
    point->x = drawing->point_scale_x * point->x;
    point->y = drawing->point_scale_y * -point->y;

    update_cbox(drawing, point);
}

/*
 * \brief Evaluate a curve into lines
 * This curve evaluator is also used in VSFilter (RTS.cpp); it's a simple
 * implementation of the De Casteljau algorithm.
 */
static bool drawing_evaluate_curve(ASS_Drawing *drawing,
                                   ASS_DrawingToken *token, char spline,
                                   int started)
{
    FT_Vector p[4];
    for (int i = 0; i < 4; ++i) {
        p[i] = token->point;
        translate_point(drawing, &p[i]);
        token = token->next;
    }

    if (spline) {
        int x01 = (p[1].x - p[0].x) / 3;
        int y01 = (p[1].y - p[0].y) / 3;
        int x12 = (p[2].x - p[1].x) / 3;
        int y12 = (p[2].y - p[1].y) / 3;
        int x23 = (p[3].x - p[2].x) / 3;
        int y23 = (p[3].y - p[2].y) / 3;

        p[0].x = p[1].x + ((x12 - x01) >> 1);
        p[0].y = p[1].y + ((y12 - y01) >> 1);
        p[3].x = p[2].x + ((x23 - x12) >> 1);
        p[3].y = p[2].y + ((y23 - y12) >> 1);
        p[1].x += x12;
        p[1].y += y12;
        p[2].x -= x12;
        p[2].y -= y12;
    }

    return (started || drawing_add_point(drawing, &p[0], FT_CURVE_TAG_ON)) &&
        drawing_add_point(drawing, &p[1], FT_CURVE_TAG_CUBIC) &&
        drawing_add_point(drawing, &p[2], FT_CURVE_TAG_CUBIC) &&
        drawing_add_point(drawing, &p[3], FT_CURVE_TAG_ON);
}

/*
 * \brief Create and initialize a new drawing and return it
 */
ASS_Drawing *ass_drawing_new(ASS_Library *lib, FT_Library ftlib)
{
    ASS_Drawing *drawing = calloc(1, sizeof(*drawing));
    if (!drawing)
        return NULL;
    drawing->cbox.xMin = drawing->cbox.yMin = INT_MAX;
    drawing->cbox.xMax = drawing->cbox.yMax = INT_MIN;
    drawing->ftlibrary = ftlib;
    drawing->library   = lib;
    drawing->scale_x = 1.;
    drawing->scale_y = 1.;

    outline_alloc(&drawing->outline, GLYPH_INITIAL_POINTS, GLYPH_INITIAL_CONTOURS);
    return drawing;
}

/*
 * \brief Free a drawing
 */
void ass_drawing_free(ASS_Drawing* drawing)
{
    if (drawing) {
        free(drawing->text);
        outline_free(&drawing->outline);
    }
    free(drawing);
}

/*
 * \brief Copy an ASCII string to the drawing text buffer
 */
void ass_drawing_set_text(ASS_Drawing* drawing, char *str, size_t len)
{
    free(drawing->text);
    drawing->text = strndup(str, len);
}

/*
 * \brief Create a hashcode for the drawing
 * XXX: To avoid collisions a better hash algorithm might be useful.
 */
void ass_drawing_hash(ASS_Drawing* drawing)
{
    if (!drawing->text)
        return;
    drawing->hash = fnv_32a_str(drawing->text, FNV1_32A_INIT);
}

/*
 * \brief Convert token list to outline.  Calls the line and curve evaluators.
 */
ASS_Outline *ass_drawing_parse(ASS_Drawing *drawing, int raw_mode)
{
    int started = 0;
    ASS_DrawingToken *token;
    FT_Vector pen = {0, 0};

    drawing->tokens = drawing_tokenize(drawing->text);
    drawing_prepare(drawing);

    token = drawing->tokens;
    while (token) {
        // Draw something according to current command
        switch (token->type) {
        case TOKEN_MOVE_NC:
            pen = token->point;
            translate_point(drawing, &pen);
            token = token->next;
            break;
        case TOKEN_MOVE:
            pen = token->point;
            translate_point(drawing, &pen);
            if (started) {
                if (!drawing_close_shape(drawing))
                    goto error;
                started = 0;
            }
            token = token->next;
            break;
        case TOKEN_LINE: {
            FT_Vector to;
            to = token->point;
            translate_point(drawing, &to);
            if (!started && !drawing_add_point(drawing, &pen, FT_CURVE_TAG_ON))
                goto error;
            if (!drawing_add_point(drawing, &to, FT_CURVE_TAG_ON))
                goto error;
            started = 1;
            token = token->next;
            break;
        }
        case TOKEN_CUBIC_BEZIER:
            if (token_check_values(token, 3, TOKEN_CUBIC_BEZIER) &&
                token->prev) {
                if (!drawing_evaluate_curve(drawing, token->prev, 0, started))
                    goto error;
                token = token->next;
                token = token->next;
                token = token->next;
                started = 1;
            } else
                token = token->next;
            break;
        case TOKEN_B_SPLINE:
            if (token_check_values(token, 3, TOKEN_B_SPLINE) &&
                token->prev) {
                if (!drawing_evaluate_curve(drawing, token->prev, 1, started))
                    goto error;
                token = token->next;
                started = 1;
            } else
                token = token->next;
            break;
        default:
            token = token->next;
            break;
        }
    }

    // Close the last contour
    if (started && !drawing_close_shape(drawing))
        goto error;

    drawing_finish(drawing, raw_mode);
    drawing_free_tokens(drawing->tokens);
    return &drawing->outline;

error:
    drawing_free_tokens(drawing->tokens);
    return NULL;
}

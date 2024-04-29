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

#include "config.h"
#include "ass_compat.h"

#include <math.h>
#include <stdbool.h>
#include <limits.h>

#include "ass_utils.h"
#include "ass_drawing.h"
#include "ass_font.h"

#define DRAWING_INITIAL_POINTS 100
#define DRAWING_INITIAL_SEGMENTS 100

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

static inline bool add_node(ASS_DrawingToken **tail, ASS_TokenType type, ASS_Vector point)
{
    assert(tail && *tail);

    ASS_DrawingToken *new_tail = malloc(sizeof(**tail));
    if (!new_tail)
        return false;
    (*tail)->next = new_tail;
    new_tail->prev = *tail;
    new_tail->next = NULL;
    new_tail->type = type;
    new_tail->point = point;
    *tail = new_tail;
    return true;
}

static inline bool get_point(const char **str, ASS_Vector *point)
{
    double x, y;
    if (!mystrtod((char **) str, &x) || !mystrtod((char **) str, &y))
        return false;
    *point = (ASS_Vector) {double_to_d6(x), double_to_d6(y)};
    return true;
}


/**
 * Parses and advances the string for exactly 3 points.
 * If the string contains fewer than 3 points,
 * any initial matching coordinates are still consumed.
 * If an allocation fails, error will be set to true.
 * \return whether three valid points were added
 */
static bool add_3_points(const char **str, ASS_DrawingToken **tail, ASS_TokenType type, bool *error)
{
    ASS_Vector buf[3];

    if (!*str)
        return false;

    bool valid = get_point(str, buf + 0);
    valid = valid && get_point(str, buf + 1);
    valid = valid && get_point(str, buf + 2);

    if (!valid)
        return false;

    valid = add_node(tail, type, buf[0]);
    valid = valid && add_node(tail, type, buf[1]);
    valid = valid && add_node(tail, type, buf[2]);
    if (!valid) {
        *error = true;
        return false;
    }

    return true;
}

/*
 * Parses and advances the string while it matches points.
 * Each set of batch_size points will be turned into tokens and appended to tail.
 * Partial matches (i.e. an insufficient amount of coordinates) are still consumed.
 * If an allocation fails, error will be set to true.
 * \return count of added points
 */
static size_t add_many_points(const char **str, ASS_DrawingToken **tail,
                              ASS_TokenType type, size_t batch_size, bool *error)
{
    ASS_Vector buf[3];
    assert(batch_size <= (sizeof(buf) / sizeof(*buf)));

    if (!*str)
        return 0;

    size_t count_total = 0;
    size_t count_batch = 0;
    while (**str) {
        ASS_Vector point;
        if (!get_point(str, &point))
            break;
        buf[count_batch] = point;
        count_total++;
        count_batch++;

        if (count_batch != batch_size)
            continue;

        for (size_t i = 0; i < count_batch; i++)
            if (!add_node(tail, type, buf[i])) {
                *error = true;
                return count_total - count_batch + i;
            }
        count_batch = 0;
    }

    return count_total - count_batch;
}

static inline bool add_root_node(ASS_DrawingToken **root, ASS_DrawingToken **tail,
                                 size_t *points, ASS_Vector point, ASS_TokenType type)
{
    *root = *tail = calloc(1, sizeof(ASS_DrawingToken));
    if (!*root)
        return false;
    (*root)->point = point;
    (*root)->type = type;
    *points = 1;
    return true;
}

/*
 * \brief Tokenize a drawing string into a list of ASS_DrawingToken
 * This also expands points for closing b-splines
 */
static ASS_DrawingToken *drawing_tokenize(const char *str)
{
    const char *p = str;
    ASS_DrawingToken *root = NULL, *tail = NULL, *spline_start = NULL;
    size_t points = 0;
    bool m_seen = false;
    bool error = false;

    while (p && *p) {
        char cmd = *p;
        p++;
        /* VSFilter compat:
         * In guliverkli(2) VSFilter all drawings
         * whose first known (but potentially invalid) command isn't m are rejected.
         * xy-VSF and MPC-HC ISR later relaxed this (possibly inadvertenly),
         * such that all known commands but n are ignored if there was no prior node yet.
         * If an invalid m preceded n, the latter becomes the root node, otherwise
         * if n comes before any other not-ignored command the entire drawing is rejected.
         * 'p' is further restricted and ignored unless there are already >= 3 nodes.
         * This relaxation was a byproduct of a fix for crashing on drawings
         * containing commands with fewer preceding nodes than expected.
         */
        switch (cmd) {
        case 'm':
            m_seen = true;
            if (!root) {
                ASS_Vector point;
                if (!get_point(&p, &point))
                    continue;
                if (!add_root_node(&root, &tail, &points, point, TOKEN_MOVE))
                    return NULL;
            }
            points += add_many_points(&p, &tail, TOKEN_MOVE, 1, &error);
            break;
        case 'n':
            if (!root) {
                ASS_Vector point;
                if (!get_point(&p, &point))
                    continue;
                if (!m_seen)
                    return NULL;
                if (!add_root_node(&root, &tail, &points, point, TOKEN_MOVE_NC))
                    return NULL;
            }
            points += add_many_points(&p, &tail, TOKEN_MOVE_NC, 1, &error);
            break;
        case 'l':
            if (!root)
                continue;
            points += add_many_points(&p, &tail, TOKEN_LINE, 1, &error);
            break;
        case 'b':
            if (!root)
                continue;
            points += add_many_points(&p, &tail, TOKEN_CUBIC_BEZIER, 3, &error);
            break;
        case 's':
            if (!root)
                continue;
            // Only the initial 3 points are TOKEN_B_SPLINE,
            // all following ones are TOKEN_EXTEND_SPLINE
            spline_start = tail;
            if (!add_3_points(&p, &tail, TOKEN_B_SPLINE, &error)) {
                spline_start = NULL;
                break;
            }
            points += 3;
            //-fallthrough
        case 'p':
            if (points < 3)
                continue;
            points += add_many_points(&p, &tail, TOKEN_EXTEND_SPLINE, 1, &error);
            break;
        case 'c':
            if (!spline_start)
                continue;
            // Close b-splines: add the first three points of the b-spline back to the end
            for (int i = 0; i < 3; i++) {
                if (!add_node(&tail, TOKEN_EXTEND_SPLINE, spline_start->point)) {
                    error = true;
                    break;
                }
                spline_start = spline_start->next;
            }
            spline_start = NULL;
            break;
        default:
            // Ignore, just search for next valid command
            break;
        }
        if (error)
            goto fail;
    }

    return root;

fail:
    drawing_free_tokens(root);
    return NULL;
}

/*
 * \brief Add curve to drawing
 */
static bool drawing_add_curve(ASS_Outline *outline, ASS_Rect *cbox,
                              ASS_DrawingToken *token, bool spline, int started)
{
    ASS_Vector p[4];
    for (int i = 0; i < 4; ++i) {
        p[i] = token->point;
        rectangle_update(cbox, p[i].x, p[i].y, p[i].x, p[i].y);
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

    return (started || ass_outline_add_point(outline, p[0], 0)) &&
        ass_outline_add_point(outline, p[1], 0) &&
        ass_outline_add_point(outline, p[2], 0) &&
        ass_outline_add_point(outline, p[3], OUTLINE_CUBIC_SPLINE);
}

/*
 * Anything produced by our tokenizer is already supposed to fullfil these requirements
 * where relevant, but let's check with an assert in builds without NDEBUG
 */
static inline void assert_3_forward(ASS_DrawingToken *token)
{
    assert(token && token->prev && token->next && token->next->next);
}

static inline void assert_4_back(ASS_DrawingToken *token)
{
    assert(token && token->prev && token->prev->prev && token->prev->prev->prev);
}

/*
 * \brief Convert token list to outline.  Calls the line and curve evaluators.
 */
bool ass_drawing_parse(ASS_Outline *outline, ASS_Rect *cbox,
                       const char *text, ASS_Library *lib)
{
    if (!ass_outline_alloc(outline, DRAWING_INITIAL_POINTS, DRAWING_INITIAL_SEGMENTS))
        return false;
    rectangle_reset(cbox);

    ASS_DrawingToken *tokens = drawing_tokenize(text);

    bool started = false;
    ASS_Vector pen = {0, 0};
    ASS_DrawingToken *token = tokens;
    while (token) {
        // Draw something according to current command
        switch (token->type) {
        case TOKEN_MOVE_NC:
            pen = token->point;
            rectangle_update(cbox, pen.x, pen.y, pen.x, pen.y);
            token = token->next;
            break;
        case TOKEN_MOVE:
            pen = token->point;
            rectangle_update(cbox, pen.x, pen.y, pen.x, pen.y);
            if (started) {
                if (!ass_outline_add_segment(outline, OUTLINE_LINE_SEGMENT))
                    goto error;
                ass_outline_close_contour(outline);
                started = false;
            }
            token = token->next;
            break;
        case TOKEN_LINE: {
            ASS_Vector to = token->point;
            rectangle_update(cbox, to.x, to.y, to.x, to.y);
            if (!started && !ass_outline_add_point(outline, pen, 0))
                goto error;
            if (!ass_outline_add_point(outline, to, OUTLINE_LINE_SEGMENT))
                goto error;
            started = true;
            token = token->next;
            break;
        }
        case TOKEN_CUBIC_BEZIER:
            assert_3_forward(token);
            if (!drawing_add_curve(outline, cbox, token->prev, false, started))
                goto error;
            token = token->next;
            token = token->next;
            token = token->next;
            started = true;
            break;
        case TOKEN_B_SPLINE:
            assert_3_forward(token);
            if (!drawing_add_curve(outline, cbox, token->prev, true, started))
                goto error;
            token = token->next;
            token = token->next;
            token = token->next;
            started = true;
            break;
        case TOKEN_EXTEND_SPLINE:
            assert_4_back(token);
            if (!drawing_add_curve(outline, cbox, token->prev->prev->prev, true, started))
                goto error;
            token = token->next;
            started = true;
            break;
        }
    }

    // Close the last contour
    if (started) {
        if (!ass_outline_add_segment(outline, OUTLINE_LINE_SEGMENT))
            goto error;
        ass_outline_close_contour(outline);
    }

    if (lib)
        ass_msg(lib, MSGL_V,
                "Parsed drawing with %zu points and %zu segments",
                outline->n_points, outline->n_segments);

    drawing_free_tokens(tokens);
    return true;

error:
    drawing_free_tokens(tokens);
    ass_outline_free(outline);
    return false;
}

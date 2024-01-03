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
 * \brief Check whether a number of items on the list is available
 */
static bool token_check_values(ASS_DrawingToken *token, int i, ASS_TokenType type)
{
    for (int j = 0; j < i; j++) {
        if (!token || token->type != type) return false;
        token = token->next;
    }

    return true;
}

/*
 * \brief Tokenize a drawing string into a list of ASS_DrawingToken
 * This also expands points for closing b-splines
 */
static ASS_DrawingToken *drawing_tokenize(const char *str)
{
    char *p = (char *) str;
    ASS_TokenType type = TOKEN_INVALID;
    int is_set = 0;
    double val;
    ASS_Vector point = {0, 0};

    ASS_DrawingToken *root = NULL, *tail = NULL, *spline_start = NULL;

    while (p && *p) {
        int got_coord = 0;
        if (*p == 'c' && spline_start) {
            // Close b-splines: add the first three points of the b-spline
            // back to the end
            if (token_check_values(spline_start->next, 2, TOKEN_B_SPLINE)) {
                for (int i = 0; i < 3; i++) {
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
            got_coord = 1;
            p--;
        } else if (is_set == 1 && mystrtod(&p, &val)) {
            point.y = double_to_d6(val);
            is_set = 2;
            got_coord = 1;
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

        // Ignore the odd extra value, it makes no sense.
        if (!got_coord)
            is_set = 0;

        if (type != TOKEN_INVALID && is_set == 2) {
            if (root) {
                tail->next = calloc(1, sizeof(ASS_DrawingToken));
                tail->next->prev = tail;
                tail = tail->next;
            } else {
                /* VSFilter compat:
                 * In guliverkli(2) VSFilter all drawings
                 * whose first valid command isn't m are rejected.
                 * xy-VSF and MPC-HC ISR this was (possibly inadvertenly) later relaxed,
                 * such that all valid commands but n are ignored if there was no m yet.
                 */
                if (type == TOKEN_MOVE_NC) {
                    return NULL;
                } else if (type != TOKEN_MOVE) {
                    p++;
                    continue;
                }
                root = tail = calloc(1, sizeof(ASS_DrawingToken));
            }
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

    return (started ||
        ass_outline_add_point(outline, p[0], 0)) &&
        ass_outline_add_point(outline, p[1], 0) &&
        ass_outline_add_point(outline, p[2], 0) &&
        ass_outline_add_point(outline, p[3], OUTLINE_CUBIC_SPLINE);
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
            if (token_check_values(token, 3, TOKEN_CUBIC_BEZIER) &&
                token->prev) {
                if (!drawing_add_curve(outline, cbox, token->prev, false, started))
                    goto error;
                token = token->next;
                token = token->next;
                token = token->next;
                started = true;
            } else
                token = token->next;
            break;
        case TOKEN_B_SPLINE:
            if (token_check_values(token, 3, TOKEN_B_SPLINE) &&
                token->prev) {
                if (!drawing_add_curve(outline, cbox, token->prev, true, started))
                    goto error;
                token = token->next;
                started = true;
            } else
                token = token->next;
            break;
        default:
            token = token->next;
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

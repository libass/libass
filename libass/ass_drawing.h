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

#ifndef LIBASS_DRAWING_H
#define LIBASS_DRAWING_H

#include "ass.h"
#include "ass_outline.h"
#include "ass_bitmap.h"

typedef enum {
    TOKEN_INVALID,
    TOKEN_MOVE,
    TOKEN_MOVE_NC,
    TOKEN_LINE,
    TOKEN_CUBIC_BEZIER,
    TOKEN_CONIC_BEZIER,
    TOKEN_B_SPLINE,
    TOKEN_EXTEND_SPLINE,
    TOKEN_CLOSE
} ASS_TokenType;

typedef struct ass_drawing_token {
    ASS_TokenType type;
    ASS_Vector point;
    struct ass_drawing_token *next;
    struct ass_drawing_token *prev;
} ASS_DrawingToken;

bool ass_drawing_parse(ASS_Outline *outline, ASS_Rect *cbox,
                       const char *text, ASS_Library *lib);

#endif /* LIBASS_DRAWING_H */

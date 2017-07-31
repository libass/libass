/*
 * Copyright (C) 2016 Vabishchevich Nikolay <vabnick@gmail.com>
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

#ifndef LIBASS_OUTLINE_H
#define LIBASS_OUTLINE_H

#include <ft2build.h>
#include FT_OUTLINE_H
#include <stdbool.h>


typedef struct ass_outline {
    size_t n_contours, max_contours;
    size_t *contours;
    size_t n_points, max_points;
    FT_Vector *points;
    char *tags;
} ASS_Outline;

bool outline_alloc(ASS_Outline *outline, size_t n_points, size_t n_contours);
bool outline_convert(ASS_Outline *outline, const FT_Outline *source);
bool outline_copy(ASS_Outline *outline, const ASS_Outline *source);
void outline_free(ASS_Outline *outline);

bool outline_add_point(ASS_Outline *outline, FT_Vector pt, char tag);
bool outline_close_contour(ASS_Outline *outline);

void outline_translate(const ASS_Outline *outline, FT_Pos dx, FT_Pos dy);
void outline_transform(const ASS_Outline *outline, const FT_Matrix *matrix);
void outline_update_cbox(const ASS_Outline *outline, FT_BBox *cbox);
void outline_get_cbox(const ASS_Outline *outline, FT_BBox *cbox);

bool outline_stroke(ASS_Outline *result, ASS_Outline *result1,
                    const ASS_Outline *path, int xbord, int ybord, int eps);


#endif /* LIBASS_OUTLINE_H */

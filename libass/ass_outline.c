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

#include "config.h"
#include "ass_compat.h"

#include "ass_utils.h"
#include "ass_outline.h"



bool outline_alloc(ASS_Outline *outline, size_t n_points, size_t n_contours)
{
    outline->contours = malloc(sizeof(size_t) * n_contours);
    outline->points = malloc(sizeof(FT_Vector) * n_points);
    outline->tags = malloc(n_points);
    if (!outline->contours || !outline->points || !outline->tags)
        return false;

    outline->max_contours = n_contours;
    outline->max_points = n_points;
    return true;
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

    for (int i = 0; i < source->n_contours; i++)
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


void outline_translate(const ASS_Outline *outline, FT_Pos dx, FT_Pos dy)
{
    for (size_t i = 0; i < outline->n_points; i++) {
        outline->points[i].x += dx;
        outline->points[i].y += dy;
    }
}

void outline_transform(const ASS_Outline *outline, const FT_Matrix *matrix)
{
    for (size_t i = 0; i < outline->n_points; i++) {
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
    for (size_t i = 1; i < outline->n_points; i++) {
        cbox->xMin = FFMIN(cbox->xMin, outline->points[i].x);
        cbox->xMax = FFMAX(cbox->xMax, outline->points[i].x);
        cbox->yMin = FFMIN(cbox->yMin, outline->points[i].y);
        cbox->yMax = FFMAX(cbox->yMax, outline->points[i].y);
    }
}


/**
 * \brief Calculate the cbox of a series of points
 */
static void
get_contour_cbox(FT_BBox *box, FT_Vector *points, int start, int end)
{
    box->xMin = box->yMin = INT_MAX;
    box->xMax = box->yMax = INT_MIN;
    for (int i = start; i <= end; i++) {
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
    int x = points[end].x;
    int y = points[end].y;
    for (int i = start; i <= end; i++) {
        area += (long long)(points[i].x + x) * (points[i].y - y);
        x = points[i].x;
        y = points[i].y;
    }
    return area;
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
            int width = box.xMax - box.xMin;
            int height = box.yMax - box.yMin;
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


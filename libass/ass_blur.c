/*
 * Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
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

#include "ass_utils.h"
#include "ass_bitmap.h"


/*
 * Cascade Blur Algorithm
 *
 * The main idea is simple: to approximate a gaussian blur with large radius,
 * you can scale down, apply a filter with a relatively small pattern, then scale back up.
 *
 * To achieve the desired precision, scaling should be done with sufficiently smooth kernel.
 * Experiments show that downscaling of factor 2 with kernel [1, 5, 10, 10, 5, 1] and
 * corresponding upscaling are enough for 8-bit precision.
 *
 * Here we use generic filters with 5 different kernel widths (9 to 17-tap).
 * Kernel coefficients of that filter are obtained from the solution of the least-squares problem
 * for the Fourier transform of the resulting kernel.
 */

static void calc_gauss(double *res, int n, double r2)
{
    double alpha = 0.5 / r2;
    double mul = exp(-alpha), mul2 = mul * mul;
    double cur = sqrt(alpha / ASS_PI);

    res[0] = cur;
    cur *= mul;
    res[1] = cur;
    for (int i = 2; i < n; i++) {
        mul *= mul2;
        cur *= mul;
        res[i] = cur;
    }
}

static void coeff_filter(double *coeff, int n, const double kernel[4])
{
    double prev1 = coeff[1], prev2 = coeff[2], prev3 = coeff[3];
    for (int i = 0; i < n; i++) {
        double res = coeff[i + 0]  * kernel[0] +
            (prev1 + coeff[i + 1]) * kernel[1] +
            (prev2 + coeff[i + 2]) * kernel[2] +
            (prev3 + coeff[i + 3]) * kernel[3];
        prev3 = prev2;
        prev2 = prev1;
        prev1 = coeff[i];
        coeff[i] = res;
    }
}

static void calc_matrix(double mat[][8], const double *mat_freq, int n)
{
    for (int i = 0; i < n; i++) {
        mat[i][i] = mat_freq[2 * i + 2] + 3 * mat_freq[0] - 4 * mat_freq[i + 1];
        for (int j = i + 1; j < n; j++)
            mat[i][j] = mat[j][i] = mat_freq[i + j + 2] + mat_freq[j - i] +
                2 * (mat_freq[0] - mat_freq[i + 1] - mat_freq[j + 1]);
    }

    // invert transpose
    for (int k = 0; k < n; k++) {
        double z = 1 / mat[k][k];
        mat[k][k] = 1;
        for (int i = 0; i < n; i++) {
            if (i == k)
                continue;

            double mul = mat[i][k] * z;
            mat[i][k] = 0;
            for (int j = 0; j < n; j++)
                mat[i][j] -= mat[k][j] * mul;
        }
        for (int j = 0; j < n; j++)
            mat[k][j] *= z;
    }
}

/**
 * \brief Solve least squares problem for kernel of the main filter
 * \param mu out: output coefficients
 * \param n in: filter kernel radius
 * \param r2 in: desired standard deviation squared
 * \param mul in: scale multiplier
 */
static void calc_coeff(double mu[], int n, double r2, double mul)
{
    assert(n > 0 && n <= 8);

    const double w = 12096;
    double kernel[] = {
        ((( + 3280 / w) * mul + 1092 / w) * mul + 2520 / w) * mul + 5204 / w,
        ((( - 2460 / w) * mul -  273 / w) * mul -  210 / w) * mul + 2943 / w,
        ((( +  984 / w) * mul -  546 / w) * mul -  924 / w) * mul +  486 / w,
        ((( -  164 / w) * mul +  273 / w) * mul -  126 / w) * mul +   17 / w,
    };

    double mat_freq[17] = { kernel[0], kernel[1], kernel[2], kernel[3] };
    coeff_filter(mat_freq, 7, kernel);

    double vec_freq[12];
    calc_gauss(vec_freq, n + 4, r2 * mul);
    coeff_filter(vec_freq, n + 1, kernel);

    double mat[8][8];
    calc_matrix(mat, mat_freq, n);

    double vec[8];
    for (int i = 0; i < n; i++)
        vec[i] = mat_freq[0] - mat_freq[i + 1] - vec_freq[0] + vec_freq[i + 1];

    for (int i = 0; i < n; i++) {
        double res = 0;
        for (int j = 0; j < n; j++)
            res += mat[i][j] * vec[j];
        mu[i] = FFMAX(0, res);
    }
}

typedef struct {
    int level, radius;
    int16_t coeff[8];
} BlurMethod;

static void find_best_method(BlurMethod *blur, double r2)
{
    double mu[8];
    if (r2 < 0.5) {
        blur->level = 0;
        blur->radius = 4;
        mu[1] = 0.085 * r2 * r2 * r2;
        mu[0] = 0.5 * r2 - 4 * mu[1];
        mu[2] = mu[3] = 0;
    } else {
        double frac = frexp(sqrt(0.11569 * r2 + 0.20591047), &blur->level);
        double mul = pow(0.25, blur->level);
        blur->radius = 8 - (int) ((10.1525 + 0.8335 * mul) * (1 - frac));
        blur->radius = FFMAX(blur->radius, 4);
        calc_coeff(mu, blur->radius, r2, mul);
    }
    for (int i = 0; i < blur->radius; i++)
        blur->coeff[i] = (int) (0x10000 * mu[i] + 0.5);
}

/**
 * \brief Perform approximate gaussian blur
 * \param r2x in: desired standard deviation along X axis squared
 * \param r2y in: desired standard deviation along Y axis squared
 */
bool ass_gaussian_blur(const BitmapEngine *engine, Bitmap *bm, double r2x, double r2y)
{
    BlurMethod blur_x, blur_y;
    find_best_method(&blur_x, r2x);
    if (r2y == r2x)
        memcpy(&blur_y, &blur_x, sizeof(blur_y));
    else find_best_method(&blur_y, r2y);

    uint32_t w = bm->w, h = bm->h;
    int offset_x = ((2 * blur_x.radius + 9) << blur_x.level) - 5;
    int offset_y = ((2 * blur_y.radius + 9) << blur_y.level) - 5;
    uint32_t end_w = ((w + offset_x) & ~((1 << blur_x.level) - 1)) - 4;
    uint32_t end_h = ((h + offset_y) & ~((1 << blur_y.level) - 1)) - 4;

    const int stripe_width = 1 << (engine->align_order - 1);
    uint64_t size = (((uint64_t) end_w + stripe_width - 1) & ~(stripe_width - 1)) * end_h;
    if (size > INT_MAX / 4)
        return false;

    int16_t *tmp = ass_aligned_alloc(2 * stripe_width, 4 * size, false);
    if (!tmp)
        return false;

    engine->stripe_unpack(tmp, bm->buffer, bm->stride, w, h);
    int16_t *buf[2] = {tmp, tmp + size};
    int index = 0;

    for (int i = 0; i < blur_y.level; i++) {
        engine->shrink_vert(buf[index ^ 1], buf[index], w, h);
        h = (h + 5) >> 1;
        index ^= 1;
    }
    for (int i = 0; i < blur_x.level; i++) {
        engine->shrink_horz(buf[index ^ 1], buf[index], w, h);
        w = (w + 5) >> 1;
        index ^= 1;
    }
    assert(blur_x.radius >= 4 && blur_x.radius <= 8);
    engine->blur_horz[blur_x.radius - 4](buf[index ^ 1], buf[index], w, h, blur_x.coeff);
    w += 2 * blur_x.radius;
    index ^= 1;
    assert(blur_y.radius >= 4 && blur_y.radius <= 8);
    engine->blur_vert[blur_y.radius - 4](buf[index ^ 1], buf[index], w, h, blur_y.coeff);
    h += 2 * blur_y.radius;
    index ^= 1;
    for (int i = 0; i < blur_x.level; i++) {
        engine->expand_horz(buf[index ^ 1], buf[index], w, h);
        w = 2 * w + 4;
        index ^= 1;
    }
    for (int i = 0; i < blur_y.level; i++) {
        engine->expand_vert(buf[index ^ 1], buf[index], w, h);
        h = 2 * h + 4;
        index ^= 1;
    }
    assert(w == end_w && h == end_h);

    if (!ass_realloc_bitmap(engine, bm, w, h)) {
        ass_aligned_free(tmp);
        return false;
    }
    bm->left -= ((blur_x.radius + 4) << blur_x.level) - 4;
    bm->top  -= ((blur_y.radius + 4) << blur_y.level) - 4;

    engine->stripe_pack(bm->buffer, bm->stride, buf[index], w, h);
    ass_aligned_free(tmp);
    return true;
}


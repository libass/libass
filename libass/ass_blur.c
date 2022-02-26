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


#define STRIPE_WIDTH  (1 << (C_ALIGN_ORDER - 1))
#define STRIPE_MASK   (STRIPE_WIDTH - 1)
static int16_t zero_line[STRIPE_WIDTH];
static int16_t dither_line[2 * STRIPE_WIDTH] = {
#if STRIPE_WIDTH > 8
     8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,
    56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24,
#else
     8, 40,  8, 40,  8, 40,  8, 40,
    56, 24, 56, 24, 56, 24, 56, 24,
#endif
};

inline static const int16_t *get_line(const int16_t *ptr, uintptr_t offs, uintptr_t size)
{
    return offs < size ? ptr + offs : zero_line;
}

inline static void copy_line(int16_t *buf, const int16_t *ptr, uintptr_t offs, uintptr_t size)
{
    memcpy(buf, get_line(ptr, offs, size), STRIPE_WIDTH * sizeof(buf[0]));
}

/*
 * Unpack/Pack Functions
 *
 * Convert between regular 8-bit bitmap and internal format.
 * Internal image is stored as set of vertical stripes of size [STRIPE_WIDTH x height].
 * Each pixel is represented as 16-bit integer in range of [0-0x4000].
 */

void ass_stripe_unpack_c(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
                         uintptr_t width, uintptr_t height)
{
    for (uintptr_t y = 0; y < height; y++) {
        int16_t *ptr = dst;
        for (uintptr_t x = 0; x < width; x += STRIPE_WIDTH) {
            for (int k = 0; k < STRIPE_WIDTH; k++)
                ptr[k] = (uint16_t) (((src[x + k] << 7) | (src[x + k] >> 1)) + 1) >> 1;
                //ptr[k] = (0x4000 * src[x + k] + 127) / 255;
            ptr += STRIPE_WIDTH * height;
        }
        dst += STRIPE_WIDTH;
        src += src_stride;
    }
}

void ass_stripe_pack_c(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
                       uintptr_t width, uintptr_t height)
{
    for (uintptr_t x = 0; x < width; x += STRIPE_WIDTH) {
        uint8_t *ptr = dst;
        for (uintptr_t y = 0; y < height; y++) {
            const int16_t *dither = dither_line + (y & 1) * STRIPE_WIDTH;
            for (int k = 0; k < STRIPE_WIDTH; k++)
                ptr[k] = (uint16_t) (src[k] - (src[k] >> 8) + dither[k]) >> 6;
                //ptr[k] = (255 * src[k] + 0x1FFF) / 0x4000;
            ptr += dst_stride;
            src += STRIPE_WIDTH;
        }
        dst += STRIPE_WIDTH;
    }
    uintptr_t left = dst_stride - ((width + STRIPE_MASK) & ~STRIPE_MASK);
    for (uintptr_t y = 0; y < height; y++) {
        for (uintptr_t x = 0; x < left; x++)
            dst[x] = 0;
        dst += dst_stride;
    }
}

/*
 * Contract Filters
 *
 * Contract image by factor 2 with kernel [1, 5, 10, 10, 5, 1].
 */

static inline int16_t shrink_func(int16_t p1p, int16_t p1n,
                                  int16_t z0p, int16_t z0n,
                                  int16_t n1p, int16_t n1n)
{
    /*
    return (1 * p1p + 5 * p1n + 10 * z0p + 10 * z0n + 5 * n1p + 1 * n1n + 16) >> 5;
    */
    int32_t r = (p1p + p1n + n1p + n1n) >> 1;
    r = (r + z0p + z0n) >> 1;
    r = (r + p1n + n1p) >> 1;
    return (r + z0p + z0n + 2) >> 2;
}

void ass_shrink_horz_c(int16_t *dst, const int16_t *src,
                       uintptr_t src_width, uintptr_t src_height)
{
    uintptr_t dst_width = (src_width + 5) >> 1;
    uintptr_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    uintptr_t step = STRIPE_WIDTH * src_height;

    uintptr_t offs = 0;
    int16_t buf[3 * STRIPE_WIDTH];
    int16_t *ptr = buf + STRIPE_WIDTH;
    for (uintptr_t x = 0; x < dst_width; x += STRIPE_WIDTH) {
        for (uintptr_t y = 0; y < src_height; y++) {
            copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
            copy_line(ptr + 0 * STRIPE_WIDTH, src, offs + 0 * step, size);
            copy_line(ptr + 1 * STRIPE_WIDTH, src, offs + 1 * step, size);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = shrink_func(ptr[2 * k - 4], ptr[2 * k - 3],
                                     ptr[2 * k - 2], ptr[2 * k - 1],
                                     ptr[2 * k + 0], ptr[2 * k + 1]);
            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        offs += step;
    }
}

void ass_shrink_vert_c(int16_t *dst, const int16_t *src,
                       uintptr_t src_width, uintptr_t src_height)
{
    uintptr_t dst_height = (src_height + 5) >> 1;
    uintptr_t step = STRIPE_WIDTH * src_height;

    for (uintptr_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        uintptr_t offs = 0;
        for (uintptr_t y = 0; y < dst_height; y++) {
            const int16_t *p1p = get_line(src, offs - 4 * STRIPE_WIDTH, step);
            const int16_t *p1n = get_line(src, offs - 3 * STRIPE_WIDTH, step);
            const int16_t *z0p = get_line(src, offs - 2 * STRIPE_WIDTH, step);
            const int16_t *z0n = get_line(src, offs - 1 * STRIPE_WIDTH, step);
            const int16_t *n1p = get_line(src, offs - 0 * STRIPE_WIDTH, step);
            const int16_t *n1n = get_line(src, offs + 1 * STRIPE_WIDTH, step);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = shrink_func(p1p[k], p1n[k], z0p[k], z0n[k], n1p[k], n1n[k]);
            dst  += 1 * STRIPE_WIDTH;
            offs += 2 * STRIPE_WIDTH;
        }
        src += step;
    }
}

/*
 * Expand Filters
 *
 * Expand image by factor 2 with kernel [5, 10, 1], [1, 10, 5].
 */

static inline void expand_func(int16_t *rp, int16_t *rn,
                               int16_t p1, int16_t z0, int16_t n1)
{
    /*
    *rp = (5 * p1 + 10 * z0 + 1 * n1 + 8) >> 4;
    *rn = (1 * p1 + 10 * z0 + 5 * n1 + 8) >> 4;
    */
    uint16_t r = (uint16_t) (((uint16_t) (p1 + n1) >> 1) + z0) >> 1;
    *rp = (uint16_t) (((uint16_t) (r + p1) >> 1) + z0 + 1) >> 1;
    *rn = (uint16_t) (((uint16_t) (r + n1) >> 1) + z0 + 1) >> 1;
}

void ass_expand_horz_c(int16_t *dst, const int16_t *src,
                       uintptr_t src_width, uintptr_t src_height)
{
    uintptr_t dst_width = 2 * src_width + 4;
    uintptr_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    uintptr_t step = STRIPE_WIDTH * src_height;

    uintptr_t offs = 0;
    int16_t buf[2 * STRIPE_WIDTH];
    int16_t *ptr = buf + STRIPE_WIDTH;
    for (uintptr_t x = STRIPE_WIDTH; x < dst_width; x += 2 * STRIPE_WIDTH) {
        for (uintptr_t y = 0; y < src_height; y++) {
            copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
            copy_line(ptr - 0 * STRIPE_WIDTH, src, offs - 0 * step, size);
            for (int k = 0; k < STRIPE_WIDTH / 2; k++)
                expand_func(&dst[2 * k], &dst[2 * k + 1],
                            ptr[k - 2], ptr[k - 1], ptr[k]);
            int16_t *next = dst + step - STRIPE_WIDTH;
            for (int k = STRIPE_WIDTH / 2; k < STRIPE_WIDTH; k++)
                expand_func(&next[2 * k], &next[2 * k + 1],
                            ptr[k - 2], ptr[k - 1], ptr[k]);
            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        dst += step;
    }
    if ((dst_width - 1) & STRIPE_WIDTH)
        return;

    for (uintptr_t y = 0; y < src_height; y++) {
        copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
        copy_line(ptr - 0 * STRIPE_WIDTH, src, offs - 0 * step, size);
        for (int k = 0; k < STRIPE_WIDTH / 2; k++)
            expand_func(&dst[2 * k], &dst[2 * k + 1],
                        ptr[k - 2], ptr[k - 1], ptr[k]);
        dst  += STRIPE_WIDTH;
        offs += STRIPE_WIDTH;
    }
}

void ass_expand_vert_c(int16_t *dst, const int16_t *src,
                       uintptr_t src_width, uintptr_t src_height)
{
    uintptr_t dst_height = 2 * src_height + 4;
    uintptr_t step = STRIPE_WIDTH * src_height;

    for (uintptr_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        uintptr_t offs = 0;
        for (uintptr_t y = 0; y < dst_height; y += 2) {
            const int16_t *p1 = get_line(src, offs - 2 * STRIPE_WIDTH, step);
            const int16_t *z0 = get_line(src, offs - 1 * STRIPE_WIDTH, step);
            const int16_t *n1 = get_line(src, offs - 0 * STRIPE_WIDTH, step);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                expand_func(&dst[k], &dst[k + STRIPE_WIDTH],
                            p1[k], z0[k], n1[k]);
            dst  += 2 * STRIPE_WIDTH;
            offs += 1 * STRIPE_WIDTH;
        }
        src += step;
    }
}

/*
 * Main Parametric Filters
 *
 * Perform 1D convolution with kernel [..., c2, c1, c0, d, c0, c1, c2, ...],
 * cN = param[N], d = 1 - 2 * (c0 + c1 + c2 + ...),
 * number of parameters is part of the function name.
 */

static inline void blur_horz(int16_t *dst, const int16_t *src,
                             uintptr_t src_width, uintptr_t src_height,
                             const int16_t *param, const int n)
{
    uintptr_t dst_width = src_width + 2 * n;
    uintptr_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    uintptr_t step = STRIPE_WIDTH * src_height;

    uintptr_t offs = 0;
    int16_t buf[3 * STRIPE_WIDTH];
    int16_t *ptr = buf + 2 * STRIPE_WIDTH;
    for (uintptr_t x = 0; x < dst_width; x += STRIPE_WIDTH) {
        for (uintptr_t y = 0; y < src_height; y++) {
            for (int i = -((2 * n + STRIPE_WIDTH - 1u) / STRIPE_WIDTH); i <= 0; i++)
                copy_line(ptr + i * STRIPE_WIDTH, src, offs + i * step, size);
            int32_t acc[STRIPE_WIDTH];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                acc[k] = 0x8000;
            for (int i = n; i > 0; i--)
                for (int k = 0; k < STRIPE_WIDTH; k++)
                    acc[k] += (int16_t) (ptr[k - n - i] - ptr[k - n]) * param[i - 1] +
                              (int16_t) (ptr[k - n + i] - ptr[k - n]) * param[i - 1];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = ptr[k - n] + (acc[k] >> 16);

            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
    }
}

static inline void blur_vert(int16_t *dst, const int16_t *src,
                             uintptr_t src_width, uintptr_t src_height,
                             const int16_t *param, const int n)
{
    uintptr_t dst_height = src_height + 2 * n;
    uintptr_t step = STRIPE_WIDTH * src_height;

    for (uintptr_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        uintptr_t offs = 0;
        for (uintptr_t y = 0; y < dst_height; y++) {
            int32_t acc[STRIPE_WIDTH];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                acc[k] = 0x8000;
            const int16_t *center = get_line(src, offs - n * STRIPE_WIDTH, step);
            for (int i = n; i > 0; i--) {
                const int16_t *line1 = get_line(src, offs - (n + i) * STRIPE_WIDTH, step);
                const int16_t *line2 = get_line(src, offs - (n - i) * STRIPE_WIDTH, step);
                for (int k = 0; k < STRIPE_WIDTH; k++)
                    acc[k] += (int16_t) (line1[k] - center[k]) * param[i - 1] +
                              (int16_t) (line2[k] - center[k]) * param[i - 1];
            }
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = center[k] + (acc[k] >> 16);

            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        src += step;
    }
}

void ass_blur4_horz_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_horz(dst, src, src_width, src_height, param, 4);
}

void ass_blur4_vert_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_vert(dst, src, src_width, src_height, param, 4);
}

void ass_blur5_horz_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_horz(dst, src, src_width, src_height, param, 5);
}

void ass_blur5_vert_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_vert(dst, src, src_width, src_height, param, 5);
}

void ass_blur6_horz_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_horz(dst, src, src_width, src_height, param, 6);
}

void ass_blur6_vert_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_vert(dst, src, src_width, src_height, param, 6);
}

void ass_blur7_horz_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_horz(dst, src, src_width, src_height, param, 7);
}

void ass_blur7_vert_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_vert(dst, src, src_width, src_height, param, 7);
}

void ass_blur8_horz_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_horz(dst, src, src_width, src_height, param, 8);
}

void ass_blur8_vert_c(int16_t *dst, const int16_t *src,
                      uintptr_t src_width, uintptr_t src_height,
                      const int16_t *param)
{
    blur_vert(dst, src, src_width, src_height, param, 8);
}



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


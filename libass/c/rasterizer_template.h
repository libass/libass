/*
 * Copyright (C) 2014-2022 libass contributors
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

#if   TILE_SIZE == 16
#define SUFFIX(name)  name ## 16_c
#define TILE_ORDER    4
#elif TILE_SIZE == 32
#define SUFFIX(name)  name ## 32_c
#define TILE_ORDER    5
#else
#error Unsupported tile size
#endif

#define FULL_VALUE  (1 << (14 - TILE_ORDER))
#define RESCALE_AB(ab, scale) \
    (((ab) * (int64_t) (scale) + ((int64_t) 1 << (45 + TILE_ORDER))) >> (46 + TILE_ORDER))
#define RESCALE_C(c, scale) \
    (((int32_t) ((c) >> (7 + TILE_ORDER)) * (int64_t) (scale) + ((int64_t) 1 << 44)) >> 45)


void SUFFIX(ass_fill_solid_tile)(uint8_t *buf, ptrdiff_t stride, int set)
{
    uint8_t value = set ? 255 : 0;
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++)
            buf[x] = value;
        buf += stride;
    }
}


/*
 * Halfplane Filling Functions
 *
 * Fill pixels with antialiasing corresponding to equation
 * A * x + B * y < C, where
 * x, y - offset of pixel center from bottom-left,
 * A = a * scale, B = b * scale, C = c * scale / 64.
 *
 * Normalization of coefficients prior call:
 * max(abs(a), abs(b)) * scale = 1 << 61
 *
 * Used Algorithm
 * Let
 * max_ab = max(abs(A), abs(B)),
 * min_ab = min(abs(A), abs(B)),
 * CC = C - A * x - B * y, then
 * result = (clamp((CC - min_ab / 4) / max_ab) +
 *           clamp((CC + min_ab / 4) / max_ab) +
 *           1) / 2,
 * where clamp(Z) = max(-0.5, min(0.5, Z)).
 */

void SUFFIX(ass_fill_halfplane_tile)(uint8_t *buf, ptrdiff_t stride,
                                     int32_t a, int32_t b, int64_t c, int32_t scale)
{
    int16_t aa = RESCALE_AB(a, scale), bb = RESCALE_AB(b, scale);
    int16_t cc = RESCALE_C(c, scale) + FULL_VALUE / 2 - ((aa + bb) >> 1);

    int16_t abs_a = aa < 0 ? -aa : aa;
    int16_t abs_b = bb < 0 ? -bb : bb;
    int16_t delta = (FFMIN(abs_a, abs_b) + 2) >> 2;

    int16_t va1[TILE_SIZE], va2[TILE_SIZE];
    for (int x = 0; x < TILE_SIZE; x++) {
        va1[x] = aa * x - delta;
        va2[x] = aa * x + delta;
    }

    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            int16_t c1 = cc - va1[x];
            int16_t c2 = cc - va2[x];
            c1 = FFMINMAX(c1, 0, FULL_VALUE);
            c2 = FFMINMAX(c2, 0, FULL_VALUE);
            int16_t res = (c1 + c2) >> (7 - TILE_ORDER);
            buf[x] = FFMIN(res, 255);
        }
        buf += stride;
        cc -= bb;
    }
}


/*
 * Generic Filling Functions
 *
 * Used Algorithm
 * Construct trapeziod from each polyline segment and its projection into left side of tile.
 * Render that trapeziod into internal buffer with additive blending and correct sign.
 * Store clamped absolute value from internal buffer into result buffer.
 */

// Render top/bottom line of the trapeziod with antialiasing
static inline void SUFFIX(update_border_line)(int16_t res[TILE_SIZE],
                                              int16_t abs_a, const int16_t va[TILE_SIZE],
                                              int16_t b, int16_t abs_b,
                                              int16_t c, int up, int dn)
{
    int16_t size = dn - up;
    int16_t w = FULL_VALUE + (size << (8 - TILE_ORDER)) - abs_a;
    w = FFMIN(w, FULL_VALUE) << (2 * TILE_ORDER - 5);

    int16_t dc_b = abs_b * (int32_t) size >> 6;
    int16_t dc = (FFMIN(abs_a, dc_b) + 2) >> 2;

    int16_t base = (int32_t) b * (int16_t) (up + dn) >> 7;
    int16_t offs1 = size - ((base + dc) * (int32_t) w >> 16);
    int16_t offs2 = size - ((base - dc) * (int32_t) w >> 16);

    size <<= 1;
    for (int x = 0; x < TILE_SIZE; x++) {
        int16_t cw = (c - va[x]) * (int32_t) w >> 16;
        int16_t c1 = cw + offs1;
        int16_t c2 = cw + offs2;
        c1 = FFMINMAX(c1, 0, size);
        c2 = FFMINMAX(c2, 0, size);
        res[x] += c1 + c2;
    }
}

void SUFFIX(ass_fill_generic_tile)(uint8_t *buf, ptrdiff_t stride,
                                   const struct segment *line, size_t n_lines,
                                   int winding)
{
    int16_t res[TILE_SIZE][TILE_SIZE] = {0};
    int16_t delta[TILE_SIZE + 2] = {0};

    const struct segment *end = line + n_lines;
    for (; line != end; ++line) {
        assert(line->y_min >= 0 && line->y_min < 64 << TILE_ORDER);
        assert(line->y_max > 0 && line->y_max <= 64 << TILE_ORDER);
        assert(line->y_min <= line->y_max);

        int16_t up_delta = line->flags & SEGFLAG_DN ? 4 : 0;
        int16_t dn_delta = up_delta;
        if (!line->x_min && (line->flags & SEGFLAG_EXACT_LEFT)) dn_delta ^= 4;
        if (line->flags & SEGFLAG_UL_DR) {
            int16_t tmp = up_delta;
            up_delta = dn_delta;
            dn_delta = tmp;
        }

        int up = line->y_min >> 6, dn = line->y_max >> 6;
        int16_t up_pos = line->y_min & 63;
        int16_t up_delta1 = up_delta * up_pos;
        int16_t dn_pos = line->y_max & 63;
        int16_t dn_delta1 = dn_delta * dn_pos;
        delta[up + 1] -= up_delta1;
        delta[up] -= (up_delta << 6) - up_delta1;
        delta[dn + 1] += dn_delta1;
        delta[dn] += (dn_delta << 6) - dn_delta1;
        if (line->y_min == line->y_max)
            continue;

        int16_t a = RESCALE_AB(line->a, line->scale);
        int16_t b = RESCALE_AB(line->b, line->scale);
        int16_t c = RESCALE_C(line->c, line->scale) - (a >> 1) - b * up;

        int16_t va[TILE_SIZE];
        for (int x = 0; x < TILE_SIZE; x++)
            va[x] = a * x;
        int16_t abs_a = a < 0 ? -a : a;
        int16_t abs_b = b < 0 ? -b : b;
        int16_t dc = (FFMIN(abs_a, abs_b) + 2) >> 2;
        int16_t base = FULL_VALUE / 2 - (b >> 1);
        int16_t dc1 = base + dc;
        int16_t dc2 = base - dc;

        if (up_pos) {
            if (dn == up) {
                SUFFIX(update_border_line)(res[up], abs_a, va, b, abs_b, c, up_pos, dn_pos);
                continue;
            }
            SUFFIX(update_border_line)(res[up], abs_a, va, b, abs_b, c, up_pos, 64);
            up++;
            c -= b;
        }
        for (int y = up; y < dn; y++) {
            for (int x = 0; x < TILE_SIZE; x++) {
                int16_t c1 = c - va[x] + dc1;
                int16_t c2 = c - va[x] + dc2;
                c1 = FFMINMAX(c1, 0, FULL_VALUE);
                c2 = FFMINMAX(c2, 0, FULL_VALUE);
                res[y][x] += (c1 + c2) >> (7 - TILE_ORDER);
            }
            c -= b;
        }
        if (dn_pos)
            SUFFIX(update_border_line)(res[dn], abs_a, va, b, abs_b, c, 0, dn_pos);
    }

    int16_t cur = 256 * (int8_t) winding;
    for (int y = 0; y < TILE_SIZE; y++) {
        cur += delta[y];
        for (int x = 0; x < TILE_SIZE; x++) {
            int16_t val = res[y][x] + cur, neg_val = -val;
            val = (val > neg_val ? val : neg_val);
            buf[x] = FFMIN(val, 255);
        }
        buf += stride;
    }
}


void SUFFIX(ass_merge_tile)(uint8_t *buf, ptrdiff_t stride, const uint8_t *tile)
{
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++)
            buf[x] = FFMAX(buf[x], tile[x]);
        buf += stride;
        tile += TILE_SIZE;
    }
}


#undef SUFFIX
#undef TILE_ORDER
#undef FULL_VALUE
#undef RESCALE_AB
#undef RESCALE_C

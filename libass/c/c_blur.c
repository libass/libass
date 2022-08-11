/*
 * Copyright (C) 2015-2022 libass contributors
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

#include <stddef.h>
#include <stdint.h>
#include <memory.h>


static int16_t zero_line[16];
static int16_t dither_line[32] = {
     8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,  8, 40,
    56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24, 56, 24,
};

inline static const int16_t *get_line(const int16_t *ptr, size_t offs, size_t size)
{
    return offs < size ? ptr + offs : zero_line;
}

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


#define ALIGNMENT     16
#define SUFFIX(name)  name ## 16_c
#include "blur_template.h"
#undef ALIGNMENT
#undef SUFFIX

#define ALIGNMENT     32
#define SUFFIX(name)  name ## 32_c
#include "blur_template.h"
#undef ALIGNMENT
#undef SUFFIX

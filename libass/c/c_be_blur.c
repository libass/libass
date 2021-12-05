/*
 * Copyright (C) 2009-2022 libass contributors
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


static inline uint16_t sliding_sum(uint16_t *prev, uint16_t next)
{
    uint16_t sum = *prev + next;
    *prev = next;
    return sum;
}

/**
 * \brief Blur with [[1,2,1], [2,4,2], [1,2,1]] kernel
 * This blur is the same as the one employed by vsfilter.
 * Pure C implementation.
 */
void ass_be_blur_c(uint8_t *buf, ptrdiff_t stride,
                   size_t width, size_t height, uint16_t *tmp)
{
    uint16_t *col_pix_buf = tmp;
    uint16_t *col_sum_buf = tmp + stride;

    {
        size_t x = 1;
        uint16_t sum = buf[x - 1];
        for ( ; x < width; x++) {
            uint16_t col_pix = sliding_sum(&sum, buf[x - 1] + buf[x]);
            col_pix_buf[x - 1] = col_sum_buf[x - 1] = col_pix;
        }
        uint16_t col_pix = sum + buf[x - 1];
        col_pix_buf[x - 1] = col_sum_buf[x - 1] = col_pix;
    }

    for (size_t y = 1; y < height; y++) {
        uint8_t *dst = buf;
        buf += stride;

        size_t x = 1;
        uint16_t sum = buf[x - 1];
        for ( ; x < width; x++) {
            uint16_t col_pix = sliding_sum(&sum, buf[x - 1] + buf[x]);
            uint16_t col_sum = sliding_sum(&col_pix_buf[x - 1], col_pix);
            dst[x - 1] = sliding_sum(&col_sum_buf[x - 1], col_sum) >> 4;
        }
        uint16_t col_pix = sum + buf[x - 1];
        uint16_t col_sum = sliding_sum(&col_pix_buf[x - 1], col_pix);
        dst[x - 1] = sliding_sum(&col_sum_buf[x - 1], col_sum) >> 4;
    }

    for (size_t x = 0; x < width; x++)
        buf[x] = (col_sum_buf[x] + col_pix_buf[x]) >> 4;
}

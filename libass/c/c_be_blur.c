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


/**
 * \brief Blur with [[1,2,1], [2,4,2], [1,2,1]] kernel
 * This blur is the same as the one employed by vsfilter.
 * Pure C implementation.
 */
void ass_be_blur_c(uint8_t *buf, intptr_t stride,
                   intptr_t width, intptr_t height, uint16_t *tmp)
{
    uint16_t *col_pix_buf = tmp;
    uint16_t *col_sum_buf = tmp + width;
    unsigned x, y, old_pix, old_sum, temp1, temp2;
    uint8_t *src, *dst;
    y = 0;

    {
        src=buf+y*stride;

        x = 1;
        old_pix = src[x-1];
        old_sum = old_pix;
        for ( ; x < width; x++) {
            temp1 = src[x];
            temp2 = old_pix + temp1;
            old_pix = temp1;
            temp1 = old_sum + temp2;
            old_sum = temp2;
            col_pix_buf[x-1] = temp1;
            col_sum_buf[x-1] = temp1;
        }
        temp1 = old_sum + old_pix;
        col_pix_buf[x-1] = temp1;
        col_sum_buf[x-1] = temp1;
    }

    for (y++; y < height; y++) {
        src=buf+y*stride;
        dst=buf+(y-1)*stride;

        x = 1;
        old_pix = src[x-1];
        old_sum = old_pix;
        for ( ; x < width; x++) {
            temp1 = src[x];
            temp2 = old_pix + temp1;
            old_pix = temp1;
            temp1 = old_sum + temp2;
            old_sum = temp2;

            temp2 = col_pix_buf[x-1] + temp1;
            col_pix_buf[x-1] = temp1;
            dst[x-1] = (col_sum_buf[x-1] + temp2) >> 4;
            col_sum_buf[x-1] = temp2;
        }
        temp1 = old_sum + old_pix;
        temp2 = col_pix_buf[x-1] + temp1;
        col_pix_buf[x-1] = temp1;
        dst[x-1] = (col_sum_buf[x-1] + temp2) >> 4;
        col_sum_buf[x-1] = temp2;
    }

    {
        dst=buf+(y-1)*stride;
        for (x = 0; x < width; x++)
            dst[x] = (col_sum_buf[x] + col_pix_buf[x]) >> 4;
    }
}

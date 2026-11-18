/*
 * Copyright (C) 2024 libass contributors
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

#include "ass_utils.h"


/**
 * \brief Shift a bitmap by a fraction of a pixel in x and y directions
 * expressed in 26.6 fixed point.
 * Pure C implementation.
 */
void ass_shift_bitmap_c(uint8_t *restrict buf, ptrdiff_t stride,
                        size_t width, size_t height,
                        uint32_t shift_x, uint32_t shift_y, uint16_t *restrict tmp)
{
    ASSUME(shift_x < 64 && shift_y < 64 && !(stride % 16) && width > 0 && height > 0 && stride > 0);

    for (size_t y = 0; y < height; y++) {
        uint16_t shifted_from_left = 0;
        for (size_t x = 0; x < width; x++) {
            uint16_t px = buf[x] << 1;
            uint16_t shifted_from_top = tmp[x];

            uint16_t b_x = px * shift_x >> 6;

            px += shifted_from_left;
            px -= b_x;

            uint16_t b_y = px * shift_y >> 6;

            px += shifted_from_top;
            px -= b_y;

            buf[x] = (px + 1) >> 1;

            shifted_from_left = b_x;
            tmp[x] = b_y;
        }

        buf += stride;
    }
}

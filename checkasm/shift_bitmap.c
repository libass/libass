/*
 * Copyright (C) 2020-2024 libass contributors
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

#include "checkasm.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define HEIGHT 8
#define STRIDE 64
#define MIN_WIDTH 2

static void check_shift_bitmap(BitmapShiftFunc func)
{
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    ALIGN(uint16_t tmp[STRIDE], 32);
    declare_func(void,
                 uint8_t *restrict buf, ptrdiff_t stride,
                 size_t width, size_t height,
                 uint32_t shift_x, uint32_t shift_y, uint16_t *restrict tmp);

    if (check_func(func, "shift_bitmap")) {
        bool failed = false;
        for (int shift = 0; shift < 64 && !failed; shift++) {
            for (int w = MIN_WIDTH; w <= STRIDE && !failed; w++) {
                memset(buf_ref, 0, sizeof(buf_ref));
                memset(buf_new, 0, sizeof(buf_new));
                for (int y = 0; y < HEIGHT; y++) {
                    for (int x = 0; x < w - 1; x++)
                        buf_ref[y * STRIDE + x] = buf_new[y * STRIDE + x] = rnd();
                }

                memset(tmp, 0, sizeof(tmp));
                call_ref(buf_ref, STRIDE, w, HEIGHT, shift ? shift : 1, shift, tmp);

                memset(tmp, 0, sizeof(tmp));
                call_new(buf_new, STRIDE, w, HEIGHT, shift ? shift : 1, shift, tmp);

                for (int y = 0; y < HEIGHT; y++) {
                    if (memcmp(buf_ref + STRIDE * y, buf_new + STRIDE * y, w)) {
                        printf("FAILED: %i %i\n", shift, w);
                        failed = true;
                        fail();
                        break;
                    }
                }
            }
        }

        bench_new(buf_new, STRIDE, STRIDE, HEIGHT, 32, 32, tmp);
    }

    report("shift_bitmap");
}

void checkasm_check_shift_bitmap(unsigned cpu_flag)
{
    BitmapEngine engine = ass_bitmap_engine_init(cpu_flag);
    check_shift_bitmap(engine.shift_bitmap);
}

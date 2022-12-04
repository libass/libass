/*
 * Copyright (C) 2020-2022 libass contributors
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

#define HEIGHT 8
#define STRIDE 64
#define MIN_WIDTH 2

static void check_be_blur(BeBlurFunc func)
{
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    ALIGN(uint16_t tmp[STRIDE * 2], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride,
                 size_t width, size_t height, uint16_t *tmp);

    if (check_func(func, "be_blur")) {
        for (int w = MIN_WIDTH; w <= STRIDE; w++) {
            memset(buf_ref, 0, sizeof(buf_ref));
            memset(buf_new, 0, sizeof(buf_new));
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < w - 1; x++)
                    buf_ref[y * STRIDE + x] = buf_new[y * STRIDE + x] = rnd();
            }

            for (int i = 0; i < 2 * STRIDE; i++)
                tmp[i] = rnd();
            call_ref(buf_ref, STRIDE, w, HEIGHT, tmp);

            for (int i = 0; i < 2 * STRIDE; i++)
                tmp[i] = rnd();
            call_new(buf_new, STRIDE, w, HEIGHT, tmp);

            if (memcmp(buf_ref, buf_new, sizeof(buf_ref))) {
                fail();
                break;
            }
        }

        bench_new(buf_new, STRIDE, STRIDE, HEIGHT, tmp);
    }

    report("be_blur");
}

void checkasm_check_be_blur(unsigned cpu_flag)
{
    BitmapEngine engine = ass_bitmap_engine_init(cpu_flag);
    check_be_blur(engine.be_blur);
}

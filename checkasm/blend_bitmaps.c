/*
 * Copyright (C) 2020 rcombs <rcombs@rcombs.me>
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
#define DST_STRIDE 64
#define MIN_WIDTH  1
#define SRC1_STRIDE 96
#define SRC2_STRIDE 128

static void check_blend_bitmaps(BitmapBlendFunc func, const char *name)
{
    if (check_func(func, name)) {
        ALIGN(uint8_t src[SRC1_STRIDE * HEIGHT], 32);
        ALIGN(uint8_t dst_ref[DST_STRIDE * HEIGHT], 32);
        ALIGN(uint8_t dst_new[DST_STRIDE * HEIGHT], 32);
        declare_func(void,
                     uint8_t *dst, intptr_t dst_stride,
                     uint8_t *src, intptr_t src_stride,
                     intptr_t height, intptr_t width);

        for (int w = MIN_WIDTH; w <= DST_STRIDE; w++) {
            for (int i = 0; i < sizeof(src); i++)
                src[i] = rnd();

            for (int i = 0; i < sizeof(dst_ref); i++)
                dst_ref[i] = dst_new[i] = rnd();

            call_ref(dst_ref, DST_STRIDE,
                     src, SRC1_STRIDE,
                     HEIGHT, w);
            call_new(dst_new, DST_STRIDE,
                     src, SRC1_STRIDE,
                     HEIGHT, w);

            for (int i = 0; i < HEIGHT; i++) {
                if (memcmp(dst_ref + DST_STRIDE * i,
                           dst_new + DST_STRIDE * i,
                           w)) {
                    fail();
                    goto fail;
                }
            }
        }

        fail:
        bench_new(dst_new, DST_STRIDE, src, SRC1_STRIDE, HEIGHT, DST_STRIDE);
    }

    report(name);
}

static void check_mul_bitmaps(BitmapMulFunc func)
{
    if (check_func(func, "mul_bitmaps")) {
        ALIGN(uint8_t src1[SRC1_STRIDE * HEIGHT], 32);
        ALIGN(uint8_t src2[SRC2_STRIDE * HEIGHT], 32);
        ALIGN(uint8_t dst_ref[DST_STRIDE * HEIGHT], 32);
        ALIGN(uint8_t dst_new[DST_STRIDE * HEIGHT], 32);
        declare_func(void,
                     uint8_t *dst, intptr_t dst_stride,
                     uint8_t *src1, intptr_t src1_stride,
                     uint8_t *src2, intptr_t src2_stride,
                     intptr_t width, intptr_t height);

        for (int w = MIN_WIDTH; w < DST_STRIDE; w++) {
            for (int i = 0; i < sizeof(src1); i++)
                src1[i] = rnd();

            for (int i = 0; i < sizeof(src2); i++)
                src2[i] = rnd();

            call_ref(dst_ref, DST_STRIDE,
                     src1, SRC1_STRIDE,
                     src2, SRC2_STRIDE,
                     w, HEIGHT);
            call_new(dst_new, DST_STRIDE,
                     src1, SRC1_STRIDE,
                     src2, SRC2_STRIDE,
                     w, HEIGHT);

            for (int i = 0; i < HEIGHT; i++) {
                if (memcmp(dst_ref + DST_STRIDE * i,
                           dst_new + DST_STRIDE * i,
                           w)) {
                    fail();
                    goto fail;
                }
            }
        }

        fail:
        bench_new(dst_new, DST_STRIDE, src1, SRC1_STRIDE, src2, SRC2_STRIDE, DST_STRIDE, HEIGHT);
    }

    report("mul_bitmaps");
}

void checkasm_check_blend_bitmaps(const BitmapEngine *engine)
{
    check_blend_bitmaps(engine->add_bitmaps, "add_bitmaps");
    check_blend_bitmaps(engine->sub_bitmaps, "sub_bitmaps");
    check_mul_bitmaps(engine->mul_bitmaps);
}

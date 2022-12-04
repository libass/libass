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
#define DST_STRIDE 64
#define MIN_WIDTH  1
#define SRC1_STRIDE 96
#define SRC2_STRIDE 128

static void check_blend_bitmaps(BitmapBlendFunc func, const char *name)
{
    ALIGN(uint8_t src[SRC1_STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_ref[DST_STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_new[DST_STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *dst, ptrdiff_t dst_stride,
                 const uint8_t *src, ptrdiff_t src_stride,
                 size_t width, size_t height);

    if (check_func(func, name)) {
        for (int w = MIN_WIDTH; w <= DST_STRIDE; w++) {
            for (int i = 0; i < sizeof(src); i++)
                src[i] = rnd();

            for (int i = 0; i < sizeof(dst_ref); i++)
                dst_ref[i] = dst_new[i] = rnd();

            call_ref(dst_ref, DST_STRIDE, src, SRC1_STRIDE, w, HEIGHT);
            call_new(dst_new, DST_STRIDE, src, SRC1_STRIDE, w, HEIGHT);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, DST_STRIDE, src, SRC1_STRIDE, DST_STRIDE, HEIGHT);
    }

    report(name);
}

static void check_mul_bitmaps(BitmapMulFunc func)
{
    ALIGN(uint8_t src1[SRC1_STRIDE * HEIGHT], 32);
    ALIGN(uint8_t src2[SRC2_STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_ref[DST_STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_new[DST_STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *dst, ptrdiff_t dst_stride,
                 const uint8_t *src1, ptrdiff_t src1_stride,
                 const uint8_t *src2, ptrdiff_t src2_stride,
                 size_t width, size_t height);

    if (check_func(func, "mul_bitmaps")) {
        for (int w = MIN_WIDTH; w < DST_STRIDE; w++) {
            for (int i = 0; i < sizeof(src1); i++)
                src1[i] = rnd();

            for (int i = 0; i < sizeof(src2); i++)
                src2[i] = rnd();

            memset(dst_ref, 0, sizeof(dst_ref));
            memset(dst_new, 0, sizeof(dst_new));

            call_ref(dst_ref, DST_STRIDE, src1, SRC1_STRIDE, src2, SRC2_STRIDE, w, HEIGHT);
            call_new(dst_new, DST_STRIDE, src1, SRC1_STRIDE, src2, SRC2_STRIDE, w, HEIGHT);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, DST_STRIDE, src1, SRC1_STRIDE, src2, SRC2_STRIDE, DST_STRIDE, HEIGHT);
    }

    report("mul_bitmaps");
}

void checkasm_check_blend_bitmaps(unsigned cpu_flag)
{
    BitmapEngine engine = ass_bitmap_engine_init(cpu_flag);
    check_blend_bitmaps(engine.add_bitmaps, "add_bitmaps");
    check_blend_bitmaps(engine.imul_bitmaps, "imul_bitmaps");
    check_mul_bitmaps(engine.mul_bitmaps);
}

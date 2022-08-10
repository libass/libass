/*
 * Copyright (C) 2021-2022 libass contributors
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

#define HEIGHT 13
#define STRIDE 64
#define MIN_WIDTH 1

static void check_stripe_unpack(Convert8to16Func func, const char *name, int align)
{
    ALIGN(uint8_t src[STRIDE * HEIGHT], 32);
    ALIGN(int16_t dst_ref[STRIDE * HEIGHT], 32);
    ALIGN(int16_t dst_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
                 size_t width, size_t height);

    if (check_func(func, name, align)) {
        for (int w = MIN_WIDTH; w <= STRIDE; w++) {
            for (int i = 0; i < sizeof(src); i++)
                src[i] = rnd();

            for (int i = 0; i < sizeof(dst_ref) / 2; i++)
                dst_ref[i] = dst_new[i] = rnd();

            int h = HEIGHT - (rnd() & 3);
            call_ref(dst_ref, src, STRIDE, w, h);
            call_new(dst_new, src, STRIDE, w, h);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, src, STRIDE, STRIDE, HEIGHT);
    }

    report(name, align);
}

static void check_stripe_pack(Convert16to8Func func, const char *name, int align)
{
    ALIGN(int16_t src[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t dst_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
                 size_t width, size_t height);

    if (check_func(func, name, align)) {
        for (int w = MIN_WIDTH; w <= STRIDE; w++) {
            for (int i = 0; i < sizeof(src) / 2; i++)
                src[i] = rnd() % 0x4001;

            memset(dst_ref, 0, sizeof(dst_ref));
            memset(dst_new, 0, sizeof(dst_new));
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < w; x++)
                    dst_ref[y * STRIDE + x] = dst_new[y * STRIDE + x] = rnd();
            }

            int h = HEIGHT - (rnd() & 3);
            call_ref(dst_ref, STRIDE, src, w, h);
            call_new(dst_new, STRIDE, src, w, h);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, STRIDE, src, STRIDE, HEIGHT);
    }

    report(name, align);
}

static void check_fixed_filter(FilterFunc func, const char *name, int align)
{
    enum { PADDING = FFMAX(32 * HEIGHT, 4 * STRIDE) };

    ALIGN(int16_t src[STRIDE * HEIGHT], 32);
    ALIGN(int16_t dst_ref[2 * STRIDE * HEIGHT + PADDING], 32);
    ALIGN(int16_t dst_new[2 * STRIDE * HEIGHT + PADDING], 32);
    declare_func(void,
                 int16_t *dst, const int16_t *src,
                 size_t src_width, size_t src_height);

    if (check_func(func, name, align)) {
        for (int w = MIN_WIDTH; w <= STRIDE; w++) {
            for (int i = 0; i < sizeof(src) / 2; i++)
                src[i] = rnd() % 0x4001;

            for (int i = 0; i < sizeof(dst_ref) / 2; i++)
                dst_ref[i] = dst_new[i] = rnd();

            int h = HEIGHT - (rnd() & 3);
            call_ref(dst_ref, src, w, h);
            call_new(dst_new, src, w, h);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, src, STRIDE, HEIGHT);
    }

    report(name, align);
}

static void check_param_filter(ParamFilterFunc func, const char *name, int n, int align)
{
    enum { PADDING = FFMAX(32 * HEIGHT, 16 * STRIDE) };

    ALIGN(int16_t src[STRIDE * HEIGHT], 32);
    ALIGN(int16_t dst_ref[STRIDE * HEIGHT + PADDING], 32);
    ALIGN(int16_t dst_new[STRIDE * HEIGHT + PADDING], 32);
    int16_t param[8];
    declare_func(void,
                 int16_t *dst, const int16_t *src,
                 size_t src_width, size_t src_height,
                 const int16_t *param);

    if (check_func(func, name, n, align)) {
        for (int w = MIN_WIDTH; w <= STRIDE; w++) {
            for (int i = 0; i < sizeof(src) / 2; i++)
                src[i] = rnd() % 0x4001;

            for (int i = 0; i < sizeof(dst_ref) / 2; i++)
                dst_ref[i] = dst_new[i] = rnd();

            int left = 0x8000;
            for (int i = 0; i < n; i++) {
                param[i] = rnd() % (left + 1);
                left -= param[i];
            }

            int h = HEIGHT - (rnd() & 3);
            call_ref(dst_ref, src, w, h, param);
            call_new(dst_new, src, w, h, param);

            if (memcmp(dst_ref, dst_new, sizeof(dst_ref))) {
                fail();
                break;
            }
        }

        bench_new(dst_new, src, STRIDE, HEIGHT, param);
    }

    report(name, n, align);
}

void checkasm_check_blur(unsigned cpu_flag)
{
    BitmapEngine engine[2] = {
        ass_bitmap_engine_init(cpu_flag),
        ass_bitmap_engine_init(cpu_flag | ASS_FLAG_WIDE_STRIPE)
    };
    for (int i = 0; i < 2; i++) {
        int align = 1 << engine[i].align_order;
        check_stripe_unpack(engine[i].stripe_unpack, "stripe_unpack%d", align);
        check_stripe_pack(engine[i].stripe_pack, "stripe_pack%d", align);
        check_fixed_filter(engine[i].shrink_horz, "shrink_horz%d", align);
        check_fixed_filter(engine[i].shrink_vert, "shrink_vert%d", align);
        check_fixed_filter(engine[i].expand_horz, "expand_horz%d", align);
        check_fixed_filter(engine[i].expand_vert, "expand_vert%d", align);
        for (int n = 4; n <= 8; n++) {
            check_param_filter(engine[i].blur_horz[n - 4], "blur%d_horz%d", n, align);
            check_param_filter(engine[i].blur_vert[n - 4], "blur%d_vert%d", n, align);
        }
    }
}

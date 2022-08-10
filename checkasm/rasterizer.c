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
#include "ass_rasterizer.h"

#define HEIGHT 34
#define STRIDE 96
#define BORD_X 32
#define BORD_Y 1
#define REP_COUNT 8
#define MAX_SEG 8


static void generate_segment(struct segment *line, int tile_size, int y1, int y2)
{
    unsigned flags = rnd();
    line->a = rnd() & 0x3FFFFFFF;
    line->b = rnd() & 0x3FFFFFFF;
    if (flags % 3u != 1)
        line->a |= 0x40000000;
    if (flags % 3u != 2)
        line->b |= 0x40000000;
    int32_t max_ab = FFMAX(line->a, line->b);  // 2^30 <= max_ab < 2^31
    if (flags & 1)
        line->a = -line->a;
    if (flags & 2)
        line->b = -line->b;

    int mask = (64 * tile_size << 16) - 1;
    int x = rnd() & mask, y = rnd() & mask;
    line->c = (line->a * (int64_t) x + line->b * (int64_t) y + (1 << 15)) >> 16;
    // |line->c| <= 2^(tile_order + 7) * max_ab < 2^(tile_order + 38)

#if 0
    line->scale = ((uint64_t) 1 << 60) / max_ab;
#else
    line->scale = (rnd() & 0x1FFFFFFF) | 0x20000000;
    if (line->scale * (uint64_t) max_ab > (uint64_t) 1 << 60)
        line->scale ^= 0x20000000;
#endif

    line->flags = flags & 4 ? SEGFLAG_EXACT_LEFT : 0;  // only left is used
    if (line->a >= 0)
        line->flags ^= SEGFLAG_DN | SEGFLAG_UL_DR;
    if (line->b > 0)
        line->flags ^= SEGFLAG_UL_DR;

    line->x_min = flags & 8 ? 0x1234ABCD : 0;
    line->x_max = 0xDEADC0DE;  // not used

    y1 &= 64 * tile_size - 1;
    y2 &= 64 * tile_size - 1;
    line->y_min = FFMIN(y1, y2 + 1);
    line->y_max = FFMAX(y1, y2 + 1);
}


static void check_fill_solid(FillSolidTileFunc func, const char *name, int tile_size)
{
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride, int set);

    if (check_func(func, name, tile_size)) {
        for(int set = 0; set <= 1; set++) {
            for (int i = 0; i < sizeof(buf_ref); i++)
                buf_ref[i] = buf_new[i] = rnd();

            call_ref(buf_ref + BORD_Y * STRIDE + BORD_X, STRIDE, set);
            call_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, set);

            if (memcmp(buf_ref, buf_new, sizeof(buf_ref))) {
                fail();
                break;
            }
        }

        bench_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, 1);
    }

    report(name, tile_size);
}

static void check_fill_halfplane(FillHalfplaneTileFunc func, const char *name, int tile_size)
{
    struct segment line;
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride,
                 int32_t a, int32_t b, int64_t c, int32_t scale);

    if (check_func(func, name, tile_size)) {
        for(int rep = 0; rep < REP_COUNT; rep++) {
            for (int i = 0; i < sizeof(buf_ref); i++)
                buf_ref[i] = buf_new[i] = rnd();

            generate_segment(&line, tile_size, 0, 0);
            call_ref(buf_ref + BORD_Y * STRIDE + BORD_X, STRIDE, line.a, line.b, line.c, line.scale);
            call_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, line.a, line.b, line.c, line.scale);

            if (memcmp(buf_ref, buf_new, sizeof(buf_ref))) {
                fail();
                break;
            }
        }

        bench_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, line.a, line.b, line.c, line.scale);
    }

    report(name, tile_size);
}

static void check_fill_generic(FillGenericTileFunc func, const char *name, int tile_size)
{
    struct segment line[MAX_SEG];
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride,
                 const struct segment *line, size_t n_lines,
                 int winding);

    if (check_func(func, name, tile_size)) {
        for(int rep = 0; rep < REP_COUNT; rep++) {
            for (int i = 0; i < sizeof(buf_ref); i++)
                buf_ref[i] = buf_new[i] = rnd();

            int n = rnd() % MAX_SEG + 1;
            for (int i = 0; i < n; i++)
                generate_segment(line + i, tile_size, rnd(), rnd());

            int winding = rnd() % 5 - 2;
            call_ref(buf_ref + BORD_Y * STRIDE + BORD_X, STRIDE, line, n, winding);
            call_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, line, n, winding);

            if (memcmp(buf_ref, buf_new, sizeof(buf_ref))) {
                fail();
                break;
            }
        }

        generate_segment(line + 0, tile_size, 3 * 64 + 0, 7 * 64 - 1);
        generate_segment(line + 1, tile_size, 3 * 64 + 5, 7 * 64 - 5);
        generate_segment(line + 2, tile_size, 5 * 64 + 3, 5 * 64 + 9);
        generate_segment(line + 3, tile_size, 5 * 64 + 9, 5 * 64 + 8);
        bench_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, line, 4, 0);
    }

    report(name, tile_size);
}

static void check_merge_tile(MergeTileFunc func, const char *name, int tile_size)
{
    ALIGN(uint8_t src[32 * 32], 32);
    ALIGN(uint8_t buf_ref[STRIDE * HEIGHT], 32);
    ALIGN(uint8_t buf_new[STRIDE * HEIGHT], 32);
    declare_func(void,
                 uint8_t *buf, ptrdiff_t stride, const uint8_t *tile);

    if (check_func(func, name, tile_size)) {
        for (int i = 0; i < sizeof(src); i++)
            src[i] = rnd();
        for (int i = 0; i < sizeof(buf_ref); i++)
            buf_ref[i] = buf_new[i] = rnd();

        call_ref(buf_ref + BORD_Y * STRIDE + BORD_X, STRIDE, src);
        call_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, src);

        if (memcmp(buf_ref, buf_new, sizeof(buf_ref)))
            fail();

        bench_new(buf_new + BORD_Y * STRIDE + BORD_X, STRIDE, src);
    }

    report(name, tile_size);
}


void checkasm_check_rasterizer(unsigned cpu_flag)
{
    BitmapEngine engine[2] = {
        ass_bitmap_engine_init(cpu_flag),
        ass_bitmap_engine_init(cpu_flag | ASS_FLAG_LARGE_TILES)
    };
    for (int i = 0; i < 2; i++) {
        int tile_size = 1 << engine[i].tile_order;
        check_fill_solid(engine[i].fill_solid, "fill_solid_tile%d", tile_size);
        check_fill_halfplane(engine[i].fill_halfplane, "fill_halfplane_tile%d", tile_size);
        check_fill_generic(engine[i].fill_generic, "fill_generic_tile%d", tile_size);
        check_merge_tile(engine[i].merge, "merge_tile%d", tile_size);
    }
}

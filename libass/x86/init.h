/*
 * Copyright (C) 2021 rcombs <rcombs@rcombs.me>
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

#include "ass_bitmap_engine.h"
#include "ass_cpu.h"

#define ENGINE_SUFFIX   sse2
#include "ass_func_template.h"
#undef ENGINE_SUFFIX

#define ENGINE_SUFFIX   avx2
#include "ass_func_template.h"
#undef ENGINE_SUFFIX

void ass_bitmap_init_x86(BitmapEngine* engine, ASS_CPUFlags flags)
{
    if (flags & ASS_CPU_FLAG_X86_SSE2) {
#if CONFIG_LARGE_TILES
        engine->fill_solid = ass_fill_solid_tile32_sse2;
        engine->fill_halfplane = ass_fill_halfplane_tile32_sse2;
        engine->fill_generic = ass_fill_generic_tile32_sse2;
#else
        engine->fill_solid = ass_fill_solid_tile16_sse2;
        engine->fill_halfplane = ass_fill_halfplane_tile16_sse2;
        engine->fill_generic = ass_fill_generic_tile16_sse2;
#endif

        engine->add_bitmaps = ass_add_bitmaps_sse2;
#ifdef __x86_64__
        engine->sub_bitmaps = ass_sub_bitmaps_sse2;
        engine->mul_bitmaps = ass_mul_bitmaps_sse2;
#endif

#ifdef __x86_64__
        engine->be_blur = ass_be_blur_sse2;
#endif

        engine->stripe_unpack = ass_stripe_unpack_sse2;
        engine->stripe_pack = ass_stripe_pack_sse2;
        engine->shrink_horz = ass_shrink_horz_sse2;
        engine->shrink_vert = ass_shrink_vert_sse2;
        engine->expand_horz = ass_expand_horz_sse2;
        engine->expand_vert = ass_expand_vert_sse2;

        engine->blur_horz[0] = ass_blur4_horz_sse2;
        engine->blur_horz[1] = ass_blur5_horz_sse2;
        engine->blur_horz[2] = ass_blur6_horz_sse2;
        engine->blur_horz[3] = ass_blur7_horz_sse2;
        engine->blur_horz[4] = ass_blur8_horz_sse2;

        engine->blur_horz[0] = ass_blur4_vert_sse2;
        engine->blur_horz[1] = ass_blur5_vert_sse2;
        engine->blur_horz[2] = ass_blur6_vert_sse2;
        engine->blur_horz[3] = ass_blur7_vert_sse2;
        engine->blur_horz[4] = ass_blur8_vert_sse2;
    }

    if (flags & ASS_CPU_FLAG_X86_AVX2) {
#if CONFIG_LARGE_TILES
        engine->fill_solid = ass_fill_solid_tile32_avx2;
        engine->fill_halfplane = ass_fill_halfplane_tile32_avx2;
        engine->fill_generic = ass_fill_generic_tile32_avx2;
#else
        engine->fill_solid = ass_fill_solid_tile16_avx2;
        engine->fill_halfplane = ass_fill_halfplane_tile16_avx2;
        engine->fill_generic = ass_fill_generic_tile16_avx2;
#endif

        engine->add_bitmaps = ass_add_bitmaps_avx2;
#ifdef __x86_64__
        engine->sub_bitmaps = ass_sub_bitmaps_avx2;
        engine->mul_bitmaps = ass_mul_bitmaps_avx2;
#endif

        engine->stripe_unpack = ass_stripe_unpack_avx2;
        engine->stripe_pack = ass_stripe_pack_avx2;
        engine->shrink_horz = ass_shrink_horz_avx2;
        engine->shrink_vert = ass_shrink_vert_avx2;
        engine->expand_horz = ass_expand_horz_avx2;
        engine->expand_vert = ass_expand_vert_avx2;

        engine->blur_horz[0] = ass_blur4_horz_avx2;
        engine->blur_horz[1] = ass_blur5_horz_avx2;
        engine->blur_horz[2] = ass_blur6_horz_avx2;
        engine->blur_horz[3] = ass_blur7_horz_avx2;
        engine->blur_horz[4] = ass_blur8_horz_avx2;

        engine->blur_horz[0] = ass_blur4_vert_avx2;
        engine->blur_horz[1] = ass_blur5_vert_avx2;
        engine->blur_horz[2] = ass_blur6_vert_avx2;
        engine->blur_horz[3] = ass_blur7_vert_avx2;
        engine->blur_horz[4] = ass_blur8_vert_avx2;
    }
};

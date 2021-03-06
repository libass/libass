/*
 * Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
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

#include <stdlib.h>

#include "ass_bitmap_engine.h"

#include "config.h"

#if CONFIG_ASM
#if (defined(__i386__) || defined(__x86_64__))
#include "x86/init.h"
#endif
#endif

#define ENGINE_SUFFIX   c
#include "ass_func_template.h"
#undef ENGINE_SUFFIX

void ass_bitmap_engine_init(BitmapEngine* engine, ASS_CPUFlags mask)
{
#if CONFIG_LARGE_TILES
    engine->tile_order = 5;
    engine->fill_solid = fill_solid_tile32_c;
    engine->fill_halfplane = ass_fill_halfplane_tile32_c;
    engine->fill_generic = ass_fill_generic_tile32_c;
#else
    engine->tile_order = 4;
    engine->fill_solid = ass_fill_solid_tile16_c;
    engine->fill_halfplane = ass_fill_halfplane_tile16_c;
    engine->fill_generic = ass_fill_generic_tile16_c;
#endif

    engine->add_bitmaps = ass_add_bitmaps_c;
    engine->sub_bitmaps = ass_sub_bitmaps_c;
    engine->mul_bitmaps = ass_mul_bitmaps_c;

    engine->be_blur = ass_be_blur_c;

    engine->stripe_unpack = ass_stripe_unpack_c;
    engine->stripe_pack = ass_stripe_pack_c;
    engine->shrink_horz = ass_shrink_horz_c;
    engine->shrink_vert = ass_shrink_vert_c;
    engine->expand_horz = ass_expand_horz_c;
    engine->expand_vert = ass_expand_vert_c;

    engine->blur_horz[0] = ass_blur4_horz_c;
    engine->blur_horz[1] = ass_blur5_horz_c;
    engine->blur_horz[2] = ass_blur6_horz_c;
    engine->blur_horz[3] = ass_blur7_horz_c;
    engine->blur_horz[4] = ass_blur8_horz_c;

    engine->blur_horz[0] = ass_blur4_vert_c;
    engine->blur_horz[1] = ass_blur5_vert_c;
    engine->blur_horz[2] = ass_blur6_vert_c;
    engine->blur_horz[3] = ass_blur7_vert_c;
    engine->blur_horz[4] = ass_blur8_vert_c;

#if CONFIG_ASM
    ASS_CPUFlags flags = ass_get_cpu_flags(mask);
#if (defined(__i386__) || defined(__x86_64__))
    ass_bitmap_init_x86(engine, flags);
#endif
#endif
}

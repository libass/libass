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

#ifndef X86_INIT_H
#define X86_INIT_H

#include "ass_bitmap_engine.h"
#include "ass_cpu.h"

#define ENGINE_SUFFIX   neon
#include "ass_func_template.h"
#undef ENGINE_SUFFIX

void ass_bitmap_init_aarch64(BitmapEngine* engine, ASS_CPUFlags flags)
{
    if (flags & ASS_CPU_FLAG_ARM_NEON) {
        engine->add_bitmaps = ass_add_bitmaps_neon;
        engine->imul_bitmaps = ass_imul_bitmaps_neon;
        engine->mul_bitmaps = ass_mul_bitmaps_neon;

        engine->be_blur = ass_be_blur_neon;
    }
};

#endif /* X86_INIT_H */

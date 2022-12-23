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

#include "config.h"
#include "ass_compat.h"

#include <stdbool.h>

#include "ass_bitmap_engine.h"
#include "x86/cpuid.h"


#define RASTERIZER_PROTOTYPES(tile_size, suffix) \
    FillSolidTileFunc     ass_fill_solid_tile     ## tile_size ## _ ## suffix; \
    FillHalfplaneTileFunc ass_fill_halfplane_tile ## tile_size ## _ ## suffix; \
    FillGenericTileFunc   ass_fill_generic_tile   ## tile_size ## _ ## suffix; \
    MergeTileFunc         ass_merge_tile          ## tile_size ## _ ## suffix;

#define RASTERIZER_FUNCTION(name, suffix) \
    engine.name = mask & ASS_FLAG_LARGE_TILES ? \
        ass_ ## name ## _tile32_ ## suffix : \
        ass_ ## name ## _tile16_ ## suffix;

#define RASTERIZER_FUNCTIONS(suffix) \
    RASTERIZER_FUNCTION(fill_solid,     suffix) \
    RASTERIZER_FUNCTION(fill_halfplane, suffix) \
    RASTERIZER_FUNCTION(fill_generic,   suffix) \
    RASTERIZER_FUNCTION(merge,          suffix)


#define GENERIC_PROTOTYPES(suffix) \
    BitmapBlendFunc ass_add_bitmaps_  ## suffix; \
    BitmapBlendFunc ass_imul_bitmaps_ ## suffix; \
    BitmapMulFunc   ass_mul_bitmaps_  ## suffix; \
    BeBlurFunc      ass_be_blur_      ## suffix;

#define GENERIC_FUNCTION(name, suffix) \
    engine.name = ass_ ## name ## _ ## suffix;

#define GENERIC_FUNCTIONS(suffix) \
    GENERIC_FUNCTION(add_bitmaps,  suffix) \
    GENERIC_FUNCTION(imul_bitmaps, suffix) \
    GENERIC_FUNCTION(mul_bitmaps,  suffix) \
    GENERIC_FUNCTION(be_blur,      suffix)


#define PARAM_BLUR_SET(suffix) \
    ass_blur4_ ## suffix, \
    ass_blur5_ ## suffix, \
    ass_blur6_ ## suffix, \
    ass_blur7_ ## suffix, \
    ass_blur8_ ## suffix

#define BLUR_PROTOTYPES(stripe_width, suffix) \
    Convert8to16Func ass_stripe_unpack  ## stripe_width ## _ ## suffix; \
    Convert16to8Func ass_stripe_pack    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_shrink_horz    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_shrink_vert    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_expand_horz    ## stripe_width ## _ ## suffix; \
    FilterFunc       ass_expand_vert    ## stripe_width ## _ ## suffix; \
    ParamFilterFunc PARAM_BLUR_SET(horz ## stripe_width ## _ ## suffix); \
    ParamFilterFunc PARAM_BLUR_SET(vert ## stripe_width ## _ ## suffix);

#define BLUR_FUNCTION(name, alignment, suffix) \
    engine.name = ass_ ## name ## alignment ## _ ## suffix;

#define PARAM_BLUR_FUNCTION(dir, alignment, suffix) \
    engine.blur_ ## dir[0] = ass_blur4_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[1] = ass_blur5_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[2] = ass_blur6_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[3] = ass_blur7_ ## dir ## alignment ## _ ## suffix; \
    engine.blur_ ## dir[4] = ass_blur8_ ## dir ## alignment ## _ ## suffix;

#define BLUR_FUNCTIONS(align_order_, alignment, suffix) \
    BLUR_FUNCTION(stripe_unpack, alignment, suffix) \
    BLUR_FUNCTION(stripe_pack,   alignment, suffix) \
    BLUR_FUNCTION(shrink_horz,   alignment, suffix) \
    BLUR_FUNCTION(shrink_vert,   alignment, suffix) \
    BLUR_FUNCTION(expand_horz,   alignment, suffix) \
    BLUR_FUNCTION(expand_vert,   alignment, suffix) \
    PARAM_BLUR_FUNCTION(horz, alignment, suffix) \
    PARAM_BLUR_FUNCTION(vert, alignment, suffix) \
    engine.align_order = align_order_;


#define ALL_PROTOTYPES(alignment, suffix) \
    RASTERIZER_PROTOTYPES(16, suffix) \
    RASTERIZER_PROTOTYPES(32, suffix) \
    GENERIC_PROTOTYPES(suffix) \
    BLUR_PROTOTYPES(alignment, suffix)

#define ALL_FUNCTIONS(align_order_, alignment, suffix) \
    RASTERIZER_FUNCTIONS(suffix) \
    GENERIC_FUNCTIONS(suffix) \
    BLUR_FUNCTIONS(align_order_, alignment, suffix)


unsigned ass_get_cpu_flags(unsigned mask)
{
    unsigned flags = ASS_CPU_FLAG_NONE;

#if CONFIG_ASM && ARCH_X86

    if (!ass_has_cpuid())
        return flags & mask;

    uint32_t eax = 0, ebx, ecx, edx;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    uint32_t max_leaf = eax;

    bool avx = false;
    if (max_leaf >= 1) {
        eax = 1;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);
        if (edx & (1 << 26)) {  // SSE2
            flags |= ASS_CPU_FLAG_X86_SSE2;
            if (ecx & (1 << 0) &&  // SSE3
                ecx & (1 << 9))    // SSSE3
                    flags |= ASS_CPU_FLAG_X86_SSSE3;
        }

        if (ecx & (1 << 27) &&  // OSXSAVE
            ecx & (1 << 28)) {  // AVX
            uint32_t xcr0l, xcr0h;
            ass_get_xgetbv(0, &xcr0l, &xcr0h);
            if (xcr0l & (1 << 1) &&  // XSAVE for XMM
                xcr0l & (1 << 2))    // XSAVE for YMM
                    avx = true;
        }
    }

    if (max_leaf >= 7) {
        eax = 7;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);
        if (avx && ebx & (1 << 5))  // AVX2
            flags |= ASS_CPU_FLAG_X86_AVX2;
    }

#endif

#if ARCH_AARCH64
    flags = ASS_CPU_FLAG_ARM_NEON;
#endif

    return flags & mask;
}

BitmapEngine ass_bitmap_engine_init(unsigned mask)
{
    ALL_PROTOTYPES(16, c)
    BLUR_PROTOTYPES(32, c)
    BitmapEngine engine = {0};
    engine.tile_order = mask & ASS_FLAG_LARGE_TILES ? 5 : 4;

#if CONFIG_ASM
    unsigned flags = ass_get_cpu_flags(mask);
#if ARCH_X86
    if (flags & ASS_CPU_FLAG_X86_AVX2) {
        ALL_PROTOTYPES(32, avx2)
        ALL_FUNCTIONS(5, 32, avx2)
        return engine;
    } else if (flags & ASS_CPU_FLAG_X86_SSE2) {
        ALL_PROTOTYPES(16, sse2)
        ALL_FUNCTIONS(4, 16, sse2)
        if (flags & ASS_CPU_FLAG_X86_SSSE3) {
            ALL_PROTOTYPES(16, ssse3)
            RASTERIZER_FUNCTION(fill_generic, ssse3)
            GENERIC_FUNCTION(be_blur, ssse3)
            BLUR_FUNCTION(shrink_horz, 16, ssse3)
            BLUR_FUNCTION(expand_horz, 16, ssse3)
            PARAM_BLUR_FUNCTION(horz, 16, ssse3)
        }
        return engine;
    }
#elif ARCH_AARCH64
    if (flags & ASS_CPU_FLAG_ARM_NEON) {
        ALL_PROTOTYPES(16, neon)
        ALL_FUNCTIONS(4, 16, neon)
        return engine;
    }
#endif
#endif

    ALL_FUNCTIONS(4, 16, c)
    if (mask & ASS_FLAG_WIDE_STRIPE) {
        BLUR_FUNCTIONS(5, 32, c)
    }
    return engine;
}

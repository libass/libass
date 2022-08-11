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

#define GENERIC_PROTOTYPES(suffix) \
    BitmapBlendFunc ass_add_bitmaps_  ## suffix; \
    BitmapBlendFunc ass_imul_bitmaps_ ## suffix; \
    BitmapMulFunc   ass_mul_bitmaps_  ## suffix; \
    BeBlurFunc      ass_be_blur_      ## suffix;

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

#define BITMAP_ENGINE(align_order_, alignment, tile_order_, tile_size, suffix, be_suffix) \
    const BitmapEngine ass_bitmap_engine_ ## be_suffix = { \
        .align_order    = align_order_, \
        .tile_order     = tile_order_, \
        .fill_solid     = ass_fill_solid_tile     ## tile_size ## _ ## suffix, \
        .fill_halfplane = ass_fill_halfplane_tile ## tile_size ## _ ## suffix, \
        .fill_generic   = ass_fill_generic_tile   ## tile_size ## _ ## suffix, \
        .merge_tile     = ass_merge_tile          ## tile_size ## _ ## suffix, \
        .add_bitmaps    = ass_add_bitmaps_  ## suffix, \
        .imul_bitmaps   = ass_imul_bitmaps_ ## suffix, \
        .mul_bitmaps    = ass_mul_bitmaps_  ## suffix, \
        .be_blur        = ass_be_blur_      ## suffix, \
        .stripe_unpack  = ass_stripe_unpack ## alignment ## _ ## suffix, \
        .stripe_pack    = ass_stripe_pack   ## alignment ## _ ## suffix, \
        .shrink_horz    = ass_shrink_horz   ## alignment ## _ ## suffix, \
        .shrink_vert    = ass_shrink_vert   ## alignment ## _ ## suffix, \
        .expand_horz    = ass_expand_horz   ## alignment ## _ ## suffix, \
        .expand_vert    = ass_expand_vert   ## alignment ## _ ## suffix, \
        .blur_horz  = { PARAM_BLUR_SET(horz ## alignment ## _ ## suffix) }, \
        .blur_vert  = { PARAM_BLUR_SET(vert ## alignment ## _ ## suffix) }, \
    };


RASTERIZER_PROTOTYPES(16, c)
RASTERIZER_PROTOTYPES(32, c)
GENERIC_PROTOTYPES(c)
BLUR_PROTOTYPES(16, c)
BLUR_PROTOTYPES(32, c)
BITMAP_ENGINE(4, 16, 4, 16, c, c)
BITMAP_ENGINE(4, 16, 5, 32, c, lt_c)
BITMAP_ENGINE(5, 32, 4, 16, c, c32)
BITMAP_ENGINE(5, 32, 5, 32, c, lt_c32)

#if CONFIG_ASM && ARCH_X86

RASTERIZER_PROTOTYPES(16, sse2)
RASTERIZER_PROTOTYPES(32, sse2)
GENERIC_PROTOTYPES(sse2)
BLUR_PROTOTYPES(16, sse2)
BITMAP_ENGINE(4, 16, 4, 16, sse2, sse2)
BITMAP_ENGINE(4, 16, 5, 32, sse2, lt_sse2)

RASTERIZER_PROTOTYPES(16, avx2)
RASTERIZER_PROTOTYPES(32, avx2)
GENERIC_PROTOTYPES(avx2)
BLUR_PROTOTYPES(32, avx2)
BITMAP_ENGINE(5, 32, 4, 16, avx2, avx2)
BITMAP_ENGINE(5, 32, 5, 32, avx2, lt_avx2)

#endif


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
        if (edx & (1 << 26))  // SSE2
            flags |= ASS_CPU_FLAG_X86_SSE2;

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

    return flags & mask;
}

const BitmapEngine *ass_bitmap_engine_init(unsigned mask)
{
#if CONFIG_ASM
    unsigned flags = ass_get_cpu_flags(mask);
#if ARCH_X86
    if (flags & ASS_CPU_FLAG_X86_AVX2)
        return mask & ASS_FLAG_LARGE_TILES ? &ass_bitmap_engine_lt_avx2 : &ass_bitmap_engine_avx2;
    if (flags & ASS_CPU_FLAG_X86_SSE2)
        return mask & ASS_FLAG_LARGE_TILES ? &ass_bitmap_engine_lt_sse2 : &ass_bitmap_engine_sse2;
#endif
#endif
    if (mask & ASS_FLAG_WIDE_STRIPE)
        return mask & ASS_FLAG_LARGE_TILES ? &ass_bitmap_engine_lt_c32 : &ass_bitmap_engine_c32;
    return mask & ASS_FLAG_LARGE_TILES ? &ass_bitmap_engine_lt_c : &ass_bitmap_engine_c;
}

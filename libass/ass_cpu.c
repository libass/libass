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

#include "config.h"

#include "ass_cpu.h"
#include "x86/cpuid.h"

ASS_CPUFlags ass_get_cpu_flags(ASS_CPUFlags mask)
{
    ASS_CPUFlags flags = ASS_CPU_FLAG_NONE;

#if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_leaf = 0;

    flags = ASS_CPU_FLAG_X86;

    eax = 0;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    max_leaf = eax;

    if (max_leaf >= 1) {
        eax = 1;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);
        if (edx & (1 << 26)) // SSE2
            flags |= ASS_CPU_FLAG_X86_SSE2;

        if (ecx & (1 << 27) && // OSXSAVE
            ecx & (1 << 28)) { // AVX
            uint32_t xcr0l, xcr0h;
            ass_get_xgetbv(0, &xcr0l, &xcr0h);
            if (xcr0l & (1 << 1) && // XSAVE for XMM
                xcr0l & (1 << 2))   // XSAVE for YMM
                flags |= ASS_CPU_FLAG_X86_AVX;
        }
    }

    if (max_leaf >= 7) {
        eax = 7;
        ass_get_cpuid(&eax, &ebx, &ecx, &edx);

        if (flags & ASS_CPU_FLAG_X86_AVX &&
            (ebx & (1 << 5))) // AVX2
            flags |= ASS_CPU_FLAG_X86_AVX2;
    }
#endif

    return flags & mask;
}

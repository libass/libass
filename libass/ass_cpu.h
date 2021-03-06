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

#ifndef LIBASS_CPU_H
#define LIBASS_CPU_H

typedef enum ASS_CPUFlags {
    ASS_CPU_FLAG_NONE          = 0x00,
    ASS_CPU_FLAG_X86           = 0x01,
    ASS_CPU_FLAG_X86_SSE2      = 0x02,
    ASS_CPU_FLAG_X86_AVX       = 0x04,
    ASS_CPU_FLAG_X86_AVX2      = 0x08,
    ASS_CPU_FLAG_X86_AVX512ICL = 0x10, // Placeholder for checkasm
    ASS_CPU_FLAG_MAX,
    ASS_CPU_FLAG_ALL = ((ASS_CPU_FLAG_MAX & ~1ULL) << 1) - 1,
} ASS_CPUFlags;

ASS_CPUFlags ass_get_cpu_flags(ASS_CPUFlags mask);

#endif

/*
 * Copyright (C) 2021 rcombs
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

.macro function name, export=1
    .macro endfunc
        .purgem endfunc
    .endm
    .text
    .if \export
        .global EXTERN_PREFIX\()ass_\name
    .endif
    .align 2
    EXTERN_PREFIX\()ass_\name:
.endmacro

.macro const name, align=4
    .macro endconst
    .endm
    .data
    .align \align
    \name:
.endmacro

.macro loadaddr reg, name
#if CONFIG_DARWIN
    adrp \reg, \name\()@PAGE
    add \reg, \reg, #\name\()@PAGEOFF
#elif !defined(PIC)
    ldr \reg, =\name
#else
#error No support for PIC on this platform yet!
#endif
.endmacro

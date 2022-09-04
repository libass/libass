;******************************************************************************
;* be_blur.asm: SSE2 \be blur
;******************************************************************************
;* Copyright (C) 2013 rcombs <rcombs@rcombs.me>
;*
;* This file is part of libass.
;*
;* Permission to use, copy, modify, and distribute this software for any
;* purpose with or without fee is hereby granted, provided that the above
;* copyright notice and this permission notice appear in all copies.
;*
;* THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
;* WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
;* MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
;* ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
;* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
;* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
;* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;******************************************************************************

%include "x86/utils.asm"

SECTION .text

;------------------------------------------------------------------------------
; BE_BLUR
; void be_blur(uint8_t *buf, ptrdiff_t stride,
;              size_t width, size_t height, uint16_t *tmp);
;------------------------------------------------------------------------------

%macro BE_BLUR 0
cglobal be_blur, 5,7,8
    lea r0, [r0 + r2]
    lea r4, [r4 + 4 * r2]
    mov r6, r0
    neg r2
    mov r5, r2
    imul r3, r1
    add r3, r0
    pxor m6, m6

    mova m3, [r0 + r5]
%if mmsize == 32
    vpermq m3, m3, q3120
%endif
    punpcklbw m4, m3, m6
%if mmsize == 32
    vperm2i128 m0, m6, m4, 0x21
    vpalignr m5,m4,m0, 14
%else
    pslldq m5, m4, 2
%endif
    paddw m5, m4
    punpckhbw m0, m3, m6
    jmp .first_loop_entry

.first_width_loop:
    mova m3, [r0 + r5]
%if mmsize == 32
    vpermq m3, m3, q3120
%endif
    punpcklbw m4, m3, m6
%if mmsize == 32
    vperm2i128 m0, m0, m4, 0x21
%endif
    PALIGNR m5,m4,m0, m0, 14
    paddw m5, m4
    punpckhbw m0, m3, m6
%if mmsize == 32
    vperm2i128 m7, m5, m1, 0x03
    vpalignr m3, m7, m1, 2
%else
    PALIGNR m3,m5,m1, m7, 2
%endif
    paddw m3, m1

    mova [r4 + 4 * r5 - 2 * mmsize], m3
    mova [r4 + 4 * r5 - mmsize], m3

.first_loop_entry:
%if mmsize == 32
    vperm2i128 m4, m4, m0, 0x21
%endif
    PALIGNR m1,m0,m4, m4, 14
    paddw m1, m0
%if mmsize == 32
    vperm2i128 m7, m1, m5, 0x03
    vpalignr m3, m7, m5, 2
%else
    PALIGNR m3,m1,m5, m7, 2
%endif
    paddw m3, m5

    mova [r4 + 4 * r5], m3
    mova [r4 + 4 * r5 + mmsize], m3

    add r5, mmsize
    jnc .first_width_loop

    psrldq m0, 14
%if mmsize == 32
    vperm2i128 m7, m0, m1, 0x13
    vpalignr m3, m7, m1, 2
%else
    PALIGNR m3,m0,m1, m7, 2
%endif
    paddw m3, m1

    mova [r4 + 4 * r5 - 2 * mmsize], m3
    mova [r4 + 4 * r5 - mmsize], m3

    add r0, r1
    cmp r0, r3
    jge .last_row

.height_loop:
    mov r5, r2
    mova m3, [r0 + r5]
%if mmsize == 32
    vpermq m3, m3, q3120
%endif
    punpcklbw m4, m3, m6
%if mmsize == 32
    vperm2i128 m0, m6, m4, 0x21
    vpalignr m5,m4,m0, 14
%else
    pslldq m5, m4, 2
%endif
    paddw m5, m4
    punpckhbw m0, m3, m6
    jmp .loop_entry

.width_loop:
    mova m3, [r0 + r5]
%if mmsize == 32
    vpermq m3, m3, q3120
%endif
    punpcklbw m4, m3, m6
%if mmsize == 32
    vperm2i128 m0, m0, m4, 0x21
%endif
    PALIGNR m5,m4,m0, m0, 14
    paddw m5, m4
    punpckhbw m0, m3, m6
%if mmsize == 32
    vperm2i128 m7, m5, m1, 0x03
    vpalignr m3, m7, m1, 2
%else
    PALIGNR m3,m5,m1, m7, 2
%endif
    paddw m3, m1

    paddw m1, m3, [r4 + 4 * r5 - 2 * mmsize]
    mova [r4 + 4 * r5 - 2 * mmsize], m3
    paddw m3, m1, [r4 + 4 * r5 - mmsize]
    mova [r4 + 4 * r5 - mmsize], m1
    psrlw m3, 4
    packuswb m2, m3
%if mmsize == 32
    vpermq m2, m2, q3120
%endif
    mova [r6 + r5 - mmsize], m2

.loop_entry:
%if mmsize == 32
    vperm2i128 m4, m4, m0, 0x21
%endif
    PALIGNR m1,m0,m4, m4, 14
    paddw m1, m0
%if mmsize == 32
    vperm2i128 m7, m1, m5, 0x03
    vpalignr m3, m7, m5, 2
%else
    PALIGNR m3,m1,m5, m7, 2
%endif
    paddw m3, m5

    paddw m4, m3, [r4 + 4 * r5]
    mova [r4 + 4 * r5], m3
    paddw m2, m4, [r4 + 4 * r5 + mmsize]
    mova [r4 + 4 * r5 + mmsize], m4
    psrlw m2, 4

    add r5, mmsize
    jnc .width_loop

    psrldq m0, 14
%if mmsize == 32
    vperm2i128 m7, m0, m1, 0x13
    vpalignr m3, m7, m1, 2
%else
    PALIGNR m3,m0,m1, m7, 2
%endif
    paddw m3, m1

    paddw m1, m3, [r4 + 4 * r5 - 2 * mmsize]
    mova [r4 + 4 * r5 - 2 * mmsize], m3
    paddw m3, m1, [r4 + 4 * r5 - mmsize]
    mova [r4 + 4 * r5 - mmsize], m1
    psrlw m3, 4
    packuswb m2, m3
%if mmsize == 32
    vpermq m2, m2, q3120
%endif
    mova [r6 + r5 - mmsize], m2

    add r0, r1
    add r6, r1
    cmp r0, r3
    jl .height_loop

.last_row:
    mov r5, r2
.last_width_loop:
    mova m2, [r4 + 4 * r5]
    paddw m2, [r4 + 4 * r5 + mmsize]
    psrlw m2, 4
    mova m3, [r4 + 4 * r5 + 2 * mmsize]
    paddw m3, [r4 + 4 * r5 + 3 * mmsize]
    psrlw m3, 4
    packuswb m2, m3
%if mmsize == 32
    vpermq m2, m2, q3120
%endif
    mova [r6 + r5], m2
    add r5, mmsize
    jnc .last_width_loop
    RET
%endmacro

INIT_XMM sse2
BE_BLUR
INIT_XMM ssse3
BE_BLUR
INIT_YMM avx2
BE_BLUR

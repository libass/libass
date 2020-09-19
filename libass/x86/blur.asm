;******************************************************************************
;* blur.asm: SSE2/AVX2 cascade blur
;******************************************************************************
;* Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
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

SECTION_RODATA 32

words_zero: times 16 dw 0
words_one: times 16 dw 1
words_15_6: times 8 dw 15, 6
words_dither0: times 8 dw  8, 40
words_dither1: times 8 dw 56, 24
words_sign: times 16 dw 0x8000

dwords_two: times 8 dd 2
dwords_32: times 8 dd 32
dwords_round: times 8 dd 0x8000
dwords_lomask: times 8 dd 0xFFFF

SECTION .text

;------------------------------------------------------------------------------
; STRIPE_UNPACK
; void stripe_unpack(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
;                    uintptr_t width, uintptr_t height);
;------------------------------------------------------------------------------

%macro STRIPE_UNPACK 0
cglobal stripe_unpack, 5,6,3
    lea r3, [2 * r3 + mmsize - 1]
    and r3, ~(mmsize - 1)
    mov r5, r3
    imul r3, r4
    shr r5, 1
    MUL r4, mmsize
    and r5, ~(mmsize - 1)
    sub r3, r4
    sub r2, r5
    xor r5, r5
    mova m2, [words_one]
    jmp .row_loop

.col_loop:
    mova m1, [r1]
%if mmsize == 32
    vpermq m1, m1, q3120
%endif
    punpcklbw m0, m1, m1
    punpckhbw m1, m1
    psrlw m0, 1
    psrlw m1, 1
    paddw m0, m2
    paddw m1, m2
    psrlw m0, 1
    psrlw m1, 1
    mova [r0 + r5], m0
    add r5, r4
    mova [r0 + r5], m1
    add r5, r4
    add r1, mmsize
.row_loop:
    cmp r5, r3
    jl .col_loop
    sub r5, r4
    cmp r5, r3
    jge .skip_odd

    add r5, r4
    mova m0, [r1]
%if mmsize == 32
    vpermq m0, m0, q3120
%endif
    punpcklbw m0, m0
    psrlw m0, 1
    paddw m0, m2
    psrlw m0, 1
    mova [r0 + r5], m0

.skip_odd:
    add r5, mmsize
    sub r5, r3
    add r1, r2
    cmp r5, r4
    jb .row_loop
    RET
%endmacro

INIT_XMM sse2
STRIPE_UNPACK
INIT_YMM avx2
STRIPE_UNPACK

;------------------------------------------------------------------------------
; STRIPE_PACK
; void stripe_pack(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
;                  uintptr_t width, uintptr_t height);
;------------------------------------------------------------------------------

%macro STRIPE_PACK 0
cglobal stripe_pack, 5,7,5
    lea r3, [2 * r3 + mmsize - 1]
    mov r6, r1
    and r3, ~(mmsize - 1)
    mov r5, mmsize
    imul r3, r4
    imul r6, r4
    add r3, r2
    MUL r4, mmsize
    sub r5, r6
    jmp .row_loop

.col_loop:
    mova m0, [r2]
    mova m2, m0
    psrlw m2, 8
    psubw m0, m2
    mova m1, [r2 + r4]
    mova m2, m1
    psrlw m2, 8
    psubw m1, m2
    paddw m0, m3
    paddw m1, m3
    psrlw m0, 6
    psrlw m1, 6
    packuswb m0, m1
%if mmsize == 32
    vpermq m0, m0, q3120
%endif
    mova [r0], m0
    mova m2, m3
    mova m3, m4
    mova m4, m2
    add r2, mmsize
    add r0, r1
    cmp r2, r6
    jb .col_loop
    add r0, r5
    add r2, r4
.row_loop:
    mova m3, [words_dither0]
    mova m4, [words_dither1]
    lea r6, [r2 + r4]
    cmp r6, r3
    jb .col_loop
    cmp r2, r3
    jb .odd_stripe
    RET

.odd_stripe:
    mova m0, [r2]
    mova m2, m0
    psrlw m2, 8
    psubw m0, m2
    pxor m1, m1
    paddw m0, m3
    psrlw m0, 6
    packuswb m0, m1
%if mmsize == 32
    vpermq m0, m0, q3120
%endif
    mova [r0], m0
    mova m2, m3
    mova m3, m4
    mova m4, m2
    add r2, mmsize
    add r0, r1
    cmp r2, r6
    jb .odd_stripe
    RET
%endmacro

INIT_XMM sse2
STRIPE_PACK
INIT_YMM avx2
STRIPE_PACK

;------------------------------------------------------------------------------
; LOAD_LINE 1:m_dst, 2:base, 3:max, 4:zero_offs,
;           5:offs(lea arg), 6:tmp, [7:left/right]
; LOAD_LINE_COMPACT 1:m_dst, 2:base, 3:max,
;                   4:offs(register), 5:tmp, [6:left/right]
; Load xmm/ymm register with correct source bitmap data
;------------------------------------------------------------------------------

%macro LOAD_LINE 6-7
    lea %6, [%5]
    cmp %6, %3
    cmovae %6, %4
%if mmsize != 32 || %0 < 7
    mova m%1, [%2 + %6]
%elifidn %7, left
    mova xm%1, [%2 + %6]
%elifidn %7, right
    mova xm%1, [%2 + %6 + 16]
%else
    %error "left/right expected"
%endif
%endmacro

%macro LOAD_LINE_COMPACT 5-6
    lea %5, [words_zero]
    sub %5, %2
    cmp %4, %3
    cmovb %5, %4
%if mmsize != 32 || %0 < 6
    mova m%1, [%2 + %5]
%elifidn %6, left
    mova xm%1, [%2 + %5]
%elifidn %6, right
    mova xm%1, [%2 + %5 + 16]
%else
    %error "left/right expected"
%endif
%endmacro

;------------------------------------------------------------------------------
; SHRINK_HORZ
; void shrink_horz(int16_t *dst, const int16_t *src,
;                  uintptr_t src_width, uintptr_t src_height);
;------------------------------------------------------------------------------

%macro SHRINK_HORZ 0
%if ARCH_X86_64
cglobal shrink_horz, 4,9,9
    DECLARE_REG_TMP 8
%else
cglobal shrink_horz, 4,7,8
    DECLARE_REG_TMP 6
%endif
    lea t0, [r2 + mmsize + 3]
    lea r2, [2 * r2 + mmsize - 1]
    and t0, ~(mmsize - 1)
    and r2, ~(mmsize - 1)
    imul t0, r3
    imul r2, r3
    add t0, r0
    xor r4, r4
    MUL r3, mmsize
    sub r4, r3
    mova m7, [dwords_lomask]
%if ARCH_X86_64
    mova m8, [dwords_two]
    lea r7, [words_zero]
    sub r7, r1
%else
    PUSH t0
%endif

    lea r5, [r0 + r3]
.main_loop:
%if ARCH_X86_64
    LOAD_LINE 0, r1,r2,r7, r4 + 0 * r3, r6, right
    LOAD_LINE 1, r1,r2,r7, r4 + 1 * r3, r6
    LOAD_LINE 2, r1,r2,r7, r4 + 2 * r3, r6
%else
    LOAD_LINE_COMPACT 0, r1,r2,r4, r6, right
    add r4, r3
    LOAD_LINE_COMPACT 1, r1,r2,r4, r6
    add r4, r3
    LOAD_LINE_COMPACT 2, r1,r2,r4, r6
    sub r4, r3
    sub r4, r3
%endif

%if mmsize == 32
    vperm2i128 m3, m0, m1, 0x20
    vperm2i128 m4, m1, m2, 0x21
%else
    mova m3, m0
    mova m4, m1
%endif
    PALIGNR m3,m1,m3, m6, 10
    PALIGNR m4,m2,m4, m6, 10
    paddw m3, m1
    paddw m4, m2
    pand m3, m7
    pand m4, m7

    psrld xm6, xm0, 16
    paddw xm0, xm6
    psrld m6, m1, 16
    paddw m1, m6
    psrld m6, m2, 16
    paddw m2, m6
    pand xm0, xm7
    pand m1, m7
    pand m2, m7

%if mmsize == 32
    vperm2i128 m0, m0, m1, 0x20
%endif
    PALIGNR m5,m1,m0, m6, 8
    paddd m5, m1
    psrld m5, 1
    PALIGNR m0,m1,m0, m6, 12
    paddd m5, m0
    psrld m5, 1
    paddd m5, m3
    psrld m5, 1
    paddd m0, m5

%if mmsize == 32
    vperm2i128 m1, m1, m2, 0x21
%endif
    PALIGNR m5,m2,m1, m6, 8
    paddd m5, m2
    psrld m5, 1
    PALIGNR m1,m2,m1, m6, 12
    paddd m5, m1
    psrld m5, 1
    paddd m5, m4
    psrld m5, 1
    paddd m1, m5

%if ARCH_X86_64
    paddd m0, m8
    paddd m1, m8
%else
    mova m6, [dwords_two]
    paddd m0, m6
    paddd m1, m6
%endif
    psrld m0, 2
    psrld m1, 2
    packssdw m0, m1
%if mmsize == 32
    vpermq m0, m0, q3120
%endif

    mova [r0], m0
    add r0, mmsize
    add r4, mmsize
    cmp r0, r5
    jb .main_loop
    add r4, r3
    add r5, r3
%if ARCH_X86_64
    cmp r0, t0
%else
    cmp r0, [rstk]
%endif
    jb .main_loop
%if ARCH_X86_64 == 0
    ADD rstk, 4
%endif
    RET
%endmacro

INIT_XMM sse2
SHRINK_HORZ
INIT_YMM avx2
SHRINK_HORZ

;------------------------------------------------------------------------------
; SHRINK_VERT
; void shrink_vert(int16_t *dst, const int16_t *src,
;                  uintptr_t src_width, uintptr_t src_height);
;------------------------------------------------------------------------------

%macro SHRINK_VERT 0
%if ARCH_X86_64
cglobal shrink_vert, 4,7,9
%else
cglobal shrink_vert, 4,7,8
%endif
    lea r2, [2 * r2 + mmsize - 1]
    lea r5, [r3 + 5]
    and r2, ~(mmsize - 1)
    shr r5, 1
    imul r2, r5
    MUL r3, mmsize
    add r2, r0
    mova m7, [words_one]
%if ARCH_X86_64
    mova m8, [words_sign]
%endif
    lea r6, [words_zero]
    sub r6, r1

.col_loop:
    mov r4, -4 * mmsize
    pxor m0, m0
    pxor m1, m1
    pxor m2, m2
    pxor m3, m3
.row_loop:
    LOAD_LINE 4, r1,r3,r6, r4 + 4 * mmsize, r5
    LOAD_LINE 5, r1,r3,r6, r4 + 5 * mmsize, r5

%if ARCH_X86_64
    mova m6, m8
%else
    psllw m6, m7, 15
%endif
    paddw m1, m4
    paddw m4, m5
    pand m6, m0
    pand m6, m4
    paddw m0, m4
    psrlw m0, 1
    por m0, m6
    pand m6, m2
    paddw m0, m2
    psrlw m0, 1
    por m0, m6
    pand m6, m1
    paddw m0, m1
    psrlw m0, 1
    por m0, m6
    paddw m0, m2
    psrlw m0, 1
    por m0, m6
    paddw m0, m7
    psrlw m0, 1

    mova [r0], m0
    add r4, 2 * mmsize
    add r0, mmsize
    mova m0, m2
    mova m1, m3
    mova m2, m4
    mova m3, m5
    cmp r4, r3
    jl .row_loop
    add r1, r3
    sub r6, r3
    cmp r0, r2
    jb .col_loop
    RET
%endmacro

INIT_XMM sse2
SHRINK_VERT
INIT_YMM avx2
SHRINK_VERT

;------------------------------------------------------------------------------
; EXPAND_HORZ
; void expand_horz(int16_t *dst, const int16_t *src,
;                  uintptr_t src_width, uintptr_t src_height);
;------------------------------------------------------------------------------

%macro EXPAND_HORZ 0
%if ARCH_X86_64
cglobal expand_horz, 4,9,5
    DECLARE_REG_TMP 8
%else
cglobal expand_horz, 4,7,5
    DECLARE_REG_TMP 6
%endif
    lea t0, [4 * r2 + 7]
    lea r2, [2 * r2 + mmsize - 1]
    and t0, ~(mmsize - 1)
    and r2, ~(mmsize - 1)
    imul t0, r3
    imul r2, r3
    add t0, r0
    xor r4, r4
    MUL r3, mmsize
    sub r4, r3
    mova m4, [words_one]
%if ARCH_X86_64
    lea r7, [words_zero]
    sub r7, r1
%endif

    lea r5, [r0 + r3]
    cmp r0, t0
    jae .odd_stripe
%if ARCH_X86_64 == 0
    PUSH t0
%endif
.main_loop:
%if ARCH_X86_64
    LOAD_LINE 2, r1,r2,r7, r4 + 0 * r3, r6, right
    LOAD_LINE 1, r1,r2,r7, r4 + 1 * r3, r6
%else
    LOAD_LINE_COMPACT 2, r1,r2,r4, r6, right
    add r4, r3
    LOAD_LINE_COMPACT 1, r1,r2,r4, r6
    sub r4, r3
%endif

%if mmsize == 32
    vperm2i128 m2, m2, m1, 0x20
%endif
    PALIGNR m0,m1,m2, m3, 12
    PALIGNR m2,m1,m2, m3, 14
    paddw m3, m0, m1
    psrlw m3, 1
    paddw m3, m2
    psrlw m3, 1
    paddw m0, m3
    paddw m1, m3
    psrlw m0, 1
    psrlw m1, 1
    paddw m0, m2
    paddw m1, m2
    paddw m0, m4
    paddw m1, m4
    psrlw m0, 1
    psrlw m1, 1

%if mmsize == 32
    vpermq m0, m0, q3120
    vpermq m1, m1, q3120
%endif
    punpcklwd m2, m0, m1
    punpckhwd m0, m1
    mova [r0], m2
    mova [r0 + r3], m0
    add r0, mmsize
    add r4, mmsize
    cmp r0, r5
    jb .main_loop
    add r0, r3
    lea r5, [r0 + r3]
%if ARCH_X86_64 == 0
    mov t0, [rstk]
%endif
    cmp r0, t0
    jb .main_loop
    add t0, r3
%if ARCH_X86_64 == 0
    ADD rstk, 4
%endif
    cmp r0, t0
    jb .odd_stripe
    RET

.odd_stripe:
%if ARCH_X86_64
    LOAD_LINE 2, r1,r2,r7, r4 + 0 * r3, r6, right
    LOAD_LINE 1, r1,r2,r7, r4 + 1 * r3, r6, left
%else
    LOAD_LINE_COMPACT 2, r1,r2,r4, r6, right
    add r4, r3
    LOAD_LINE_COMPACT 1, r1,r2,r4, r6, left
    sub r4, r3
%endif

    PALIGNR xm0,xm1,xm2, xm3, 12
    PALIGNR xm2,xm1,xm2, xm3, 14
    paddw xm3, xm0, xm1
    psrlw xm3, 1
    paddw xm3, xm2
    psrlw xm3, 1
    paddw xm0, xm3
    paddw xm1, xm3
    psrlw xm0, 1
    psrlw xm1, 1
    paddw xm0, xm2
    paddw xm1, xm2
    paddw xm0, xm4
    paddw xm1, xm4
    psrlw xm0, 1
    psrlw xm1, 1

%if mmsize == 32
    vpermq m0, m0, q3120
    vpermq m1, m1, q3120
%endif
    punpcklwd m0, m1
    mova [r0], m0
    add r0, mmsize
    add r4, mmsize
    cmp r0, r5
    jb .odd_stripe
    RET
%endmacro

INIT_XMM sse2
EXPAND_HORZ
INIT_YMM avx2
EXPAND_HORZ

;------------------------------------------------------------------------------
; EXPAND_VERT
; void expand_vert(int16_t *dst, const int16_t *src,
;                  uintptr_t src_width, uintptr_t src_height);
;------------------------------------------------------------------------------

%macro EXPAND_VERT 0
cglobal expand_vert, 4,7,5
    lea r2, [2 * r2 + mmsize - 1]
    lea r5, [2 * r3 + 4]
    and r2, ~(mmsize - 1)
    imul r2, r5
    MUL r3, mmsize
    add r2, r0
    mova m4, [words_one]
    lea r6, [words_zero]
    sub r6, r1

.col_loop:
    mov r4, -2 * mmsize
    pxor m0, m0
    pxor m1, m1
.row_loop:
    LOAD_LINE 2, r1,r3,r6, r4 + 2 * mmsize, r5

    paddw m3, m0, m2
    psrlw m3, 1
    paddw m3, m1
    psrlw m3, 1
    paddw m0, m3
    paddw m3, m2
    psrlw m0, 1
    psrlw m3, 1
    paddw m0, m1
    paddw m3, m1
    paddw m0, m4
    paddw m3, m4
    psrlw m0, 1
    psrlw m3, 1

    mova [r0], m0
    mova [r0 + mmsize], m3
    add r4, mmsize
    add r0, 2 * mmsize
    mova m0, m1
    mova m1, m2
    cmp r4, r3
    jl .row_loop
    add r1, r3
    sub r6, r3
    cmp r0, r2
    jb .col_loop
    RET
%endmacro

INIT_XMM sse2
EXPAND_VERT
INIT_YMM avx2
EXPAND_VERT

;------------------------------------------------------------------------------
; LOAD_MULTIPLIER 1:n, 2:m_mul, 3:src, 4:tmp
; Load blur parameters into xmm/ymm registers
;------------------------------------------------------------------------------

%macro LOAD_MULTIPLIER 4
%if ARCH_X86_64
    %assign %%t %2 + (%1 - 1) / 2
%else
    %assign %%t %2
%endif
    movu xm %+ %%t, [%3]
%if %1 % 2
    pextrw %4d, xm %+ %%t, 0
    pslldq xm %+ %%t, 2
    pinsrw xm %+ %%t, %4d, 0
%endif
%if mmsize == 32
    vpermq m %+ %%t, m %+ %%t, q1010
%endif
%if ARCH_X86_64
    %assign %%i 0
%rep (%1 + 1) / 2
    %assign %%c %2 + %%i
    pshufd m %+ %%c, m %+ %%t, q1111 * %%i
    %assign %%i %%i + 1
%endrep
%endif
%endmacro

;------------------------------------------------------------------------------
; FILTER_PAIR 1-2:m_acc[2], 3-4:m_line[2], 5:m_tmp, 6:m_mul, 7:pos
; Calculate acc += line[0] * mul[odd] + line[1] * mul[even]
;------------------------------------------------------------------------------

%macro FILTER_PAIR 7
    punpcklwd m%5, m%4, m%3
    punpckhwd m%4, m%3
    %assign %%p ((%7) - 1) / 2
%if ARCH_X86_64
    %assign %%p %6 + %%p
%else
    pshufd m%3, m%6, q1111 * %%p
    %assign %%p %3
%endif
    pmaddwd m%5, m %+ %%p
    pmaddwd m%4, m %+ %%p
%ifidn %1, %5
    paddd m%1, m%2
%else
    paddd m%1, m%5
%endif
    paddd m%2, m%4
%endmacro

;------------------------------------------------------------------------------
; NEXT_DIFF 1:m_res, 2:m_side, 3:m_center, 4:position, 5:left/right
; Calculate difference between next offset line and center line
;------------------------------------------------------------------------------

%macro NEXT_DIFF 5
%ifidn %5, left

%if cpuflag(ssse3)
    palignr m%1, m%3, m%2, 16 - (%4)
%else
    psrldq m%2, 2
    pslldq m%1, m%3, %4
    por m%1, m%2
%endif

%elifidn %5, right

%if cpuflag(ssse3)
    palignr m%1, m%2, m%3, %4
%else
    pslldq m%2, 2
    psrldq m%1, m%3, %4
    por m%1, m%2
%endif

%else
    %error "left/right expected"
%endif
    psubw m%1, m%3
%endmacro

;------------------------------------------------------------------------------
; BLUR_HORZ 1:radius
; void blurN_horz(int16_t *dst, const int16_t *src,
;                 uintptr_t src_width, uintptr_t src_height,
;                 const int16_t *param);
;------------------------------------------------------------------------------

%macro BLUR_HORZ 1
%if ARCH_X86_64
    %assign %%narg 9 + (%1 + 1) / 2
cglobal blur%1_horz, 5,8,%%narg
%else
cglobal blur%1_horz, 5,7,8
    SWAP 7, 9
%endif
    LOAD_MULTIPLIER %1, 9, r4, r5
    lea r5, [2 * r2 + mmsize + 4 * %1 - 1]
    lea r2, [2 * r2 + mmsize - 1]
    and r5, ~(mmsize - 1)
    and r2, ~(mmsize - 1)
    imul r5, r3
    imul r2, r3
    add r5, r0
    xor r4, r4
    MUL r3, mmsize
%if mmsize != 32 && %1 > 4
    sub r4, r3
%endif
    sub r4, r3
%if ARCH_X86_64
    mova m7, [dwords_round]
    lea r7, [words_zero]
    sub r7, r1
%endif

.main_loop:
%if ARCH_X86_64
%if %1 > 4
    LOAD_LINE 1, r1,r2,r7, r4 + 0 * r3, r6
%else
    LOAD_LINE 1, r1,r2,r7, r4 + 0 * r3, r6, right
%endif
    LOAD_LINE 2, r1,r2,r7, r4 + 1 * r3, r6
%if mmsize != 32 && %1 > 4
    LOAD_LINE 0, r1,r2,r7, r4 + 2 * r3, r6
    SWAP 0, 2
%endif
%else
%if %1 > 4
    LOAD_LINE_COMPACT 1, r1,r2,r4, r6
%else
    LOAD_LINE_COMPACT 1, r1,r2,r4, r6, right
%endif
    add r4, r3
    LOAD_LINE_COMPACT 2, r1,r2,r4, r6
%if mmsize != 32 && %1 > 4
    add r4, r3
    LOAD_LINE_COMPACT 0, r1,r2,r4, r6
    SWAP 0, 2
    sub r4, r3
%endif
    sub r4, r3
%endif

%if %1 > 4
%if mmsize == 32
    vperm2i128 m0, m1, m2, 0x21
%endif
%if cpuflag(ssse3)
    PALIGNR m1,m0,m1, m3, 16 - 2 * %1
%else
    PALIGNR m1,m0,m1, m3, 32 - 4 * %1
%endif
    PALIGNR m0,m2,m0, m3, 16 - 2 * %1
%else
%if mmsize == 32
    vperm2i128 m1, m1, m2, 0x20
%endif
%if cpuflag(ssse3)
    palignr m0, m2, m1, 8
    pslldq m1, 8
%else
    shufpd m0, m1, m2, 5
%endif
%endif

%if ARCH_X86_64
    mova m6, m7
%else
    mova m6, [dwords_round]
    mova [r0], m1
    SWAP 1, 8
%endif

    %assign %%i %1
    psubw m3, m2, m0
%if cpuflag(ssse3) && %1 < 8
    psrldq m2, 16 - 2 * %1
%endif
    NEXT_DIFF 4,2,0, 2 * %%i - 2, right
    FILTER_PAIR 5,6, 3,4, 5, 9,%%i
%rep %1 / 2 - 1
    %assign %%i %%i - 2
    NEXT_DIFF 3,2,0, 2 * %%i, right
    NEXT_DIFF 4,2,0, 2 * %%i - 2, right
    FILTER_PAIR 5,6, 3,4, 8, 9,%%i
%endrep

%if ARCH_X86_64 == 0
    SWAP 1, 8
    mova m1, [r0]
%if %1 % 2
    mova [r0], m2
%endif
    SWAP 2, 8
%endif

    %assign %%i %1
%if cpuflag(ssse3) && %1 < 8
    NEXT_DIFF 3,1,0, 2 * %%i, left
%else
    psubw m3, m1, m0
%endif
    NEXT_DIFF 4,1,0, 2 * %%i - 2, left
    FILTER_PAIR 5,6, 3,4, 8, 9,%%i
%rep %1 / 2 - 1
    %assign %%i %%i - 2
    NEXT_DIFF 3,1,0, 2 * %%i, left
    NEXT_DIFF 4,1,0, 2 * %%i - 2, left
    FILTER_PAIR 5,6, 3,4, 8, 9,%%i
%endrep

%if %%i > 2
    %assign %%i %%i - 2
%if ARCH_X86_64 == 0
    SWAP 2, 8
    mova m2, [r0]
%endif
    NEXT_DIFF 3,1,0, 2 * %%i, left
    NEXT_DIFF 4,2,0, 2 * %%i, right
    FILTER_PAIR 5,6, 3,4, 1, 9,%%i
%endif

    psrad m5, 16
    psrad m6, 16
    packssdw m5, m6
    paddw m0, m5
    mova [r0], m0
    add r0, mmsize
    add r4, mmsize
    cmp r0, r5
    jb .main_loop
    RET
%endmacro

INIT_XMM sse2
BLUR_HORZ 4
BLUR_HORZ 5
BLUR_HORZ 6
BLUR_HORZ 7
BLUR_HORZ 8
INIT_YMM avx2
BLUR_HORZ 4
BLUR_HORZ 5
BLUR_HORZ 6
BLUR_HORZ 7
BLUR_HORZ 8

;------------------------------------------------------------------------------
; BLUR_VERT 1:radius
; void blurN_vert(int16_t *dst, const int16_t *src,
;                 uintptr_t src_width, uintptr_t src_height,
;                 const int16_t *param);
;------------------------------------------------------------------------------

%macro BLUR_VERT 1
%if ARCH_X86_64
    %assign %%narg 7 + (%1 + 1) / 2
cglobal blur%1_vert, 5,7,%%narg
%else
cglobal blur%1_vert, 5,7,8
%endif
    LOAD_MULTIPLIER %1, 7, r4, r5
    lea r2, [2 * r2 + mmsize - 1]
    lea r5, [r3 + 2 * %1]
    and r2, ~(mmsize - 1)
    imul r2, r5
    MUL r3, mmsize
    add r2, r0
    mova m4, [dwords_round]
    lea r6, [words_zero]
    sub r6, r1

.col_loop:
    mov r4, -2 * %1 * mmsize
.row_loop:
    mova m5, m4
    mova m6, m4
    LOAD_LINE 0, r1,r3,r6, r4 + %1 * mmsize, r5

    %assign %%i %1
%rep %1 / 2

    LOAD_LINE 1, r1,r3,r6, r4 + (%1 - %%i) * mmsize, r5
    LOAD_LINE 2, r1,r3,r6, r4 + (%1 - %%i + 1) * mmsize, r5
    psubw m1, m0
    psubw m2, m0
    FILTER_PAIR 5,6, 1,2, 3, 7,%%i

    LOAD_LINE 1, r1,r3,r6, r4 + (%1 + %%i) * mmsize, r5
    LOAD_LINE 2, r1,r3,r6, r4 + (%1 + %%i - 1) * mmsize, r5
    psubw m1, m0
    psubw m2, m0
    FILTER_PAIR 5,6, 1,2, 3, 7,%%i

    %assign %%i %%i - 2
%endrep

%if %%i > 0
    LOAD_LINE 1, r1,r3,r6, r4 + (%1 - %%i) * mmsize, r5
    LOAD_LINE 2, r1,r3,r6, r4 + (%1 + %%i) * mmsize, r5
    psubw m1, m0
    psubw m2, m0
    FILTER_PAIR 5,6, 1,2, 3, 7,%%i
%endif

    psrad m5, 16
    psrad m6, 16
    packssdw m5, m6
    paddw m0, m5
    mova [r0], m0
    add r4, mmsize
    add r0, mmsize
    cmp r4, r3
    jl .row_loop
    add r1, r3
    sub r6, r3
    cmp r0, r2
    jb .col_loop
    RET
%endmacro

INIT_XMM sse2
BLUR_VERT 4
BLUR_VERT 5
BLUR_VERT 6
BLUR_VERT 7
BLUR_VERT 8
INIT_YMM avx2
BLUR_VERT 4
BLUR_VERT 5
BLUR_VERT 6
BLUR_VERT 7
BLUR_VERT 8

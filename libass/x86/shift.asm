;******************************************************************************
;* shift.asm: SSE2/AVX2 subpixel bitmap shift
;******************************************************************************
;* Copyright (C) 2026 libass contributors
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
; SHIFT_X 1:m_dst, 2:m_src1, 3:m_src2,
;         4:m_tmp, 5:m_mul(64), 6:r_mul(32), 6:mode
; Perform subpixel shift along X axis
;------------------------------------------------------------------------------

%macro SHIFT_X 7
%ifidn %7, even
    %assign %%mode 0
%elifidn %7, odd
    %assign %%mode 1
%elifidn %7, last
    %assign %%mode 2
%else
    %error "even/odd/last expected"
%endif

%if %%mode == 2 && mmsize != 32
    psrldq m%1, m%2, 2
%else
    PALIGNR m%1,m%3,m%2, m%4, 2
%endif
%if %%mode == 1 && mmsize == 32
    mova m%3, m%4
%endif
    psubw m%1, m%2
%if ARCH_X86_64
    pmullw m%1, m%5
%else
    BCASTD %4, %6d
    pmullw m%1, m%4
%endif
%if %%mode == 0 && mmsize == 32
    psllw m%4, m%2, 6
    paddw m%1, m%4
%else
    psllw m%2, 6
    paddw m%1, m%2
%endif
%endmacro

;------------------------------------------------------------------------------
; SHIFT_Y 1:m_dst/m_src1, 2:m_src2,
;         3:m_tmp, 4:m_mul(64), 5:r_mul(32), 6:m_w32(64), 7:mode
; Perform subpixel shift along Y axis
;------------------------------------------------------------------------------

%macro SHIFT_Y 6-7
%ifidn %7, last
    paddw m%3, m%1, m%1
%else
    psubw m%3, m%1, m%2
    paddw m%3, m%3
%endif
%if ARCH_X86_64
    pmulhw m%3, m%4
    psubw m%1, m%3
    paddw m%1, m%6
%else
    BCASTD %2, %5d
    pmulhw m%3, m%2
    psubw m%1, m%3
    pcmpeqw m%2, m%2
    psllw m%2, 5
    psubw m%1, m%2
%endif
    psrlw m%1, 6
%endmacro

;------------------------------------------------------------------------------
; SHIFT
; void ass_shift_c(uint8_t *buf, ptrdiff_t stride,
;                  size_t width, size_t height, int shift_x, int shift_y);
;------------------------------------------------------------------------------

%macro SHIFT 0
%if ARCH_X86_64
cglobal shift, 6,6,13
    DECLARE_REG_TMP 5
%else
cglobal shift, 6,7,8
    DECLARE_REG_TMP 6
    sub r3, 1
%endif

    lea r0, [r0 + r2]
    neg r2
    imul r3, r1
    add r3, r0

%if ARCH_X86_64
    pxor m9, m9
    BCASTW 10, r4d
    shl r5d, 9
    BCASTW 11, r5d
    mov r4d, 32
    BCASTW 12, r4d
    lea r4, [r0 + r1]
%else
    imul r4d, 0x10001
    imul r5d, 0x10001 << 9
%endif

.height_loop:
    mov t0, r2
    mova m0, [r0 + t0]
%if ARCH_X86_64
    punpcklbw m2, m0, m9
    punpckhbw m4, m0, m9
    mova m0, [r4 + t0]
    punpcklbw m3, m0, m9
    punpckhbw m5, m0, m9
%else
    pxor m7, m7
    add r0, r1
    punpcklbw m2, m0, m7
    punpckhbw m4, m0, m7
    mova m0, [r0 + t0]
    sub r0, r1
    punpcklbw m3, m0, m7
    punpckhbw m5, m0, m7
%endif
    jmp .loop_entry

.width_loop:
    mova m0, [r0 + t0]
%if ARCH_X86_64

%if mmsize == 32
    punpcklbw m7, m0, m9
    vperm2i128 m2, m2, m7, 0x21
%else
    punpcklbw m2, m0, m9
%endif
    SHIFT_X 6,4,2, 7,10,r4, odd
    punpckhbw m4, m0, m9

    mova m0, [r4 + t0]
%if mmsize == 32
    punpcklbw m8, m0, m9
    vperm2i128 m3, m3, m8, 0x21
%else
    punpcklbw m3, m0, m9
%endif
    SHIFT_X 7,5,3, 8,10,r4, odd
    punpckhbw m5, m0, m9

%else

    add r0, r1
    pxor m6, m6
%if mmsize == 32
    punpcklbw m7, m0, m6
    vperm2i128 m2, m2, m7, 0x21
%else
    punpcklbw m2, m0, m6
%endif
    SHIFT_X 6,4,2, 7,10,r4, odd

    mova m7, [r0 + t0]
    sub r0, r1
    punpckhqdq m4, m0, m7
    pxor m0, m0
%if mmsize == 32
    punpcklbw m0, m7, m0
    vperm2i128 m3, m3, m0, 0x21
%else
    punpcklbw m3, m7, m0
%endif
    SHIFT_X 7,5,3, 0,10,r4, odd
    pxor m0, m0
    punpckhbw m5, m4, m0
    punpcklbw m4, m4, m0

%endif

    SHIFT_Y 6,7, 0,11,r5,12
    packuswb m1, m6
    mova [r0 + t0 - mmsize], m1

.loop_entry:
    SHIFT_X 1,2,4, 0,10,r4, even
    SHIFT_X 7,3,5, 0,10,r4, even
    SHIFT_Y 1,7, 0,11,r5,12

    add t0, mmsize
    jnc .width_loop

%if mmsize == 32
    vperm2i128 m2, m2, m2, 0x81
    vperm2i128 m3, m3, m2, 0x81
%endif
    SHIFT_X 6,4,2, 0,10,r4, last
    SHIFT_X 7,5,3, 0,10,r4, last
    SHIFT_Y 6,7, 0,11,r5,12
    packuswb m1, m6
    mova [r0 + t0 - mmsize], m1

%if ARCH_X86_64
    mov r0, r4
    add r4, r1
    cmp r4, r3
%else
    add r0, r1
    cmp r0, r3
%endif
    jl .height_loop

    mov t0, r2
    mova m0, [r0 + t0]
%if !ARCH_X86_64
    pxor m3, m3
    %define m9 m3
%endif
    punpcklbw m2, m0, m9
    punpckhbw m4, m0, m9
    jmp .last_loop_entry

.last_width_loop:
    mova m0, [r0 + t0]
%if mmsize == 32
    punpcklbw m7, m0, m9
    vperm2i128 m2, m2, m7, 0x21
%else
    punpcklbw m2, m0, m9
%endif
    SHIFT_X 6,4,2, 7,10,r4, odd
    punpckhbw m4, m0, m9

    SHIFT_Y 6,7, 0,11,r5,12, last
    packuswb m1, m6
    mova [r0 + t0 - mmsize], m1

.last_loop_entry:
    SHIFT_X 1,2,4, 0,10,r4, even
    SHIFT_Y 1,7, 0,11,r5,12, last

    add t0, mmsize
    jnc .last_width_loop

%if mmsize == 32
    vperm2i128 m2, m2, m2, 0x81
%endif
    SHIFT_X 6,4,2, 0,10,r4, last
    SHIFT_Y 6,7, 0,11,r5,12, last
    packuswb m1, m6
    mova [r0 + t0 - mmsize], m1
    RET
%endmacro

INIT_XMM sse2
SHIFT
INIT_XMM ssse3
SHIFT
INIT_YMM avx2
SHIFT

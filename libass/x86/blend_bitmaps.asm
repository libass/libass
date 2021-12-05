;******************************************************************************
;* blend_bitmaps.asm: SSE2 and AVX2 bitmap blending
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

SECTION_RODATA 32

%if ARCH_X86_64 || !PIC
times 32 db 0xFF
edge_mask: times 32 db 0x00
words_255: times 16 dw 0xFF
%endif

SECTION .text

;------------------------------------------------------------------------------
; LOAD_EDGE_MASK 1:m_dst, 2:n, 3:tmp
; Set n last bytes of xmm/ymm register to zero and other bytes to 255
;------------------------------------------------------------------------------

%macro LOAD_EDGE_MASK 3
%if !PIC
    movu m%1, [edge_mask + %2 - mmsize]
%elif ARCH_X86_64
    lea %3, [rel edge_mask]
    movu m%1, [%3 + %2 - mmsize]
%elif mmsize <= STACK_ALIGNMENT
    %assign %%pad -(stack_offset + gprsize) & (mmsize - 1)
    pxor m%1, m%1
    mova [rsp - %%pad - mmsize], m%1
    pcmpeqb m%1, m%1
    mova [rsp - %%pad - 2 * mmsize], m%1
    movu m%1, [rsp + %2 - %%pad - 2 * mmsize]
%else
    mov %3, rsp
    and %3, -mmsize
    pxor m%1, m%1
    mova [%3 - mmsize], m%1
    pcmpeqb m%1, m%1
    mova [%3 - 2 * mmsize], m%1
    movu m%1, [%3 + %2 - 2 * mmsize]
%endif
%endmacro

;------------------------------------------------------------------------------
; ADD_BITMAPS
; void add_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
;                  const uint8_t *src, ptrdiff_t src_stride,
;                  size_t width, size_t height);
;------------------------------------------------------------------------------

%macro ADD_BITMAPS 0
%if ARCH_X86_64
cglobal add_bitmaps, 6,8,3
    DECLARE_REG_TMP 7
%else
cglobal add_bitmaps, 5,7,3
    DECLARE_REG_TMP 5
%endif
    lea r0, [r0 + r4]
    lea r2, [r2 + r4]
    neg r4
    mov r6, r4
    and r4, mmsize - 1
    LOAD_EDGE_MASK 2, r4, t0
%if !ARCH_X86_64
    mov r5, r5m
%endif
    imul r5, r3
    add r5, r2
    mov r4, r6
    jmp .loop_entry

.width_loop:
    paddusb m0, m1
    movu [r0 + r4 - mmsize], m0
.loop_entry:
    movu m0, [r0 + r4]
    movu m1, [r2 + r4]
    add r4, mmsize
    jnc .width_loop
    pand m1, m2
    paddusb m0, m1
    movu [r0 + r4 - mmsize], m0
    add r0, r1
    add r2, r3
    mov r4, r6
    cmp r2, r5
    jl .loop_entry
    RET
%endmacro

INIT_XMM sse2
ADD_BITMAPS
INIT_YMM avx2
ADD_BITMAPS

;------------------------------------------------------------------------------
; IMUL_BITMAPS
; void imul_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
;                   const uint8_t *src, ptrdiff_t src_stride,
;                   size_t width, size_t height);
;------------------------------------------------------------------------------

%macro IMUL_BITMAPS 0
%if ARCH_X86_64
cglobal imul_bitmaps, 6,8,8
    DECLARE_REG_TMP 7
%else
cglobal imul_bitmaps, 5,7,8
    DECLARE_REG_TMP 5
%endif
    lea r0, [r0 + r4]
    lea r2, [r2 + r4]
    neg r4
    mov r6, r4
    and r4, mmsize - 1
    LOAD_EDGE_MASK 4, r4, t0
%if ARCH_X86_64 || !PIC
    mova m5, [words_255]
%else
    mov t0d, 255 * 0x10001
    BCASTD 5, t0d
%endif
    pxor m6, m6
    pcmpeqb m7, m7
%if !ARCH_X86_64
    mov r5, r5m
%endif
    imul r5, r3
    add r5, r2
    mov r4, r6
    jmp .loop_entry

.width_loop:
    pxor m1, m7
    punpckhbw m2, m0, m6
    punpckhbw m3, m1, m6
    punpcklbw m0, m6
    punpcklbw m1, m6
    pmullw m2, m3
    pmullw m0, m1
    paddw m2, m5
    paddw m0, m5
    psrlw m2, 8
    psrlw m0, 8
    packuswb m0, m2
    movu [r0 + r4 - mmsize], m0
.loop_entry:
    movu m0, [r0 + r4]
    movu m1, [r2 + r4]
    add r4, mmsize
    jnc .width_loop
    pand m1, m4
    pxor m1, m7
    punpckhbw m2, m0, m6
    punpckhbw m3, m1, m6
    punpcklbw m0, m6
    punpcklbw m1, m6
    pmullw m2, m3
    pmullw m0, m1
    paddw m2, m5
    paddw m0, m5
    psrlw m2, 8
    psrlw m0, 8
    packuswb m0, m2
    movu [r0 + r4 - mmsize], m0
    add r0, r1
    add r2, r3
    mov r4, r6
    cmp r2, r5
    jl .loop_entry
    RET
%endmacro

INIT_XMM sse2
IMUL_BITMAPS
INIT_YMM avx2
IMUL_BITMAPS

;------------------------------------------------------------------------------
; MUL_BITMAPS
; void mul_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
;                  const uint8_t *src1, ptrdiff_t src1_stride,
;                  const uint8_t *src2, ptrdiff_t src2_stride,
;                  size_t width, size_t height);
;------------------------------------------------------------------------------

%macro MUL_BITMAPS 0
%if ARCH_X86_64
cglobal mul_bitmaps, 7,9,7
    DECLARE_REG_TMP 8,7
%else
cglobal mul_bitmaps, 1,7,7
    DECLARE_REG_TMP 1,3
    mov r2, r2m
    mov r4, r4m
    mov r5, r5m
    mov r6, r6m
%endif
    lea r0, [r0 + r6]
    lea r2, [r2 + r6]
    lea r4, [r4 + r6]
    neg r6
    mov t0, r6
    and r6, mmsize - 1
    LOAD_EDGE_MASK 4, r6, t1
%if ARCH_X86_64 || !PIC
    mova m5, [words_255]
%else
    mov t1d, 255 * 0x10001
    BCASTD 5, t1d
%endif
    pxor m6, m6
    mov t1, r7m
    imul t1, r5
    add t1, r4
    mov r6, t0
    jmp .loop_entry

.width_loop:
    mova [r0 + r6 - mmsize], m0
.loop_entry:
    movu m0, [r2 + r6]
    movu m1, [r4 + r6]
    punpckhbw m2, m0, m6
    punpckhbw m3, m1, m6
    punpcklbw m0, m6
    punpcklbw m1, m6
    pmullw m2, m3
    pmullw m0, m1
    paddw m2, m5
    paddw m0, m5
    psrlw m2, 8
    psrlw m0, 8
    packuswb m0, m2
    add r6, mmsize
    jnc .width_loop
    pand m0, m4
    mova [r0 + r6 - mmsize], m0
%if ARCH_X86_64
    add r0, r1
    add r2, r3
%else
    add r0, r1m
    add r2, r3m
%endif
    add r4, r5
    mov r6, t0
    cmp r4, t1
    jl .loop_entry
    RET
%endmacro

INIT_XMM sse2
MUL_BITMAPS
INIT_YMM avx2
MUL_BITMAPS

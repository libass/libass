;******************************************************************************
;* add_bitmaps.asm: SSE2 and x86 add_bitmaps
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

%include "x86/x86inc.asm"

SECTION_RODATA 32

times 32 db 0xFF
edge_mask: times 32 db 0x00
words_255: times 16 dw 0xFF

SECTION .text

;------------------------------------------------------------------------------
; BLEND_BITMAPS 1:add/sub
; void add_bitmaps(uint8_t *dst, intptr_t dst_stride,
;                  uint8_t *src, intptr_t src_stride,
;                  intptr_t height, intptr_t width);
; void sub_bitmaps(uint8_t *dst, intptr_t dst_stride,
;                  uint8_t *src, intptr_t src_stride,
;                  intptr_t height, intptr_t width);
;------------------------------------------------------------------------------

%macro BLEND_BITMAPS 1
%if ARCH_X86_64
cglobal %1_bitmaps, 6,8,3
    DECLARE_REG_TMP 7
%else
cglobal %1_bitmaps, 4,7,3
    DECLARE_REG_TMP 4
    mov r5, r5m
%endif
    lea r0, [r0 + r5]
    lea r2, [r2 + r5]
    neg r5
    mov r6, r5
    and r5, mmsize - 1
    lea t0, [edge_mask]
    movu m2, [t0 + r5 - mmsize]
%if !ARCH_X86_64
    mov r4, r4m
%endif
    imul r4, r3
    add r4, r2
    mov r5, r6
    jmp .loop_entry

.width_loop:
    p%1usb m0, m1
    movu [r0 + r5 - mmsize], m0
.loop_entry:
    movu m0, [r0 + r5]
    movu m1, [r2 + r5]
    add r5, mmsize
    jnc .width_loop
    pand m1, m2
    p%1usb m0, m1
    movu [r0 + r5 - mmsize], m0
    add r0, r1
    add r2, r3
    mov r5, r6
    cmp r2, r4
    jl .loop_entry
    RET
%endmacro

INIT_XMM sse2
BLEND_BITMAPS add
BLEND_BITMAPS sub
INIT_YMM avx2
BLEND_BITMAPS add
BLEND_BITMAPS sub

;------------------------------------------------------------------------------
; MUL_BITMAPS
; void mul_bitmaps(uint8_t *dst, intptr_t dst_stride,
;                  uint8_t *src1, intptr_t src1_stride,
;                  uint8_t *src2, intptr_t src2_stride,
;                  intptr_t width, intptr_t height);
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
    lea t1, [edge_mask]
    movu m4, [t1 + r6 - mmsize]
    mova m5, [words_255]
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

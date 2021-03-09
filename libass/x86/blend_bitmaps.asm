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


%if ARCH_X86_64

;------------------------------------------------------------------------------
; void mul_bitmaps( uint8_t *dst, intptr_t dst_stride,
;                   uint8_t *src1, intptr_t src1_stride,
;                   uint8_t *src2, intptr_t src2_stride,
;                   intptr_t width, intptr_t height );
;------------------------------------------------------------------------------

INIT_XMM
cglobal mul_bitmaps_x86, 8,12
.skip_prologue:
    imul r7, r3
    add r7, r2 ; last address
.height_loop:
    xor r8, r8 ; x offset
.stride_loop:
    movzx r9, byte [r2 + r8]
    movzx r10, byte [r4 + r8]
    imul r9, r10
    add r9, 255
    shr r9, 8
    mov byte [r0 + r8], r9b
    inc r8
    cmp r8, r6
    jl .stride_loop ; still in scan line
    add r0, r1
    add r2, r3
    add r4, r5
    cmp r2, r7
    jl .height_loop
    RET

INIT_XMM sse2
cglobal mul_bitmaps, 8,12
.skip_prologue:
    cmp r6, 8
    jl mul_bitmaps_x86.skip_prologue
    imul r7, r3
    add r7, r2 ; last address
    pxor xmm2, xmm2
    movdqa xmm3, [words_255]
    mov r9, r6
    and r9, -8 ; &= (~8);
.height_loop:
    xor r8, r8 ; x offset
.stride_loop:
    movq xmm0, [r2 + r8]
    movq xmm1, [r4 + r8]
    punpcklbw xmm0, xmm2
    punpcklbw xmm1, xmm2
    pmullw xmm0, xmm1
    paddw xmm0, xmm3
    psrlw xmm0, 0x08
    packuswb xmm0, xmm0
    movq [r0 + r8], xmm0
    add r8, 8
    cmp r8, r9
    jl .stride_loop ; still in scan line
.stride_loop2:
    cmp r8, r6
    jge .finish
    movzx r10, byte [r2 + r8]
    movzx r11, byte [r4 + r8]
    imul r10, r11
    add r10, 255
    shr r10, 8
    mov byte [r0 + r8], r10b
    inc r8
    jmp .stride_loop2
.finish:
    add r0, r1
    add r2, r3
    add r4, r5
    cmp r2, r7
    jl .height_loop
    RET

INIT_YMM avx2
cglobal mul_bitmaps, 8,12
    cmp r6, 16
    jl mul_bitmaps_sse2.skip_prologue
    %if mmsize == 32
        vzeroupper
    %endif
    imul r7, r3
    add r7, r2 ; last address
    vpxor ymm2, ymm2
    vmovdqa ymm3, [words_255]
    mov r9, r6
    and r9, -16 ; &= (~16);
.height_loop:
    xor r8, r8 ; x offset
.stride_loop:
    vmovdqu xmm0, [r2 + r8]
    vpermq ymm0, ymm0, 0x10
    vmovdqu xmm1, [r4 + r8]
    vpermq ymm1, ymm1, 0x10
    vpunpcklbw ymm0, ymm0, ymm2
    vpunpcklbw ymm1, ymm1, ymm2
    vpmullw ymm0, ymm0, ymm1
    vpaddw ymm0, ymm0, ymm3
    vpsrlw ymm0, ymm0, 0x08
    vextracti128 xmm4, ymm0, 0x1
    vpackuswb ymm0, ymm0, ymm4
    vmovdqa [r0 + r8], xmm0
    add r8, 16
    cmp r8, r9
    jl .stride_loop ; still in scan line
.stride_loop2:
    cmp r8, r6
    jge .finish
    movzx r10, byte [r2 + r8]
    movzx r11, byte [r4 + r8]
    imul r10, r11
    add r10, 255
    shr r10, 8
    mov byte [r0 + r8], r10b
    inc r8
    jmp .stride_loop2
.finish:
    add r0, r1
    add r2, r3
    add r4, r5
    cmp r2, r7
    jl .height_loop
    RET

%endif

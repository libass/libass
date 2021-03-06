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

%include "x86/x86inc.asm"

SECTION_RODATA 32
low_word_zero: dd 0xFFFF0000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF

SECTION .text

;------------------------------------------------------------------------------
; void be_blur_pass( uint8_t *buf, unsigned width,
;                    unsigned height, unsigned stride,
;                    uint16_t *tmp);
;------------------------------------------------------------------------------

%macro BE_BLUR 0
cglobal be_blur, 5,15,9
    %if mmsize == 32
        cmp r1, 32
        jl be_blur_sse2.skip_prologue
        vmovdqa ymm7, [low_word_zero]
    %endif
.skip_prologue:
    pxor m6, m6 ; __m128i temp3 = 0;
    mov r7, r0 ; unsigned char *src=buf;
    lea r12, [r4 + r3 * 2] ; unsigned char *col_sum_buf = tmp + stride * 2;
    lea r14, [r1 - 2] ; tmpreg = (w-2);
    and r14, -(mmsize/2) ; tmpreg &= (~7);

    mov r6, 1 ; int x = 1;
    movzx r8, byte [r7] ; int old_pix = src[0];
    mov r9, r8 ; int old_sum = old_pix;
.prime_loop:
    movzx r10, byte [r7 + r6] ; int temp1 = src[x];
    lea r11, [r8 + r10] ; int temp2 = old_pix + temp1;
    mov r8, r10 ; old_pix = temp1;
    lea r10, [r9 + r11] ; temp1 = old_sum + temp2;
    mov r9, r11 ; old_sum = temp2;
    mov word [r4 + r6 * 2 - 2], r10w ; col_pix_buf[x-1] = temp1;
    mov word [r12 + r6 * 2 - 2], r10w ; col_sum_buf[x-1] = temp1;
    inc r6 ; x++
    cmp r6, r1 ; x < w
    jl .prime_loop
    add r8, r9 ; temp1 = old_sum + old_pix;
    mov word [r4 + r6 * 2 - 2], r8w  ; col_pix_buf[x-1] = temp1;
    mov word [r12 + r6 * 2 - 2], r8w ; col_sum_buf[x-1] = temp1;
    mov r5, 1 ; int y = 1;
.height_loop:
    mov r13, r7 ; dst = src
    add r7, r3 ; src += stride;
    mov r6, 1 ; int x = 1;
    movzx r10, byte [r7] ; temp1 = src[0];
    movd xm0, r10d; __m128i old_pix_128 = temp1;
    movd xm1, r10d; __m128i old_sum_128 = temp1;
jmp .width_check
.width_loop:
    %if mmsize == 32
        vpermq ymm2, [r7 + r6], 0x10
        vpunpcklbw ymm2, ymm2, ymm6 ; new_pix = _mm_unpacklo_epi8(new_pix, temp3);
        vpermq ymm8, ymm2, 0x4e
        vpalignr ymm3, ymm2, ymm8, 14
        vpand ymm3, ymm3, ymm7
        vpaddw ymm3, ymm0 ; temp = _mm_add_epi16(temp, old_pix_128);
        vpaddw ymm3, ymm2 ; temp = _mm_add_epi16(temp, new_pix);
        vperm2i128 ymm0, ymm2, ymm6, 0x21
        vpsrldq ymm0, ymm0, 14; temp = temp >> 14 * 8;
        vpermq ymm8, ymm3, 0x4e
        vpand ymm8, ymm8, ymm7;
        vpalignr ymm2, ymm3, ymm8, 14
        vpand ymm2, ymm2, ymm7
        vpaddw ymm2, ymm1 ; new_pix = _mm_add_epi16(new_pix, old_sum_128);
        vpaddw ymm2, ymm3 ; new_pix = _mm_add_epi16(new_pix, temp);
        vperm2i128 ymm1, ymm3, ymm6, 0x21
        vpsrldq ymm1, ymm1, 14; temp = temp << 2 * 8;
        vmovdqu ymm4, [r4 + r6 * 2 - 2] ; __m128i old_col_pix = *(col_pix_buf+x);
        vmovdqu [r4 + r6 * 2 - 2], ymm2 ; *(col_pix_buf+x) = new_pix ;
        vmovdqu ymm5, [r12 + r6 * 2 - 2] ; __m128i old_col_sum = *(col_pix_sum+x);
        vpaddw ymm3, ymm2, ymm4
        vmovdqu [r12 + r6 * 2 - 2], ymm3 ; *(col_sum_buf+x) = temp;
        vpaddw ymm5, ymm3 ; old_col_sum = _mm_add_epi16(old_col_sum, temp);
        vpsrlw ymm5, 4 ; old_col_sum = old_col_sum >> 4;
        vpackuswb ymm5, ymm5 ; old_col_sum = _mm_packus_epi16(old_col_sum, old_col_sum);
        vpermq ymm5, ymm5, 11_01_10_00b
        vmovdqu [r13 + r6 - 1], xmm5 ; *(dst+x-1) = old_col_sum;
    %else
        movq xmm2, [r7 + r6]; __m128i new_pix = (src+x);
        punpcklbw xmm2, xmm6 ; new_pix = _mm_unpacklo_epi8(new_pix, temp3);
        movdqa xmm3, xmm2 ; __m128i temp = new_pix;
        pslldq xmm3, 2 ; temp = temp << 2 * 8;
        paddw xmm3, xmm0 ; temp = _mm_add_epi16(temp, old_pix_128);
        paddw xmm3, xmm2 ; temp = _mm_add_epi16(temp, new_pix);
        movdqa xmm0, xmm2 ; old_pix_128 = new_pix;
        psrldq xmm0, 14 ; old_pix_128 = old_pix_128 >> 14 * 8;
        movdqa xmm2, xmm3 ; new_pix = temp;
        pslldq xmm2, 2 ; new_pix = new_pix << 2 * 8;
        paddw xmm2, xmm1 ; new_pix = _mm_add_epi16(new_pix, old_sum_128);
        paddw xmm2, xmm3 ; new_pix = _mm_add_epi16(new_pix, temp);
        movdqa xmm1, xmm3 ; old_sum_128 = temp;
        psrldq xmm1, 14 ; old_sum_128 = old_sum_128 >> 14 * 8;
        movdqu xmm4, [r4 + r6 * 2 - 2] ; __m128i old_col_pix = *(col_pix_buf+x-1);
        movdqu [r4 + r6 * 2 - 2], xmm2 ; *(col_pix_buf+x-1) = new_pix ;
        movdqu xmm5, [r12 + r6 * 2 - 2] ; __m128i old_col_sum = *(col_pix_sum+x-1);
        movdqa xmm3, xmm2 ; temp = new_pix;
        paddw xmm3, xmm4 ; temp = _mm_add_epi16(temp, old_col_pix);
        movdqu [r12 + r6 * 2 - 2], xmm3 ; *(col_sum_buf+x-1) = temp;
        paddw xmm5, xmm3 ; old_col_sum = _mm_add_epi16(old_col_sum, temp);
        psrlw xmm5, 4 ; old_col_sum = old_col_sum >> 4;
        packuswb xmm5, xmm5 ; old_col_sum = _mm_packus_epi16(old_col_sum, old_col_sum);
        movq qword [r13 + r6 - 1], xmm5 ; *(dst+x-1) = old_col_sum;
    %endif
    add r6, (mmsize/2); x += 8;
.width_check:
    cmp r6, r14; x < ((w - 2) & (~7));
    jl .width_loop
    movd r8d, xm0 ; old_pix = old_pix[mmsize/2-1]
    mov r8w, r8w
    movd r9d, xm1 ; old_sum = old_sum[mmsize/2-1]
    mov r9w, r9w
    jmp .final_width_check
.final_width_loop:
    movzx r10, byte [r7 + r6] ; temp1 = src[x];
    lea r11, [r8 + r10] ; temp2 = old_pix + temp1;
    mov r8, r10 ; old_pix = temp1;
    lea r10, [r9 + r11] ; temp1 = old_sum + temp2;
    mov r9, r11 ; old_sum = temp2;
    movzx r11, word [r4 + r6 * 2 - 2] ; temp2 = col_pix_buf[x-1];
    add r11, r10 ; temp2 += temp1;
    mov word [r4 + r6 * 2 - 2], r10w ; col_pix_buf[x-1] = temp1;
    movzx r10, word [r12 + r6 * 2 - 2] ; temp1 = col_sum_buf[x-1];
    add r10, r11 ; temp1 += temp2;
    shr r10, 4 ; temp1 >>= 4;
    mov byte [r13 + r6 - 1], r10b ; dst[x-1] = temp1
    mov [r12 + r6 * 2 - 2], r11w ; col_sum_buf[x-1] = temp2;
    inc r6 ; x++
.final_width_check:
    cmp r6, r1 ; x < w
    jl .final_width_loop

    add r9, r8 ; old_sum += old_pix;
    movzx r11, word [r4 + r6 * 2 - 2] ; temp2 = col_pix_buf[x-1];
    add r11, r9 ; temp2 += old_sum;
    mov word [r4 + r6 * 2 - 2], r9w ; col_pix_buf[x-1] = old_sum;
    movzx r10, word [r12 + r6 * 2 - 2] ; temp2 = col_sum_buf[x-1];
    add r10, r11 ; temp1 += temp2;
    shr r10, 4 ; temp1 >>= 4
    mov byte [r13 + r6 - 1], r10b ; dst[x-1] = temp1
    mov [r12 + r6 * 2 - 2], r11w ; col_sum_buf[x-1] = temp2;

    inc r5 ; y++;
    cmp r5, r2 ; y < h;
    jl .height_loop

    mov r13, r7 ; dst = src;
    xor r6, r6 ; x = 0

    mov r14, r1 ; tmpreg = w;
    and r14, -(mmsize/2) ; tmpreg &= (~7);

jmp .lastline_width_check
.lastline_width_loop:
    movdqu m4, [r4 + r6 * 2] ; __m128i old_col_pix = *(col_pix_buf+x);
    movdqu m5, [r12 + r6 * 2] ; __m128i old_col_sum = *(col_pix_sum+x);

    paddw m5, m4 ; old_col_sum = _mm_add_epi16(old_col_sum, temp);
    psrlw m5, 4 ; old_col_sum = old_col_sum >> 4;
    packuswb m5, m5 ; old_col_sum = _mm_packus_epi16(old_col_sum, old_col_sum);
    %if mmsize == 32
        vpermq m5, m5, 11_01_10_00b
        vmovdqu [r13 + r6], xm5 ; *(dst+x) = old_col_sum;
    %else
        movq qword [r13 + r6], xm5 ; *(dst+x) = old_col_sum;
    %endif

    add r6, (mmsize/2); x += 8;
.lastline_width_check:
    cmp r6, r14; x < ((w - 2) & (~7));
    jl .lastline_width_loop
    jmp .lastline_final_width_check
.lastline_final_width_loop:

    movzx r11, word [r4 + r6 * 2] ; temp2 = col_pix_buf[x];
    movzx r10, word [r12 + r6 * 2] ; temp1 = col_sum_buf[x-1];

    add r10, r11 ; temp1 += temp2;
    shr r10, 4 ; temp1 >>= 4;
    mov byte [r13 + r6], r10b ; dst[x] = temp1

    inc r6 ; x++

.lastline_final_width_check:
    cmp r6, r1 ; x < w
    jl .lastline_final_width_loop

    RET
%endmacro

INIT_XMM sse2
BE_BLUR
INIT_YMM avx2
BE_BLUR


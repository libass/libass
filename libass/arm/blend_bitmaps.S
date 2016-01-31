/*
 * void add_bitmaps( uint8_t *dst, intptr_t dst_stride,
 *                   uint8_t *src, intptr_t src_stride,
 *                   intptr_t width, intptr_t height );
*/

#include "asm.S"

.macro blend_func type
function \type\()_bitmaps_arm
    push   {r4-r7}
    ldrd   r4, r5, [sp, #16]
@.Lskip_prologue:
    mla    r5, r5, r3, r2
0: @ height loop
    mov    r6, #0 @ x offset
1: @ stride loop
    ldr    r7, [r0, r6]
    ldr    ip, [r2, r6]
    uq\type\()8 ip, r7, ip
    str    ip, [r0, r6]
    add    r6, #4
    cmp    r6, r4
    blo    1b @ still in scan line
    add    r0, r1
    add    r2, r3
    cmp    r2, r5
    blo    0b
    pop    {r4-r7}
    bx     lr
endfunc
.endm

blend_func add
blend_func sub

/*
 * void mul_bitmaps( uint8_t *dst, intptr_t dst_stride,
 *                   uint8_t *src1, intptr_t src1_stride,
 *                   uint8_t *src2, intptr_t src2_stride,
 *                   intptr_t width, intptr_t height );
*/
function mul_bitmaps_arm
    mov r11, #255
    push   {r4-r11}
    ldrd   r4, r5, [sp, #32]
    ldrd   r6, r7, [sp, #40]
    mla    r7, r7, r3, r2 @ last address
0: @ height loop
    mov    r8, #0 @ x offset
1: @ stride loop
    ldrb   r9,  [r2, r8]
    ldrb   r10, [r4, r8]
    mla    r9, r9, r10, r11
    asr    r9, r9, #8
    strb   r9,  [r0, r8]
    add r8, r8, #1
    cmp r8, r6
    blo 1b @ still in scan line
    add r0, r1
    add r2, r3
    add r4, r5
    cmp r2, r7
    blo 0b
    pop    {r4-r11}
    bx     lr
endfunc

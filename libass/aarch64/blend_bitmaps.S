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

#include "asm.S"

/*
 * load_edge_mask
 * Set n first bytes of NEON register to 255 and other bytes to 0
 */

const edge_mask, align=16
    .dcb.b 16, 0xFF
edge_mask_zeroes:
    .dcb.b 16, 0x00
endconst

.macro load_edge_mask dst, n, tmp
    loadaddr \tmp, edge_mask_zeroes
    sub \tmp, \tmp, \n
    ld1 {\dst\().16B}, [\tmp]
.endm

/*
 * void add_bitmaps(uint8_t *dst, intptr_t dst_stride,
 *                  uint8_t *src, intptr_t src_stride,
 *                  intptr_t width, intptr_t height);
 */

function add_bitmaps_neon
    and x6, x4, 15
    load_edge_mask v2, x6, x9
    add x6, x4, 15
    and x6, x6, ~15
    sub x1, x1, x6
    sub x3, x3, x6
0:
    mov x7, x4
1:
    ld1 {v0.16b}, [x0]
    ld1 {v1.16b}, [x2], #16
    subs x7, x7, 16
    uqadd v0.16b, v0.16b, v1.16b
    b.pl 2f
    and v0.16b, v0.16b, v2.16b
2:
    st1 {v0.16b}, [x0], #16
    b.hi 1b
    add x0, x0, x1
    add x2, x2, x3
    subs x5, x5, #1
    b.hi 0b
    ret
endfunc


/*
 * void imul_bitmaps(uint8_t *dst, intptr_t dst_stride,
 *                   uint8_t *src, intptr_t src_stride,
 *                   intptr_t width, intptr_t height);
 */

function imul_bitmaps_neon
    and x6, x4, 15
    load_edge_mask v4, x6, x9
    add x6, x4, 15
    and x6, x6, ~15
    sub x1, x1, x6
    sub x3, x3, x6

0:
    mov x7, x4
1:
    ld1 {v0.16b}, [x0]
    ld1 {v1.16b}, [x2], #16
    subs x7, x7, 16
    b.pl 2f
    and v1.16b, v1.16b, v4.16b
2:
    movi v2.8h, 255
    movi v3.8h, 255
    mvn v1.16b, v1.16b
    umlal v2.8h, v0.8b, v1.8b
    umlal2 v3.8h, v0.16b, v1.16b
    uqshrn v0.8b, v2.8h, 8
    uqshrn2 v0.16b, v3.8h, 8
    st1 {v0.16b}, [x0], #16
    b.hi 1b
    add x0, x0, x1
    add x2, x2, x3
    subs x5, x5, #1
    b.hi 0b
    ret
endfunc

/*
 * void mul_bitmaps(uint8_t *dst, intptr_t dst_stride,
 *                  uint8_t *src1, intptr_t src1_stride,
 *                  uint8_t *src2, intptr_t src2_stride,
 *                  intptr_t width, intptr_t height);
 */

function mul_bitmaps_neon
    and x8, x6, 15
    load_edge_mask v4, x8, x9
    add x8, x6, 15
    and x8, x8, ~15
    sub x1, x1, x8
    sub x3, x3, x8
    sub x5, x5, x8
0:
    mov x8, x6
1:
    ld1 {v0.16b}, [x2], #16
    subs x8, x8, 16
    ld1 {v1.16b}, [x4], #16
    movi v2.8h, 255
    movi v3.8h, 255
    umlal v2.8h, v0.8b, v1.8b
    umlal2 v3.8h, v0.16b, v1.16b
    uqshrn v0.8b, v2.8h, 8
    uqshrn2 v0.16b, v3.8h, 8
    b.pl 2f
    and v0.16b, v0.16b, v4.16b
2:
    st1 {v0.16b}, [x0], #16
    b.hi 1b
    add x0, x0, x1
    add x2, x2, x3
    add x4, x4, x5
    subs x7, x7, #1
    b.hi 0b
    ret
endfunc

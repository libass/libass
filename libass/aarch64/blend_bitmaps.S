/*
 * Copyright (C) 2022 libass contributors
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

const edge_mask, align=4
    .dcb.b 16, 0xFF
    .dcb.b 16, 0x00
endconst

/*
 * void ass_add_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
 *                      const uint8_t *src, ptrdiff_t src_stride,
 *                      size_t width, size_t height);
 */

function add_bitmaps_neon, export=1
    neg x6, x4
    and x6, x6, 15
    movrel x7, edge_mask
    add x7, x7, x6
    ld1 {v0.16b}, [x7]
    add x6, x6, x4
    sub x6, x6, 16
    sub x1, x1, x6
    sub x3, x3, x6
0:
    subs x6, x4, 16
    b.ls 2f
1:
    ld1 {v1.16b}, [x0]
    ld1 {v2.16b}, [x2], 16
    uqadd v1.16b, v1.16b, v2.16b
    st1 {v1.16b}, [x0], 16
    subs x6, x6, 16
    b.hi 1b
2:
    ld1 {v1.16b}, [x0]
    ld1 {v2.16b}, [x2]
    and v2.16b, v2.16b, v0.16b
    uqadd v1.16b, v1.16b, v2.16b
    st1 {v1.16b}, [x0]
    subs x5, x5, 1
    add x0, x0, x1
    add x2, x2, x3
    b.ne 0b
    ret
endfunc

/*
 * void ass_imul_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
 *                       const uint8_t *src, ptrdiff_t src_stride,
 *                       size_t width, size_t height);
 */

function imul_bitmaps_neon, export=1
    neg x6, x4
    and x6, x6, 15
    movrel x7, edge_mask
    add x7, x7, x6
    ld1 {v0.16b}, [x7]
    add x6, x6, x4
    sub x6, x6, 16
    sub x1, x1, x6
    sub x3, x3, x6
0:
    subs x6, x4, 16
    b.ls 2f
1:
    ld1 {v1.16b}, [x0]
    ld1 {v2.16b}, [x2], 16
    movi v3.8h, 255
    movi v4.8h, 255
    not v2.16b, v2.16b
    umlal v3.8h, v1.8b, v2.8b
    umlal2 v4.8h, v1.16b, v2.16b
    uzp2 v1.16b, v3.16b, v4.16b
    st1 {v1.16b}, [x0], 16
    subs x6, x6, 16
    b.hi 1b
2:
    ld1 {v1.16b}, [x0]
    ld1 {v2.16b}, [x2]
    and v2.16b, v2.16b, v0.16b
    movi v3.8h, 255
    movi v4.8h, 255
    not v2.16b, v2.16b
    umlal v3.8h, v1.8b, v2.8b
    umlal2 v4.8h, v1.16b, v2.16b
    uzp2 v1.16b, v3.16b, v4.16b
    st1 {v1.16b}, [x0]
    subs x5, x5, 1
    add x0, x0, x1
    add x2, x2, x3
    b.ne 0b
    ret
endfunc

/*
 * void ass_mul_bitmaps(uint8_t *dst, ptrdiff_t dst_stride,
 *                      const uint8_t *src1, ptrdiff_t src1_stride,
 *                      const uint8_t *src2, ptrdiff_t src2_stride,
 *                      size_t width, size_t height);
 */

function mul_bitmaps_neon, export=1
    neg x8, x6
    and x8, x8, 15
    movrel x9, edge_mask
    add x9, x9, x8
    ld1 {v0.16b}, [x9]
    add x8, x8, x6
    sub x8, x8, 16
    sub x1, x1, x8
    sub x3, x3, x8
    sub x5, x5, x8
0:
    subs x8, x6, 16
    b.ls 2f
1:
    ld1 {v1.16b}, [x2], 16
    ld1 {v2.16b}, [x4], 16
    movi v3.8h, 255
    movi v4.8h, 255
    umlal v3.8h, v1.8b, v2.8b
    umlal2 v4.8h, v1.16b, v2.16b
    uzp2 v1.16b, v3.16b, v4.16b
    st1 {v1.16b}, [x0], 16
    subs x8, x8, 16
    b.hi 1b
2:
    ld1 {v1.16b}, [x2]
    ld1 {v2.16b}, [x4]
    movi v3.8h, 255
    movi v4.8h, 255
    umlal v3.8h, v1.8b, v2.8b
    umlal2 v4.8h, v1.16b, v2.16b
    uzp2 v1.16b, v3.16b, v4.16b
    and v1.16b, v1.16b, v0.16b
    st1 {v1.16b}, [x0]
    subs x7, x7, 1
    add x0, x0, x1
    add x2, x2, x3
    add x4, x4, x5
    b.ne 0b
    ret
endfunc

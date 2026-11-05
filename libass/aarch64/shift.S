/*
 * Copyright (C) 2026 libass contributors
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
 * shift_x
 * Perform subpixel shift along X axis
 */

.macro shift_x dst1, dst2, src1, src2, mul, vtmp1, vtmp2
    ext \vtmp1\().16b, \src1\().16b, \src2\().16b, 1
    ushll \dst1\().8h, \src1\().8b, 6
    usubl \vtmp2\().8h, \src1\().8b, \vtmp1\().8b
    mls \dst1\().8h, \vtmp2\().8h, \mul\().h[0]
    ushll2 \dst2\().8h, \src1\().16b, 6
    usubl2 \vtmp2\().8h, \src1\().16b, \vtmp1\().16b
    mls \dst2\().8h, \vtmp2\().8h, \mul\().h[0]
.endm

/*
 * shift_y
 * Perform subpixel shift along Y axis
 */

.macro shift_y src11, src12, src21, src22, mul, mode=
.ifb \mode
    sub \src21\().8h, \src11\().8h, \src21\().8h
    sqdmulh \src21\().8h, \src21\().8h, \mul\().h[1]
.else
    sqdmulh \src21\().8h, \src11\().8h, \mul\().h[1]
.endif
    sub \src11\().8h, \src11\().8h, \src21\().8h
    uqrshrn \src11\().8b, \src11\().8h, 6
.ifb \mode
    sub \src22\().8h, \src12\().8h, \src22\().8h
    sqdmulh \src22\().8h, \src22\().8h, \mul\().h[1]
.else
    sqdmulh \src22\().8h, \src12\().8h, \mul\().h[1]
.endif
    sub \src12\().8h, \src12\().8h, \src22\().8h
    uqrshrn2 \src11\().16b, \src12\().8h, 6
.endm

/*
 * void ass_shift_c(uint8_t *buf, ptrdiff_t stride,
 *                  size_t width, size_t height, int shift_x, int shift_y);
 */

function shift_neon, export=1
    add w4, w4, w5, lsl 16 + 9
    mov v6.s[0], w4
    add x4, x0, x1
    sub x1, x1, x2
    and x1, x1, ~15
    sub x3, x3, 1
    movi v7.16b, 0

0:
    mov x6, x0
    ld1 {v0.16b}, [x0], #16
    ld1 {v1.16b}, [x4], #16
    subs x5, x2, 16
    b.ls 2f
1:
    mov v16.16b, v0.16b
    ld1 {v0.16b}, [x0], #16
    shift_x v2, v3, v16, v0, v6, v17, v18
    mov v16.16b, v1.16b
    ld1 {v1.16b}, [x4], #16
    shift_x v4, v5, v16, v1, v6, v17, v18
    shift_y v2, v3, v4, v5, v6
    st1 {v2.16b}, [x6], #16
    subs x5, x5, 16
    b.hi 1b
2:
    shift_x v2, v3, v0, v7, v6, v17, v18
    shift_x v4, v5, v1, v7, v6, v17, v18
    shift_y v2, v3, v4, v5, v6
    st1 {v2.16b}, [x6]
    add x0, x0, x1
    add x4, x4, x1
    subs x3, x3, 1
    b.hi 0b

    mov x6, x0
    ld1 {v0.16b}, [x0], #16
    subs x5, x2, 16
    b.ls 4f
3:
    mov v16.16b, v0.16b
    ld1 {v0.16b}, [x0], #16
    shift_x v2, v3, v16, v0, v6, v17, v18
    shift_y v2, v3, v4, v5, v6, last
    st1 {v2.16b}, [x6], #16
    subs x5, x5, 16
    b.hi 3b
4:
    shift_x v2, v3, v0, v7, v6, v17, v18
    shift_y v2, v3, v4, v5, v6, last
    st1 {v2.16b}, [x6]
    ret
endfunc

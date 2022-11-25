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
 * void be_blur(uint8_t *buf, intptr_t stride,
 *              intptr_t width, intptr_t height, uint16_t *tmp);
 */

function be_blur_neon, export=1
    sub x1, x1, x2
    and x1, x1, ~15
    mov x6, x0
    mov x7, x4
    movi v16.16b, 0
    mov x9, x2

    ld1 {v3.16b}, [x0], #16
    ushll v4.8h, v3.8b, 0

    ext v5.16b, v16.16b, v4.16b, 14
    add v5.8h, v5.8h, v4.8h

    ushll2 v0.8h, v3.16b, 0
    b 1f

0:
    ld1 {v3.16b}, [x0], #16
    ushll v4.8h, v3.8b, 0
    ext v5.16b, v0.16b, v4.16b, 14
    add v5.8h, v5.8h, v4.8h
    ushll2 v0.8h, v3.16b, 0
    ext v3.16b, v1.16b, v5.16b, 2
    add v3.8h, v3.8h, v1.8h
    mov v2.16b, v3.16b

    st1 {v2.8h, v3.8h}, [x4], #32

1:
    ext v1.16b, v4.16b, v0.16b, 14
    add v1.8h, v1.8h, v0.8h
    ext v3.16b, v5.16b, v1.16b, 2
    add v3.8h, v3.8h, v5.8h

    mov v4.16b, v3.16b
    st1 {v3.8h, v4.8h}, [x4], #32

    subs x2, x2, 16
    b.hi 0b

    ext v0.16b, v0.16b, v16.16b, 14
    ext v3.16b, v1.16b, v0.16b, 2
    add v3.8h, v3.8h, v1.8h

    mov v4.16b, v3.16b
    st1 {v3.8h, v4.8h}, [x4], #32

    add x0, x0, x1
    subs x3, x3, 1
    b.le 3f

0:
    mov x4, x7
    mov x2, x9
    ld1 {v2.16b}, [x0], #16
    ushll v4.8h, v2.8b, 0
    ext v5.16b, v16.16b, v4.16b, 14
    add v5.8h, v5.8h, v4.8h
    ushll2 v0.8h, v2.16b, 0

    b 2f

1:
    ld1 {v2.16b}, [x0], #16
    ushll v4.8h, v2.8b, 0
    ext v5.16b, v0.16b, v4.16b, 14
    add v5.8h, v5.8h, v4.8h
    ushll2 v0.8h, v2.16b, 0
    ext v2.16b, v1.16b, v5.16b, 2
    add v6.8h, v2.8h, v1.8h

    ld1 {v1.8h, v2.8h}, [x4]
    add v7.8h, v1.8h, v6.8h
    st1 {v6.8h, v7.8h}, [x4], #32
    add v2.8h, v2.8h, v7.8h
    uqshrn2 v3.16b, v2.8h, 4

    st1 {v3.16b}, [x6], #16

2:
    ext v1.16b, v4.16b, v0.16b, 14
    add v1.8h, v1.8h, v0.8h
    ext v2.16b, v5.16b, v1.16b, 2
    add v2.8h, v2.8h, v5.8h

    ld1 {v3.8h, v4.8h}, [x4]
    add v3.8h, v3.8h, v2.8h
    st1 {v2.8h, v3.8h}, [x4], #32
    add v4.8h, v4.8h, v3.8h
    uqshrn v3.8b, v4.8h, 4

    subs x2, x2, 16
    b.hi 1b

    ext v0.16b, v0.16b, v16.16b, 14
    ext v2.16b, v1.16b, v0.16b, 2
    add v4.8h, v2.8h, v1.8h

    ld1 {v0.8h, v1.8h}, [x4]
    add v5.8h, v0.8h, v4.8h
    st1 {v4.8h, v5.8h}, [x4], #32
    add v1.8h, v1.8h, v5.8h
    uqshrn2 v3.16b, v1.8h, 4
    st1 {v3.16b}, [x6], #16

    add x0, x0, x1
    add x6, x6, x1
    subs x3, x3, 1
    b.hi 0b

3:
    mov x2, x9
    mov x4, x7
0:
    ld1 {v2.8h, v3.8h, v4.8h, v5.8h}, [x4], #64
    add v2.8h, v2.8h, v3.8h
    uqshrn v2.8b, v2.8h, 4
    add v3.8h, v4.8h, v5.8h
    uqshrn2 v2.16b, v3.8h, 4
    st1 {v2.16b}, [x6], #16
    subs x2, x2, 16
    b.hi 0b
    ret
endfunc

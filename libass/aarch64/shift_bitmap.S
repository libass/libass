/*
 * Copyright (C) 2024 rcombs
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
 * void shift_bitmap(uint8_t *buf, intptr_t stride,
 *                   intptr_t width, intptr_t height,
 *                   int32_t shift_x, int32_t shift_y, uint16_t *tmp);
 */

function shift_bitmap_neon, export=1
    mov        x8, 0
    dup        v0.8h, w4 // shift_x
    dup        v1.8h, w5 // shift_y

0:
    mov        x7, 0    // x = 0
    movi       v2.8h, 0 // prev = 0
    mov        x10, x6  // tmprow = tmp

1:
    ldr        q4, [x0, x7] // px = buf[x]

    ushll      v20.8h, v4.8b,  1 // px <<= 1
    ushll2     v21.8h, v4.16b, 1

    mul        v16.8h, v0.8h, v20.8h // b_x = px * shift_x
    mul        v17.8h, v0.8h, v21.8h
    ushr       v16.8h, v16.8h, 6 // >> 6
    ushr       v17.8h, v17.8h, 6

    ext        v2.16b,  v2.16b, v16.16b, 14 // shift prev
    ext        v3.16b, v16.16b, v17.16b, 14

    add        v20.8h, v20.8h, v2.8h // px += prev
    add        v21.8h, v21.8h, v3.8h
    sub        v20.8h, v20.8h, v16.8h // px -= b_x
    sub        v21.8h, v21.8h, v17.8h

    ld1        {v22.8h, v23.8h}, [x10] // tmpv = tmp[x]

    mul        v18.8h, v1.8h, v20.8h // b_y = px * shift_y
    mul        v19.8h, v1.8h, v21.8h
    ushr       v18.8h, v18.8h, 6 // >> 6
    ushr       v19.8h, v19.8h, 6

    st1        {v18.8h, v19.8h}, [x10], #0x20 // tmp[x] = b_y

    add        v20.8h, v20.8h, v22.8h // px += tmpv
    add        v21.8h, v21.8h, v23.8h
    sub        v20.8h, v20.8h, v18.8h // px -= b_y
    sub        v21.8h, v21.8h, v19.8h

    rshrn      v4.8b,  v20.8h, 1 // px >>= 1
    rshrn2     v4.16b, v21.8h, 1

    str        q4, [x0, x7] // buf[x] = px

    mov        v2.8h, v17.8h // prev = b_x

    add        x7, x7, #0x10 // x++
    cmp        x7, x2 // x < w
    b.lt       1b

    add        x0, x0, x1 // buf += stride
    add        x8, x8, #0x1 // y++
    cmp        x8, x3 // y < height
    b.ne       0b

    ret
endfunc

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


#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
.set big_endian, 0
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
.set big_endian, 1
#else
.error "unknown byte order"
#endif


const words_index, align=4
    .dc.w 0, 1, 2, 3, 4, 5, 6, 7
endconst

/*
 * fill_line
 * Fill size bytes (16 or 32) starting from dst with val
 */

.macro fill_line dst, val, size
.if \size == 16
    str \val, [\dst]
.elseif \size == 32
    stp \val, \val, [\dst]
.else
.error "invalid line size"
.endif
.endm

/*
 * void fill_solid_tile(uint8_t *buf, ptrdiff_t stride, int set);
 */

.macro fill_solid_tile tile_size
function fill_solid_tile\tile_size\()_neon, export=1
    cmp w2, 0
    csetm w2, ne
    dup v0.4s, w2
.rept \tile_size - 1
    fill_line x0, q0, \tile_size
    add x0, x0, x1
.endr
    fill_line x0, q0, \tile_size
    ret
endfunc
.endm

fill_solid_tile 16
fill_solid_tile 32

/*
 * calc_line
 * Calculate line using antialiased halfplane algorithm
 */

.macro calc_line dst, src, delta, zero, full, tmp
    add \tmp\().8h, \src\().8h, \delta\().8h
    smax \dst\().8h, \src\().8h, \zero\().8h
    smax \tmp\().8h, \tmp\().8h, \zero\().8h
    smin \dst\().8h, \dst\().8h, \full\().8h
    smin \tmp\().8h, \tmp\().8h, \full\().8h
    add \dst\().8h, \dst\().8h, \tmp\().8h
.endm

/*
 * void fill_halfplane_tile(uint8_t *buf, ptrdiff_t stride,
 *                          int32_t a, int32_t b, int64_t c, int32_t scale);
 */

.macro fill_halfplane_tile tile_order, tile_size
function fill_halfplane_tile\tile_size\()_neon, export=1
    mov x6, 1 << (45 + \tile_order)
    smaddl x2, w2, w5, x6
    smaddl x3, w3, w5, x6
    asr x2, x2, 46 + \tile_order
    asr x3, x3, 46 + \tile_order
    mov x6, 1 << 44
    asr x4, x4, 7 + \tile_order
    smaddl x4, w4, w5, x6
    asr x4, x4, 45
    add w6, w2, w3
    add w4, w4, 1 << (13 - \tile_order)
    sub w4, w4, w6, asr 1

    cmp w2, 0
    csneg w5, w2, w2, ge
    cmp w3, 0
    csneg w6, w3, w3, ge
    cmp w5, w6
    csel w5, w5, w6, le
    add w5, w5, 2
    lsr w5, w5, 2
    sub w4, w4, w5
    add w5, w5, w5

    dup v0.8h, w4
    movrel x6, words_index
    ld1 {v1.8h}, [x6]
    dup v2.8h, w2
    mls v0.8h, v1.8h, v2.8h

    mov w6, (1 << \tile_order) - 8
    msub w3, w2, w6, w3
    dup v1.8h, w5
    shl v2.8h, v2.8h, 3
    dup v3.8h, w3

    movi v4.8h, 0
    movi v5.8h, 1 << (6 - \tile_order), lsl 8
.if (1 << \tile_order) > 16
    sub x1, x1, (1 << \tile_order) - 16
.endif
    mov w3, 1 << \tile_order
0:
.set pos, 0
.rept (1 << \tile_order) / 16
    calc_line v6, v0, v1, v4, v5, v16
    sub v0.8h, v0.8h, v2.8h
    calc_line v7, v0, v1, v4, v5, v16
    uqshrn v6.8b, v6.8h, 7 - \tile_order
    uqshrn2 v6.16b, v7.8h, 7 - \tile_order
.set pos, pos + 16
.if pos == (1 << \tile_order)
    st1 {v6.16b}, [x0]
    sub v0.8h, v0.8h, v3.8h
.else
    st1 {v6.16b}, [x0], 16
    sub v0.8h, v0.8h, v2.8h
.endif
.endr
    subs w3, w3, 1
    add x0, x0, x1
    b.ne 0b
    ret
endfunc
.endm

fill_halfplane_tile 4, 16
fill_halfplane_tile 5, 32

/*
 * struct segment {
 *     int64_t c;
 *     int32_t a, b, scale, flags;
 *     int32_t x_min, x_max, y_min, y_max;
 * };
 */

.set line_c,       0
.set line_a,       8
.set line_b,      12
.set line_scale,  16
.set line_flags,  20
.set line_x_min,  24
.set line_x_max,  28
.set line_y_min,  32
.set line_y_max,  36
.set sizeof_line, 40

/*
 * update_border_line
 * Render top/bottom line of the trapezium with antialiasing
 */

.macro update_border_line tile_order, res, abs_a, b, abs_b, size, sum, vc, van, zero, \
                          tmp1, tmp2, vtmp1, vtmp2, vtmp3, vtmp4, vtmp5, vtmp6
    subs \tmp1, \abs_a, \size, lsl 8 - \tile_order
    csneg \tmp1, wzr, \tmp1, lt
    add \tmp1, \tmp1, 1 << (14 - \tile_order)
    lsl \tmp1, \tmp1, 2 * \tile_order - 5
    dup \vtmp1\().8h, \tmp1

    mul \tmp2, \abs_b, \size
    lsr \tmp2, \tmp2, 6
    cmp \tmp2, \abs_a
    csel \tmp2, \tmp2, \abs_a, le
    add \tmp2, \tmp2, 2
    lsr \tmp2, \tmp2, 2

    mul \sum, \sum, \b
    asr \sum, \sum, 7
    add \sum, \sum, \tmp2
    sub \tmp2, \sum, \tmp2, lsl 1
    mul \sum, \sum, \tmp1
    mul \tmp2, \tmp2, \tmp1
    sub \sum, \size, \sum, asr 16
    sub \tmp2, \size, \tmp2, asr 16
    dup \vtmp2\().8h, \sum
    dup \vtmp3\().8h, \tmp2

    lsl \size, \size, 1
    dup \vtmp4\().8h, \size
.set pos, 0
.rept (1 << \tile_order) / 8
    smull \vtmp5\().4s, \vc\().4h, \vtmp1\().4h
    smull2 \vtmp6\().4s, \vc\().8h, \vtmp1\().8h
    uzp2 \vtmp5\().8h, \vtmp5\().8h, \vtmp6\().8h
    add \vtmp6\().8h, \vtmp5\().8h, \vtmp2\().8h
    add \vtmp5\().8h, \vtmp5\().8h, \vtmp3\().8h
    smax \vtmp6\().8h, \vtmp6\().8h, \zero\().8h
    smax \vtmp5\().8h, \vtmp5\().8h, \zero\().8h
    smin \vtmp6\().8h, \vtmp6\().8h, \vtmp4\().8h
    smin \vtmp5\().8h, \vtmp5\().8h, \vtmp4\().8h
    add \vtmp5\().8h, \vtmp5\().8h, \vtmp6\().8h
    ld1 {\vtmp6\().8h}, [\res]
    add \vtmp6\().8h, \vtmp6\().8h, \vtmp5\().8h
    st1 {\vtmp6\().8h}, [\res], 16
.set pos, pos + 16
.if pos < (2 << \tile_order)
    sub \vc\().8h, \vc\().8h, \van\().8h
.endif
.endr
.endm

/*
 * void fill_generic_tile(uint8_t *buf, ptrdiff_t stride,
 *                        const struct segment *line, size_t n_lines,
 *                        int winding);
 */

.macro fill_generic_tile tile_order, tile_size
function fill_generic_tile\tile_size\()_neon, export=1
    .set delta_offs, 2 << (2 * \tile_order)
    .set buf_size, delta_offs + (2 << \tile_order) + 16
    movi v0.8h, 0
.rept buf_size / 32
    stp q0, q0, [sp, -32]!
.endr
.if (buf_size & 16) != 0
    str q0, [sp, -16]!
.endif

    movi v1.8h, 1 << (6 - \tile_order), lsl 8
    movrel x5, words_index
    ld1 {v2.8h}, [x5]
0:
    ldr x5, [x2, line_flags]
.if big_endian
    ror x5, x5, 32
.endif
    and w6, w5, 2
    add w6, w6, 2
    mov w7, -5
    bic x7, x5, x7
    cmp x7, 4
    csel w6, w6, wzr, eq
    tst w5, 1
    cinv w5, w6, ne

    ldp w6, w7, [x2, line_y_min]
    asr w8, w6, 6
    asr w9, w7, 6
    and w10, w6, 63
    and w11, w7, 63
    mov w12, 256 << 16
    tst w5, 4
    b.eq 1f
    add x13, sp, x8, lsl 1
    ldr w14, [x13, delta_offs]
.if big_endian
    ror w14, w14, 16
.endif
    sub w14, w14, w10, lsl 18
    ror w14, w14, 16
    add w14, w14, w10, lsl 18
    sub w14, w14, w12
.if !big_endian
    ror w14, w14, 16
.endif
    str w14, [x13, delta_offs]
1:
    tst w5, 2
    b.eq 2f
    add x13, sp, x9, lsl 1
    ldr w14, [x13, delta_offs]
.if big_endian
    ror w14, w14, 16
.endif
    add w14, w14, w11, lsl 18
    ror w14, w14, 16
    sub w14, w14, w11, lsl 18
    add w14, w14, w12
.if !big_endian
    ror w14, w14, 16
.endif
    str w14, [x13, delta_offs]
2:
    cmp w6, w7
    b.eq 7f

    ldp w6, w7, [x2, line_a]
    ldr w12, [x2, line_scale]
    mov x13, 1 << (45 + \tile_order)
    smaddl x6, w6, w12, x13
    smaddl x7, w7, w12, x13
    asr x6, x6, 46 + \tile_order
    asr x7, x7, 46 + \tile_order
    ldr x5, [x2, line_c]
    mov x13, 1 << 44
    asr x5, x5, 7 + \tile_order
    smaddl x5, w5, w12, x13
    asr x5, x5, 45
    sub w5, w5, w6, asr 1
    msub w5, w7, w8, w5

    cmp w6, 0
    csneg w12, w6, w6, ge
    cmp w7, 0
    csneg w13, w7, w7, ge
    dup v3.8h, w5
    dup v4.8h, w6
    mls v3.8h, v2.8h, v4.8h
    mov w5, (1 << \tile_order) - 8
    msub w5, w5, w6, w7
    shl v4.8h, v4.8h, 3
    dup v5.8h, w5

    lsl w8, w8, \tile_order + 1
    lsl w9, w9, \tile_order + 1
    add x8, sp, x8
    add x9, sp, x9
    cmp x8, x9
    b.eq 6f
    cmp w10, 0
    b.eq 3f
    mov w14, 64
    sub w14, w14, w10
    add w10, w10, 64
    update_border_line \tile_order, x8, w12, w7, w13, w14, w10, v3, v4, v0, \
                       w5, w6, v6, v7, v16, v17, v18, v19
    sub v3.8h, v3.8h, v5.8h
    cmp x8, x9
    b.eq 5f
3:
    cmp w12, w13
    csel w5, w12, w13, le
    add w5, w5, 2
    lsr w5, w5, 2
    mov w6, 1 << (13 - \tile_order)
    sub w6, w6, w7, asr 1
    sub w6, w6, w5
    add w5, w5, w5
    dup v6.8h, w6
    dup v7.8h, w5
    add v3.8h, v3.8h, v6.8h
4:
.set pos, 0
.rept (1 << \tile_order) / 8
    calc_line v16, v3, v7, v0, v1, v17
    ld1 {v17.8h}, [x8]
    ssra v17.8h, v16.8h, 7 - \tile_order
    st1 {v17.8h}, [x8], 16
.set pos, pos + 16
.if pos < (2 << \tile_order)
    sub v3.8h, v3.8h, v4.8h
.else
    sub v3.8h, v3.8h, v5.8h
.endif
.endr
    cmp x8, x9
    b.ne 4b
    sub v3.8h, v3.8h, v6.8h
5:
    cmp w11, 0
    b.eq 7f
    mov w10, 0
6:
    sub w14, w11, w10
    add w10, w11, w10
    update_border_line \tile_order, x8, w12, w7, w13, w14, w10, v3, v4, v0, \
                       w5, w6, v6, v7, v16, v17, v18, v19
7:
    subs x3, x3, 1
    add x2, x2, sizeof_line
    b.ne 0b

    lsl w4, w4, 8
    add x3, sp, delta_offs
.if (1 << \tile_order) > 16
    sub x1, x1, (1 << \tile_order) - 16
.endif
    mov w2, 1 << \tile_order
8:
    ldrsh w5, [x3], 2
    add w4, w4, w5
    dup v2.8h, w4
.set pos, 0
.rept (1 << \tile_order) / 16
    ld1 {v0.8h, v1.8h}, [sp], 32
    add v0.8h, v0.8h, v2.8h
    add v1.8h, v1.8h, v2.8h
    abs v0.8h, v0.8h
    abs v1.8h, v1.8h
    uqxtn v0.8b, v0.8h
    uqxtn2 v0.16b, v1.8h
.set pos, pos + 16
.if pos == (1 << \tile_order)
    st1 {v0.16b}, [x0]
.else
    st1 {v0.16b}, [x0], 16
.endif
.endr
    subs w2, w2, 1
    add x0, x0, x1
    b.ne 8b
    add sp, sp, buf_size - delta_offs
    ret
endfunc
.endm

fill_generic_tile 4, 16
fill_generic_tile 5, 32

/*
 * merge_line
 * Calculate maximum of two lines
 */

.macro merge_line dst, src, size
.if \size == 16
    ld1 {v0.16b}, [\dst]
    ld1 {v1.16b}, [\src], 16
    umax v0.16b, v0.16b, v1.16b
    st1 {v0.16b}, [\dst]
.elseif \size == 32
    ld1 {v0.16b, v1.16b}, [\dst]
    ld1 {v2.16b, v3.16b}, [\src], 32
    umax v0.16b, v0.16b, v2.16b
    umax v1.16b, v1.16b, v3.16b
    st1 {v0.16b, v1.16b}, [\dst]
.else
.error "invalid line size"
.endif
.endm

/*
 * void merge_tile(uint8_t *buf, ptrdiff_t stride, const uint8_t *tile);
 */

.macro merge_tile tile_size
function merge_tile\tile_size\()_neon, export=1
.rept \tile_size - 1
    merge_line x0, x2, \tile_size
    add x0, x0, x1
.endr
    merge_line x0, x2, \tile_size
    ret
endfunc
.endm

merge_tile 16
merge_tile 32

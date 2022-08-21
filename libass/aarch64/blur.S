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

const words_zero, align=16
    .dc.w 0, 0, 0, 0, 0, 0, 0, 0
endconst

/*
 * void stripe_unpack(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
 *                    size_t width, size_t height);
 */

function stripe_unpack16_neon, export=1
    add x3, x3, 7
    lsl x5, x4, 4
    bic x7, x3, 15
    sub x2, x2, x7
0:
    mov x6, x0
    subs x7, x3, 16
    b.lo 2f
1:
    ld1 {v0.16b}, [x1], 16
    zip1 v1.16b, v0.16b, v0.16b
    zip2 v0.16b, v0.16b, v0.16b
    urshr v1.8h, v1.8h, 2
    urshr v0.8h, v0.8h, 2
    st1 {v1.8h}, [x6]
    add x6, x6, x5
    st1 {v0.8h}, [x6]
    add x6, x6, x5
    subs x7, x7, 16
    b.hs 1b
2:
    tst x7, 8
    b.eq 3f
    ld1 {v0.16b}, [x1]
    zip1 v0.16b, v0.16b, v0.16b
    urshr v0.8h, v0.8h, 2
    st1 {v0.8h}, [x6]
3:
    subs x4, x4, 1
    add x0, x0, 16
    add x1, x1, x2
    b.ne 0b
    ret
endfunc

/*
 * void stripe_pack(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
 *                  size_t width, size_t height);
 */

function stripe_pack16_neon, export=1
    lsl x4, x4, 4
    mov w5, 8
    movk w5, 40, lsl 16
    movi v1.8h, 48
    subs x3, x3, 9
    b.lo 2f
0:
    mov x6, x0
    mov x7, x4
    dup v0.4s, w5
1:
    add x8, x2, x4
    ld1 {v2.8h}, [x2], 16
    ld1 {v3.8h}, [x8]
    ushr v4.8h, v2.8h, 8
    ushr v5.8h, v3.8h, 8
    sub v2.8h, v2.8h, v4.8h
    sub v3.8h, v3.8h, v5.8h
    add v2.8h, v2.8h, v0.8h
    add v3.8h, v3.8h, v0.8h
    shrn v2.8b, v2.8h, 6
    shrn2 v2.16b, v3.8h, 6
    st1 {v2.16b}, [x6]
    subs x7, x7, 16
    eor v0.16b, v0.16b, v1.16b
    add x6, x6, x1
    b.ne 1b
    subs x3, x3, 16
    add x0, x0, 16
    add x2, x2, x4
    b.hs 0b
2:
    tst x3, 8
    b.eq 4f
    dup v0.4s, w5
3:
    ld1 {v2.8h}, [x2], 16
    ushr v4.8h, v2.8h, 8
    sub v2.8h, v2.8h, v4.8h
    add v2.8h, v2.8h, v0.8h
    shrn v2.8b, v2.8h, 6
    st1 {v2.16b}, [x0]
    subs x4, x4, 16
    eor v0.16b, v0.16b, v1.16b
    add x0, x0, x1
    b.ne 3b
4:
    ret
endfunc

/*
 * load_line
 * Load vN register with correct source bitmap data
 */

.macro load_line dst, base, offs, max, zero_offs, tmp
    cmp \offs, \max
    csel \tmp, \offs, \zero_offs, lo
    add \tmp, \tmp, \base
    ld1 {\dst\().8h}, [\tmp]
.endm

/*
 * void shrink_horz(int16_t *dst, const int16_t *src,
 *                  size_t src_width, size_t src_height);
 */

function shrink_horz16_neon, export=1
    lsl x4, x2, 1
    add x4, x4, 15
    bic x4, x4, 15
    mul x4, x4, x3
    add x2, x2, 5 - 2
    movrel x5, words_zero
    sub x5, x5, x1
    mov x6, 0
0:
    mov x7, x3
1:
    sub x8, x6, x3, lsl 4
    load_line v1, x1, x8, x4, x5, x9
    load_line v2, x1, x6, x4, x5, x9
    add x8, x6, x3, lsl 4
    load_line v3, x1, x8, x4, x5, x9
    uzp1 v0.8h, v1.8h, v1.8h
    uzp2 v1.8h, v1.8h, v1.8h
    uzp1 v4.8h, v2.8h, v3.8h
    uzp2 v5.8h, v2.8h, v3.8h
    ext v2.16b, v0.16b, v4.16b, 14
    ext v3.16b, v1.16b, v5.16b, 14
    ext v0.16b, v0.16b, v4.16b, 12
    ext v1.16b, v1.16b, v5.16b, 12

    add v0.8h, v0.8h, v5.8h
    add v1.8h, v1.8h, v4.8h
    add v2.8h, v2.8h, v3.8h
    uhadd v0.8h, v0.8h, v1.8h
    uhadd v0.8h, v0.8h, v2.8h
    uhadd v0.8h, v0.8h, v1.8h
    uhadd v0.8h, v0.8h, v2.8h
    urshr v0.8h, v0.8h, 1
    st1 {v0.8h}, [x0], 16

    subs x7, x7, 1
    add x6, x6, 16
    b.ne 1b
    subs x2, x2, 16
    add x6, x6, x3, lsl 4
    b.hs 0b
    ret
endfunc

/*
 * void shrink_vert(int16_t *dst, const int16_t *src,
 *                  size_t src_width, size_t src_height);
 */

function shrink_vert16_neon, export=1
    lsl x3, x3, 4
    movrel x4, words_zero
    sub x4, x4, x1
0:
    add x5, x3, (5 - 2) * 16
    movi v0.8h, 0
    movi v1.8h, 0
    movi v2.8h, 0
    movi v3.8h, 0
    mov x6, 0
1:
    load_line v4, x1, x6, x3, x4, x7
    add x6, x6, 16
    load_line v5, x1, x6, x3, x4, x7
    add x6, x6, 16

    add v0.8h, v0.8h, v5.8h
    add v1.8h, v1.8h, v4.8h
    add v6.8h, v2.8h, v3.8h
    uhadd v0.8h, v0.8h, v1.8h
    uhadd v0.8h, v0.8h, v6.8h
    uhadd v0.8h, v0.8h, v1.8h
    uhadd v0.8h, v0.8h, v6.8h
    urshr v0.8h, v0.8h, 1
    st1 {v0.8h}, [x0], 16

    subs x5, x5, 32
    mov v0.16b, v2.16b
    mov v1.16b, v3.16b
    mov v2.16b, v4.16b
    mov v3.16b, v5.16b
    b.hs 1b
    subs x2, x2, 8
    add x1, x1, x3
    sub x4, x4, x3
    b.hi 0b
    ret
endfunc

/*
 * void expand_horz(int16_t *dst, const int16_t *src,
 *                  size_t src_width, size_t src_height);
 */

function expand_horz16_neon, export=1
    lsl x4, x2, 1
    add x4, x4, 15
    bic x4, x4, 15
    mul x4, x4, x3
    movrel x5, words_zero
    sub x5, x5, x1
    subs x2, x2, 3
    mov x6, 0
    b.lo 2f
0:
    mov x7, x3
1:
    sub x8, x6, x3, lsl 4
    load_line v1, x1, x8, x4, x5, x9
    load_line v2, x1, x6, x4, x5, x9
    ext v0.16b, v1.16b, v2.16b, 12
    ext v1.16b, v1.16b, v2.16b, 14

    uhadd v3.8h, v0.8h, v2.8h
    uhadd v3.8h, v3.8h, v1.8h
    uhadd v0.8h, v0.8h, v3.8h
    uhadd v2.8h, v2.8h, v3.8h
    urhadd v0.8h, v0.8h, v1.8h
    urhadd v2.8h, v2.8h, v1.8h
    zip1 v1.8h, v0.8h, v2.8h
    zip2 v2.8h, v0.8h, v2.8h
    add x9, x0, x3, lsl 4
    st1 {v1.8h}, [x0]
    st1 {v2.8h}, [x9]

    subs x7, x7, 1
    add x0, x0, 16
    add x6, x6, 16
    b.ne 1b
    subs x2, x2, 8
    add x0, x0, x3, lsl 4
    b.hs 0b
2:
    tst x2, 4
    b.eq 4f
    mov x7, x3
3:
    sub x8, x6, x3, lsl 4
    load_line v1, x1, x8, x4, x5, x9
    load_line v2, x1, x6, x4, x5, x9
    ext v0.16b, v1.16b, v2.16b, 12
    ext v1.16b, v1.16b, v2.16b, 14

    uhadd v3.8h, v0.8h, v2.8h
    uhadd v3.8h, v3.8h, v1.8h
    uhadd v0.8h, v0.8h, v3.8h
    uhadd v2.8h, v2.8h, v3.8h
    urhadd v0.8h, v0.8h, v1.8h
    urhadd v2.8h, v2.8h, v1.8h
    zip1 v1.8h, v0.8h, v2.8h
    st1 {v1.8h}, [x0], 16

    subs x7, x7, 1
    add x6, x6, 16
    b.ne 3b
4:
    ret
endfunc

/*
 * void expand_vert(int16_t *dst, const int16_t *src,
 *                  size_t src_width, size_t src_height);
 */

function expand_vert16_neon, export=1
    lsl x3, x3, 4
    movrel x4, words_zero
    sub x4, x4, x1
0:
    add x5, x3, 32
    movi v0.8h, 0
    movi v1.8h, 0
    mov x6, 0
1:
    load_line v2, x1, x6, x3, x4, x7
    add x6, x6, 16

    uhadd v3.8h, v0.8h, v2.8h
    uhadd v3.8h, v3.8h, v1.8h
    uhadd v0.8h, v0.8h, v3.8h
    uhadd v3.8h, v2.8h, v3.8h
    urhadd v0.8h, v0.8h, v1.8h
    urhadd v3.8h, v3.8h, v1.8h
    st1 {v0.8h}, [x0], 16
    st1 {v3.8h}, [x0], 16

    subs x5, x5, 16
    mov v0.16b, v1.16b
    mov v1.16b, v2.16b
    b.ne 1b
    subs x2, x2, 8
    add x1, x1, x3
    sub x4, x4, x3
    b.hi 0b
    ret
endfunc

/*
 * calc_diff
 * Calculate difference between offset line and center line
 */

.macro calc_diff dst, line0, line1, line2, pos, center
.if \pos == 0
    sub \dst\().8h, \line2\().8h, \center\().8h
.elseif \pos > 0 && \pos < 8
    ext \dst\().16b, \line1\().16b, \line2\().16b, 16 - 2 * \pos
    sub \dst\().8h, \dst\().8h, \center\().8h
.elseif \pos == 8
    sub \dst\().8h, \line1\().8h, \center\().8h
.elseif \pos > 8 && \pos < 16
    ext \dst\().16b, \line0\().16b, \line1\().16b, 32 - 2 * \pos
    sub \dst\().8h, \dst\().8h, \center\().8h
.elseif \pos == 16
    sub \dst\().8h, \line0\().8h, \center\().8h
.else
.error "invalid pos"
.endif
.endm

/*
 * calc_blur
 * Calculate filterd line
 */

.macro calc_blur dst, line0, line1, line2, n, center, params, vtmp1, vtmp2, vtmp3
    movi \vtmp1\().4s, 0x80, lsl 8
    movi \vtmp2\().4s, 0x80, lsl 8
.set pos, 0
.rept \n
    calc_diff \vtmp3, \line0, \line1, \line2, (\n - pos - 1), \center
    smlal \vtmp1\().4s, \vtmp3\().4h, \params\().h[pos]
    smlal2 \vtmp2\().4s, \vtmp3\().8h, \params\().h[pos]
    calc_diff \vtmp3, \line0, \line1, \line2, (\n + pos + 1), \center
    smlal \vtmp1\().4s, \vtmp3\().4h, \params\().h[pos]
    smlal2 \vtmp2\().4s, \vtmp3\().8h, \params\().h[pos]
.set pos, pos + 1
.endr
    uzp2 \vtmp1\().8h, \vtmp1\().8h, \vtmp2\().8h
    add \vtmp1\().8h, \vtmp1\().8h, \center\().8h
    st1 {\vtmp1\().8h}, [\dst], 16
.endm

/*
 * void blur_horz(int16_t *dst, const int16_t *src,
 *                size_t src_width, size_t src_height,
 *                const int16_t *param);
 */

.macro blur_horz n
function blur\n\()_horz16_neon, export=1
    ld1 {v0.8h}, [x4]
    lsl x4, x2, 1
    add x4, x4, 15
    bic x4, x4, 15
    mul x4, x4, x3
    movrel x5, words_zero
    sub x5, x5, x1
    add x2, x2, 2 * \n
    mov x6, 0
0:
    mov x7, x3
1:
.if \n > 4
    sub x8, x6, x3, lsl 5
    load_line v1, x1, x8, x4, x5, x9
.endif
    sub x8, x6, x3, lsl 4
    load_line v2, x1, x8, x4, x5, x9
    load_line v3, x1, x6, x4, x5, x9

.if \n < 8
    ext v7.16b, v2.16b, v3.16b, 16 - 2 * \n
    calc_blur x0, v1, v2, v3, \n, v7, v0, v4, v5, v6
.else
    calc_blur x0, v1, v2, v3, \n, v2, v0, v4, v5, v6
.endif

    subs x7, x7, 1
    add x6, x6, 16
    b.ne 1b
    subs x2, x2, 8
    b.hi 0b
    ret
endfunc
.endm

blur_horz 4
blur_horz 5
blur_horz 6
blur_horz 7
blur_horz 8

/*
 * void blur_vert(int16_t *dst, const int16_t *src,
 *                size_t src_width, size_t src_height,
 *                const int16_t *param);
 */

.macro blur_vert n
function blur\n\()_vert16_neon, export=1
    ld1 {v0.8h}, [x4]
    lsl x3, x3, 4
    movrel x4, words_zero
    sub x4, x4, x1
0:
    add x5, x3, 32 * \n
    mov x6, -16 * \n
1:
    load_line v1, x1, x6, x3, x4, x7
    movi v2.4s, 0x80, lsl 8
    movi v3.4s, 0x80, lsl 8
.set pos, 0
.rept \n
    sub x8, x6, 16 * (pos + 1)
    load_line v4, x1, x8, x3, x4, x7
    sub v4.8h, v4.8h, v1.8h
    smlal v2.4s, v4.4h, v0.h[pos]
    smlal2 v3.4s, v4.8h, v0.h[pos]
    add x8, x6, 16 * (pos + 1)
    load_line v4, x1, x8, x3, x4, x7
    sub v4.8h, v4.8h, v1.8h
    smlal v2.4s, v4.4h, v0.h[pos]
    smlal2 v3.4s, v4.8h, v0.h[pos]
.set pos, pos + 1
.endr
    uzp2 v2.8h, v2.8h, v3.8h
    add v2.8h, v2.8h, v1.8h
    st1 {v2.8h}, [x0], 16

    subs x5, x5, 16
    add x6, x6, 16
    b.ne 1b
    subs x2, x2, 8
    add x1, x1, x3
    sub x4, x4, x3
    b.hi 0b
    ret
endfunc
.endm

blur_vert 4
blur_vert 5
blur_vert 6
blur_vert 7
blur_vert 8

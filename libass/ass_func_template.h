/*
 * Copyright (C) 2015 Vabishchevich Nikolay <vabnick@gmail.com>
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


FillSolidTileFunc DECORATE(fill_solid_tile16);
FillHalfplaneTileFunc DECORATE(fill_halfplane_tile16);
FillGenericTileFunc DECORATE(fill_generic_tile16);
MergeTileFunc DECORATE(merge_tile16);

FillSolidTileFunc DECORATE(fill_solid_tile32);
FillHalfplaneTileFunc DECORATE(fill_halfplane_tile32);
FillGenericTileFunc DECORATE(fill_generic_tile32);
MergeTileFunc DECORATE(merge_tile32);

BitmapBlendFunc DECORATE(add_bitmaps), DECORATE(imul_bitmaps);
BitmapMulFunc DECORATE(mul_bitmaps);

BeBlurFunc DECORATE(be_blur);

Convert8to16Func DECORATE(stripe_unpack);
Convert16to8Func DECORATE(stripe_pack);
FilterFunc DECORATE(shrink_horz), DECORATE(shrink_vert);
FilterFunc DECORATE(expand_horz), DECORATE(expand_vert);
ParamFilterFunc DECORATE(blur4_horz), DECORATE(blur4_vert);
ParamFilterFunc DECORATE(blur5_horz), DECORATE(blur5_vert);
ParamFilterFunc DECORATE(blur6_horz), DECORATE(blur6_vert);
ParamFilterFunc DECORATE(blur7_horz), DECORATE(blur7_vert);
ParamFilterFunc DECORATE(blur8_horz), DECORATE(blur8_vert);


const BitmapEngine DECORATE(bitmap_engine) = {
    .align_order = ALIGN,

#if CONFIG_LARGE_TILES
    .tile_order = 5,
    .fill_solid = DECORATE(fill_solid_tile32),
    .fill_halfplane = DECORATE(fill_halfplane_tile32),
    .fill_generic = DECORATE(fill_generic_tile32),
    .merge_tile = DECORATE(merge_tile32),
#else
    .tile_order = 4,
    .fill_solid = DECORATE(fill_solid_tile16),
    .fill_halfplane = DECORATE(fill_halfplane_tile16),
    .fill_generic = DECORATE(fill_generic_tile16),
    .merge_tile = DECORATE(merge_tile16),
#endif

    .add_bitmaps = DECORATE(add_bitmaps),
    .imul_bitmaps = DECORATE(imul_bitmaps),
    .mul_bitmaps = DECORATE(mul_bitmaps),

    .be_blur = DECORATE(be_blur),

    .stripe_unpack = DECORATE(stripe_unpack),
    .stripe_pack = DECORATE(stripe_pack),
    .shrink_horz = DECORATE(shrink_horz),
    .shrink_vert = DECORATE(shrink_vert),
    .expand_horz = DECORATE(expand_horz),
    .expand_vert = DECORATE(expand_vert),
    .blur_horz = { DECORATE(blur4_horz), DECORATE(blur5_horz), DECORATE(blur6_horz), DECORATE(blur7_horz), DECORATE(blur8_horz) },
    .blur_vert = { DECORATE(blur4_vert), DECORATE(blur5_vert), DECORATE(blur6_vert), DECORATE(blur7_vert), DECORATE(blur8_vert) },
};

/*
 * Copyright (C) 2017 Vabishchevich Nikolay <vabnick@gmail.com>
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

#include "image.h"
#include <png.h>


static inline bool is_little_endian(void)
{
    return *(char *) &(uint16_t) {1};
}

static void read_callback(png_structp png_ptr, png_bytep data, size_t length)
{
    if (png_ptr == NULL)
        return;

    if (fread(data, 1, length, png_get_io_ptr(png_ptr)) < length)
        png_error(png_ptr, "Read Error");
}

bool read_png(const char *path, Image16 *img)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return false;
    }

    png_byte *volatile buf = NULL;
    png_byte **volatile rows = NULL;
    if (setjmp(png_jmpbuf(png))) {
        free(buf);
        free(rows);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    // Explicitly set a read callback instead of using the default.
    // This ensures all accesses to the FILE* happen within the program that created it,
    // rather than in the libpng library, which may be linked against a different libc
    // (particularly on win32).
    png_set_read_fn(png, fp, &read_callback);
    png_init_io(png, fp);
    png_read_info(png, info);

    uint32_t w = png_get_image_width(png, info);
    uint32_t h = png_get_image_height(png, info);
    uint32_t a = png_get_valid(png, info, PNG_INFO_tRNS);
    int type   = png_get_color_type(png, info);
    int depth  = png_get_bit_depth(png, info);

    if (w > 0xFFFF || h > 0xFFFF) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    if (!(type & PNG_COLOR_MASK_COLOR))
        png_set_gray_to_rgb(png);
    if ((type & PNG_COLOR_MASK_PALETTE) || a)
        png_set_expand(png);
    if (!a && !(type & PNG_COLOR_MASK_ALPHA))
        png_set_filler(png, -1, PNG_FILLER_AFTER);

    ptrdiff_t stride = 8 * w;
    buf = malloc(stride * h);
    rows = malloc(h * sizeof(png_byte *));
    if (!buf || !rows) {
        free(buf);
        free(rows);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_byte *ptr = buf;
    ptrdiff_t half = 4 * w;
    if (depth < 16)
        ptr += half;
    else if (is_little_endian())
        png_set_swap(png);

    for (uint32_t i = 0; i < h; i++) {
        rows[i] = ptr;
        ptr += stride;
    }

    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    // convert to premultiplied with inverted alpha
    if (depth < 16) {
        uint8_t *ptr = (uint8_t *) buf;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t r = ptr[half + 4 * x + 0];
                uint8_t g = ptr[half + 4 * x + 1];
                uint8_t b = ptr[half + 4 * x + 2];
                uint8_t a = ptr[half + 4 * x + 3];
                uint16_t ra = (uint16_t) r * a;
                uint16_t ga = (uint16_t) g * a;
                uint16_t ba = (uint16_t) b * a;
                ptr[8 * x + 0] = ptr[8 * x + 1] = (ra + (ra >> 8) + 128) >> 8;
                ptr[8 * x + 2] = ptr[8 * x + 3] = (ga + (ga >> 8) + 128) >> 8;
                ptr[8 * x + 4] = ptr[8 * x + 5] = (ba + (ba >> 8) + 128) >> 8;
                ptr[8 * x + 6] = ptr[8 * x + 7] = ~a;
            }
            ptr += stride;
        }
    } else {
        uint16_t *ptr = (uint16_t *) buf;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint16_t r = ptr[4 * x + 0];
                uint16_t g = ptr[4 * x + 1];
                uint16_t b = ptr[4 * x + 2];
                uint16_t a = ptr[4 * x + 3];
                uint32_t ra = (uint32_t) r * a;
                uint32_t ga = (uint32_t) g * a;
                uint32_t ba = (uint32_t) b * a;
                ptr[4 * x + 0] = (ra + (ra >> 16) + (1 << 15)) >> 16;
                ptr[4 * x + 1] = (ga + (ga >> 16) + (1 << 15)) >> 16;
                ptr[4 * x + 2] = (ba + (ba >> 16) + (1 << 15)) >> 16;
                ptr[4 * x + 3] = ~a;
            }
            ptr += half;
        }
    }

    img->width = w;
    img->height = h;
    img->buffer = (uint16_t *) buf;
    return true;
}

static bool write_png(const char *path, uint32_t width, uint32_t height,
                      ptrdiff_t stride, const void *buffer, int depth)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return false;

    png_structp png =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return false;
    }

    png_byte **rows = malloc(height * sizeof(png_byte *));
    if (!rows) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_byte *ptr = (png_byte *) buffer;
    for (uint32_t i = 0; i < height; i++) {
        rows[i] = (png_byte *) ptr;
        ptr += stride;
    }

    if (setjmp(png_jmpbuf(png))) {
        free(rows);
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_compression_level(png, 9);

    png_set_IHDR(png, info, width, height, depth,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    if (depth > 8 && is_little_endian())
        png_set_swap(png);
    png_write_image(png, rows);
    png_write_end(png, NULL);

    free(rows);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return true;
}

bool write_png8(const char *path, Image8 *img)
{
    uint8_t *ptr = img->buffer;
    size_t size = (size_t) img->width * img->height;
    for (size_t i = 0; i < size; i++) {
        uint8_t alpha = ~ptr[3];
        if (alpha) {
            const uint32_t offs = (uint32_t) 1 << 15;
            uint32_t inv = ((uint32_t) 255 << 16) / alpha + 1;
            // equivalent to (255 * ptr[k] + alpha / 2) / alpha
            ptr[0] = (ptr[0] * inv + offs) >> 16;
            ptr[1] = (ptr[1] * inv + offs) >> 16;
            ptr[2] = (ptr[2] * inv + offs) >> 16;
        }
        ptr[3] = alpha;
        ptr += 4;
    }
    return write_png(path, img->width, img->height,
                     4 * img->width, img->buffer, 8);
}

bool write_png16(const char *path, Image16 *img)
{
    uint16_t *ptr = img->buffer;
    size_t size = (size_t) img->width * img->height;
    for (size_t i = 0; i < size; i++) {
        uint16_t alpha = ~ptr[3];
        if (alpha) {
            const uint64_t offs = (uint64_t) 1 << 32;
            uint64_t inv = ((uint64_t) 65535 << 33) / alpha + 1;
            // equivalent to (65535 * ptr[k] + alpha / 2) / alpha
            ptr[0] = (ptr[0] * inv + offs) >> 33;
            ptr[1] = (ptr[1] * inv + offs) >> 33;
            ptr[2] = (ptr[2] * inv + offs) >> 33;
        }
        ptr[3] = alpha;
        ptr += 4;
    }
    return write_png(path, img->width, img->height,
                     8 * img->width, img->buffer, 16);
}

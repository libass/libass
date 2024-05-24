/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../libass/ass.h"
#include <png.h>

typedef struct image_s {
    int width, height, stride;
    unsigned char *buffer;      // RGBA32
} image_t;

ASS_Library *ass_library;
ASS_Renderer *ass_renderer;

void msg_callback(int level, const char *fmt, va_list va, void *data)
{
    if (level > 6)
        return;
    printf("libass: ");
    vprintf(fmt, va);
    printf("\n");
}

static void write_png(char *fname, image_t *img)
{
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_byte **volatile row_pointers = NULL;

    FILE *fp = fopen(fname, "wb");
    if (fp == NULL) {
        printf("PNG Error opening %s for writing!\n", fname);
        goto fail;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        printf("PNG Error creating write struct!\n");
        goto fail;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        printf("PNG Error creating info struct!\n");
        goto fail;
    }

    row_pointers = malloc(img->height * sizeof(png_byte *));
    if (!row_pointers) {
        printf("PNG Failed to allocate row pointers!\n");
        goto fail;
    }
    for (int k = 0; k < img->height; k++)
        row_pointers[k] = img->buffer + img->stride * k;


    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("PNG unknown error!\n");
        goto fail;
    }

    png_init_io(png_ptr, fp);
    png_set_compression_level(png_ptr, 9);

    png_set_IHDR(png_ptr, info_ptr, img->width, img->height,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, info_ptr);

fail:
    free(row_pointers);

    if (png_ptr && !info_ptr)
        png_destroy_write_struct(&png_ptr, NULL);
    else if (png_ptr && info_ptr)
        png_destroy_write_struct(&png_ptr, &info_ptr);

    if (fp)
        fclose(fp);
}

static void init(int frame_w, int frame_h)
{
    ass_library = ass_library_init();
    if (!ass_library) {
        printf("ass_library_init failed!\n");
        exit(1);
    }

    ass_set_message_cb(ass_library, msg_callback, NULL);
    ass_set_extract_fonts(ass_library, 1);

    ass_renderer = ass_renderer_init(ass_library);
    if (!ass_renderer) {
        printf("ass_renderer_init failed!\n");
        exit(1);
    }

    ass_set_storage_size(ass_renderer, frame_w, frame_h);
    ass_set_frame_size(ass_renderer, frame_w, frame_h);
    ass_set_fonts(ass_renderer, NULL, "sans-serif",
                  ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
}

static image_t *gen_image(int width, int height)
{
    image_t *img = malloc(sizeof(image_t));
    img->width = width;
    img->height = height;
    img->stride = width * 4;
    img->buffer = calloc(1, height * width * 4);
    return img;
}

static void blend_single(image_t * frame, ASS_Image *img)
{
    unsigned char r = img->color >> 24;
    unsigned char g = (img->color >> 16) & 0xFF;
    unsigned char b = (img->color >> 8) & 0xFF;
    unsigned char a = 255 - (img->color & 0xFF);

    unsigned char *src = img->bitmap;
    unsigned char *dst = frame->buffer + img->dst_y * frame->stride + img->dst_x * 4;

    for (int y = 0; y < img->h; ++y) {
        for (int x = 0; x < img->w; ++x) {
            unsigned k = ((unsigned) src[x]) * a;
            // For high-quality output consider using dithering instead;
            // this static offset results in biased rounding but is faster
            unsigned rounding_offset = 255 * 255 / 2;
            // If the original frame is not in premultiplied alpha, convert it beforehand or adjust
            // the blending code. For fully-opaque output frames there's no difference either way.
            dst[x * 4 + 0] = (k *   r + (255 * 255 - k) * dst[x * 4 + 0] + rounding_offset) / (255 * 255);
            dst[x * 4 + 1] = (k *   g + (255 * 255 - k) * dst[x * 4 + 1] + rounding_offset) / (255 * 255);
            dst[x * 4 + 2] = (k *   b + (255 * 255 - k) * dst[x * 4 + 2] + rounding_offset) / (255 * 255);
            dst[x * 4 + 3] = (k * 255 + (255 * 255 - k) * dst[x * 4 + 3] + rounding_offset) / (255 * 255);
        }
        src += img->stride;
        dst += frame->stride;
    }
}

static void blend(image_t * frame, ASS_Image *img)
{
    int cnt = 0;
    while (img) {
        blend_single(frame, img);
        ++cnt;
        img = img->next;
    }
    printf("%d images blended\n", cnt);

    // Convert from pre-multiplied to straight alpha
    // (not needed for fully-opaque output)
    for (int y = 0; y < frame->height; y++) {
        unsigned char *row = frame->buffer + y * frame->stride;
        for (int x = 0; x < frame->width; x++) {
            const unsigned char alpha = row[4 * x + 3];
            if (alpha) {
                // For each color channel c:
                //   c = c / (255.0 / alpha)
                // but only using integers and a biased rounding offset
                const uint32_t offs = (uint32_t) 1 << 15;
                uint32_t inv = ((uint32_t) 255 << 16) / alpha + 1;
                row[x * 4 + 0] = (row[x * 4 + 0] * inv + offs) >> 16;
                row[x * 4 + 1] = (row[x * 4 + 1] * inv + offs) >> 16;
                row[x * 4 + 2] = (row[x * 4 + 2] * inv + offs) >> 16;
            }
        }
    }
}

char *font_provider_labels[] = {
    [ASS_FONTPROVIDER_NONE]       = "None",
    [ASS_FONTPROVIDER_AUTODETECT] = "Autodetect",
    [ASS_FONTPROVIDER_CORETEXT]   = "CoreText",
    [ASS_FONTPROVIDER_FONTCONFIG] = "Fontconfig",
    [ASS_FONTPROVIDER_DIRECTWRITE]= "DirectWrite",
};

static void print_font_providers(ASS_Library *ass_library)
{
    int i;
    ASS_DefaultFontProvider *providers;
    size_t providers_size = 0;
    ass_get_available_font_providers(ass_library, &providers, &providers_size);
    printf("test.c: Available font providers (%zu): ", providers_size);
    for (i = 0; i < providers_size; i++) {
        const char *separator = i > 0 ? ", ": "";
        printf("%s'%s'", separator,  font_provider_labels[providers[i]]);
    }
    printf(".\n");
    free(providers);
}

int main(int argc, char *argv[])
{
    int frame_w = 1280;
    int frame_h = 720;

    if (argc != 4 && argc != 6) {
        printf("usage: %s <image file> <subtitle file> <time> "
               "[<storage width> <storage height>]\n",
                argv[0] ? argv[0] : "test");
        exit(1);
    }
    char *imgfile = argv[1];
    char *subfile = argv[2];
    double tm = strtod(argv[3], 0);
    if (argc == 6) {
        frame_w = atoi(argv[4]);
        frame_h = atoi(argv[5]);
        if (frame_w <= 0 || frame_h <= 0) {
            printf("storage size must be non-zero and positive!\n");
            exit(1);
        }
    }

    print_font_providers(ass_library);

    init(frame_w, frame_h);
    ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
    if (!track) {
        printf("track init failed!\n");
        return 1;
    }

    ASS_Image *img =
        ass_render_frame(ass_renderer, track, (int) (tm * 1000), NULL);
    image_t *frame = gen_image(frame_w, frame_h);
    blend(frame, img);

    ass_free_track(track);
    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);

    write_png(imgfile, frame);
    free(frame->buffer);
    free(frame);

    return 0;
}

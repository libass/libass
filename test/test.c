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

static void write_png(char *fname, ASS_Image *img)
{
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_byte **row_pointers;
    int k;

    png_ptr =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);
    fp = NULL;

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    fp = fopen(fname, "wb");
    if (fp == NULL) {
        printf("PNG Error opening %s for writing!\n", fname);
        return;
    }

    png_init_io(png_ptr, fp);
    png_set_compression_level(png_ptr, 0);

    png_set_IHDR(png_ptr, info_ptr, img->w, img->h,
                 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    row_pointers = (png_byte **) malloc(img->h * sizeof(png_byte *));
    for (k = 0; k < img->h; k++)
        row_pointers[k] = img->bitmap + img->stride * k;

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    free(row_pointers);

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

    ass_renderer = ass_renderer_init(ass_library);
    if (!ass_renderer) {
        printf("ass_renderer_init failed!\n");
        exit(1);
    }

    ass_set_frame_size(ass_renderer, frame_w, frame_h);
    ass_set_fonts(ass_renderer, NULL, "sans-serif",
                  ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
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
}

int main(int argc, char *argv[])
{
    const int frame_w = 1280;
    const int frame_h = 720;

    if (argc < 4) {
        printf("usage: %s <image file> <subtitle file> <time>\n", argv[0]);
        exit(1);
    }
    char *imgfile = argv[1];
    char *subfile = argv[2];
    double tm = strtod(argv[3], 0);

    print_font_providers(ass_library);

    init(frame_w, frame_h);
    ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
    if (!track) {
        printf("track init failed!\n");
        return 1;
    }

    ASS_Image *img = ass_render_rgba_frame(ass_renderer, track, (int) (tm * 1000), NULL, 1);

    ass_free_track(track);
    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);

    write_png(imgfile, img);
    ass_frame_unref(img);

    return 0;
}

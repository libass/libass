/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
 * Copyright (C) 2013 Rodger Combs <rcombs@rcombs.me>
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
#include <ass.h>

typedef struct image_s {
    int width, height, stride;
    unsigned char *buffer;      // RGB24
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
    ass_set_fonts(ass_renderer, NULL, "Sans", 1, NULL, 1);
}

static image_t *gen_image(int width, int height)
{
    image_t *img = malloc(sizeof(image_t));
    img->width = width;
    img->height = height;
    img->stride = width * 3;
    img->buffer = (unsigned char *) calloc(1, height * width * 3);
    return img;
}

#define _r(c)  ((c)>>24)
#define _g(c)  (((c)>>16)&0xFF)
#define _b(c)  (((c)>>8)&0xFF)
#define _a(c)  ((c)&0xFF)

static void blend_single(image_t * frame, ASS_Image *img)
{
    int x, y;
    unsigned char opacity = 255 - _a(img->color);
    unsigned char r = _r(img->color);
    unsigned char g = _g(img->color);
    unsigned char b = _b(img->color);

    unsigned char *src;
    unsigned char *dst;

    src = img->bitmap;
    dst = frame->buffer + img->dst_y * frame->stride + img->dst_x * 3;
    for (y = 0; y < img->h; ++y) {
        for (x = 0; x < img->w; ++x) {
            unsigned k = ((unsigned) src[x]) * opacity / 255;
            // possible endianness problems
            dst[x * 3] = (k * b + (255 - k) * dst[x * 3]) / 255;
            dst[x * 3 + 1] = (k * g + (255 - k) * dst[x * 3 + 1]) / 255;
            dst[x * 3 + 2] = (k * r + (255 - k) * dst[x * 3 + 2]) / 255;
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
}

int main(int argc, char *argv[])
{
    const int frame_w = 1280;
    const int frame_h = 720;

    if (argc < 5) {
        printf("usage: %s <subtitle file> <start time> <fps> <time to render>\n", argv[0]);
        exit(1);
    }
    char *subfile = argv[1];
    double tm = strtod(argv[2], 0);
    double fps = strtod(argv[3], 0);
    double time = strtod(argv[4], 0);
    
    if(fps == 0){
        printf("fps cannot equal 0\n");
        exit(1);
    }

    init(frame_w, frame_h);
    ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
    if (!track) {
        printf("track init failed!\n");
        exit(1);
    }
    
    image_t *frame = gen_image(frame_w, frame_h);

    while(tm < time){
        ASS_Image *img =
            ass_render_frame(ass_renderer, track, (int) (tm * 1000), NULL);
        blend(frame, img);
        tm += 1 / fps;
    }

    ass_free_track(track);
    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);

    return 0;
}

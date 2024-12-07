/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
 * Copyright (C) 2013 rcombs <rcombs@rcombs.me>
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
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include "../libass/ass.h"

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
    char fmt_buf[1024];
    snprintf(fmt_buf, sizeof(fmt_buf), "libass: %s\n", fmt);
    vfprintf(stderr, fmt_buf, va);
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
    ass_set_fonts(ass_renderer, NULL, "Sans", 1, NULL, 1);
}

static double gettime(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec + spec.tv_nsec / 1000000000.;
#endif
}

int main(int argc, char *argv[])
{
    const int frame_w = 1280;
    const int frame_h = 720;

    if (argc < 5) {
        printf("usage: %s <subtitle file> <start time> <fps> <end time>\n",
               argv[0] ? argv[0] : "profile");
        exit(1);
    }
    char *subfile = argv[1];
    double tm = strtod(argv[2], 0);
    double fps = strtod(argv[3], 0);
    double end_time = strtod(argv[4], 0);

    if (fps == 0) {
        printf("fps cannot equal 0\n");
        exit(1);
    }

    double start_time = gettime();

    init(frame_w, frame_h);

    double init_time = gettime();

    ASS_Track *track = ass_read_file(ass_library, subfile, NULL);
    if (!track) {
        printf("track init failed!\n");
        exit(1);
    }

    double read_time = gettime();
    double last_frame_time = read_time;

    double first_frame_time;
    double worst_frame = 0;
    bool got_frame = false;
    int frames = 0;

    while (tm < end_time) {
        bool got = ass_render_frame(ass_renderer, track, (int) (tm * 1000), NULL) != NULL;
        tm += 1 / fps;

        double current_time = gettime();
        if (got && !got_frame) {
            first_frame_time = current_time;
            got_frame = true;
        } else {
            double current_frame = current_time - last_frame_time;
            if (current_frame > worst_frame)
                worst_frame = current_frame;
        }

        last_frame_time = current_time;

        frames++;
    }

    ass_free_track(track);
    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);

    double cleanup_time = gettime();

    printf("Timing:\n");
    printf("           init: %f\n", init_time - start_time);
    printf("           read: %f\n", read_time - init_time);
    printf("   total render: %f\n", last_frame_time - read_time);
    if (frames > 0) {
        printf("    first frame: %f\n", first_frame_time - read_time);
        printf("     post-first: %f\n", last_frame_time - first_frame_time);
        printf("    worst frame: %f\n", worst_frame);
    }
    printf("        cleanup: %f\n", cleanup_time - last_frame_time);
    if (frames > 0) {
        printf("      total fps: %f\n", frames / (last_frame_time - read_time));
        printf("     post-1 fps: %f\n", frames / (last_frame_time - first_frame_time));
        printf("     post-1 fps: %f\n", frames / (last_frame_time - first_frame_time));
    }

    return 0;
}

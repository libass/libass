/*
 * Copyright (C) 2026 libass contributors
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

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "ass.h"
#include "ass_types.h"
#include "ass_drawing.h"
#include "ass_outline.h"

#define FUZZMODE_STANDALONE       0
#define FUZZMODE_AFLXX_SHAREDMEM  1
#define FUZZMODE_LIBFUZZER        2
#ifndef ASS_FUZZMODE
    #define ASS_FUZZMODE FUZZMODE_STANDALONE
#endif

#ifdef ASSFUZZ_MAX_LEN
    #define LEN_IN_RANGE(s) ((s) <= (ASSFUZZ_MAX_LEN))
#else
    #define LEN_IN_RANGE(s) true
#endif

#define STR_(x) #x
#define STR(x) STR_(x)

static ASS_Library *ass_library = NULL;
static ASS_Renderer *ass_renderer = NULL;
static bool quiet = false;
static uint8_t hval = 0;

static const int RWIDTH  = 854;
static const int RHEIGHT = 480;

static void msg_callback(int level, const char *fmt, va_list va, void *data)
{
#if ASS_FUZZMODE == FUZZMODE_STANDALONE
    if (level > 6 || quiet) return;
    printf("libass: ");
    vprintf(fmt, va);
    printf("\n");
#else
    char msg[2048];
    int l = vsnprintf(msg, sizeof(msg), fmt, va) - 1;
    l = l >= sizeof(msg) ? sizeof(msg) - 1 : l;
    l = l < 0 ? 0 : l;
    hval ^= *(msg + l);
#endif
}

static bool init_renderer(void)
{
    if (ass_renderer)
        return true;

    ass_renderer = ass_renderer_init(ass_library);
    if (!ass_renderer)
        return false;

    ass_set_fonts(ass_renderer, NULL, "sans-serif",
                  ASS_FONTPROVIDER_AUTODETECT, NULL, 1);
    ass_set_frame_size(ass_renderer, RWIDTH, RHEIGHT);
    ass_set_storage_size(ass_renderer, RWIDTH, RHEIGHT);
    ass_set_cache_limits(ass_renderer, 1000, 32);

    return true;
}

static bool init(void)
{
    ass_library = ass_library_init();
    if (!ass_library)
        return false;

    ass_set_message_cb(ass_library, msg_callback, NULL);

    if (!init_renderer()) {
        ass_library_done(ass_library);
        ass_library = NULL;
        return false;
    }

    return true;
}

static inline void process_image(ASS_Image *imgs)
{
    for (; imgs; imgs = imgs->next) {
        assert(imgs->w >= 0 && imgs->h >= 0 &&
               imgs->dst_x >= 0 && imgs->dst_y >= 0 &&
               imgs->dst_x + imgs->w <= RWIDTH &&
               imgs->dst_y + imgs->h <= RHEIGHT &&
               imgs->stride >= imgs->w);
        if (imgs->w && imgs->h)
            hval ^= *(imgs->bitmap + imgs->stride * (imgs->h - 1) + imgs->w - 1);
    }
}

static void consume_track(ASS_Renderer *renderer, ASS_Track *track)
{
    // Sanity limit the scale to prevent massive memory allocations
    if (track->PlayResY < 100)
        track->PlayResY = 100;
    if (track->PlayResX < 100)
        track->PlayResX = 100;

    int max_events = track->n_events > 30 ? 30 : track->n_events;
    for (int n = 0; n < max_events; ++n) {
        int change;
        ASS_Event event = track->events[n];
        process_image(ass_render_frame(renderer, track, event.Start, &change));
        if (event.Duration > 1) {
            process_image(ass_render_frame(renderer, track, event.Start + event.Duration / 2, &change));
            process_image(ass_render_frame(renderer, track, event.Start + event.Duration - 1, &change));
        }
    }
}

static void fuzz_drawing_input(const uint8_t *data, size_t size)
{
    char *draw_str = malloc(size + 1);
    if (!draw_str)
        return;
    memcpy(draw_str, data, size);
    draw_str[size] = '\0';

    // 1. Direct outline parser execution (fuzzes all drawing commands, beziers, and splines)
    ASS_Outline outline = {0};
    ASS_Rect cbox = {0};
    bool valid = ass_drawing_parse(&outline, &cbox, draw_str, ass_library);
    if (valid) {
        ass_outline_free(&outline);
    }

    // Check bounding box before rendering to prevent pathological multi-gigabyte allocations
    int64_t w_d6 = (int64_t)cbox.x_max - cbox.x_min;
    int64_t h_d6 = (int64_t)cbox.y_max - cbox.y_min;
    bool bbox_safe = valid && w_d6 >= 0 && h_d6 >= 0 &&
                     (w_d6 >> 6) <= 4096 && (h_d6 >> 6) <= 4096;

    // 2. Full render pipeline via synthetic ASS dialogue (only for reasonable bounding boxes)
    if (bbox_safe) {
        static const char header[] =
            "[Script Info]\n"
            "PlayResX: 640\n"
            "PlayResY: 360\n"
            "[V4+ Styles]\n"
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
            "Style: Default,sans-serif,20,&H0000FFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,0,5,10,10,10,1\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,{\\p1}";
        static const char footer[] = "{\\p0}\n";

        size_t script_len = sizeof(header) - 1 + size + sizeof(footer);
        char *script = malloc(script_len);
        if (script) {
            memcpy(script, header, sizeof(header) - 1);
            memcpy(script + sizeof(header) - 1, draw_str, size);
            memcpy(script + sizeof(header) - 1 + size, footer, sizeof(footer));

            ASS_Track *track = ass_read_memory(ass_library, script, script_len - 1, NULL);
            if (track) {
                consume_track(ass_renderer, track);
                ass_free_track(track);
            }
            free(script);
        }
    }

    free(draw_str);
}

#if ASS_FUZZMODE == FUZZMODE_STANDALONE
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: %s <drawing text file>\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        return 1;
    }
    uint8_t *buf = malloc(sz);
    if (!buf) {
        fclose(f);
        return 1;
    }
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    if (!init()) {
        free(buf);
        return 1;
    }

    fuzz_drawing_input(buf, sz);

    free(buf);
    if (ass_renderer) ass_renderer_done(ass_renderer);
    if (ass_library)  ass_library_done(ass_library);
    return 0;
}
#elif ASS_FUZZMODE == FUZZMODE_LIBFUZZER
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
#ifdef ASSFUZZ_FONTCONFIG_SYSROOT
    setenv("FONTCONFIG_SYSROOT", STR(ASSFUZZ_FONTCONFIG_SYSROOT), 1);
#endif
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!LEN_IN_RANGE(size))
        return 0;

    if (!init())
        exit(1);

    fuzz_drawing_input(data, size);

    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);
    ass_renderer = NULL;
    ass_library = NULL;

    return 0;
}
#elif ASS_FUZZMODE == FUZZMODE_AFLXX_SHAREDMEM
__AFL_FUZZ_INIT();
#pragma clang optimize off
#pragma GCC   optimize("O0")
int main(int argc, char *argv[])
{
    ssize_t len;
    unsigned char *buf;
#ifdef ASSFUZZ_FONTCONFIG_SYSROOT
    setenv("FONTCONFIG_SYSROOT", STR(ASSFUZZ_FONTCONFIG_SYSROOT), 1);
#endif
    if (!init()) return 1;
    __AFL_INIT();
    buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(100000)) {
        len = __AFL_FUZZ_TESTCASE_LEN;
        if (!LEN_IN_RANGE(len)) continue;
        if (!init_renderer()) continue;
        fuzz_drawing_input(buf, len);
        ass_renderer_done(ass_renderer);
        ass_renderer = NULL;
        ass_clear_fonts(ass_library);
    }
    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);
    return 0;
}
#else
    #error Unknown fuzzer mode selected!
#endif

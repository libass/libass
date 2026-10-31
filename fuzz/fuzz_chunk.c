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
    for (int n = 0; n < track->n_events; ++n) {
        int change;
        ASS_Event event = track->events[n];
        process_image(ass_render_frame(renderer, track, event.Start, &change));
        if (event.Duration > 1) {
            process_image(ass_render_frame(renderer, track, event.Start + event.Duration / 2, &change));
            process_image(ass_render_frame(renderer, track, event.Start + event.Duration - 1, &change));
        }
    }
}

static void fuzz_chunk_input(const uint8_t *data, size_t size)
{
    if (size < 4)
        return;

    uint8_t flags = data[0];
    int check_readorder = (flags & 1);

    uint16_t priv_len = ((uint16_t)data[1] << 8) | data[2];
    size_t rem = size - 3;
    if (priv_len > rem)
        priv_len = rem / 2;

    const char *priv_data = (const char *)(data + 3);
    const char *chunks_data = (const char *)(data + 3 + priv_len);
    size_t chunks_len = rem - priv_len;

    ASS_Track *track = ass_new_track(ass_library);
    if (!track)
        return;

    ass_set_check_readorder(track, check_readorder);

    if (priv_len > 0)
        ass_process_codec_private(track, (char *)priv_data, priv_len);

    // Process remainder as newline-separated chunks
    size_t pos = 0;
    long long timecode = 0;
    while (pos < chunks_len) {
        const char *chunk_start = chunks_data + pos;
        const char *nl = memchr(chunk_start, '\n', chunks_len - pos);
        size_t chunk_size = nl ? (size_t)(nl - chunk_start) : (chunks_len - pos);

        if (chunk_size > 0) {
            long long duration = 1000 + (long long)(chunk_size * 50);
            ass_process_chunk(track, (char *)chunk_start, (int)chunk_size, timecode, duration);
            timecode += 1000;
        }

        pos += chunk_size + (nl ? 1 : 0);
    }

    consume_track(ass_renderer, track);
    ass_free_track(track);
}

#if ASS_FUZZMODE == FUZZMODE_STANDALONE
int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: %s <input file>\n", argv[0]);
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

    fuzz_chunk_input(buf, sz);

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

    fuzz_chunk_input(data, size);

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
        fuzz_chunk_input(buf, len);
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

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

// Limit maximal processed size in continuous fuzzing;
// to be used when the fuzzing setup doesn't expose its own max size control.
// Has no effect in standalone builds
#ifdef ASSFUZZ_MAX_LEN
    #define LEN_IN_RANGE(s) ((s) <= (ASSFUZZ_MAX_LEN))
#else
    #define LEN_IN_RANGE(s) true
#endif

#define STR_(x) #x
#define STR(x) STR_(x)

// MSAN: will trigger MSAN if any pixel in bitmap not written to (costly)
#ifndef ASSFUZZ_HASH_WHOLEBITMAP
    #define ASSFUZZ_HASH_WHOLEBITMAP 0
#endif

ASS_Library *ass_library = NULL;
ASS_Renderer *ass_renderer = NULL;
bool quiet = false;

uint8_t hval = 0;

#if ASSFUZZ_HASH_WHOLEBITMAP
static inline void hash(const void *buf, size_t len)
{
    const uint8_t *ptr = buf;
    const uint8_t *end = ptr + len;
    while (ptr < end)
        hval ^= *ptr++;
    // MSAN doesn't trigger on the XORs, but will on conditional branches
    if (hval)
        hval ^= 57;
}
#endif

void msg_callback(int level, const char *fmt, va_list va, void *data)
{
#if ASS_FUZZMODE == FUZZMODE_STANDALONE
    if (level > 6 || quiet) return;
    printf("libass: ");
    vprintf(fmt, va);
    printf("\n");
#else
    // still check for type-mismatches even when not printing
    // (seems to be cheap enough from some simple perormance tests)
    char msg[2048];
    int l = vsnprintf(msg, sizeof(msg), fmt, va) - 1;
    l = l >= sizeof(msg) ? sizeof(msg) - 1 : l;
    l = l < 0 ? 0 : l;
    hval ^= *(msg + l);
#endif
}

static const int RWIDTH  = 854;
static const int RHEIGHT = 480;

// libass scales the script coordinate system (PlayRes) up to the storage
// size, so a hostile tiny PlayRes would blow up glyph outlines and
// rasterizer scratch buffers (and thus transient allocations) accordingly;
// clamp it to keep the maximum upscale factor bounded.
static const int MIN_PLAYRES = 100;

// Cap the amount of render work done per input
static const int MAX_RENDERED_EVENTS = 30;

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
    if (!ass_library) {
        printf("ass_library_init failed!\n");
        return false;
    }

    ass_set_message_cb(ass_library, msg_callback, NULL);
    ass_set_extract_fonts(ass_library, 1);

    if (!init_renderer()) {
        ass_library_done(ass_library);
        ass_library = NULL;
        printf("ass_renderer_init failed!\n");
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
#if !ASSFUZZ_HASH_WHOLEBITMAP
        // Check last pixel to probe for out-of-bounds errors
        if (imgs->w && imgs->h)
            hval ^= *(imgs->bitmap + imgs->stride * (imgs->h - 1) + imgs->w - 1);
#else
        unsigned char *src = imgs->bitmap;
        for (int y = 0; y < imgs->h; ++y) {
            hash(src, imgs->w);
            src += imgs->stride;
        }
#endif
    }
}

static void consume_track(ASS_Renderer *renderer, ASS_Track *track)
{
    // See MIN_PLAYRES above
    if (track->PlayResY < MIN_PLAYRES)
        track->PlayResY = MIN_PLAYRES;
    if (track->PlayResX < MIN_PLAYRES)
        track->PlayResX = MIN_PLAYRES;

    int max_events = track->n_events > MAX_RENDERED_EVENTS
                         ? MAX_RENDERED_EVENTS
                         : track->n_events;
    for (int n = 0; n < max_events; ++n) {
        int change;
        ASS_Event event = track->events[n];
        process_image(ass_render_frame(ass_renderer, track, event.Start, &change));
        if (event.Duration > 1) {
            process_image(ass_render_frame(ass_renderer, track, event.Start + event.Duration/2, &change));
            process_image(ass_render_frame(ass_renderer, track, event.Start + event.Duration-1, &change));
        }
    }
}

/*
 * Extra configuration exercised with each input, selected by the final byte
 * of the input. It is stripped off before the remaining bytes are handed to
 * the parser (see consume_input()), so the input itself stays an ordinary
 * subtitle file (in particular, byte 0 remains the file's actual first byte).
 */
#define FUZZCFG_SHAPER_SIMPLE   (1 << 0)
#define FUZZCFG_STYLE_OVERRIDES (1 << 1)
#define FUZZCFG_MARGINS         (1 << 2)

/*
 * \brief Parse (all but the final byte of) an input and render the result.
 *
 * The final input byte selects FUZZCFG_* configuration; everything before it
 * is parsed as an ordinary subtitle file. The -o writeout mode does not use
 * this convention and always parses its input as-is.
 */
static void consume_input(const uint8_t *data, size_t size)
{
    uint8_t flags = size > 0 ? data[size - 1] : 0;
    if (size > 0)
        --size;

    ass_set_shaper(ass_renderer, (flags & FUZZCFG_SHAPER_SIMPLE) ?
                                       ASS_SHAPING_SIMPLE : ASS_SHAPING_COMPLEX);

    // Style overrides are sticky on the library, so explicitly clear them
    // when unset (relevant for the persistent-library fuzzing modes)
    static char *overrides[] = {
        "FontSize=24", "ScaledBorderAndShadow=yes", NULL
    };
    ass_set_style_overrides(ass_library,
                            (flags & FUZZCFG_STYLE_OVERRIDES) ? overrides : NULL);

    if (flags & FUZZCFG_MARGINS) {
        ass_set_line_spacing(ass_renderer, 4.0);
        ass_set_line_position(ass_renderer, 50.0);
        ass_set_margins(ass_renderer, 10, 10, 10, 10);
    }

    ASS_Track *track = ass_read_memory(ass_library, (char *)data, size, NULL);
    if (!track)
        return;

    if (flags & FUZZCFG_STYLE_OVERRIDES)
        ass_process_force_style(track);

    consume_track(ass_renderer, track);
    ass_free_track(track);
}

#if ASS_FUZZMODE == FUZZMODE_STANDALONE
#include "writeout.h"

struct settings {
    enum {
        CONSUME_INPUT,
        WRITEOUT_TRACK
    } mode;
    const char *input; // path or "-" for stdin
    const char *output; // path or NULL for tmp file
};

#ifdef _WIN32
#define READ_RET int
#else
#define READ_RET ssize_t
#endif

/**
 * \brief Read raw bytes from stdin until EOF
 * \param out_len will be set to the number of bytes read
 * \return NUL-terminated buffer to be free()d by the caller, or NULL on error
 */
static uint8_t *read_stdin_bytes(size_t *out_len)
{
    size_t smax = 4096;
    uint8_t *buf = malloc(smax);
    if (!buf)
        goto error;
    size_t s = 0;
    READ_RET read_b = 0;
    do {
        // AFL++ docs recommend using raw file descriptors
        // to avoid buffering issues with stdin
        read_b = read(fileno(stdin), buf + s, smax - s);
        s += read_b > 0 ? read_b : 0;
        if (s == smax) {
            size_t new_smax = smax > SIZE_MAX / 2 ? SIZE_MAX : smax * 2;
            uint8_t *new_buf = realloc(buf, new_smax);
            if (!new_buf || new_smax <= smax) {
                free(new_buf ? new_buf : buf);
                goto error;
            }
            smax = new_smax;
            buf = new_buf;
        }
    } while (read_b > 0);
    buf[s] = '\0';
    *out_len = s;
    return buf;
error:
    return NULL;
}

static ASS_Track *read_track_from_stdin(void)
{
    size_t s;
    uint8_t *buf = read_stdin_bytes(&s);
    if (!buf) {
        printf("Input too large!\n");
        return NULL;
    }

    ASS_Track *track = ass_read_memory(ass_library, (char *)buf, s, NULL);
    free(buf);
    return track;
}

/**
 * \brief Read a whole file into memory
 * \param out_len will be set to the file size in bytes
 * \return buffer to be free()d by the caller, or NULL on error
 */
static uint8_t *read_file_bytes(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buf = malloc(sz);
    if (!buf || (sz > 0 && fread(buf, 1, sz, f) != (size_t)sz)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);

    *out_len = sz;
    return buf;
}

/**
 * \param argc
 * \param argv
 * \param settings will be filled according to parsed args or defaults
 * \return whether CLI args could be parsed successfully
 */
static bool parse_cmdline(int argc, char *argv[], struct settings *settings)
{
    // defaults
    settings->mode = CONSUME_INPUT;
    settings->input = NULL;
    settings->output = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        const char *param = argv[i];
        if (!param || param[0] != '-' || !param[1])
            goto no_more_args;

        switch (param[1]) {
        case 'q':
            quiet = true;
            break;

        case 'o':
            settings->mode = WRITEOUT_TRACK;
            // optional argument
            if (argc - i > 1 && argv[i + 1][0] != '-') {
                settings->output = argv[i + 1];
                i++;
            }
            break;

        case '-':
            if (param[2]) {
                return false;
            } else {
                i++;
                goto no_more_args;
            }

        default:
            return false;
        }

        continue;

no_more_args:
        break;
    }

    if (argc < 2 || argc - i > 1 || argc == i)
        return false;

    settings->input = argv[argc - 1];
    return !!settings->input;
}

int main(int argc, char *argv[])
{
    /* Default failure code of sanitisers is 1, unless
     * changed via env (A|UB|..)SAN_OPTIONS=exitcode=21
     * Except, LLVM's UBSAN always exits with 0 (unless using
     * -fsanitize-undefined-trap-on-error which will SIGILL without an
     * error report being printed), see https://reviews.llvm.org/D35085
     */
    enum {
        FUZZ_OK = 0,
        //SANITISER_FAIL = 1,
        // Invalid parameters passed etc
        FUZZ_BAD_USAGE = 2,
        // Error before rendering starts
        FUZZ_INIT_ERR = 0
    };

    ASS_Track *track = NULL;
    int retval = FUZZ_OK;

    struct settings settings;
    if (!parse_cmdline(argc, argv, &settings)) {
        printf("usage: %s [-q] [-o [output_file]] [--] <subtitle file>\n"
               "  -q:\n"
               "    Hide libass log messages\n"
               "\n"
               "  -o [FILE]:\n"
               "    Write out parsed file content in a standardized form\n"
               "    into FILE or if omitted a generated temporary file.\n"
               "    If used the input file will not be processed, only parsed.\n"
               "\n"
               "  Without -o the input is rendered; its final byte selects\n"
               "    additional renderer configuration to exercise (this makes\n"
               "    it usable as a drop-in reproducer for the fuzz targets).\n",
               argc ? argv[0] : "fuzz");
        return FUZZ_BAD_USAGE;
    }

    if (!init()) {
        printf("library init failed!\n");
        retval = FUZZ_INIT_ERR;
        goto cleanup;
    }

    if (settings.mode == WRITEOUT_TRACK) {
        if (strcmp(settings.input, "-"))
            track = ass_read_file(ass_library, settings.input, NULL);
        else
            track = read_track_from_stdin();

        if (!track) {
            printf("track init failed!\n");
            retval = FUZZ_INIT_ERR;
            goto cleanup;
        }

        write_out_track(track, settings.output);
    } else {
        // Rendering path: the final input byte selects the configuration to
        // exercise, matching the behaviour of the libFuzzer/AFL++ targets
        size_t in_len;
        uint8_t *in_bytes;
        if (strcmp(settings.input, "-"))
            in_bytes = read_file_bytes(settings.input, &in_len);
        else
            in_bytes = read_stdin_bytes(&in_len);

        if (!in_bytes) {
            printf("could not read input!\n");
            retval = FUZZ_INIT_ERR;
            goto cleanup;
        }

        consume_input(in_bytes, in_len);
        free(in_bytes);
    }

cleanup:
    if (track)        ass_free_track(track);
    if (ass_renderer) ass_renderer_done(ass_renderer);
    if (ass_library)  ass_library_done(ass_library);

    return retval;
}
#elif ASS_FUZZMODE == FUZZMODE_AFLXX_SHAREDMEM
__AFL_FUZZ_INIT();
/*
 * AFL++ docs recommend to disable optimisation for the main function
 * and GCC and Clang are the only AFL compilers.
 */
#pragma clang optimize off
#pragma GCC   optimize("O0")
int main(int argc, char *argv[])
{
    // AFLs buffer and length macros should not be used directly
    ssize_t len;
    unsigned char *buf;

#ifdef ASSFUZZ_FONTCONFIG_SYSROOT
    setenv("FONTCONFIG_SYSROOT", STR(ASSFUZZ_FONTCONFIG_SYSROOT), 1);
#endif

    if (!init()) {
        printf("library init failed!\n");
        return 1;
    }

    __AFL_INIT();
    buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(100000)) {
        len = __AFL_FUZZ_TESTCASE_LEN;
        if (!LEN_IN_RANGE(len))
            continue;

        if (!init_renderer()) {
            printf("Failing renderer init, skipping a sample!\n");
            continue;
        }

        consume_input(buf, len);

        ass_renderer_done(ass_renderer);
        ass_renderer = NULL;
        ass_clear_fonts(ass_library);
    }

    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);

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
    // OSS Fuzz docs recommend just returning 0 on too large input
    // libFuzzer docs tell us to use -1 to prevent addition to corpus
    // BUT HongFuzz' LLVMFuzzerTestOneInput hook can't handle it and
    // neither does OSS Fuzz convert this in their wrapper, see:
    // https://github.com/google/oss-fuzz/issues/11983
    if (!LEN_IN_RANGE(size))
        return 0;

    if (!init())
        exit(1);

    consume_input(data, size);

    ass_renderer_done(ass_renderer);
    ass_library_done(ass_library);
    ass_renderer = NULL;
    ass_library = NULL;

    return 0;
}
#else
    #error Unknown fuzzer mode selected!
#endif

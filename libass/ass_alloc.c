/*
 * Copyright (C) 2020 libass contributors
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

#include "config.h"
#include "ass_compat.h"

#include <stdlib.h>
#include <string.h>

#include "ass_alloc.h"
#include "ass_utils.h"

// strdup and strndup are only part of ISO C since C2x; libass only requires C99
#ifndef HAVE_STRDUP
char *ass_strdup_replacement(const char *str)
{
    size_t len    = strlen(s) + 1;
    char *new_str = malloc(len);
    if (new_str)
        memcpy(new_str, str, len);
    return new_str;
}
#define strdup ass_strdup_replacement
#endif

#ifndef HAVE_STRNDUP
char *ass_strndup_replacement(const char *s, size_t n)
{
    char *end = memchr(s, 0, n);
    size_t len = end ? end - s : n;
    char *new = len < SIZE_MAX ? malloc(len + 1) : NULL;
    if (new) {
        memcpy(new, s, len);
        new[len] = 0;
    }
    return new;
}
#define strndup ass_strndup_replacement
#endif


// Following macros expect fail_msg and lib to be defined and initialised
#ifndef NDEBUG
    #define ASS_ALLOC_TEST(ptr) \
        if (!ptr) \
            ass_msg(lib, MSGL_ERR, fail_msg)
#else
    #define ASS_ALLOC_TEST(ptr) do { } while (0)
#endif

#define ASS_RELAY_ALLOC(fun, ...)       \
    void *new_ptr = (fun)(__VA_ARGS__); \
    ASS_ALLOC_TEST(new_ptr);            \
    return new_ptr

void *ass_malloc_fun(const char *fail_msg, ASS_Library *lib,
                     size_t size)
{
    ASS_RELAY_ALLOC(malloc, size);
}

void *ass_calloc_fun(const char *fail_msg, ASS_Library *lib,
                     size_t n, size_t memb_s)
{
    ASS_RELAY_ALLOC(calloc, n, memb_s);
}
void *ass_realloc_fun(const char *fail_msg, ASS_Library *lib,
                      void *prev, size_t new_size)
{
    ASS_RELAY_ALLOC(realloc, prev, new_size);
}

char *ass_strdup_fun(const char *fail_msg, ASS_Library *lib,
                     const char *str)
{
    ASS_RELAY_ALLOC(strdup, str);
}

char *ass_strndup_fun(const char *fail_msg, ASS_Library *lib,
                      const char *str, size_t size)
{
    ASS_RELAY_ALLOC(strndup, str, size);
}

void ass_free(void *ptr)
{
    free(ptr);
}

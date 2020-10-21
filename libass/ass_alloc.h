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

#ifndef LIBASS_ALLOC_H
#define LIBASS_ALLOC_H

#include "ass_types.h"

void *ass_malloc_fun(const char *fail_msg, ASS_Library *lib, size_t size);
void *ass_calloc_fun(const char *fail_msg, ASS_Library *lib, size_t nmemb, size_t smemb);
void *ass_realloc_fun(const char *fail_msg, ASS_Library *lib, void *ptr, size_t new_size);
char *ass_strdup_fun(const char *fail_msg, ASS_Library *lib, const char *str);
char *ass_strndup_fun(const char *fail_msg, ASS_Library *lib, const char *str, size_t size);

#define TO_STRING_(str) #str
#define TO_STRING(str)  TO_STRING_(str)
#define OOM_MESSAGE     "Allocation failed at " __FILE__ ":" TO_STRING(__LINE__)

#define ASS_MALLOC(l, s)     ass_malloc_fun (OOM_MESSAGE, l, s)
#define ASS_CALLOC(l, n, s)  ass_calloc_fun (OOM_MESSAGE, l, n, s)
#define ASS_REALLOC(l, p, s) ass_realloc_fun(OOM_MESSAGE, l, p, s)
#define ASS_STRDUP(l, s)     ass_strdup_fun (OOM_MESSAGE, l, s)
#define ASS_STRNDUP(l, s, n) ass_strndup_fun(OOM_MESSAGE, l, s, n)

void ass_free(void *ptr);

#endif /* LIBASS_ALLOC_H */

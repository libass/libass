/*
 * Copyright (C) 2015 Oleg Oshmyan <chortos@inbox.lv>
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

#ifndef LIBASS_COMPAT_H
#define LIBASS_COMPAT_H

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#define _USE_MATH_DEFINES
#define inline __inline
#endif

// Work around build failures on Windows with static FriBidi.
// Possible because we only use FriBidi functions, not objects.
#if defined(_WIN32) && !defined(FRIBIDI_LIB_STATIC)
#define FRIBIDI_LIB_STATIC
#endif

#ifndef HAVE_STRDUP
char *ass_strdup_fallback(const char *s); // definition in ass_utils.c
#define strdup ass_strdup_fallback
#endif

#ifndef HAVE_STRNDUP
#include <stddef.h>
char *ass_strndup_fallback(const char *s, size_t n); // definition in ass_utils.c
#define strndup ass_strndup_fallback
#endif

#endif                          /* LIBASS_COMPAT_H */

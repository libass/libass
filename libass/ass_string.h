/*
 * Copyright (C) 2015 Grigori Goronzy <greg@kinoho.net>
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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef ASS_STRING_H
#define ASS_STRING_H

int ass_strcasecmp(const char *s1, const char *s2);
int ass_strncasecmp(const char *s1, const char *s2, size_t n);

static inline int ass_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
           c == '\f' || c == '\r';
}

static inline int ass_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

// ASS_StringView struct and utilities

typedef struct {
    const char *str;
    size_t len;
} ASS_StringView;

#define ASS_SV(lit) (ASS_StringView){ lit, sizeof(lit) - 1 }

static inline char *ass_copy_string(ASS_StringView src)
{
    char *buf = malloc(src.len + 1);
    if (buf) {
        memcpy(buf, src.str, src.len);
        buf[src.len] = '\0';
    }
    return buf;
}

static inline char ass_sv_peekc(ASS_StringView str)
{
    if (!str.len)
        return 0;

    return *str.str;
}

static inline ASS_StringView ass_sv_peek(ASS_StringView str, size_t n)
{
    if (n > str.len)
        n = str.len;

    return (ASS_StringView){ str.str, n };
}

static inline char ass_sv_getc(ASS_StringView* str)
{
    if (!str->len)
        return 0;

    str->len--;
    return *str->str++;
}

static inline ASS_StringView ass_sv_get(ASS_StringView* str, size_t n)
{
    if (n > str->len)
        n = str->len;

    ASS_StringView ret = { str->str, n };

    str->str += n;
    str->len -= n;

    return ret;
}

static inline bool ass_string_equal(ASS_StringView str1, ASS_StringView str2)
{
    return str1.len == str2.len && !memcmp(str1.str, str2.str, str1.len);
}

#define ASS_SV_EQ(view, lit) ass_string_equal(view, ASS_SV(lit))

static inline bool ass_sv_equal_cstr(ASS_StringView str1, const char *str2)
{
    return !strncmp(str1.str, str2, str1.len) && str2[str1.len] == 0;
}

// Check if str1 starts with str2
static inline bool ass_sv_startswith(ASS_StringView str1, ASS_StringView str2)
{
    return str1.len >= str2.len && !memcmp(str1.str, str2.str, str2.len);
}

#define ASS_SV_STARTSWITH(view, lit) ass_sv_startswith(view, ASS_SV(lit))

static inline bool ass_sv_iequal(ASS_StringView str1, ASS_StringView str2)
{
    return str1.len == str2.len && !ass_strncasecmp(str1.str, str2.str, str1.len);
}

#define ASS_SV_IEQ(view, lit) ass_sv_iequal(view, ASS_SV(lit))

bool ass_sv_iequal_cstr(ASS_StringView str1, const char *str2);

static inline bool ass_sv_istartswith(ASS_StringView str, ASS_StringView check)
{
    return ass_sv_iequal(ass_sv_peek(str, check.len), check);
}

#define ASS_SV_ISTARTSWITH(view, lit) ass_sv_istartswith(view, ASS_SV(lit))

#endif

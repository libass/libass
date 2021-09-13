/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
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

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>

#include "ass_library.h"
#include "ass.h"
#include "ass_utils.h"
#include "ass_string.h"

#if (defined(__i386__) || defined(__x86_64__)) && CONFIG_ASM

#include "x86/cpuid.h"

int has_sse2(void)
{
    uint32_t eax = 1, ebx, ecx, edx;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    return (edx >> 26) & 0x1;
}

int has_avx(void)
{
    uint32_t eax = 1, ebx, ecx, edx;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    if (!(ecx & (1 << 27))) // not OSXSAVE
        return 0;
    uint32_t misc = ecx;
    ass_get_xgetbv(0, &eax, &edx);
    if ((eax & 0x6) != 0x6)
        return 0;
    eax = 0;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    return (ecx & 0x6) == 0x6 ? (misc >> 28) & 0x1 : 0; // check high bits are relevant, then AVX support
}

int has_avx2(void)
{
    uint32_t eax = 7, ebx, ecx, edx;
    ass_get_cpuid(&eax, &ebx, &ecx, &edx);
    return (ebx >> 5) & has_avx();
}

#endif // ASM

// Fallbacks
#ifndef HAVE_STRDUP
char *ass_strdup_fallback(const char *str)
{
    size_t len    = strlen(str) + 1;
    char *new_str = malloc(len);
    if (new_str)
        memcpy(new_str, str, len);
    return new_str;
}
#endif

#ifndef HAVE_STRNDUP
char *ass_strndup_fallback(const char *s, size_t n)
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
#endif

void *ass_aligned_alloc(size_t alignment, size_t size, bool zero)
{
    assert(!(alignment & (alignment - 1))); // alignment must be power of 2
    if (size >= SIZE_MAX - alignment - sizeof(void *))
        return NULL;
    char *allocation = zero ? calloc(size + sizeof(void *) + alignment - 1, 1)
                            : malloc(size + sizeof(void *) + alignment - 1);
    if (!allocation)
        return NULL;
    char *ptr = allocation + sizeof(void *);
    unsigned int misalign = (uintptr_t)ptr & (alignment - 1);
    if (misalign)
        ptr += alignment - misalign;
    *((void **)ptr - 1) = allocation;
    return ptr;
}

void ass_aligned_free(void *ptr)
{
    if (ptr)
        free(*((void **)ptr - 1));
}

/**
 * This works similar to realloc(ptr, nmemb * size), but checks for overflow.
 *
 * Unlike some implementations of realloc, this never acts as a call to free().
 * If the total size is 0, it is bumped up to 1. This means a NULL return always
 * means allocation failure, and the unportable realloc(0, 0) case is avoided.
 */
void *ass_realloc_array(void *ptr, size_t nmemb, size_t size)
{
    if (nmemb > (SIZE_MAX / size))
        return NULL;
    size *= nmemb;
    if (size < 1)
        size = 1;

    return realloc(ptr, size);
}

/**
 * Like ass_realloc_array(), but:
 * 1. on failure, return the original ptr value, instead of NULL
 * 2. set errno to indicate failure (errno!=0) or success (errno==0)
 */
void *ass_try_realloc_array(void *ptr, size_t nmemb, size_t size)
{
    void *new_ptr = ass_realloc_array(ptr, nmemb, size);
    if (new_ptr) {
        errno = 0;
        return new_ptr;
    } else {
        errno = ENOMEM;
        return ptr;
    }
}

void skip_spaces(const char **str)
{
    const char *p = *str;
    while ((*p == ' ') || (*p == '\t'))
        ++p;
    *str = p;
}

void vskip_spaces(ASS_StringView *str)
{
    while (str->len > 0 && ((*str->str == ' ') || (*str->str == '\t')))
        ++str->str, --str->len;
}

void rskip_spaces(const char **str, const char *limit)
{
    const char *p = *str;
    while ((p > limit) && ((p[-1] == ' ') || (p[-1] == '\t')))
        --p;
    *str = p;
}

void vrskip_spaces(ASS_StringView *str)
{
    while (str->len > 0 && ((str->str[str->len - 1] == ' ') ||
                            (str->str[str->len - 1] == '\t')))
        --str->len;
}

static int read_digits(ASS_StringView *str, unsigned base, uint32_t *res)
{
    ASS_StringView start = *str;
    uint32_t val = 0;

    while (1) {
        unsigned digit;
        char c = ass_sv_peekc(*str);

        if (c >= '0' && c < FFMIN(base, 10) + '0')
            digit = c - '0';
        else if (c >= 'a' && c < base - 10 + 'a')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c < base - 10 + 'A')
            digit = c - 'A' + 10;
        else
            break;
        val = val * base + digit;
        ass_sv_getc(str);
    }

    *res = val;
    return str->str != start.str;
}

/**
 * \brief Convert a string to an integer reduced modulo 2**32
 * Follows the rules for strtoul but reduces the number modulo 2**32
 * instead of saturating it to 2**32 - 1.
 */
static int mystrtoi32_modulo(ASS_StringView *p, unsigned base, int32_t *res)
{
    // This emulates scanf with %d or %x format as it works on
    // Windows, because that's what is used by VSFilter. In practice,
    // scanf works the same way on other platforms too, but
    // the standard leaves its behavior on overflow undefined.

    // Unlike scanf and like strtoul, produce 0 for invalid inputs.

    ASS_StringView start = *p;
    int sign = 1;

    vskip_spaces(p);

    if (*p->str == '+')
        ++p->str, --p->len;
    else if (*p->str == '-')
        sign = -1, ++p->str, --p->len;

    uint32_t ret = 0;
    if (read_digits(p, base, &ret)) {
        *res = ret * sign;
        return 1;
    } else {
        *p = start;
        return 0;
    }
}

int32_t parse_alpha_tag(const char *str)
{
    int32_t alpha = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &alpha);
    return alpha;
}

uint32_t parse_color_tag(const char *str)
{
    int32_t color = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &color);
    return ass_bswap32((uint32_t) color);
}

int32_t parse_int_header(ASS_StringView str)
{
    int32_t val = 0;
    unsigned base;

    vskip_spaces(&str);

    if (ASS_SV_ISTARTSWITH(str, "&h") || ASS_SV_ISTARTSWITH(str, "0x")) {
        str.str += 2, str.len -= 2;
        base = 16;
    } else
        base = 10;

    mystrtoi32_modulo(&str, base, &val);

    return val;
}

uint32_t parse_color_header(ASS_StringView str)
{
    uint32_t ret = parse_int_header(str);
    return ass_bswap32(ret);
}

// Return a boolean value for a string
// String must be terminated in
char parse_bool(ASS_StringView str)
{
    vskip_spaces(&str);
    return ASS_SV_ISTARTSWITH(str, "yes") || parse_int_header(str) > 0;
}

int parse_ycbcr_matrix(ASS_StringView str)
{
    vskip_spaces(&str);
    if (!str.len)
        return YCBCR_DEFAULT;

    vrskip_spaces(&str);

    if (ASS_SV_IEQ(str, "none"))
        return YCBCR_NONE;
    if (ASS_SV_IEQ(str, "tv.601"))
        return YCBCR_BT601_TV;
    if (ASS_SV_IEQ(str, "pc.601"))
        return YCBCR_BT601_PC;
    if (ASS_SV_IEQ(str, "tv.709"))
        return YCBCR_BT709_TV;
    if (ASS_SV_IEQ(str, "pc.709"))
        return YCBCR_BT709_PC;
    if (ASS_SV_IEQ(str, "tv.240m"))
        return YCBCR_SMPTE240M_TV;
    if (ASS_SV_IEQ(str, "pc.240m"))
        return YCBCR_SMPTE240M_PC;
    if (ASS_SV_IEQ(str, "tv.fcc"))
        return YCBCR_FCC_TV;
    if (ASS_SV_IEQ(str, "pc.fcc"))
        return YCBCR_FCC_PC;
    return YCBCR_UNKNOWN;
}

/**
 * \brief converts numpad-style align to align.
 */
int numpad2align(int val)
{
    if (val < -INT_MAX)
        // Pick an alignment somewhat arbitrarily. VSFilter handles
        // INT32_MIN as a mix of 1, 2 and 3, so prefer one of those values.
        val = 2;
    else if (val < 0)
        val = -val;

    int res = ((val - 1) % 3) + 1;  // horizontal alignment
    if (val <= 3)
        res |= VALIGN_SUB;
    else if (val <= 6)
        res |= VALIGN_CENTER;
    else
        res |= VALIGN_TOP;
    return res;
}

void ass_msg(ASS_Library *priv, int lvl, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    priv->msg_callback(lvl, fmt, va, priv->msg_callback_data);
    va_end(va);
}

unsigned ass_utf8_get_char(const char **str)
{
    uint8_t *strp = (uint8_t *) * str;
    unsigned c = *strp++;
    unsigned mask = 0x80;
    int len = -1;
    while (c & mask) {
        mask >>= 1;
        len++;
    }
    if (len <= 0 || len > 4)
        goto no_utf8;
    c &= mask - 1;
    while ((*strp & 0xc0) == 0x80) {
        if (len-- <= 0)
            goto no_utf8;
        c = (c << 6) | (*strp++ & 0x3f);
    }
    if (len)
        goto no_utf8;
    *str = (char *) strp;
    return c;

  no_utf8:
    strp = (uint8_t *) * str;
    c = *strp++;
    *str = (char *) strp;
    return c;
}

/**
 * Original version from http://www.cprogramming.com/tutorial/utf8.c
 * \brief Converts a single UTF-32 code point to UTF-8
 * \param dest Buffer to write to. Writes a NULL terminator.
 * \param ch 32-bit character code to convert
 * \return number of bytes written
 * converts a single character and ASSUMES YOU HAVE ENOUGH SPACE
 */
unsigned ass_utf8_put_char(char *dest, uint32_t ch)
{
    char *orig_dest = dest;

    if (ch < 0x80) {
        *dest++ = (char)ch;
    } else if (ch < 0x800) {
        *dest++ = (ch >> 6) | 0xC0;
        *dest++ = (ch & 0x3F) | 0x80;
    } else if (ch < 0x10000) {
        *dest++ = (ch >> 12) | 0xE0;
        *dest++ = ((ch >> 6) & 0x3F) | 0x80;
        *dest++ = (ch & 0x3F) | 0x80;
    } else if (ch < 0x110000) {
        *dest++ = (ch >> 18) | 0xF0;
        *dest++ = ((ch >> 12) & 0x3F) | 0x80;
        *dest++ = ((ch >> 6) & 0x3F) | 0x80;
        *dest++ = (ch & 0x3F) | 0x80;
    }

    *dest = '\0';
    return dest - orig_dest;
}

/**
 * \brief Parse UTF-16 and return the code point of the sequence starting at src.
 * \param src pointer to a pointer to the start of the UTF-16 data
 *            (will be set to the start of the next code point)
 * \return the code point
 */
static uint32_t ass_read_utf16be(const uint8_t **src, size_t bytes)
{
    if (bytes < 2)
        goto too_short;

    uint32_t cp = ((*src)[0] << 8) | (*src)[1];
    *src += 2;
    bytes -= 2;

    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (bytes < 2)
            goto too_short;

        uint32_t cp2 = ((*src)[0] << 8) | (*src)[1];

        if (cp2 < 0xDC00 || cp2 > 0xDFFF)
            return 0xFFFD;

        *src += 2;

        cp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
    }

    if (cp >= 0xDC00 && cp <= 0xDFFF)
        return 0xFFFD;

    return cp;

too_short:
    *src += bytes;
    return 0xFFFD;
}

void ass_utf16be_to_utf8(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    const uint8_t *end = src + src_size;

    if (!dst_size)
        return;

    while (src < end) {
        uint32_t cp = ass_read_utf16be(&src, end - src);
        if (dst_size < 5)
            break;
        unsigned s = ass_utf8_put_char(dst, cp);
        dst += s;
        dst_size -= s;
    }

    *dst = '\0';
}

/**
 * \brief find style by name
 * \param track track
 * \param name style name
 * \return index in track->styles
 * Returns 0 if no styles found => expects at least 1 style.
 * Parsing code always adds "Default" style in the beginning.
 */
int lookup_style(ASS_Track *track, ASS_StringView name)
{
    int i;
    // '*' seem to mean literally nothing;
    // VSFilter removes them as soon as it can
    while (name.len && *name.str == '*')
        ++name.str, --name.len;
    // VSFilter then normalizes the case of "Default"
    // (only in contexts where this function is called)
    if (ASS_SV_IEQ(name, "Default"))
        name = ASS_SV("Default");
    for (i = track->n_styles - 1; i >= 0; --i) {
        if (ass_sv_equal_cstr(name, track->styles[i].Name))
            return i;
    }
    i = track->default_style;
    ass_msg(track->library, MSGL_WARN,
            "[%p]: Warning: no style named '%.*s' found, using '%s'",
            track, (int)name.len, name.str, track->styles[i].Name);
    return i;
}

/**
 * \brief find style by name as in \r
 * \param track track
 * \param name style name
 * \param len style name length
 * \return style in track->styles
 * Returns NULL if no style has the given name.
 */
ASS_Style *lookup_style_strict(ASS_Track *track, const char *name, size_t len)
{
    int i;
    for (i = track->n_styles - 1; i >= 0; --i) {
        if (strncmp(track->styles[i].Name, name, len) == 0 &&
            track->styles[i].Name[len] == '\0')
            return track->styles + i;
    }
    ass_msg(track->library, MSGL_WARN,
            "[%p]: Warning: no style named '%.*s' found",
            track, (int) len, name);
    return NULL;
}


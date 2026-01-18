/*
 * Copyright (C) 2013 Stefano Pigozzi <stefano.pigozzi@gmail.com>
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
#include <AvailabilityMacros.h>
#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <CoreText/CoreText.h>
#else
#include <ApplicationServices/ApplicationServices.h>
#endif

#include "ass_coretext.h"

#if __has_builtin(__builtin_available)
#define CHECK_AVAILABLE(sym, ...) __builtin_available(__VA_ARGS__)
#else
// Cast to suppress "comparison never succeeds" warnings in some compilers
// when the build target is new enough that sym isn't a weak symbol
#define CHECK_AVAILABLE(sym, ...) (!!(intptr_t) &sym)
#endif

#define SAFE_CFRelease(x) do { if (x) CFRelease(x); } while (0)

static const ASS_FontMapping font_substitutions[] = {
    {"sans-serif", "Helvetica"},
    {"serif", "Times"},
    {"monospace", "Courier"}
};

static char *cfstr2buf(CFStringRef string)
{
    if (!string)
        return NULL;
    const int encoding = kCFStringEncodingUTF8;
    const char *buf_ptr = CFStringGetCStringPtr(string, encoding);
    if (buf_ptr) {
        return strdup(buf_ptr);
    } else {
        size_t len = CFStringGetLength(string);
        CFIndex buf_len = CFStringGetMaximumSizeForEncoding(len, encoding);
        char *buf = malloc(buf_len);
        if (buf)
            CFStringGetCString(string, buf, buf_len, encoding);
        return buf;
    }
}

typedef struct {
    CFCharacterSetRef charset;
    CTFontRef font;
    bool has_color_glyphs;
} FontPriv;

// Check if font has color glyph tables (sbix, COLR, CBDT)
static bool font_has_color_tables(CTFontRef font)
{
    if (!font)
        return false;

    // Check for sbix (Apple color bitmap)
    CFDataRef sbix = CTFontCopyTable(font, kCTFontTableSbix, 0);
    if (sbix) {
        CFRelease(sbix);
        return true;
    }

    // Check for COLR (color layers)
    CFDataRef colr = CTFontCopyTable(font, kCTFontTableCOLR, 0);
    if (colr) {
        CFRelease(colr);
        return true;
    }

    // Check for CBDT (color bitmap data)
    CFDataRef cbdt = CTFontCopyTable(font, kCTFontTableCBDT, 0);
    if (cbdt) {
        CFRelease(cbdt);
        return true;
    }

    return false;
}

static void destroy_font(void *priv)
{
    FontPriv *fp = priv;
    if (fp) {
        SAFE_CFRelease(fp->charset);
        SAFE_CFRelease(fp->font);
        free(fp);
    }
}

static bool check_glyph(void *priv, uint32_t code, bool exact_match)
{
    if (code == 0)
        return true;

    FontPriv *fp = priv;
    if (!fp)
        return true;

    // For emoji codepoints, prefer color fonts unless the user explicitly
    // requested this font by name. This allows fallback fonts to prefer
    // color emoji while respecting explicit font choices.
    if (!exact_match && ass_is_emoji_codepoint(code) && !fp->has_color_glyphs)
        return false;

    // First try the character set (fast path)
    if (fp->charset && CFCharacterSetIsLongCharacterMember(fp->charset, code))
        return true;

    // Fallback: use CTFont to check glyph (works for color emoji)
    if (fp->font) {
        UniChar chars[2];
        CGGlyph glyphs[2];
        CFIndex count;

        // Handle surrogate pairs for codepoints > 0xFFFF
        if (code > 0xFFFF) {
            uint32_t adjusted = code - 0x10000;
            chars[0] = (adjusted >> 10) + 0xD800;    // high surrogate
            chars[1] = (adjusted & 0x3FF) + 0xDC00;  // low surrogate
            count = 2;
        } else {
            chars[0] = code;
            count = 1;
        }

        if (CTFontGetGlyphsForCharacters(fp->font, chars, glyphs, count) && glyphs[0] != 0)
            return true;
    }

    return false;
}

static char *get_font_file(CTFontDescriptorRef fontd)
{
    CFURLRef url = NULL;
    if (false) {}
#ifdef MAC_OS_X_VERSION_10_6
    // Declared in SDKs since 10.6, including iOS SDKs
    else if (CHECK_AVAILABLE(kCTFontURLAttribute, macOS 10.6, *)) {
        url = CTFontDescriptorCopyAttribute(fontd, kCTFontURLAttribute);
    }
#endif
#if !TARGET_OS_IPHONE && MAC_OS_X_VERSION_MIN_REQUIRED < 1060
    // ATS is declared deprecated in newer macOS SDKs
    // and not declared at all in iOS SDKs
    else {
        CTFontRef font = CTFontCreateWithFontDescriptor(fontd, 0, NULL);
        if (!font)
            return NULL;
        ATSFontRef ats_font = CTFontGetPlatformFont(font, NULL);
        FSRef fs_ref;
        if (ATSFontGetFileReference(ats_font, &fs_ref) == noErr)
            url = CFURLCreateFromFSRef(NULL, &fs_ref);
        CFRelease(font);
    }
#endif
    if (!url)
        return NULL;
    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    CFRelease(url);
    if (!path)
        return NULL;
    char *buffer = cfstr2buf(path);
    CFRelease(path);
    return buffer;
}

static char *get_name(CTFontDescriptorRef fontd, CFStringRef attr)
{
    char *ret = NULL;
    CFStringRef name = CTFontDescriptorCopyAttribute(fontd, attr);
    if (name) {
        ret = cfstr2buf(name);
        CFRelease(name);
    }
    return ret;
}

static bool get_font_info_ct(CTFontDescriptorRef fontd,
                             char **path_out,
                             ASS_FontProviderMetaData *info)
{
    char *path = get_font_file(fontd);
    *path_out = path;
    if (!path || !*path) {
        // skip the font if the URL field in the font descriptor is empty
        return false;
    }

    char *ps_name = get_name(fontd, kCTFontNameAttribute);
    info->postscript_name = ps_name;
    if (!ps_name)
        return false;

    char *family_name = get_name(fontd, kCTFontFamilyNameAttribute);
    info->extended_family = family_name;
    if (!family_name)
        return false;

    return true;
}

static void process_descriptors(ASS_FontProvider *provider, CFArrayRef fontsd)
{
    if (!fontsd)
        return;

    for (int i = 0; i < CFArrayGetCount(fontsd); i++) {
        ASS_FontProviderMetaData meta = {0};
        CTFontDescriptorRef fontd = CFArrayGetValueAtIndex(fontsd, i);
        int index = -1;

        char *path = NULL;
        if (get_font_info_ct(fontd, &path, &meta)) {
            FontPriv *fp = calloc(1, sizeof(FontPriv));
            if (fp) {
                fp->charset = CTFontDescriptorCopyAttribute(fontd, kCTFontCharacterSetAttribute);
                fp->font = CTFontCreateWithFontDescriptor(fontd, 0, NULL);
                fp->has_color_glyphs = font_has_color_tables(fp->font);
                ass_font_provider_add_font(provider, &meta, path, index, fp);
            }
        }

        free(meta.postscript_name);
        free(meta.extended_family);

        free(path);
    }
}

static void match_fonts(void *priv, ASS_Library *lib, ASS_FontProvider *provider,
                        char *name)
{
    CFStringRef cfname =
        CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    if (!cfname)
        return;

    enum { attributes_n = 3 };
    CTFontDescriptorRef ctdescrs[attributes_n] = {0};
    CFStringRef attributes[attributes_n] = {
        kCTFontFamilyNameAttribute,
        kCTFontDisplayNameAttribute,
        kCTFontNameAttribute,
    };

    CFArrayRef descriptors = NULL;
    CTFontCollectionRef ctcoll = NULL;
    CFArrayRef fontsd = NULL;

    for (int i = 0; i < attributes_n; i++) {
        CFDictionaryRef cfattrs =
            CFDictionaryCreate(NULL,
                (const void **)&attributes[i],
                (const void **)&cfname,
                1, NULL, NULL);
        if (!cfattrs)
            goto cleanup;
        ctdescrs[i] = CTFontDescriptorCreateWithAttributes(cfattrs);
        CFRelease(cfattrs);
        if (!ctdescrs[i])
            goto cleanup;
    }

    descriptors =
        CFArrayCreate(NULL, (const void **)&ctdescrs, attributes_n, NULL);
    if (!descriptors)
        goto cleanup;

    const int nonzero = 1;
    CFNumberRef cfnonzero = CFNumberCreate(NULL, kCFNumberIntType, &nonzero);
    if (!cfnonzero)
        goto cleanup;
    CFDictionaryRef options =
        CFDictionaryCreate(NULL,
            (const void **)&kCTFontCollectionRemoveDuplicatesOption,
            (const void **)&cfnonzero,
            1, NULL, NULL);
    CFRelease(cfnonzero);
    if (!options)
        goto cleanup;

    ctcoll = CTFontCollectionCreateWithFontDescriptors(descriptors, options);
    CFRelease(options);
    if (!ctcoll)
        goto cleanup;

    fontsd = CTFontCollectionCreateMatchingFontDescriptors(ctcoll);
    if (!fontsd)
        goto cleanup;

    process_descriptors(provider, fontsd);

cleanup:
    SAFE_CFRelease(fontsd);
    SAFE_CFRelease(ctcoll);

    for (int i = 0; i < attributes_n; i++)
        SAFE_CFRelease(ctdescrs[i]);

    SAFE_CFRelease(descriptors);
    CFRelease(cfname);
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *family, uint32_t codepoint)
{
    // For emoji codepoints, directly return a color emoji font.
    // CTFontCreateForString often returns .LastResort instead of the color font.
    if (ass_is_emoji_codepoint(codepoint)) {
        // Try Apple Color Emoji (macOS/iOS)
        CTFontRef emoji_font = CTFontCreateWithName(CFSTR("Apple Color Emoji"), 12.0, NULL);
        if (emoji_font) {
            if (font_has_color_tables(emoji_font)) {
                CFRelease(emoji_font);
                return strdup("Apple Color Emoji");
            }
            CFRelease(emoji_font);
        }
    }

    CFStringRef name = CFStringCreateWithBytes(
        0, (UInt8 *)family, strlen(family), kCFStringEncodingUTF8, false);
    if (!name)
        return NULL;

    CTFontRef font = CTFontCreateWithName(name, 12.0, NULL);
    CFRelease(name);
    if (!font)
        return NULL;

    uint32_t codepointle = OSSwapHostToLittleInt32(codepoint);

    CFStringRef r = CFStringCreateWithBytes(
        0, (UInt8*)&codepointle, sizeof(codepointle),
        kCFStringEncodingUTF32LE, false);
    if (!r) {
        CFRelease(font);
        return NULL;
    }

    CTFontRef fb = CTFontCreateForString(font, r, CFRangeMake(0, 1));
    CFRelease(font);
    CFRelease(r);
    if (!fb)
        return NULL;

    CFStringRef cffamily = CTFontCopyFamilyName(fb);
    CFRelease(fb);
    if (!cffamily)
        return NULL;

    char *res_family = cfstr2buf(cffamily);
    CFRelease(cffamily);

    return res_family;
}

static void get_substitutions(void *priv, const char *name,
                              ASS_FontProviderMetaData *meta)
{
    const int n = sizeof(font_substitutions) / sizeof(font_substitutions[0]);
    ass_map_font(font_substitutions, n, name, meta);
}

static ASS_FontProviderFuncs coretext_callbacks = {
    .check_glyph        = check_glyph,
    .destroy_font       = destroy_font,
    .match_fonts        = match_fonts,
    .get_substitutions  = get_substitutions,
    .get_fallback       = get_fallback,
};

ASS_FontProvider *
ass_coretext_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                          const char *config, FT_Library ftlib)
{
    return ass_font_provider_new(selector, &coretext_callbacks, NULL);
}

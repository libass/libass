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

#include <CoreFoundation/CoreFoundation.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <CoreText/CoreText.h>
#else
#include <ApplicationServices/ApplicationServices.h>
#endif

#include "ass_coretext.h"

#define SAFE_CFRelease(x) do { if (x) CFRelease(x); } while(0)

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
        CFStringGetCString(string, buf, buf_len, encoding);
        return buf;
    }
}

static void destroy_font(void *priv)
{
    CTFontDescriptorRef fontd = priv;
    SAFE_CFRelease(fontd);
}

static bool check_postscript(void *priv)
{
    CTFontDescriptorRef fontd = priv;
    CFNumberRef cfformat =
        CTFontDescriptorCopyAttribute(fontd, kCTFontFormatAttribute);
    int format;

    if (!CFNumberGetValue(cfformat, kCFNumberIntType, &format))
        return false;

    SAFE_CFRelease(cfformat);

    return format == kCTFontFormatOpenTypePostScript ||
           format == kCTFontFormatPostScript;
}

static bool check_glyph(void *priv, uint32_t code)
{
    if (code == 0)
        return true;

    CTFontDescriptorRef fontd = priv;
    CFCharacterSetRef set =
        CTFontDescriptorCopyAttribute(fontd, kCTFontCharacterSetAttribute);

    if (!set)
        return true;

    bool result = CFCharacterSetIsLongCharacterMember(set, code);
    SAFE_CFRelease(set);
    return result;
}

static char *get_font_file(CTFontDescriptorRef fontd)
{
    CFURLRef url = CTFontDescriptorCopyAttribute(fontd, kCTFontURLAttribute);
    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    char *buffer = cfstr2buf(path);
    SAFE_CFRelease(path);
    SAFE_CFRelease(url);
    return buffer;
}

static char *get_name(CTFontDescriptorRef fontd, CFStringRef attr)
{
    char *ret = NULL;
    CFStringRef name = CTFontDescriptorCopyAttribute(fontd, attr);
    if (name) {
        ret = cfstr2buf(name);
        SAFE_CFRelease(name);
    }
    return ret;
}

static void process_descriptors(ASS_Library *lib, ASS_FontProvider *provider,
                                CFArrayRef fontsd)
{
    ASS_FontProviderMetaData meta;

    if (!fontsd)
        return;

    FT_Library ftlib;
    if (FT_Init_FreeType(&ftlib)) {
        ass_msg(lib, MSGL_WARN, "Failed to create FT lib");
        return;
    }

    for (int i = 0; i < CFArrayGetCount(fontsd); i++) {
        CTFontDescriptorRef fontd = CFArrayGetValueAtIndex(fontsd, i);
        int index = -1;

        char *path = get_font_file(fontd);
        if (strcmp("", path) == 0) {
            // skip the font if the URL field in the font descriptor is empty
            free(path);
            continue;
        }

        memset(&meta, 0, sizeof(meta));

        char *ps_name = get_name(fontd, kCTFontNameAttribute);

        if (ass_get_font_info(lib, ftlib, path, ps_name, -1, &meta)) {
            CFRetain(fontd);
            ass_font_provider_add_font(provider, &meta, path, index, (void*)fontd);
        }

        for (int j = 0; j < meta.n_family; j++)
            free(meta.families[j]);

        for (int j = 0; j < meta.n_fullname; j++)
            free(meta.fullnames[j]);

        free(meta.families);
        free(meta.fullnames);

        free(meta.postscript_name);

        free(ps_name);
        free(path);
    }

    FT_Done_FreeType(ftlib);
}

static void match_fonts(ASS_Library *lib, ASS_FontProvider *provider,
                        char *name)
{
    enum { attributes_n = 3 };
    CTFontDescriptorRef ctdescrs[attributes_n];
    CFMutableDictionaryRef cfattrs[attributes_n];
    CFStringRef attributes[attributes_n] = {
        kCTFontFamilyNameAttribute,
        kCTFontDisplayNameAttribute,
        kCTFontNameAttribute,
    };

    CFStringRef cfname =
        CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);

    for (int i = 0; i < attributes_n; i++) {
        cfattrs[i] = CFDictionaryCreateMutable(NULL, 0, 0, 0);
        CFDictionaryAddValue(cfattrs[i], attributes[i], cfname);
        ctdescrs[i] = CTFontDescriptorCreateWithAttributes(cfattrs[i]);
    }

    CFArrayRef descriptors =
        CFArrayCreate(NULL, (const void **)&ctdescrs, attributes_n, NULL);

    CTFontCollectionRef ctcoll =
        CTFontCollectionCreateWithFontDescriptors(descriptors, 0);

    CFArrayRef fontsd =
        CTFontCollectionCreateMatchingFontDescriptors(ctcoll);

    process_descriptors(lib, provider, fontsd);

    SAFE_CFRelease(fontsd);
    SAFE_CFRelease(ctcoll);

    for (int i = 0; i < attributes_n; i++) {
        SAFE_CFRelease(cfattrs[i]);
        SAFE_CFRelease(ctdescrs[i]);
    }

    SAFE_CFRelease(descriptors);
    SAFE_CFRelease(cfname);
}

static char *get_fallback(void *priv, const char *family, uint32_t codepoint)
{
    CFStringRef name = CFStringCreateWithBytes(
        0, (UInt8 *)family, strlen(family), kCFStringEncodingUTF8, false);
    CTFontRef font = CTFontCreateWithName(name, 0, NULL);
    uint32_t codepointle = OSSwapHostToLittleInt32(codepoint);
    CFStringRef r = CFStringCreateWithBytes(
        0, (UInt8*)&codepointle, sizeof(codepointle),
        kCFStringEncodingUTF32LE, false);
    CTFontRef fb = CTFontCreateForString(font, r, CFRangeMake(0, 1));
    CFStringRef cffamily = CTFontCopyFamilyName(fb);
    char *res_family = cfstr2buf(cffamily);

    SAFE_CFRelease(name);
    SAFE_CFRelease(font);
    SAFE_CFRelease(r);
    SAFE_CFRelease(fb);
    SAFE_CFRelease(cffamily);

    return res_family;
}

static void get_substitutions(void *priv, const char *name,
                              ASS_FontProviderMetaData *meta)
{
    const int n = sizeof(font_substitutions) / sizeof(font_substitutions[0]);
    ass_map_font(font_substitutions, n, name, meta);
}

static ASS_FontProviderFuncs coretext_callbacks = {
    .check_postscript   = check_postscript,
    .check_glyph        = check_glyph,
    .destroy_font       = destroy_font,
    .match_fonts        = match_fonts,
    .get_substitutions  = get_substitutions,
    .get_fallback       = get_fallback,
};

ASS_FontProvider *
ass_coretext_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                          const char *config)
{
    return ass_font_provider_new(selector, &coretext_callbacks, NULL);
}

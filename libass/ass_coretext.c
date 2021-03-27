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
        CFStringGetCString(string, buf, buf_len, encoding);
        return buf;
    }
}

static void destroy_font(void *priv)
{
    CTFontDescriptorRef fontd = priv;
    SAFE_CFRelease(fontd);
}

static bool is_postscript_font_format(CFNumberRef cfformat)
{
    bool ret = false;
    int format;
    if (CFNumberGetValue(cfformat, kCFNumberIntType, &format)) {
        ret = format == kCTFontFormatOpenTypePostScript ||
              format == kCTFontFormatPostScript;
    }
    return ret;
}

static bool check_postscript(void *priv)
{
    CTFontDescriptorRef fontd = priv;
    CFNumberRef cfformat =
        CTFontDescriptorCopyAttribute(fontd, kCTFontFormatAttribute);
    bool ret = is_postscript_font_format(cfformat);
    SAFE_CFRelease(cfformat);
    return ret;
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
    if (!url)
        return NULL;
    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    if (!path) {
        SAFE_CFRelease(url);
        return NULL;
    }
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

static bool fill_family_name(CTFontDescriptorRef fontd,
                             ASS_FontProviderMetaData *info)
{
    if (info->n_family)
        return true;

    char *family_name = get_name(fontd, kCTFontFamilyNameAttribute);
    if (!family_name)
        return false;

    info->families = malloc(sizeof(char *));
    if (!info->families) {
        free(family_name);
        return false;
    }

    info->families[0] = family_name;
    info->n_family++;
    return true;
}

static bool get_font_info_ct(ASS_Library *lib, FT_Library ftlib,
                             CTFontDescriptorRef fontd,
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
    if (!ps_name)
        return false;

    bool got_info = ass_get_font_info(lib, ftlib, path, ps_name, -1, false, info);
    free(ps_name);

    return got_info && fill_family_name(fontd, info);
}

static void process_descriptors(ASS_Library *lib, FT_Library ftlib,
                                ASS_FontProvider *provider, CFArrayRef fontsd)
{
    if (!fontsd)
        return;

    for (int i = 0; i < CFArrayGetCount(fontsd); i++) {
        ASS_FontProviderMetaData meta = {0};
        CTFontDescriptorRef fontd = CFArrayGetValueAtIndex(fontsd, i);
        int index = -1;

        char *path = NULL;
        if (get_font_info_ct(lib, ftlib, fontd, &path, &meta)) {
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

        free(path);
    }
}

static void match_fonts(void *priv, ASS_Library *lib, ASS_FontProvider *provider,
                        char *name)
{
    FT_Library ftlib = priv;

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

    process_descriptors(lib, ftlib, provider, fontsd);

    SAFE_CFRelease(fontsd);
    SAFE_CFRelease(ctcoll);

    for (int i = 0; i < attributes_n; i++) {
        SAFE_CFRelease(cfattrs[i]);
        SAFE_CFRelease(ctdescrs[i]);
    }

    SAFE_CFRelease(descriptors);
    SAFE_CFRelease(cfname);
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *family, uint32_t codepoint)
{
    FT_Library ftlib = priv;

    CFStringRef name = CFStringCreateWithBytes(
        0, (UInt8 *)family, strlen(family), kCFStringEncodingUTF8, false);
    CTFontRef font = CTFontCreateWithName(name, 0, NULL);
    uint32_t codepointle = OSSwapHostToLittleInt32(codepoint);
    CFStringRef r = CFStringCreateWithBytes(
        0, (UInt8*)&codepointle, sizeof(codepointle),
        kCFStringEncodingUTF32LE, false);
    CTFontRef fb = CTFontCreateForString(font, r, CFRangeMake(0, 1));
    CTFontDescriptorRef fontd = CTFontCopyFontDescriptor(fb);

    char *res_name = NULL;
    char *path = NULL;
    ASS_FontProviderMetaData meta = {0};
    if (get_font_info_ct(lib, ftlib, fontd, &path, &meta))
        res_name = meta.families[0];

    for (int i = 1 /* skip res_name */; i < meta.n_family; i++)
        free(meta.families[i]);

    for (int i = 0; i < meta.n_fullname; i++)
        free(meta.fullnames[i]);

    free(meta.families);
    free(meta.fullnames);

    free(meta.postscript_name);

    free(path);

    SAFE_CFRelease(name);
    SAFE_CFRelease(font);
    SAFE_CFRelease(r);
    SAFE_CFRelease(fb);
    SAFE_CFRelease(fontd);

    return res_name;
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
                          const char *config, FT_Library ftlib)
{
    return ass_font_provider_new(selector, &coretext_callbacks, ftlib);
}

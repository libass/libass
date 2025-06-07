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

static void destroy_font(void *priv)
{
    CFCharacterSetRef set = priv;
    SAFE_CFRelease(set);
}

static bool check_glyph(void *priv, uint32_t code)
{
    if (code == 0)
        return true;

    CFCharacterSetRef set = priv;

    if (!set)
        return true;

    return CFCharacterSetIsLongCharacterMember(set, code);
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

static void update_best_matching_font(ASS_FontProvider *provider, ASS_FontProviderMetaData *meta,
                                      const char *path, int index, void *font_priv,
                                      ASS_FontProviderMetaData requested_font,
                                      ASS_FontInfo **best_font_info, unsigned *best_font_score,
                                      bool match_extended_family, unsigned bold, unsigned italic,
                                      uint32_t code)
{
    ASS_FontInfo* info = ass_font_provider_get_font_info(provider, meta, path, index, font_priv);
    if (!info)
        return;

    bool name_match = false;
    if (ass_update_best_matching_font(info, requested_font, match_extended_family, bold, italic, code, &name_match, best_font_score)) {
        if (*best_font_info) {
            ass_font_provider_free_fontinfo(*best_font_info);
            ass_font_provider_destroy_private_fontinfo((*best_font_info));
            free(*best_font_info);
        }
        *best_font_info = info;
    } else {
        ass_font_provider_free_fontinfo(info);
        ass_font_provider_destroy_private_fontinfo(info);
        free(info);
    }
}

static ASS_FontInfo *process_descriptors(ASS_FontProvider *provider, CFArrayRef fontsd,
                                         ASS_FontProviderMetaData requested_font,
                                         bool match_extended_family, unsigned bold,
                                         unsigned italic, uint32_t code)
{
    if (!fontsd)
        return NULL;

    ASS_FontInfo *best_font_info = NULL;
    unsigned best_font_score = UINT_MAX;

    for (int i = 0; i < CFArrayGetCount(fontsd); i++) {
        ASS_FontProviderMetaData meta = {0};
        CTFontDescriptorRef fontd = CFArrayGetValueAtIndex(fontsd, i);
        int index = -1;

        char *path = NULL;
        if (get_font_info_ct(fontd, &path, &meta)) {
            CFCharacterSetRef set =
                CTFontDescriptorCopyAttribute(fontd, kCTFontCharacterSetAttribute);
            update_best_matching_font(provider, &meta, path, index, (void*)set, requested_font,
                                      &best_font_info, &best_font_score,
                                      match_extended_family, bold, italic, code);
        }

        free(meta.postscript_name);
        free(meta.extended_family);

        free(path);
    }

    return best_font_info;
}

static ASS_FontInfo* match_fonts(void *priv, ASS_Library *lib,
                                 ASS_FontProvider *provider, char *name,
                                 bool match_extended_family,
                                 unsigned bold, unsigned italic,
                                 uint32_t code)
{
    ASS_FontProviderMetaData requested_font = {
        .n_fullname = 1,
        .fullnames  = (char **)&name,
    };

    CFStringRef cfname =
        CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    if (!cfname)
        return NULL;

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

    return process_descriptors(provider, fontsd, requested_font,
                               match_extended_family, bold, italic, code);

cleanup:
    SAFE_CFRelease(fontsd);
    SAFE_CFRelease(ctcoll);

    for (int i = 0; i < attributes_n; i++)
        SAFE_CFRelease(ctdescrs[i]);

    SAFE_CFRelease(descriptors);
    CFRelease(cfname);

    return NULL;
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *family, uint32_t codepoint)
{
    CFStringRef name = CFStringCreateWithBytes(
        0, (UInt8 *)family, strlen(family), kCFStringEncodingUTF8, false);
    if (!name)
        return NULL;

    CTFontRef font = CTFontCreateWithName(name, 0, NULL);
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

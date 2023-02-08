/*
 * Copyright (C) 2015 Stephan Vedder <stephan.vedder@gmail.com>
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
#define COBJMACROS

#include "config.h"
#include "ass_compat.h"

#include <initguid.h>
#include <wchar.h>

#include "dwrite_c.h"

#include "ass_directwrite.h"
#include "ass_utils.h"

#define FALLBACK_DEFAULT_FONT L"Arial"

static const ASS_FontMapping font_substitutions[] = {
    {"sans-serif", "Arial"},
    {"serif", "Times New Roman"},
    {"monospace", "Courier New"}
};

typedef struct ASS_SharedHDC ASS_SharedHDC;

#if ASS_WINAPI_DESKTOP

struct ASS_SharedHDC {
    HDC hdc;
    unsigned ref_count;
};

static ASS_SharedHDC *hdc_retain(ASS_SharedHDC *shared_hdc)
{
    shared_hdc->ref_count++;
    return shared_hdc;
}

static void hdc_release(ASS_SharedHDC *shared_hdc)
{
    if (!--shared_hdc->ref_count) {
        DeleteDC(shared_hdc->hdc);
        free(shared_hdc);
    }
}

#endif

/*
 * The private data stored for every font, detected by this backend.
 */
typedef struct {
#if ASS_WINAPI_DESKTOP
    ASS_SharedHDC *shared_hdc;
#endif
    IDWriteFont *font;
    IDWriteFontFace *face;
    IDWriteFontFileStream *stream;
} FontPrivate;

typedef struct {
#if ASS_WINAPI_DESKTOP
    HMODULE directwrite_lib;
#endif
    IDWriteFactory *factory;
    IDWriteGdiInterop *gdi_interop;
} ProviderPrivate;

/**
 * Custom text renderer class for logging the fonts used. It does not
 * actually render anything or do anything apart from that.
 */

typedef struct FallbackLogTextRenderer {
    IDWriteTextRenderer iface;
    IDWriteTextRendererVtbl vtbl;
    IDWriteFactory *dw_factory;
    LONG ref_count;
} FallbackLogTextRenderer;

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_IsPixelSnappingDisabled(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    BOOL* isDisabled
    )
{
    *isDisabled = true;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_GetCurrentTransform(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    DWRITE_MATRIX* transform
    )
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_GetPixelsPerDip(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    FLOAT* pixelsPerDip
    )
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_DrawGlyphRun(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode,
    DWRITE_GLYPH_RUN const* glyphRun,
    DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    IUnknown* clientDrawingEffect
    )
{
    FallbackLogTextRenderer *this = (FallbackLogTextRenderer *)This;
    HRESULT hr;
    IDWriteFontCollection *font_coll = NULL;
    IDWriteFont **font = (IDWriteFont **)clientDrawingContext;

    hr = IDWriteFactory_GetSystemFontCollection(this->dw_factory, &font_coll, FALSE);
    if (FAILED(hr))
        return E_FAIL;

    hr = IDWriteFontCollection_GetFontFromFontFace(font_coll, glyphRun->fontFace,
                                                   font);
    if (FAILED(hr))
        return E_FAIL;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_DrawUnderline(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_UNDERLINE const* underline,
    IUnknown* clientDrawingEffect
    )
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_DrawStrikethrough(
    IDWriteTextRenderer *This,
    void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_STRIKETHROUGH const* strikethrough,
    IUnknown* clientDrawingEffect
    )
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_DrawInlineObject(
    IDWriteTextRenderer *This,
    void *clientDrawingContext,
    FLOAT originX,
    FLOAT originY,
    IDWriteInlineObject *inlineObject,
    BOOL isSideways,
    BOOL isRightToLeft,
    IUnknown *clientDrawingEffect
    )
{
    return S_OK;
}

// IUnknown methods

static ULONG STDMETHODCALLTYPE FallbackLogTextRenderer_AddRef(
    IDWriteTextRenderer *This
    )
{
    FallbackLogTextRenderer *this = (FallbackLogTextRenderer *)This;
    return InterlockedIncrement(&this->ref_count);
}

static ULONG STDMETHODCALLTYPE FallbackLogTextRenderer_Release(
    IDWriteTextRenderer *This
    )
{
    FallbackLogTextRenderer *this = (FallbackLogTextRenderer *)This;
    unsigned long new_count = InterlockedDecrement(&this->ref_count);
    if (new_count == 0) {
        free(this);
        return 0;
    }

    return new_count;
}

static HRESULT STDMETHODCALLTYPE FallbackLogTextRenderer_QueryInterface(
    IDWriteTextRenderer *This,
    REFIID riid,
    void **ppvObject
    )
{
    if (IsEqualGUID(riid, &IID_IDWriteTextRenderer)
        || IsEqualGUID(riid, &IID_IDWritePixelSnapping)
        || IsEqualGUID(riid, &IID_IUnknown)) {
        *ppvObject = This;
    } else {
        *ppvObject = NULL;
        return E_FAIL;
    }

    This->lpVtbl->AddRef(This);
    return S_OK;
}

static void init_FallbackLogTextRenderer(FallbackLogTextRenderer *r,
                                         IDWriteFactory *factory)
{
    *r = (FallbackLogTextRenderer) {
        .iface = {
            .lpVtbl = &r->vtbl,
        },
        .vtbl = {
            FallbackLogTextRenderer_QueryInterface,
            FallbackLogTextRenderer_AddRef,
            FallbackLogTextRenderer_Release,
            FallbackLogTextRenderer_IsPixelSnappingDisabled,
            FallbackLogTextRenderer_GetCurrentTransform,
            FallbackLogTextRenderer_GetPixelsPerDip,
            FallbackLogTextRenderer_DrawGlyphRun,
            FallbackLogTextRenderer_DrawUnderline,
            FallbackLogTextRenderer_DrawStrikethrough,
            FallbackLogTextRenderer_DrawInlineObject,
        },
        .dw_factory = factory,
    };
}

/*
 * This function is called whenever a font is accessed for the
 * first time. It will create a FontFace for metadata access and
 * memory reading, which will be stored within the private data.
 */
static bool init_font_private_face(FontPrivate *priv)
{
    HRESULT hr;
    IDWriteFontFace *face;

    if (priv->face != NULL)
        return true;

    hr = IDWriteFont_CreateFontFace(priv->font, &face);
    if (FAILED(hr) || !face)
        return false;

    priv->face = face;
    return true;
}

/*
 * This function is called whenever a font is used for the first
 * time. It will create a FontStream for memory reading, which
 * will be stored within the private data.
 */
static bool init_font_private_stream(FontPrivate *priv)
{
    HRESULT hr = S_OK;
    IDWriteFontFile *file = NULL;
    IDWriteFontFileStream *stream = NULL;
    IDWriteFontFileLoader *loader = NULL;
    UINT32 n_files = 1;
    const void *refKey = NULL;
    UINT32 keySize = 0;

    if (priv->stream != NULL)
        return true;

    if (!init_font_private_face(priv))
        return false;

    /* DirectWrite only supports one file per face */
    hr = IDWriteFontFace_GetFiles(priv->face, &n_files, &file);
    if (FAILED(hr) || !file)
        return false;

    hr = IDWriteFontFile_GetReferenceKey(file, &refKey, &keySize);
    if (FAILED(hr)) {
        IDWriteFontFile_Release(file);
        return false;
    }

    hr = IDWriteFontFile_GetLoader(file, &loader);
    if (FAILED(hr) || !loader) {
        IDWriteFontFile_Release(file);
        return false;
    }

    hr = IDWriteFontFileLoader_CreateStreamFromKey(loader, refKey, keySize, &stream);
    if (FAILED(hr) || !stream) {
        IDWriteFontFile_Release(file);
        return false;
    }

    priv->stream = stream;
    IDWriteFontFile_Release(file);

    return true;
}

/*
 * Read a specified part of a fontfile into memory.
 * If the font wasn't used before first creates a
 * FontStream and save it into the private data for later usage.
 * If the parameter "buf" is NULL libass wants to know the
 * size of the Fontfile
 */
static size_t get_data(void *data, unsigned char *buf, size_t offset,
                       size_t length)
{
    HRESULT hr = S_OK;
    FontPrivate *priv = (FontPrivate *) data;
    const void *fileBuf = NULL;
    void *fragContext = NULL;

    if (!init_font_private_stream(priv))
        return 0;

    if (buf == NULL) {
        UINT64 fileSize;
        hr = IDWriteFontFileStream_GetFileSize(priv->stream, &fileSize);
        if (FAILED(hr))
            return 0;

        return fileSize;
    }

    hr = IDWriteFontFileStream_ReadFileFragment(priv->stream, &fileBuf, offset,
                                                length, &fragContext);

    if (FAILED(hr) || !fileBuf)
        return 0;

    memcpy(buf, fileBuf, length);

    IDWriteFontFileStream_ReleaseFileFragment(priv->stream, fragContext);

    return length;
}

/*
 * Check whether the font contains PostScript outlines.
 */
static bool check_postscript(void *data)
{
    FontPrivate *priv = (FontPrivate *) data;

    if (!init_font_private_face(priv))
        return false;

    DWRITE_FONT_FACE_TYPE type = IDWriteFontFace_GetType(priv->face);
    return type == DWRITE_FONT_FACE_TYPE_CFF ||
           type == DWRITE_FONT_FACE_TYPE_RAW_CFF ||
           type == DWRITE_FONT_FACE_TYPE_TYPE1;
}

/*
 * Lazily return index of font. It requires the FontFace to be present, which is expensive to initialize.
 */
static unsigned get_font_index(void *data)
{
    FontPrivate *priv = (FontPrivate *)data;

    if (!init_font_private_face(priv))
        return 0;

    return IDWriteFontFace_GetIndex(priv->face);
}

/*
 * Check if the passed font has a specific unicode character.
 */
static bool check_glyph(void *data, uint32_t code)
{
    HRESULT hr = S_OK;
    FontPrivate *priv = (FontPrivate *) data;
    BOOL exists = FALSE;

    if (code == 0)
        return true;

    if (priv->font) {
        hr = IDWriteFont_HasCharacter(priv->font, code, &exists);
        if (FAILED(hr))
            return false;
    } else {
        uint16_t glyph_index;
        hr = IDWriteFontFace_GetGlyphIndices(priv->face, &code, 1, &glyph_index);
        if (FAILED(hr))
            return false;
        exists = !!glyph_index;
    }

    return exists;
}

/*
 * This will release the directwrite backend
 */
static void destroy_provider(void *priv)
{
    ProviderPrivate *provider_priv = (ProviderPrivate *)priv;
    provider_priv->gdi_interop->lpVtbl->Release(provider_priv->gdi_interop);
    provider_priv->factory->lpVtbl->Release(provider_priv->factory);
#if ASS_WINAPI_DESKTOP
    FreeLibrary(provider_priv->directwrite_lib);
#endif
    free(provider_priv);
}

/*
 * This will destroy a specific font and it's
 * Fontstream (in case it does exist)
 */

static void destroy_font(void *data)
{
    FontPrivate *priv = (FontPrivate *) data;

    if (priv->font != NULL)
        IDWriteFont_Release(priv->font);
    if (priv->face != NULL)
        IDWriteFontFace_Release(priv->face);
    if (priv->stream != NULL)
        IDWriteFontFileStream_Release(priv->stream);

#if ASS_WINAPI_DESKTOP
    hdc_release(priv->shared_hdc);
#endif

    free(priv);
}

static int encode_utf16(wchar_t *chars, uint32_t codepoint)
{
    if (codepoint < 0x10000) {
        chars[0] = codepoint;
        return 1;
    } else {
        chars[0] = (codepoint >> 10) + 0xD7C0;
        chars[1] = (codepoint & 0x3FF) + 0xDC00;
        return 2;
    }
}

static char *get_utf8_name(IDWriteLocalizedStrings *names, int k)
{
    wchar_t *temp_name = NULL;
    char *mbName = NULL;

    UINT32 length;
    HRESULT hr = IDWriteLocalizedStrings_GetStringLength(names, k, &length);
    if (FAILED(hr))
        goto cleanup;

    if (length >= (UINT32) -1 || length + (size_t) 1 > SIZE_MAX / sizeof(wchar_t))
        goto cleanup;

    temp_name = malloc((length + 1) * sizeof(wchar_t));
    if (!temp_name)
        goto cleanup;

    hr = IDWriteLocalizedStrings_GetString(names, k, temp_name, length + 1);
    if (FAILED(hr))
        goto cleanup;

    int size_needed =
        WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, NULL, 0, NULL, NULL);
    if (!size_needed)
        goto cleanup;

    mbName = malloc(size_needed);
    if (!mbName)
        goto cleanup;

    WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, mbName, size_needed, NULL, NULL);

cleanup:
    free(temp_name);
    return mbName;
}

static char *get_fallback(void *priv, ASS_Library *lib,
                          const char *base, uint32_t codepoint)
{
    HRESULT hr;
    ProviderPrivate *provider_priv = (ProviderPrivate *)priv;
    IDWriteFactory *dw_factory = provider_priv->factory;
    IDWriteTextFormat *text_format = NULL;
    IDWriteTextLayout *text_layout = NULL;
    FallbackLogTextRenderer renderer;

    init_FallbackLogTextRenderer(&renderer, dw_factory);

    hr = IDWriteFactory_CreateTextFormat(dw_factory, FALLBACK_DEFAULT_FONT, NULL,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 1.0f, L"", &text_format);
    if (FAILED(hr)) {
        return NULL;
    }

    // Encode codepoint as UTF-16
    wchar_t char_string[2];
    int char_len = encode_utf16(char_string, codepoint);

    // Create a text_layout, a high-level text rendering facility, using
    // the given codepoint and dummy format.
    hr = IDWriteFactory_CreateTextLayout(dw_factory, char_string, char_len, text_format,
        0.0f, 0.0f, &text_layout);
    if (FAILED(hr)) {
        IDWriteTextFormat_Release(text_format);
        return NULL;
    }

    // Draw the layout with a dummy renderer, which logs the
    // font used and stores it.
    IDWriteFont *font = NULL;
    hr = IDWriteTextLayout_Draw(text_layout, &font, &renderer.iface, 0.0f, 0.0f);
    if (FAILED(hr) || font == NULL) {
        IDWriteTextLayout_Release(text_layout);
        IDWriteTextFormat_Release(text_format);
        return NULL;
    }

    // We're done with these now
    IDWriteTextLayout_Release(text_layout);
    IDWriteTextFormat_Release(text_format);

    // DirectWrite may not have found a valid fallback, so check that
    // the selected font actually has the requested glyph.
    BOOL exists = FALSE;
    if (codepoint > 0) {
        hr = IDWriteFont_HasCharacter(font, codepoint, &exists);
        if (FAILED(hr) || !exists) {
            IDWriteFont_Release(font);
            return NULL;
        }
    }

    // Now, just extract the first family name
    IDWriteLocalizedStrings *familyNames = NULL;
    hr = IDWriteFont_GetInformationalStrings(font,
            DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES,
            &familyNames, &exists);
    if (SUCCEEDED(hr) && !exists) {
        IDWriteFontFamily *fontFamily = NULL;
        hr = IDWriteFont_GetFontFamily(font, &fontFamily);
        if (SUCCEEDED(hr)) {
            hr = IDWriteFontFamily_GetFamilyNames(fontFamily, &familyNames);
            IDWriteFontFamily_Release(fontFamily);
        }
    }
    if (FAILED(hr)) {
        IDWriteFont_Release(font);
        return NULL;
    }

    char *family = get_utf8_name(familyNames, 0);

    IDWriteLocalizedStrings_Release(familyNames);
    IDWriteFont_Release(font);
    return family;
}

#define FONT_TYPE IDWriteFontFace3
#include "ass_directwrite_info_template.h"
#undef FONT_TYPE

#define FONT_TYPE IDWriteFont
#define FAMILY_AS_ARG
#include "ass_directwrite_info_template.h"
#undef FONT_TYPE
#undef FAMILY_AS_ARG

static void add_font_face(IDWriteFontFace *face, ASS_FontProvider *provider,
                          ASS_SharedHDC *shared_hdc,
                          IDWriteFontCollection *system_font_coll)
{
    ASS_FontProviderMetaData meta = {0};

    IDWriteFontFace3 *face3;
    HRESULT hr = IDWriteFontFace_QueryInterface(face, &IID_IDWriteFontFace3,
                                                (void **) &face3);
    if (SUCCEEDED(hr) && face3) {
        bool success = get_font_info_IDWriteFontFace3(face3, &meta);
        IDWriteFontFace3_Release(face3);
        if (!success)
            goto cleanup;
    } else if (system_font_coll) {
        IDWriteFont *font;
        hr = IDWriteFontCollection_GetFontFromFontFace(system_font_coll,
                                                       face, &font);
        if (SUCCEEDED(hr) && font) {
            bool success = get_font_info_IDWriteFont(font, NULL, &meta);
            IDWriteFont_Release(font);
            if (!success)
                goto cleanup;
        }
    }

    FontPrivate *font_priv = calloc(1, sizeof(*font_priv));
    if (!font_priv)
        goto cleanup;

    font_priv->face = face;
    face = NULL;

#if ASS_WINAPI_DESKTOP
    font_priv->shared_hdc = hdc_retain(shared_hdc);
#endif

    ass_font_provider_add_font(provider, &meta, NULL, 0, font_priv);

cleanup:
    if (meta.families) {
        for (int k = 0; k < meta.n_family; k++)
            free(meta.families[k]);
        free(meta.families);
    }

    if (meta.fullnames) {
        for (int k = 0; k < meta.n_fullname; k++)
            free(meta.fullnames[k]);
        free(meta.fullnames);
    }

    free(meta.postscript_name);

    if (face)
        IDWriteFontFace_Release(face);
}

#if ASS_WINAPI_DESKTOP

struct font_enum_priv {
    ASS_FontProvider *provider;
    IDWriteGdiInterop *gdi_interop;
    ASS_SharedHDC *shared_hdc;
    IDWriteFontCollection *system_font_coll;
};

/*
 * Windows has three similar functions: EnumFonts, EnumFontFamilies
 * and EnumFontFamiliesEx, which were introduced at different times.
 * Each takes a callback, and the declared callback type is the same
 * for all three. However, the actual arguments passed to the callback
 * have also changed over time. Some changes match the introduction
 * of new EnumFont... variants, and some don't. Documentation has
 * changed over the years, too, so it can be hard to figure out what
 * types, and when, are safe for the callback to take.
 *
 * In the header files, FONTENUMPROC is declared to take:
 *     CONST LOGFONT *, CONST TEXTMETRIC *
 * These are the baseline structs dating back to 16-bit Windows EnumFont.
 *
 * As of 2021, current versions of Microsoft's docs
 * for the EnumFontFamiliesEx callback use the same:
 *     const LOGFONT *, const TEXTMETRIC *
 * and for the EnumFontFamilies callback use:
 *     ENUMLOGFONT *, NEWTEXTMETRIC *
 * and mention that the first "can be cast as" ENUMLOGFONTEX or ENUMLOGFONTEXDV
 * while the second "can be an ENUMTEXTMETRIC structure" but for
 * non-TrueType fonts "is a pointer to a TEXTMETRIC structure".
 *
 * Docs from the 2000s (which include Win95/98/Me/NT/2000/XP details) use
 *     ENUMLOGFONTEX *, NEWTEXTMETRICEX *
 * in the EnumFontFamiliesEx callback's definition:
 *     https://web.archive.org/web/20050907052149
 *         /http://msdn.microsoft.com/library/en-us/gdi/fontext_9rmr.asp
 * and highlight these two extended struct types as advantages over
 * the EnumFontFamilies callback, suggesting that the actual arguments
 * to the EnumFontFamiliesEx callback have always been these two extended
 * structs. This is also reflected in the callback's parameter names
 * (which have stayed unchanged in the current docs).
 * EnumFontFamiliesEx itself was added in Windows NT 4.0 and 95.
 *
 * Similarly, the EnumFontFamilies callback's parameter names support
 * the idea that they have always used ENUMLOGFONT and NEWTEXTMETRIC.
 *
 * It seems the extra fields in NEWTEXTMETRIC[EX] compared to TEXTMETRIC
 * are merely zero-filled when they are irrelevant, rather than nonexistent
 * or inaccessible. This is further supported by the fact the later-still
 * struct ENUMTEXTMETRIC extends NEWTEXTMETRICEX even though the former
 * is/was (upon introduction) relevant only to PostScript fonts
 * while the latter is (supposedly) relevant only to TrueType fonts.
 *
 * Docs from the 2000s for ENUMLOGFONTEXDV and ENUMTEXTMETRIC:
 *     https://web.archive.org/web/20050306200105
 *         /http://msdn.microsoft.com/library/en-us/gdi/fontext_15gy.asp
 * seem to assert that the callback receives these two further-extended
 * structs *if and only if* running on Windows 2000 or newer.
 * We don't need them, but if we did, we'd have to check (or assume)
 * Windows version, because this extension does not seem to have
 * an associated feature check. Moreover, these structs are given
 * to the callbacks of all three EnumFont... function variants.
 * So even EnumFonts actually uses the extended structs in 21st-century
 * Windows, but the declared callback type can no longer be changed
 * without breaking existing C++ code due to its strongly-typed pointers.
 * When targeting modern Windows, though, it seems safe for consumer
 * code to take the newest structs and cast the function pointer
 * to the declared callback type.
 */
static int CALLBACK font_enum_proc(const ENUMLOGFONTW *lpelf,
                                   const NEWTEXTMETRICW *lpntm,
                                   DWORD FontType, LPARAM lParam)
{
    struct font_enum_priv *priv = (struct font_enum_priv *) lParam;
    HFONT hFont = NULL;
    HRESULT hr;

    if (FontType & RASTER_FONTTYPE)
        goto cleanup;

    hFont = CreateFontIndirectW(&lpelf->elfLogFont);
    if (!hFont)
        goto cleanup;

    HDC hdc = priv->shared_hdc->hdc;
    if (!SelectObject(hdc, hFont))
        goto cleanup;

    wchar_t selected_name[LF_FACESIZE];
    if (!GetTextFaceW(hdc, LF_FACESIZE, selected_name))
        goto cleanup;
    if (wcsncmp(selected_name, lpelf->elfLogFont.lfFaceName, LF_FACESIZE)) {
        // A different font was selected. This can happen if the requested
        // name is subject to charset-specific font substitution while
        // EnumFont... enumerates additional charsets contained in the font.
        // Alternatively, the font may have disappeared in the meantime.
        // Either way, there's no use looking at this font any further.
        goto cleanup;
    }

    // For single-font files, we could also use GetFontData, but for font
    // collection files, GDI does not expose the current font index.
    // CreateFontFaceFromHdc is able to find it out using a private API;
    // and it may also be more efficient if it doesn't copy the data.
    // The downside is that we must keep the HDC alive, at least on Windows 7,
    // to prevent the font from being deleted and breaking the IDWriteFontFace.
    IDWriteFontFace *face = NULL;
    hr = IDWriteGdiInterop_CreateFontFaceFromHdc(priv->gdi_interop,
                                                 hdc, &face);
    if (FAILED(hr) || !face)
        goto cleanup;

    add_font_face(face, priv->provider, priv->shared_hdc,
                  priv->system_font_coll);

cleanup:
    if (hFont)
        DeleteObject(hFont);

    return 1 /* continue enumerating */;
}

#else

static void add_font_set(IDWriteFontSet *fontSet, ASS_FontProvider *provider)
{
    UINT32 n = IDWriteFontSet_GetFontCount(fontSet);
    for (UINT32 i = 0; i < n; i++) {
        HRESULT hr;

        IDWriteFontFaceReference *faceRef;
        hr = IDWriteFontSet_GetFontFaceReference(fontSet, i, &faceRef);
        if (FAILED(hr) || !faceRef)
            continue;

        if (IDWriteFontFaceReference_GetLocality(faceRef) != DWRITE_LOCALITY_LOCAL)
            goto cleanup;

        // Simulations for bold or oblique are sometimes synthesized by
        // DirectWrite. We are only interested in physical fonts.
        if (IDWriteFontFaceReference_GetSimulations(faceRef) != 0)
            goto cleanup;

        IDWriteFontFace3 *face;
        hr = IDWriteFontFaceReference_CreateFontFace(faceRef, &face);
        if (FAILED(hr) || !face)
            goto cleanup;

        add_font_face((IDWriteFontFace *) face, provider, NULL, NULL);

cleanup:
        IDWriteFontFaceReference_Release(faceRef);
    }
}

static void add_font(IDWriteFont *font, IDWriteFontFamily *fontFamily,
                     ASS_FontProvider *provider)
{
    ASS_FontProviderMetaData meta = {0};
    if (!get_font_info_IDWriteFont(font, fontFamily, &meta))
        goto cleanup;

    FontPrivate *font_priv = calloc(1, sizeof(*font_priv));
    if (!font_priv)
        goto cleanup;
    font_priv->font = font;
    font = NULL;

    ass_font_provider_add_font(provider, &meta, NULL, 0, font_priv);

cleanup:
    if (meta.families) {
        for (int k = 0; k < meta.n_family; k++)
            free(meta.families[k]);
        free(meta.families);
    }

    if (meta.fullnames) {
        for (int k = 0; k < meta.n_fullname; k++)
            free(meta.fullnames[k]);
        free(meta.fullnames);
    }

    free(meta.postscript_name);

    if (font)
        IDWriteFont_Release(font);
}

#endif

/*
 * When a new font name is requested, called to load that font from Windows
 */
static void match_fonts(void *priv, ASS_Library *lib,
                        ASS_FontProvider *provider, char *name)
{
    ProviderPrivate *provider_priv = (ProviderPrivate *)priv;
    LOGFONTW lf = {0};

    // lfFaceName can hold up to LF_FACESIZE wchars; truncate longer names
    MultiByteToWideChar(CP_UTF8, 0, name, -1, lf.lfFaceName, LF_FACESIZE-1);

#if ASS_WINAPI_DESKTOP
    struct font_enum_priv enum_priv;

    enum_priv.shared_hdc = calloc(1, sizeof(ASS_SharedHDC));
    if (!enum_priv.shared_hdc)
        return;

    // Keep this HDC alive to keep the fonts alive. This seems to be necessary
    // on Windows 7, where the fonts can't be deleted as long as the DC lives
    // and where the converted IDWriteFontFaces only work as long as the fonts
    // aren't deleted, although not on Windows 10, where fonts can be deleted
    // even if the DC still lives but IDWriteFontFaces keep working even if
    // the fonts are deleted.
    //
    // But beware of threading: docs say CreateCompatibleDC(NULL) creates a DC
    // that is bound to the current thread and is deleted when the thread dies.
    // It's not forbidden to call libass functions from multiple threads
    // over the lifetime of a font provider, so this doesn't work for us.
    // Practical tests suggest that the docs are wrong and the DC actually
    // persists after its creating thread dies, but let's not rely on that.
    // The workaround is to do a longer dance that is effectively equivalent to
    // CreateCompatibleDC(NULL) but isn't specifically CreateCompatibleDC(NULL).
    HDC screen_dc = GetDC(NULL);
    if (!screen_dc) {
        free(enum_priv.shared_hdc);
        return;
    }
    HDC hdc = CreateCompatibleDC(screen_dc);
    ReleaseDC(NULL, screen_dc);
    if (!hdc) {
        free(enum_priv.shared_hdc);
        return;
    }

    enum_priv.provider = provider;
    enum_priv.gdi_interop = provider_priv->gdi_interop;
    enum_priv.shared_hdc->hdc = hdc;
    enum_priv.shared_hdc->ref_count = 1;

    enum_priv.system_font_coll = NULL;
    IDWriteFactory_GetSystemFontCollection(provider_priv->factory,
                                           &enum_priv.system_font_coll,
                                           FALSE);

    // EnumFontFamilies gives each font once, plus repeats for charset-specific
    // aliases. EnumFontFamiliesEx gives each charset of each font separately,
    // so it repeats each font as many times as it has charsets, regardless
    // of whether they have aliases. Other than this, the two functions are
    // equivalent. There's no reliable way to distinguish when two items
    // enumerated by either function refer to the same font (but different
    // aliases or charsets) or actually distinct fonts, so we add every item
    // as a separate font to our database and simply prefer the enumeration
    // function that tends to give fewer duplicates. Generally, many fonts
    // cover multiple charsets while very few have aliases, so we prefer
    // EnumFontFamilies.
    //
    // Furthermore, the requested name might be an empty string. In this case,
    // EnumFontFamilies will give us only fonts with empty names, whereas
    // EnumFontFamiliesEx would give us a list of all installed font families.
    EnumFontFamiliesW(hdc, lf.lfFaceName,
                      (FONTENUMPROCW) font_enum_proc, (LPARAM) &enum_priv);

    if (enum_priv.system_font_coll)
        IDWriteFontCollection_Release(enum_priv.system_font_coll);

    hdc_release(enum_priv.shared_hdc);
#else
    HRESULT hr;
    IDWriteFactory3 *factory3;
    hr = IDWriteFactory_QueryInterface(provider_priv->factory,
                                       &IID_IDWriteFactory3, (void **) &factory3);
    if (SUCCEEDED(hr) && factory3) {
        IDWriteFontSet *fontSet;
        hr = IDWriteFactory3_GetSystemFontSet(factory3, &fontSet);
        IDWriteFactory3_Release(factory3);
        if (FAILED(hr) || !fontSet)
            return;

        DWRITE_FONT_PROPERTY_ID property_ids[] = {
            DWRITE_FONT_PROPERTY_ID_WIN32_FAMILY_NAME,
            DWRITE_FONT_PROPERTY_ID_FULL_NAME,
            DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME,
        };
        for (int i = 0; i < sizeof property_ids / sizeof *property_ids; i++) {
            DWRITE_FONT_PROPERTY property = {
                property_ids[i],
                lf.lfFaceName,
                L"",
            };

            IDWriteFontSet *filteredSet;
            hr = IDWriteFontSet_GetMatchingFonts(fontSet, &property,
                                                 1, &filteredSet);
            if (FAILED(hr) || !filteredSet)
                continue;

            add_font_set(filteredSet, provider);

            IDWriteFontSet_Release(filteredSet);
        }

        IDWriteFontSet_Release(fontSet);
    } else {
        // We must be in Windows 8 WinRT. IDWriteFontSet is not yet
        // supported, but GDI calls are forbidden. Our only options are
        // FindFamilyName, CreateFontFromLOGFONT, and eager preloading
        // of all fonts. The two lazy options are similar, with the
        // difference that FindFamilyName searches by WWS_FAMILY_NAME
        // whereas CreateFontFromLOGFONT searches by WIN32_FAMILY_NAME.
        // The latter is what GDI uses, so pick that. In at least some
        // versions of Windows, it also searches by FULL_NAME, which is
        // even better, but it's unclear whether this includes Windows 8.
        // It never searches by POSTSCRIPT_NAME. This means we won't
        // find CFF-outline fonts by their PostScript name and may not
        // find TrueType fonts by their full name; but we can't fix this
        // without loading the entire font list.
        lf.lfWeight = FW_DONTCARE;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = ANTIALIASED_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
        IDWriteFont *font = NULL;
        hr = IDWriteGdiInterop_CreateFontFromLOGFONT(provider_priv->gdi_interop,
                                                     &lf, &font);
        if (FAILED(hr) || !font)
            return;

        IDWriteFontFamily *fontFamily = NULL;
        hr = IDWriteFont_GetFontFamily(font, &fontFamily);
        IDWriteFont_Release(font);
        if (FAILED(hr) || !fontFamily)
            return;

        UINT32 n = IDWriteFontFamily_GetFontCount(fontFamily);
        for (UINT32 i = 0; i < n; i++) {
            hr = IDWriteFontFamily_GetFont(fontFamily, i, &font);
            if (FAILED(hr))
                continue;

            // Simulations for bold or oblique are sometimes synthesized by
            // DirectWrite. We are only interested in physical fonts.
            if (IDWriteFont_GetSimulations(font) != 0) {
                IDWriteFont_Release(font);
                continue;
            }

            add_font(font, fontFamily, provider);
        }

        IDWriteFontFamily_Release(fontFamily);
    }
#endif
}

static void get_substitutions(void *priv, const char *name,
                              ASS_FontProviderMetaData *meta)
{
    const int n = sizeof(font_substitutions) / sizeof(font_substitutions[0]);
    ass_map_font(font_substitutions, n, name, meta);
}

/*
 * Called by libass when the provider should perform the
 * specified task
 */
static ASS_FontProviderFuncs directwrite_callbacks = {
    .get_data           = get_data,
    .check_postscript   = check_postscript,
    .check_glyph        = check_glyph,
    .destroy_font       = destroy_font,
    .destroy_provider   = destroy_provider,
    .match_fonts        = match_fonts,
    .get_substitutions  = get_substitutions,
    .get_fallback       = get_fallback,
    .get_font_index     = get_font_index,
};

#if ASS_WINAPI_DESKTOP
typedef HRESULT (WINAPI *DWriteCreateFactoryFn)
#else
// LoadLibrary is forbidden in WinRT/UWP apps, so use DirectWrite directly.
// These apps cannot run on older Windows that lacks DirectWrite,
// so we lose nothing.
HRESULT WINAPI DWriteCreateFactory
#endif
(
    DWRITE_FACTORY_TYPE factoryType,
    REFIID              iid,
    IUnknown            **factory
);

/*
 * Register the directwrite provider. Upon registering
 * scans all system fonts. The private data for this
 * provider is IDWriteFactory
 * On failure returns NULL
 */
ASS_FontProvider *ass_directwrite_add_provider(ASS_Library *lib,
                                               ASS_FontSelector *selector,
                                               const char *config,
                                               FT_Library ftlib)
{
    HRESULT hr = S_OK;
    IDWriteFactory *dwFactory = NULL;
    IDWriteGdiInterop *dwGdiInterop = NULL;
    ASS_FontProvider *provider = NULL;
    ProviderPrivate *priv = NULL;

#if ASS_WINAPI_DESKTOP
    HMODULE directwrite_lib = LoadLibraryW(L"Dwrite.dll");
    if (!directwrite_lib)
        goto cleanup;

    DWriteCreateFactoryFn DWriteCreateFactory =
        (DWriteCreateFactoryFn)(void *)GetProcAddress(directwrite_lib,
                                                      "DWriteCreateFactory");
    if (!DWriteCreateFactory)
        goto cleanup;
#endif

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             &IID_IDWriteFactory,
                             (IUnknown **) (&dwFactory));
    if (FAILED(hr) || !dwFactory) {
        ass_msg(lib, MSGL_WARN, "Failed to initialize directwrite.");
        dwFactory = NULL;
        goto cleanup;
    }

    hr = IDWriteFactory_GetGdiInterop(dwFactory,
                                      &dwGdiInterop);
    if (FAILED(hr) || !dwGdiInterop) {
        ass_msg(lib, MSGL_WARN, "Failed to get IDWriteGdiInterop.");
        dwGdiInterop = NULL;
        goto cleanup;
    }

    priv = calloc(sizeof(*priv), 1);
    if (!priv)
        goto cleanup;

#if ASS_WINAPI_DESKTOP
    priv->directwrite_lib = directwrite_lib;
#endif
    priv->factory = dwFactory;
    priv->gdi_interop = dwGdiInterop;

    provider = ass_font_provider_new(selector, &directwrite_callbacks, priv);
    if (!provider)
        goto cleanup;

    return provider;

cleanup:

    free(priv);
    if (dwGdiInterop)
        dwGdiInterop->lpVtbl->Release(dwGdiInterop);
    if (dwFactory)
        dwFactory->lpVtbl->Release(dwFactory);
#if ASS_WINAPI_DESKTOP
    if (directwrite_lib)
        FreeLibrary(directwrite_lib);
#endif

    return NULL;
}

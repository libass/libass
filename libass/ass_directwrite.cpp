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

#include "config.h"

#include <dwrite.h>

extern "C" {
#include "ass_directwrite.h"
#include "ass_utils.h"
}

#define NAME_MAX_LENGTH 256
#define FALLBACK_DEFAULT_FONT L"Arial"

/*
 * The private data stored for every font, detected by this backend.
 */
typedef struct {
    IDWriteFont *font;
    IDWriteFontFileStream *stream;
} FontPrivate;

/**
 * Custom text renderer class for logging the fonts used. It does not
 * actually render anything or do anything apart from that.
 */
class FallbackLogTextRenderer : public IDWriteTextRenderer {
public:
    FallbackLogTextRenderer(IDWriteFactory *factory)
        : dw_factory(factory), ref_count(0)
    {}

    IFACEMETHOD(IsPixelSnappingDisabled)(
        _In_opt_ void* clientDrawingContext,
        _Out_ BOOL* isDisabled
        )
    {
        *isDisabled = true;
        return S_OK;
    }

    IFACEMETHOD(GetCurrentTransform)(
        _In_opt_ void* clientDrawingContext,
        _Out_ DWRITE_MATRIX* transform
        )
    {
        return E_NOTIMPL;
    }

    IFACEMETHOD(GetPixelsPerDip)(
        _In_opt_ void* clientDrawingContext,
        _Out_ FLOAT* pixelsPerDip
        )
    {
        return E_NOTIMPL;
    }

    IFACEMETHOD(DrawGlyphRun)(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode,
        _In_ DWRITE_GLYPH_RUN const* glyphRun,
        _In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
        IUnknown* clientDrawingEffect
        )
    {
        HRESULT hr;
        IDWriteFontCollection *font_coll = NULL;
        IDWriteFont **font = static_cast<IDWriteFont **>(clientDrawingContext);

        hr = dw_factory->GetSystemFontCollection(&font_coll);
        if (FAILED(hr))
            return E_FAIL;

        hr = font_coll->GetFontFromFontFace(glyphRun->fontFace, font);
        if (FAILED(hr))
            return E_FAIL;

        return S_OK;
    }

    IFACEMETHOD(DrawUnderline)(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        _In_ DWRITE_UNDERLINE const* underline,
        IUnknown* clientDrawingEffect
        )
    {
        return S_OK;
    }

    IFACEMETHOD(DrawStrikethrough)(
        _In_opt_ void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        _In_ DWRITE_STRIKETHROUGH const* strikethrough,
        IUnknown* clientDrawingEffect
        )
    {
        return S_OK;
    }

    IFACEMETHOD(DrawInlineObject)(
        _In_opt_ void* clientDrawingContext,
        FLOAT originX,
        FLOAT originY,
        IDWriteInlineObject* inlineObject,
        BOOL isSideways,
        BOOL isRightToLeft,
        IUnknown* clientDrawingEffect
        )
    {
        return S_OK;
    }

    // IUnknown methods

    IFACEMETHOD_(unsigned long, AddRef)()
    {
        return InterlockedIncrement(&ref_count);
    }

    IFACEMETHOD_(unsigned long, Release)()
    {
        unsigned long new_count = InterlockedDecrement(&ref_count);
        if (new_count == 0) {
            free(this);
            return 0;
        }

        return new_count;
    }

    IFACEMETHOD(QueryInterface)(
        IID const& riid,
        void** ppvObject
        )
    {
        if (__uuidof(IDWriteTextRenderer) == riid
            || __uuidof(IDWritePixelSnapping) == riid
            || __uuidof(IUnknown) == riid) {
            *ppvObject = this;
        } else {
            *ppvObject = NULL;
            return E_FAIL;
        }

        this->AddRef();
        return S_OK;
    }

private:
    IDWriteFactory * const dw_factory;
    unsigned long ref_count;
};

/*
 * This function is called whenever a font is used for the first
 * time. It will create a FontStream for memory reading, which
 * will be stored within the private data.
 */
static bool init_font_private(FontPrivate *priv)
{
    HRESULT hr = S_OK;
    IDWriteFont *font = priv->font;
    IDWriteFontFace *face = NULL;
    IDWriteFontFile *file = NULL;
    IDWriteFontFileStream *stream = NULL;
    IDWriteFontFileLoader *loader = NULL;
    UINT32 n_files = 1;
    const void *refKey = NULL;
    UINT32 keySize = 0;

    if (priv->stream != NULL)
        return true;

    hr = font->CreateFontFace(&face);
    if (FAILED(hr) || !face)
        return false;

    /* DirectWrite only supports one file per face */
    hr = face->GetFiles(&n_files, &file);
    if (FAILED(hr) || !file) {
        face->Release();
        return false;
    }

    hr = file->GetReferenceKey(&refKey, &keySize);
    if (FAILED(hr)) {
        file->Release();
        face->Release();
        return false;
    }

    hr = file->GetLoader(&loader);
    if (FAILED(hr) || !loader) {
        file->Release();
        face->Release();
        return false;
    }

    hr = loader->CreateStreamFromKey(refKey, keySize, &stream);
    if (FAILED(hr) || !stream) {
        file->Release();
        face->Release();
        return false;
    }

    priv->stream = stream;
    file->Release();
    face->Release();

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

    if (!init_font_private(priv))
        return 0;

    if (buf == NULL) {
        UINT64 fileSize;
        hr = priv->stream->GetFileSize(&fileSize);
        if (FAILED(hr))
            return 0;

        return fileSize;
    }

    hr = priv->stream->ReadFileFragment(&fileBuf, offset, length, &fragContext);

    if (FAILED(hr) || !fileBuf)
        return 0;

    memcpy(buf, fileBuf, length);

    priv->stream->ReleaseFileFragment(fragContext);

    return length;
}

/*
 * Checks if the passed font has a specific unicode
 * character. Returns 0 for failure and 1 for success
 */
static int check_glyph(void *data, uint32_t code)
{
    HRESULT hr = S_OK;
    FontPrivate *priv = (FontPrivate *) data;
    BOOL exists = FALSE;

    if (code == 0)
        return 1;

    priv->font->HasCharacter(code, &exists);
    if (FAILED(hr))
        return 0;

    return exists;
}

/*
 * This will release the directwrite backend
 */
static void destroy_provider(void *priv)
{
    ((IDWriteFactory *) priv)->Release();
}

/*
 * This will destroy a specific font and it's
 * Fontstream (in case it does exist)
 */

static void destroy_font(void *data)
{
    FontPrivate *priv = (FontPrivate *) data;

    priv->font->Release();
    if (priv->stream != NULL)
        priv->stream->Release();

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

static char *get_fallback(void *priv, ASS_FontProviderMetaData *meta,
                          uint32_t codepoint)
{
    HRESULT hr;
    IDWriteFactory *dw_factory = static_cast<IDWriteFactory *>(priv);
    IDWriteTextFormat *text_format = NULL;
    IDWriteTextLayout *text_layout = NULL;
    FallbackLogTextRenderer renderer(dw_factory);

    hr = dw_factory->CreateTextFormat(FALLBACK_DEFAULT_FONT, NULL,
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
    hr = dw_factory->CreateTextLayout(char_string, char_len, text_format,
        0.0f, 0.0f, &text_layout);
    if (FAILED(hr)) {
        text_format->Release();
        return NULL;
    }

    // Draw the layout with a dummy renderer, which logs the
    // font used and stores it.
    IDWriteFont *font = NULL;
    hr = text_layout->Draw(&font, &renderer, 0.0f, 0.0f);
    if (FAILED(hr) || font == NULL) {
        text_layout->Release();
        text_format->Release();
        return NULL;
    }

    // We're done with these now
    text_layout->Release();
    text_format->Release();

    // Now, just extract the first family name
    BOOL exists = FALSE;
    IDWriteLocalizedStrings *familyNames = NULL;
    hr = font->GetInformationalStrings(
            DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES,
            &familyNames, &exists);
    if (FAILED(hr) || !exists) {
        font->Release();
        return NULL;
    }

    wchar_t temp_name[NAME_MAX_LENGTH];
    hr = familyNames->GetString(0, temp_name, NAME_MAX_LENGTH);
    if (FAILED(hr)) {
        familyNames->Release();
        font->Release();
        return NULL;
    }
    temp_name[NAME_MAX_LENGTH-1] = 0;

    // DirectWrite may not have found a valid fallback, so check that
    // the selected font actually has the requested glyph.
    hr = font->HasCharacter(codepoint, &exists);
    if (FAILED(hr) || !exists) {
        familyNames->Release();
        font->Release();
        return NULL;
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, NULL, 0,NULL, NULL);
    char *family = (char *) malloc(size_needed);
    WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, family, size_needed, NULL, NULL);

    familyNames->Release();
    font->Release();
    return family;
}

static int map_width(enum DWRITE_FONT_STRETCH stretch)
{
    switch (stretch) {
    case DWRITE_FONT_STRETCH_ULTRA_CONDENSED: return 50;
    case DWRITE_FONT_STRETCH_EXTRA_CONDENSED: return 63;
    case DWRITE_FONT_STRETCH_CONDENSED:       return FONT_WIDTH_CONDENSED;
    case DWRITE_FONT_STRETCH_SEMI_CONDENSED:  return 88;
    case DWRITE_FONT_STRETCH_MEDIUM:          return FONT_WIDTH_NORMAL;
    case DWRITE_FONT_STRETCH_SEMI_EXPANDED:   return 113;
    case DWRITE_FONT_STRETCH_EXPANDED:        return FONT_WIDTH_EXPANDED;
    case DWRITE_FONT_STRETCH_EXTRA_EXPANDED:  return 150;
    case DWRITE_FONT_STRETCH_ULTRA_EXPANDED:  return 200;
    default:
        assert(0);
    }
}

/*
 * Scan every system font on the current machine and add it
 * to the libass lookup. Stores the FontPrivate as private data
 * for later memory reading
 */
static void scan_fonts(IDWriteFactory *factory,
                       ASS_FontProvider *provider)
{
    HRESULT hr = S_OK;
    IDWriteFontCollection *fontCollection = NULL;
    IDWriteFont *font = NULL;
    DWRITE_FONT_METRICS metrics;
    DWRITE_FONT_STYLE style;
    ASS_FontProviderMetaData meta = ASS_FontProviderMetaData();
    hr = factory->GetSystemFontCollection(&fontCollection, FALSE);
    wchar_t temp_name[NAME_MAX_LENGTH];
    int size_needed = 0;

    if (FAILED(hr) || !fontCollection)
        return;

    UINT32 familyCount = fontCollection->GetFontFamilyCount();

    for (UINT32 i = 0; i < familyCount; ++i) {
        IDWriteFontFamily *fontFamily = NULL;
        IDWriteLocalizedStrings *familyNames = NULL;
        IDWriteLocalizedStrings *fontNames = NULL;
        IDWriteLocalizedStrings *psNames = NULL;
        BOOL exists = FALSE;
        char *psName = NULL;

        hr = fontCollection->GetFontFamily(i, &fontFamily);
        if (FAILED(hr))
            continue;

        UINT32 fontCount = fontFamily->GetFontCount();
        for (UINT32 j = 0; j < fontCount; ++j) {
            hr = fontFamily->GetFont(j, &font);
            if (FAILED(hr))
                continue;

            // Simulations for bold or oblique are sometimes synthesized by
            // DirectWrite. We are only interested in physical fonts.
            if (font->GetSimulations() != 0) {
                font->Release();
                continue;
            }

            meta.weight = font->GetWeight();
            meta.width = map_width(font->GetStretch());
            font->GetMetrics(&metrics);
            style = font->GetStyle();
            meta.slant = (style == DWRITE_FONT_STYLE_NORMAL) ? FONT_SLANT_NONE :
                         (style == DWRITE_FONT_STYLE_OBLIQUE)? FONT_SLANT_OBLIQUE :
                         (style == DWRITE_FONT_STYLE_ITALIC) ? FONT_SLANT_ITALIC : FONT_SLANT_NONE;

            hr = font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &psNames,&exists);
            if (FAILED(hr)) {
                font->Release();
                continue;
            }

            if (exists) {
                hr = psNames->GetString(0, temp_name, NAME_MAX_LENGTH);
                if (FAILED(hr)) {
                    psNames->Release();
                    font->Release();
                    continue;
                }

                temp_name[NAME_MAX_LENGTH-1] = 0;
                size_needed = WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, NULL, 0,NULL, NULL);
                psName = (char *) malloc(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, psName, size_needed, NULL, NULL);
                psNames->Release();
            }

            hr = font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_FULL_NAME, &fontNames,&exists);
            if (FAILED(hr)) {
                font->Release();
                continue;
            }

            if (exists) {
                meta.n_fullname = fontNames->GetCount();
                meta.fullnames = (char **) calloc(meta.n_fullname, sizeof(char *));
                for (UINT32 k = 0; k < meta.n_fullname; ++k) {
                    hr = fontNames->GetString(k, temp_name, NAME_MAX_LENGTH);
                    if (FAILED(hr)) {
                        continue;
                    }

                    temp_name[NAME_MAX_LENGTH-1] = 0;
                    size_needed = WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, NULL, 0, NULL, NULL);
                    char *mbName = (char *) malloc(size_needed);
                    WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, mbName, size_needed, NULL, NULL);
                    meta.fullnames[k] = mbName;
                }
                fontNames->Release();
            }

            hr = font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &familyNames, &exists);
            if (!exists)
                hr = fontFamily->GetFamilyNames(&familyNames);
            if (FAILED(hr)) {
                font->Release();
                continue;
            }

            meta.n_family = familyNames->GetCount();
            meta.families = (char **) calloc(meta.n_family, sizeof(char *));
            for (UINT32 k = 0; k < meta.n_family; ++k) {
                hr = familyNames->GetString(k, temp_name, NAME_MAX_LENGTH);
                if (FAILED(hr)) {
                    continue;
                }

                temp_name[NAME_MAX_LENGTH-1] = 0;
                size_needed = WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, NULL, 0,NULL, NULL);
                char *mbName = (char *) malloc(size_needed);
                WideCharToMultiByte(CP_UTF8, 0, temp_name, -1, mbName, size_needed, NULL, NULL);
                meta.families[k] = mbName;
            }
            familyNames->Release();

            FontPrivate *font_priv = (FontPrivate *) calloc(1, sizeof(*font_priv));
            font_priv->font = font;

            ass_font_provider_add_font(provider, &meta, NULL, 0, psName, font_priv);

            for (UINT32 k = 0; k < meta.n_family; ++k)
                free(meta.families[k]);
            for (UINT32 k = 0; k < meta.n_fullname; ++k)
                free(meta.fullnames[k]);
            free(meta.fullnames);
            free(meta.families);
            free(psName);
        }
    }
}

/*
 * Called by libass when the provider should perform the
 * specified task
 */
static ASS_FontProviderFuncs directwrite_callbacks = {
    get_data,
    check_glyph,
    destroy_font,
    destroy_provider,
    NULL,
    NULL,
    get_fallback
};


/*
 * Register the directwrite provider. Upon registering
 * scans all system fonts. The private data for this
 * provider is IDWriteFactory
 * On failure returns NULL
 */
ASS_FontProvider *ass_directwrite_add_provider(ASS_Library *lib,
                                               ASS_FontSelector *selector,
                                               const char *config)
{
    HRESULT hr = S_OK;
    IDWriteFactory *dwFactory = NULL;
    ASS_FontProvider *provider = NULL;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             (IUnknown **) (&dwFactory));

    if (FAILED(hr)) {
        ass_msg(lib, MSGL_WARN, "Failed to initialize directwrite.");
        return NULL;
    }

    provider = ass_font_provider_new(selector, &directwrite_callbacks, dwFactory);

    scan_fonts(dwFactory, provider);

    return provider;
}

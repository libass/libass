/*
 * Copyright (C) 2015 Stephan Vedder <stefano.pigozzi@gmail.com>
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

#ifdef CONFIG_DIRECTWRITE

#include <dwrite.h>

extern "C"
{
#include "ass_directwrite.h"
#include "ass_utils.h"
}

typedef struct {
	IDWriteFont *font;
	IDWriteFontFileStream *stream;
} FontPrivate;

static bool init_font_private(FontPrivate *priv)
{
	HRESULT hr = S_OK;
	IDWriteFont* font = priv->font;
	IDWriteFontFace* face = NULL;
	IDWriteFontFile* file = NULL;
	IDWriteFontFileStream* stream = NULL;
	IDWriteFontFileLoader* loader = NULL;
	UINT32 n_files = 1;
	const void* refKey = NULL;
	UINT32 keySize = 0;

	if (priv->stream != NULL)
		return true;

	hr = font->CreateFontFace(&face);
	if (FAILED(hr) || !face)
		return false;

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

static size_t get_data(void *data, unsigned char* buf, size_t offset, size_t length)
{
	HRESULT hr = S_OK;
	FontPrivate *priv = (FontPrivate *)data;
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

static int check_glyph(void *data, uint32_t code)
{
	HRESULT hr = S_OK;
	FontPrivate *priv = (FontPrivate *)data;
	BOOL exists = FALSE;

	if (code == 0)
		return 1;

	priv->font->HasCharacter(code, &exists);
	if (FAILED(hr))
		return 0;

	return exists;
}

static void destroy_provider(void *priv)
{
	((IDWriteFactory*)priv)->Release();
}

static void destroy_font(void *data)
{
	FontPrivate *priv = (FontPrivate *)data;

	priv->font->Release();
	if (priv->stream != NULL)
		priv->stream->Release();

	free(priv);
}

static int map_width(int stretch)
{
	return stretch * (100 / DWRITE_FONT_STRETCH_MEDIUM);
}

static void scan_fonts(IDWriteFactory *factory, ASS_FontProvider *provider)
{
	HRESULT hr = S_OK;
	IDWriteFontCollection* fontCollection = NULL;
	IDWriteFont* font = NULL;
	DWRITE_FONT_METRICS metrics;
	DWRITE_FONT_STYLE style;
	ASS_FontProviderMetaData meta = ASS_FontProviderMetaData();
	hr = factory->GetSystemFontCollection(&fontCollection,FALSE);
	wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
	int size_needed = 0;

	if(FAILED(hr)||!fontCollection)
		return;

	UINT32 familyCount = fontCollection->GetFontFamilyCount();

	for (UINT32 i = 0; i < familyCount; ++i)
    {
		IDWriteFontFamily* fontFamily = NULL;
		IDWriteLocalizedStrings* familyNames = NULL;
		IDWriteLocalizedStrings* fontNames = NULL;
		IDWriteLocalizedStrings* psNames = NULL;
		BOOL exists = FALSE;
		char* psName = NULL;

		// Get the font family.
		hr = fontCollection->GetFontFamily(i, &fontFamily);
		if (FAILED(hr))
			return;

		UINT32 fontCount = fontFamily->GetFontCount();
		for (UINT32 j = 0; j < fontCount; ++j)
		{
			hr = fontFamily->GetFont(j, &font);
			if (FAILED(hr))
				return;
			
			meta.weight = font->GetWeight();
			meta.width = map_width(font->GetStretch());
			font->GetMetrics(&metrics);
			style = font->GetStyle();
			meta.slant =	(style==DWRITE_FONT_STYLE_NORMAL)? FONT_SLANT_NONE:
							(style==DWRITE_FONT_STYLE_OBLIQUE)? FONT_SLANT_OBLIQUE: 
							(style==DWRITE_FONT_STYLE_ITALIC)? FONT_SLANT_ITALIC : FONT_SLANT_NONE;

			hr = font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &psNames, &exists);
			if (FAILED(hr))
				return;

			if (exists)
			{
				hr = psNames->GetString(0, localeName, LOCALE_NAME_MAX_LENGTH + 1);
				if (FAILED(hr))
					return;

				size_needed = WideCharToMultiByte(CP_UTF8, 0, localeName, -1, NULL, 0, NULL, NULL);
				psName = (char*)ass_aligned_alloc(32, size_needed);
				WideCharToMultiByte(CP_UTF8, 0, localeName, -1, psName, size_needed, NULL, NULL);
			}
		
			hr = font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_FULL_NAME, &fontNames, &exists);
			if (FAILED(hr))
				return;

			meta.n_fullname = fontNames->GetCount();
			meta.fullnames = (char **)calloc(meta.n_fullname, sizeof(char *));
			for (UINT32 k = 0; k < meta.n_fullname; ++k)
			{
				hr = fontNames->GetString(k,localeName, LOCALE_NAME_MAX_LENGTH + 1);
				if (FAILED(hr))
					return;

				size_needed = WideCharToMultiByte(CP_UTF8, 0, localeName, -1, NULL, 0, NULL, NULL);
				char* mbName = (char *)malloc(size_needed);
				WideCharToMultiByte(CP_UTF8, 0, localeName, -1, mbName, size_needed, NULL, NULL);
				meta.fullnames[k] = mbName;
			}

			hr = fontFamily->GetFamilyNames(&familyNames);
			if (FAILED(hr))
				return;
			
			meta.n_family = familyNames->GetCount();
			meta.families = (char **)calloc(meta.n_family, sizeof(char *));
			for (UINT32 k = 0; k < meta.n_family; ++k)
			{
				hr = familyNames->GetString(k, localeName, LOCALE_NAME_MAX_LENGTH + 1);
				if (FAILED(hr))
					return;

				size_needed = WideCharToMultiByte(CP_UTF8, 0, localeName, -1, NULL, 0, NULL, NULL);
				char* mbName = (char *)malloc(size_needed);
				WideCharToMultiByte(CP_UTF8, 0, localeName, -1, mbName, size_needed, NULL, NULL);
				meta.families[k] = mbName;
			}

			FontPrivate *font_priv = (FontPrivate *)calloc(1, sizeof(*font_priv));
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

static ASS_FontProviderFuncs directwrite_callbacks = {
    get_data,
	check_glyph,
    destroy_font,
    destroy_provider,
    NULL,
};

ASS_FontProvider *
ass_directwrite_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                          const char *config)
{
	HRESULT hr = S_OK;
	IDWriteFactory* dwFactory = NULL;
	ASS_FontProvider *provider = NULL;

	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            (IUnknown**)(&dwFactory));

	if(FAILED(hr))
	{
		ass_msg(lib, MSGL_WARN, "Failed to initialize directwrite.");
		goto exit;
	}	
	

    provider = ass_font_provider_new(selector, &directwrite_callbacks, dwFactory);

    scan_fonts(dwFactory,provider);
exit: 
    return provider;
}

#endif

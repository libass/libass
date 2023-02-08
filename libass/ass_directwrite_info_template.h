#define CONCAT(a, b) a ## b
#define NAME(suffix) CONCAT(get_font_info_, suffix)
static bool NAME(FONT_TYPE)(FONT_TYPE *font,
#ifdef FAMILY_AS_ARG
                            IDWriteFontFamily *fontFamily,
#endif
                            ASS_FontProviderMetaData *meta)
#undef NAME
#undef CONCAT
{
    HRESULT hr;
    BOOL exists;

    // This PostScript name will merely be logged by
    // ass_face_stream in case it encounters an error
    IDWriteLocalizedStrings *psNames;
    hr = font->lpVtbl->GetInformationalStrings(font,
            DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &psNames, &exists);
    if (FAILED(hr))
        return false;

    if (exists) {
        meta->postscript_name = get_utf8_name(psNames, 0);
        IDWriteLocalizedStrings_Release(psNames);
        if (!meta->postscript_name)
            return false;
    }

    IDWriteLocalizedStrings *familyNames;
    hr = font->lpVtbl->GetInformationalStrings(font,
            DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &familyNames, &exists);
    if (SUCCEEDED(hr) && !exists) {
#ifdef FAMILY_AS_ARG
        if (fontFamily)
            hr = IDWriteFontFamily_GetFamilyNames(fontFamily, &familyNames);
        else {
            hr = font->lpVtbl->GetFontFamily(font, &fontFamily);
            if (SUCCEEDED(hr))
                hr = IDWriteFontFamily_GetFamilyNames(fontFamily, &familyNames);
            IDWriteFontFamily_Release(fontFamily);
        }
#else
        hr = font->lpVtbl->GetFamilyNames(font, &familyNames);
#endif
    }
    if (FAILED(hr))
        return false;

    meta->extended_family = get_utf8_name(familyNames, 0);
    IDWriteLocalizedStrings_Release(familyNames);
    if (!meta->extended_family)
        return false;

    return true;
}

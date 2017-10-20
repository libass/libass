#ifdef CREATE_STRUCT_DEFINITIONS
#undef CREATE_STRUCT_DEFINITIONS
#define START(funcname, structname) \
    typedef struct structname {
#define GENERIC(type, member) \
        type member;
#define STRING(member) \
        char *member;
#define VECTOR(member) \
        ASS_Vector member;
#define END(typedefnamename) \
    } typedefnamename;

#elif defined(CREATE_COMPARISON_FUNCTIONS)
#undef CREATE_COMPARISON_FUNCTIONS
#define START(funcname, structname) \
    static bool funcname##_compare(void *key1, void *key2) \
    { \
        struct structname *a = key1; \
        struct structname *b = key2; \
        return // conditions follow
#define GENERIC(type, member) \
            a->member == b->member &&
#define STRING(member) \
            strcmp(a->member, b->member) == 0 &&
#define VECTOR(member) \
            a->member.x == b->member.x && a->member.y == b->member.y &&
#define END(typedefname) \
            true; \
    }

#elif defined(CREATE_HASH_FUNCTIONS)
#undef CREATE_HASH_FUNCTIONS
#define START(funcname, structname) \
    static uint32_t funcname##_hash(void *buf, uint32_t hval) \
    { \
        struct structname *p = buf;
#define GENERIC(type, member) \
        hval = fnv_32a_buf(&p->member, sizeof(p->member), hval);
#define STRING(member) \
        hval = fnv_32a_str(p->member, hval);
#define VECTOR(member) GENERIC(, member.x); GENERIC(, member.y);
#define END(typedefname) \
        return hval; \
    }

#else
#error missing defines
#endif



// describes an outline bitmap
START(outline_bitmap, outline_bitmap_hash_key)
    GENERIC(OutlineHashValue *, outline)
    GENERIC(int, frx) // signed 10.22
    GENERIC(int, fry) // signed 10.22
    GENERIC(int, frz) // signed 10.22
    GENERIC(int, fax) // signed 16.16
    GENERIC(int, fay) // signed 16.16
    // shift vector that was added to glyph before applying rotation
    // = 0, if frx = fry = frx = 0
    // = (glyph base point) - (rotation origin), otherwise
    GENERIC(int, shift_x)
    GENERIC(int, shift_y)
    VECTOR(advance) // subpixel shift vector
END(OutlineBitmapHashKey)

// describe a clip mask bitmap
START(clip_bitmap, clip_bitmap_hash_key)
    GENERIC(int, scale)
    STRING(text)
END(ClipMaskHashKey)

START(glyph_metrics, glyph_metrics_hash_key)
    GENERIC(ASS_Font *, font)
    GENERIC(double, size)
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
END(GlyphMetricsHashKey)

// common outline data
START(outline_common, outline_common_hash_key)
    GENERIC(unsigned, scale_x) // 16.16
    GENERIC(unsigned, scale_y) // 16.16
    VECTOR(outline) // border width, 26.6
    GENERIC(unsigned, border_style)
    GENERIC(int, scale_fix)    // 16.16
    GENERIC(int, advance)      // 26.6
END(OutlineCommonKey)

// describes an outline glyph
START(glyph, glyph_hash_key)
    GENERIC(unsigned, scale_x) // 16.16
    GENERIC(unsigned, scale_y) // 16.16
    VECTOR(outline) // border width, 26.6
    GENERIC(unsigned, border_style)
    GENERIC(int, scale_fix)    // 16.16
    GENERIC(int, advance)      // 26.6

    GENERIC(ASS_Font *, font)
    GENERIC(double, size) // font size
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
    GENERIC(int, bold)
    GENERIC(int, italic)
    GENERIC(unsigned, flags) // glyph decoration flags
END(GlyphHashKey)

// describes an outline drawing
START(drawing, drawing_hash_key)
    GENERIC(unsigned, scale_x) // 16.16
    GENERIC(unsigned, scale_y) // 16.16
    VECTOR(outline) // border width, 26.6
    GENERIC(unsigned, border_style)
    GENERIC(int, scale_fix)    // 16.16
    GENERIC(int, advance)      // 26.6

    GENERIC(int, pbo)
    GENERIC(int, scale)
    STRING(text)
END(DrawingHashKey)

// describes post-combining effects
START(filter, filter_desc)
    GENERIC(int, flags)
    GENERIC(int, be)
    GENERIC(double, blur)
    VECTOR(shadow)
END(FilterDesc)

#undef START
#undef GENERIC
#undef STRING
#undef VECTOR
#undef END

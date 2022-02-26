#ifdef CREATE_STRUCT_DEFINITIONS
#undef CREATE_STRUCT_DEFINITIONS
#define START(funcname, structname) \
    typedef struct structname {
#define GENERIC(type, member) \
        type member;
#define STRING(member) \
        ASS_StringView member;
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
            ass_string_equal(a->member, b->member) &&
#define VECTOR(member) \
            a->member.x == b->member.x && a->member.y == b->member.y &&
#define END(typedefname) \
            true; \
    }

#elif defined(CREATE_HASH_FUNCTIONS)
#undef CREATE_HASH_FUNCTIONS
#define START(funcname, structname) \
    static ass_hashcode funcname##_hash(void *buf, ass_hashcode hval) \
    { \
        struct structname *p = buf;
#define GENERIC(type, member) \
        hval = ass_hash_buf(&p->member, sizeof(p->member), hval);
#define STRING(member) \
        hval = ass_hash_buf(p->member.str, p->member.len, hval);
#define VECTOR(member) GENERIC(, member.x); GENERIC(, member.y);
#define END(typedefname) \
        return hval; \
    }

#else
#error missing defines
#endif



START(font, ass_font_desc )
    STRING(family)
    GENERIC(unsigned, bold)
    GENERIC(unsigned, italic)
    GENERIC(int, vertical)  // @font vertical layout
END(ASS_FontDesc)

// describes an outline bitmap
START(bitmap, bitmap_hash_key)
    GENERIC(OutlineHashValue *, outline)
    // quantized transform matrix
    VECTOR(offset)
    VECTOR(matrix_x)
    VECTOR(matrix_y)
    VECTOR(matrix_z)
END(BitmapHashKey)

START(glyph_metrics, glyph_metrics_hash_key)
    GENERIC(ASS_Font *, font)
    GENERIC(double, size)
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
END(GlyphMetricsHashKey)

// describes an outline glyph
START(glyph, glyph_hash_key)
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
    STRING(text)
END(DrawingHashKey)

// describes an offset outline
START(border, border_hash_key)
    GENERIC(OutlineHashValue *, outline)
    // outline is scaled by 2^scale_ord_x|y before stroking
    // to keep stoker error in allowable range
    GENERIC(int, scale_ord_x)
    GENERIC(int, scale_ord_y)
    VECTOR(border)  // border size in STROKER_ACCURACY units
END(BorderHashKey)

// describes post-combining effects
START(filter, filter_desc)
    GENERIC(int, flags)
    GENERIC(int, be)
    GENERIC(int, blur_x)
    GENERIC(int, blur_y)
    VECTOR(shadow)
END(FilterDesc)

// describes glyph bitmap reference
START(bitmap_ref, bitmap_ref_key)
    GENERIC(Bitmap *, bm)
    GENERIC(Bitmap *, bm_o)
    VECTOR(pos)
    VECTOR(pos_o)
END(BitmapRef)

#undef START
#undef GENERIC
#undef STRING
#undef VECTOR
#undef END

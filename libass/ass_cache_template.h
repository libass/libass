#ifdef CREATE_STRUCT_DEFINITIONS
#undef CREATE_STRUCT_DEFINITIONS
#define START(funcname, structname) \
    typedef struct structname {
#define GENERIC(type, member) \
        type member;
#define STRING(member) \
        char *member;
#define BINSTRING(member) \
        struct { size_t size; void *data; } member;
#define FTVECTOR(member) \
        FT_Vector member;
#define BITMAPHASHKEY(member) \
        BitmapHashKey member;
#define END(typedefnamename) \
    } typedefnamename;

#elif defined(CREATE_COMPARISON_FUNCTIONS)
#undef CREATE_COMPARISON_FUNCTIONS
#define START(funcname, structname) \
    static unsigned funcname##_compare(void *key1, void *key2, size_t key_size) \
    { \
        struct structname *a = key1; \
        struct structname *b = key2; \
        return // conditions follow
#define GENERIC(type, member) \
            a->member == b->member &&
#define STRING(member) \
            strcmp(a->member, b->member) == 0 &&
#define BINSTRING(member) \
            a->member.size == b->member.size && \
            memcmp(a->member.data, b->member.data, a->member.size) == 0 &&
#define FTVECTOR(member) \
            a->member.x == b->member.x && a->member.y == b->member.y &&
#define BITMAPHASHKEY(member) \
            bitmap_compare(&a->member, &b->member, sizeof(a->member)) &&
#define END(typedefname) \
            1; \
    }

#elif defined(CREATE_HASH_FUNCTIONS)
#undef CREATE_HASH_FUNCTIONS
#define START(funcname, structname) \
    static unsigned funcname##_hash(void *buf, size_t len) \
    { \
        struct structname *p = buf; \
        unsigned hval = FNV1_32A_INIT;
#define GENERIC(type, member) \
        hval = fnv_32a_buf(&p->member, sizeof(p->member), hval);
#define STRING(member) \
        hval = fnv_32a_str(p->member, hval);
#define BINSTRING(member) \
        hval = fnv_32a_buf(&p->member.size, sizeof(p->member.size), hval); \
        hval = fnv_32a_buf(p->member.data, p->member.size, hval);
#define FTVECTOR(member) GENERIC(, member.x); GENERIC(, member.y);
#define BITMAPHASHKEY(member) { \
        unsigned temp = bitmap_hash(&p->member, sizeof(p->member)); \
        hval = fnv_32a_buf(&temp, sizeof(temp), hval); \
        }
#define END(typedefname) \
        return hval; \
    }

#else
#error missing defines
#endif



// describes an outline bitmap
START(outline_bitmap, outline_bitmap_hash_key)
    GENERIC(OutlineHashValue *, outline)
    GENERIC(char, be) // blur edges
    GENERIC(double, blur) // gaussian blur
    GENERIC(int, frx) // signed 16.16
    GENERIC(int, fry) // signed 16.16
    GENERIC(int, frz) // signed 16.16
    GENERIC(int, fax) // signed 16.16
    GENERIC(int, fay) // signed 16.16
    // shift vector that was added to glyph before applying rotation
    // = 0, if frx = fry = frx = 0
    // = (glyph base point) - (rotation origin), otherwise
    GENERIC(int, shift_x)
    GENERIC(int, shift_y)
    FTVECTOR(advance) // subpixel shift vector
    FTVECTOR(shadow_offset) // shadow subpixel shift
END(OutlineBitmapHashKey)

// describe a clip mask bitmap
START(clip_bitmap, clip_bitmap_hash_key)
    STRING(text)
END(ClipMaskHashKey)

// describes an outline glyph
START(glyph, glyph_hash_key)
    GENERIC(ASS_Font *, font)
    GENERIC(double, size) // font size
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
    GENERIC(int, bold)
    GENERIC(int, italic)
    GENERIC(unsigned, scale_x) // 16.16
    GENERIC(unsigned, scale_y) // 16.16
    FTVECTOR(outline) // border width, 16.16
    GENERIC(unsigned, flags)    // glyph decoration flags
    GENERIC(unsigned, border_style)
    GENERIC(int, hspacing) // 16.16
END(GlyphHashKey)

START(glyph_metrics, glyph_metrics_hash_key)
    GENERIC(ASS_Font *, font)
    GENERIC(double, size)
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
    GENERIC(unsigned, scale_x)
    GENERIC(unsigned, scale_y)
END(GlyphMetricsHashKey)

// describes an outline drawing
START(drawing, drawing_hash_key)
    GENERIC(unsigned, scale_x)
    GENERIC(unsigned, scale_y)
    GENERIC(int, pbo)
    FTVECTOR(outline)
    GENERIC(unsigned, border_style)
    GENERIC(int, hspacing)
    GENERIC(int, scale)
    GENERIC(unsigned, hash)
    STRING(text)
END(DrawingHashKey)

// Cache for composited bitmaps
START(composite, composite_hash_key)
    GENERIC(unsigned, w)
    GENERIC(unsigned, h)
    GENERIC(unsigned, o_w)
    GENERIC(unsigned, o_h)
    GENERIC(int, is_drawing)
    GENERIC(unsigned, chars)
    GENERIC(int, be)
    GENERIC(double, blur)
    GENERIC(int, border_style)
    GENERIC(int, has_border)
    GENERIC(double, border_x)
    GENERIC(double, border_y)
    GENERIC(double, shadow_x)
    GENERIC(double, shadow_y)
    GENERIC(double, frx)
    GENERIC(double, fry)
    GENERIC(double, frz)
    GENERIC(double, fax)
    GENERIC(double, fay)
    GENERIC(double, scale_x)
    GENERIC(double, scale_y)
    GENERIC(double, hspacing)
    GENERIC(unsigned, italic)
    GENERIC(unsigned, bold)
    GENERIC(int, flags)
    GENERIC(unsigned, has_outline)
    GENERIC(int, shift_x)
    GENERIC(int, shift_y)
    FTVECTOR(advance)
    BINSTRING(str)
END(CompositeHashKey)

#undef START
#undef GENERIC
#undef STRING
#undef FTVECTOR
#undef BITMAPHASHKEY
#undef BINSTRING
#undef END

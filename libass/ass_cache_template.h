#ifdef CREATE_STRUCT_DEFINITIONS
#undef CREATE_STRUCT_DEFINITIONS
#define START(funcname, structname) \
    typedef struct structname {
#define GENERIC(type, member) \
        type member;
#define STRING(member) \
        char *member;
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
    GENERIC(FT_Long, frx) // signed 10.22
    GENERIC(FT_Long, fry) // signed 10.22
    GENERIC(FT_Long, frz) // signed 10.22
    GENERIC(FT_Fixed, fax) // signed 16.16
    GENERIC(FT_Fixed, fay) // signed 16.16
    // shift vector that was added to glyph before applying rotation
    // = 0, if frx = fry = frx = 0
    // = (glyph base point) - (rotation origin), otherwise
    GENERIC(int, shift_x)
    GENERIC(int, shift_y)
    FTVECTOR(advance) // subpixel shift vector
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
    GENERIC(FT_Fixed, scale_x) // 16.16
    GENERIC(FT_Fixed, scale_y) // 16.16
    FTVECTOR(outline) // border width, 16.16
    GENERIC(unsigned, flags)    // glyph decoration flags
    GENERIC(unsigned, border_style)
    GENERIC(FT_Fixed, hspacing) // 16.16
END(GlyphHashKey)

START(glyph_metrics, glyph_metrics_hash_key)
    GENERIC(ASS_Font *, font)
    GENERIC(double, size)
    GENERIC(int, face_index)
    GENERIC(int, glyph_index)
    GENERIC(FT_Fixed, scale_x)
    GENERIC(FT_Fixed, scale_y)
END(GlyphMetricsHashKey)

// describes an outline drawing
START(drawing, drawing_hash_key)
    GENERIC(FT_Fixed, scale_x)
    GENERIC(FT_Fixed, scale_y)
    GENERIC(int, pbo)
    FTVECTOR(outline)
    GENERIC(unsigned, border_style)
    GENERIC(FT_Fixed, hspacing)
    GENERIC(int, scale)
    GENERIC(unsigned, hash)
    STRING(text)
END(DrawingHashKey)

// describes post-combining effects
START(filter, filter_desc)
    GENERIC(int, flags)
    GENERIC(int, be)
    GENERIC(double, blur)
    FTVECTOR(shadow)
END(FilterDesc)

#undef START
#undef GENERIC
#undef STRING
#undef FTVECTOR
#undef BITMAPHASHKEY
#undef END

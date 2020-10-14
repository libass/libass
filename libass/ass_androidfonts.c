/*
 * Copyright (C) 2018
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TYPE1_TABLES_H
#include <android/log.h>

#include "ass_androidfonts.h"
#include "ass_fontselect.h"
#include "ass_utils.h"

/*****************************************/
/* worlds worst XML parser in <300 lines */

enum xml_type {
    XML_TYPE_EOF,
    XML_TYPE_TEXT_CONTENT,
    XML_TYPE_TAG,
    XML_TYPE_ATTRIBUTE,
};

/* only type == XML_TYPE_TAG */
#define XML_FLAG_OPENING_TAG 1
#define XML_FLAG_CLOSING_TAG 2

#define XML_MAX_TEXT_LEN  1024
#define XML_MAX_TAG_LEN   64
#define XML_MAX_KEY_LEN   XML_MAX_TAG_LEN
#define XML_MAX_VALUE_LEN 128

struct xml_thing {
    enum xml_type type;
    unsigned flags;
    union {
        struct {
            char content[XML_MAX_TEXT_LEN];
        } text;
        struct {
            char name[XML_MAX_TAG_LEN]; // empty == parsing this tag has finished
        } tag;
        struct {
            char key[XML_MAX_KEY_LEN];
            char value[XML_MAX_VALUE_LEN];
        } attr;
    };
};

enum xml_state {
    XML_STATE_DOCBEGIN,
    XML_STATE_VOID,
    XML_STATE_COMMENT,
    XML_STATE_TAGNAME,
    XML_STATE_TAGINSIDE,
    XML_STATE_DOCEND,
};

#define XML_BLANKS     " \t\v\r\n"
#define XML_NEST_DEPTH 16

struct xml_parser {
    enum xml_state state;
    FILE *s;
    char error[100];

    char unget[8];
    int unget_count;

    char nested_tags[XML_MAX_TAG_LEN][XML_NEST_DEPTH];
    int nested_tag_count;
};

static struct xml_parser *xml_init(FILE *f)
{
    struct xml_parser *p = calloc(1, sizeof(struct xml_parser));
    if (!p)
        return NULL;
    p->state = XML_STATE_DOCBEGIN;
    p->s = f;
    p->unget_count = 0;
    p->nested_tag_count = 0;
    return p;
}

#define xerror(s) do { \
        strncpy(p->error, s, sizeof(p->error)); \
        return -1; \
    } while(0)

#define xgetchar2(ch, eof_flag) do { \
        if (p->unget_count > 0) \
            *(ch) = p->unget[--p->unget_count]; \
        else \
            *(eof_flag) = (fread(ch, 1, 1, p->s) == 0); \
    } while(0)

#define xungetc(ch) do { \
        if (p->unget_count == sizeof(p->unget)) \
            xerror("Insufficient array space"); \
        p->unget[p->unget_count++] = ch; \
    } while (0)

#define xgetchar(ch) do { \
        int _eof = 0; \
        xgetchar2(ch, &_eof); \
        if (_eof) \
            xerror("Unexpected end of file"); \
    } while(0)

#define xread(buf, n) do { \
        for (int _i = 0; _i < n; _i++) \
            xgetchar(&(buf)[_i]); \
    } while(0)

#define xskipwhile(cond) \
    do { \
        xgetchar(&ch); \
    } while(cond)

#define xcopyloop(dst, index, break_cond) do { \
        while (index < sizeof(dst)) { \
            int _eof = 0; \
            xgetchar2(&ch, &_eof); \
            if(_eof) \
                break; \
            if(break_cond) { \
                xungetc(ch); \
                break; \
            } \
            (dst)[i++] = ch; \
        } \
        if (index == sizeof(dst)) \
            xerror("Insufficient buffer space"); \
        (dst)[i] = 0; \
    } while(0)

static int xml_next2(struct xml_parser *p, struct xml_thing *ret)
{
    char ch, buf[10];
    switch (p->state) {
        case XML_STATE_DOCBEGIN: {
            xread(buf, 5);
            if (strncmp(buf, "<?xml", 5))
                xerror("Not a valid XML document");
            xread(buf, 2);
            while (strncmp(buf, "?>", 2)) {
                memmove(&buf[0], &buf[1], 1);
                xgetchar(&buf[1]);
            }
            p->state = XML_STATE_VOID;
            return 0;
        }
        case XML_STATE_VOID: {
            int eof = 0;
            xgetchar2(&ch, &eof);
            if (eof) {
                p->state = XML_STATE_DOCEND;
                return 0;
            } else if(ch == '<') {
                xskipwhile(strchr(XML_BLANKS, ch));
                buf[0] = ch;
                xread(&buf[1], 2);
                if (!strncmp(buf, "!--", 3)) {
                    p->state = XML_STATE_COMMENT;
                } else if (buf[0] == '!') { // ignore doctype
                    xskipwhile(ch != '>');
                } else {
                    for (int n = 2; n >= 0; n--)
                        xungetc(buf[n]);
                    p->state = XML_STATE_TAGNAME;
                }
                return 0;
            }
            ret->type = XML_TYPE_TEXT_CONTENT;
            ret->flags = 0;
            int i = 1;
            ret->text.content[0] = ch;
            xcopyloop(ret->text.content, i, ch == '<');
            return 1;
        }
        case XML_STATE_COMMENT: {
            xread(buf, 3);
            while (strncmp(buf, "-->", 3)) {
                memmove(&buf[0], &buf[1], 2);
                xgetchar(&buf[2]);
            }
            p->state = XML_STATE_VOID;
            return 0;
        }
        case XML_STATE_TAGNAME: {
            ret->type = XML_TYPE_TAG;
            ret->flags = 0;
            xgetchar(&ch);
            if (ch == '/') {
                ret->flags |= XML_FLAG_CLOSING_TAG;
                xskipwhile(strchr(XML_BLANKS, ch));
            } else {
                ret->flags |= XML_FLAG_OPENING_TAG;
            }
            int i = 1;
            ret->tag.name[0] = ch;
            xcopyloop(ret->tag.name, i, strchr(XML_BLANKS "/>", ch));
            p->state = XML_STATE_TAGINSIDE;
            return 1;
        }
        case XML_STATE_TAGINSIDE: {
            xskipwhile(strchr(XML_BLANKS, ch));
            ret->flags = 0;
            if (ch == '>' || ch == '/') {
                ret->type = XML_TYPE_TAG;
                ret->tag.name[0] = 0;
                if (ch == '/') {
                    ret->flags |= XML_FLAG_CLOSING_TAG;
                    xskipwhile(strchr(XML_BLANKS, ch));
                    if (ch != '>')
                        xerror("Expected tag to end here");
                }
                p->state = XML_STATE_VOID;
                return 1;
            }
            ret->type = XML_TYPE_ATTRIBUTE;
            int i = 1;
            ret->attr.key[0] = ch;
            xcopyloop(ret->attr.key, i, strchr(XML_BLANKS "=", ch));
            xskipwhile(strchr(XML_BLANKS, ch));
            if (ch != '=')
                xerror("Expected '=' to follow");
            xskipwhile(strchr(XML_BLANKS, ch));
            if (ch != '"' && ch != '\'')
                xerror("Expected quotes for attribute value to follow");
            char quote = ch;
            i = 0;
            while (i < sizeof(ret->attr.value)) {
                xgetchar(&ch);
                if (ch == quote)
                    break;
                ret->attr.value[i++] = ch;
            }
            if (i == sizeof(ret->attr.value))
                xerror("Insufficient buffer space");
            ret->attr.value[i] = 0;
            return 1;
        }
        case XML_STATE_DOCEND: {
            ret->type = XML_TYPE_EOF;
            ret->flags = 0;
            return 1;
        }
    }
    xerror("State machine broke");
}

static int xml_next(struct xml_parser *p, struct xml_thing *ret)
{
    int r;
    do {
        r = xml_next2(p, ret);
    } while (r == 0);
    if (r == -1)
        return r;

    if (ret->type == XML_TYPE_TEXT_CONTENT) {
        int useful = 0;
        for (int i = 0; ret->text.content[i]; i++)
            useful |= !strchr(XML_BLANKS, ret->text.content[i]);
        if (!useful) // skip text nodes that are blank
            return xml_next(p, ret);
    } else if (ret->type == XML_TYPE_TAG) {
        if (ret->tag.name[0]) {
            if (ret->flags & XML_FLAG_OPENING_TAG) {
                if (p->nested_tag_count == XML_NEST_DEPTH)
                    xerror("Insufficient array space");
                int cnt = p->nested_tag_count++;
                strncpy(p->nested_tags[cnt], ret->tag.name, sizeof(*p->nested_tags));
            } else if (ret->flags & XML_FLAG_CLOSING_TAG) {
                if (p->nested_tag_count == 0)
                    xerror("Superfluous closing tag");
                const char* expect = p->nested_tags[--p->nested_tag_count];
                if (strcmp(expect, ret->tag.name))
                    xerror("Mismatching closing tag");
            }
        } else {
            if (ret->flags & XML_FLAG_CLOSING_TAG) // self-closing
                p->nested_tag_count--;
        }
    } else if (ret->type == XML_TYPE_EOF) {
        if (p->nested_tag_count > 0)
            xerror("Missing closing tag(s)");
    }
    return 1;
}

#undef xerror
#undef xread
#undef xgetchar
#undef xskipwhile
#undef xcopyloop

static const char *xml_error(struct xml_parser *p)
{
    return p->error;
}

static void xml_free(struct xml_parser *p)
{
    free(p);
}

/*****************************************/

#define FONTS_XML "/system/etc/fonts.xml"
#define FONTS_PATH "/system/fonts"
#define MAX_FULLNAME 100

#define ALOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR,   "libass", fmt, ##__VA_ARGS__)
#define ALOGV(fmt, ...) __android_log_print(ANDROID_LOG_VERBOSE, "libass", fmt, ##__VA_ARGS__)
#define ALOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG,   "libass", fmt, ##__VA_ARGS__)

typedef struct af_private {
    int n_substitutions;
    ASS_FontMapping *substitutions;
} ProviderPrivate;

/************/

typedef struct font_data_ft {
    FT_Face face;
} FontDataFT;

static bool check_postscript_ft(void *data)
{
    FontDataFT *fd = (FontDataFT *)data;
    PS_FontInfoRec postscript_info;
    return !FT_Get_PS_Font_Info(fd->face, &postscript_info);
}

static bool check_glyph_ft(void *data, uint32_t codepoint)
{
    FontDataFT *fd = (FontDataFT *)data;

    if (!codepoint)
        return true;

    return !!FT_Get_Char_Index(fd->face, codepoint);
}

static void destroy_font_ft(void *data)
{
    FontDataFT *fd = (FontDataFT *)data;

    FT_Done_Face(fd->face);
    free(fd);
}

static void get_substitutions(void *priv, const char *name,
                              ASS_FontProviderMetaData *meta)
{
    ProviderPrivate *af = (ProviderPrivate *)priv;
    ass_map_font(af->substitutions, af->n_substitutions, name, meta);
}

static void destroy_provider(void *priv)
{
    ProviderPrivate *af = (ProviderPrivate *)priv;
    int n;
    for (n = 0; n < af->n_substitutions; n++) {
        ASS_FontMapping *map = &af->substitutions[n];
        free((void *)map->from);
        free((void *)map->to);
    }
    free(af->substitutions);
    free(af);
}

static bool get_extra_font_info(FT_Library lib, FT_Face face, ASS_FontProviderMetaData *info)
{
    int i;
    int num_fullname = 0;
    int num_names = FT_Get_Sfnt_Name_Count(face);
    char *fullnames[MAX_FULLNAME];

    // we're only interested in outlines
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
        return false;

    for (i = 0; i < num_names; i++) {
        FT_SfntName name;

        if (FT_Get_Sfnt_Name(face, i, &name))
            continue;

        if (name.platform_id == TT_PLATFORM_MICROSOFT &&
                (name.name_id == TT_NAME_ID_FULL_NAME ||
                 name.name_id == TT_NAME_ID_FONT_FAMILY)) {
            char buf[1024];
            ass_utf16be_to_utf8(buf, sizeof(buf), (uint8_t *)name.string,
                                name.string_len);

            if (name.name_id == TT_NAME_ID_FULL_NAME) {
                fullnames[num_fullname] = strdup(buf);
                if (fullnames[num_fullname] == NULL)
                    goto error;
                num_fullname++;
            }
        }
    }

    info->postscript_name = (char *)FT_Get_Postscript_Name(face);

    if (num_fullname) {
        info->fullnames = calloc(sizeof(char *), num_fullname);
        if (info->fullnames == NULL)
            goto error;
        memcpy(info->fullnames, &fullnames, sizeof(char *) * num_fullname);
        info->n_fullname = num_fullname;
    }

    return true;

error:
    for (i = 0; i < num_fullname; i++)
        free(fullnames[i]);

    free(info->fullnames);

    return false;
}

static void free_extra_font_info(ASS_FontProviderMetaData *meta)
{
    int i;

    for (i = 0; i < meta->n_fullname; i++)
        free(meta->fullnames[i]);

    free(meta->fullnames);
}

/************/

void *g_ftlibrary;

static void add_font(ASS_FontProvider *provider, ASS_FontProviderMetaData *meta, int index, char *path)
{
    int rc;
    FT_Face face;
    FontDataFT *ft;
    FT_Library ftlibrary = (FT_Library) g_ftlibrary;

    if (!meta->families[0]) // TODO: don't skip fonts w/o family?
        return;

    //ALOGD("adding font '%s' @ %s#%d, weight: %d, slant: %d\n", meta->families[0], path, index, meta->weight, meta->slant);

    rc = FT_New_Face(ftlibrary, path, index, &face);
    if (rc) {
        ALOGE("Error opening system font at '%s'", path);
        return;
    }

    charmap_magic(NULL, face);

    if (!get_extra_font_info(ftlibrary, face, meta)) {
        ALOGE("Error getting metadata for system font at '%s'", path);
        FT_Done_Face(face);
        return;
    }

    ft = calloc(1, sizeof(FontDataFT));

    if (ft == NULL) {
        free_extra_font_info(meta);
        FT_Done_Face(face);
        return;
    }

    ft->face = face;

    if (!ass_font_provider_add_font(provider, meta, path, index, ft)) {
        ALOGE("Failed to add system font at '%s'", path);
    } else {
        //ALOGD("add font success!\n");
    }

    free_extra_font_info(meta);
}

static void scan_fonts(ASS_FontProvider *provider, ProviderPrivate *af)
{
    ASS_FontProviderMetaData meta;
    FILE *f = NULL;
    struct xml_parser *p = NULL;
    struct xml_thing t;
    int ret;

    f = fopen(FONTS_XML, "r");
    if (!f) {
        ALOGE("fopen failed\n");
        goto end;
    }
    p = xml_init(f);
    if (!p)
        goto end;

    if (xml_next(p, &t) == -1 ||
        t.type != XML_TYPE_TAG ||
        strcmp(t.tag.name, "familyset") != 0) {
        ALOGE("invalid xml\n");
        goto end;
    }
    do
        ret = xml_next(p, &t);
    while (ret == 1 && t.type != XML_TYPE_TAG && t.tag.name[0] != 0);
    if (ret == -1) { ALOGE("xml error: %s\n", xml_error(p)); goto end; }

    int fam_read = 0, font_attr_read = 0, alias_read = 0;
    char *path, *alias_name, *alias_to;
    int index, alias_weight;
    meta.n_fullname = 0;
    meta.n_family = 1;
    meta.families = malloc(sizeof(char*));
    meta.families[0] = NULL;
    meta.postscript_name = NULL;
    path = NULL;
    index = 0;

    while (1) {
        ret = xml_next(p, &t);
        if (ret == -1) { ALOGE("xml error: %s\n", xml_error(p)); goto end; }

        if (t.type == XML_TYPE_TAG) {
            if (!strcmp(t.tag.name, "family")) {
                if (t.flags & XML_FLAG_OPENING_TAG) {
                    fam_read = 1;
                    if (meta.families[0])
                        free(meta.families[0]);
                    meta.families[0] = NULL;
                }
            } else if (!strcmp(t.tag.name, "font")) {
                if (t.flags & XML_FLAG_OPENING_TAG) {
                    font_attr_read = 1;
                } else {
                    // lol
                    meta.width = strstr(path, "Condensed") == NULL ? FONT_WIDTH_NORMAL : FONT_WIDTH_CONDENSED;
                    add_font(provider, &meta, index, path);
                    index = 0;
                }
            } else if (!strcmp(t.tag.name, "alias")) {
                alias_read = 1;
            } else if (!t.tag.name[0]) {
                if (alias_read) {
                    //ALOGD("adding alias from %s to %s\n", alias_name, alias_to);
                    af->n_substitutions++;
                    af->substitutions = realloc(af->substitutions, af->n_substitutions * sizeof(ASS_FontMapping));
                    af->substitutions[af->n_substitutions - 1].from = alias_name;
                    af->substitutions[af->n_substitutions - 1].to = alias_to;
                    // TODO: use alias_weight
                    alias_name = NULL;
                    alias_to = NULL;
                }
                fam_read = 0;
                font_attr_read = 0;
                alias_read = 0;
            }
            continue;
        } else if (t.type == XML_TYPE_ATTRIBUTE) {
            if (fam_read && !strcmp(t.attr.key, "name")) {
                meta.families[0] = strdup(t.attr.value);
            } else if (font_attr_read && !strcmp(t.attr.key, "weight")) {
                meta.weight = atoi(t.attr.value);
            } else if (font_attr_read && !strcmp(t.attr.key, "style")) {
                meta.slant = !strcmp(t.attr.value, "italic") ? FONT_SLANT_ITALIC : FONT_SLANT_NONE;
            } else if (font_attr_read && !strcmp(t.attr.key, "index")) {
                index = atoi(t.attr.value);
            } else if (alias_read && !strcmp(t.attr.key, "weight")) {
                alias_weight = atoi(t.attr.value);
            } else if (alias_read && !strcmp(t.attr.key, "name")) {
                alias_name = strdup(t.attr.value);
            } else if (alias_read && !strcmp(t.attr.key, "to")) {
                alias_to = strdup(t.attr.value);
            }
        } else if (t.type == XML_TYPE_TEXT_CONTENT) {
            if (path)
                free(path);
            char buf[123];
            snprintf(buf, sizeof(buf), FONTS_PATH "/%s", t.text.content);
            path = strdup(buf);
        } else if (t.type == XML_TYPE_EOF) {
            break;
        }
    }

    if (alias_name)
        free(alias_name);
    if (alias_to)
        free(alias_to);
    if (meta.families[0])
        free(meta.families[0]);
    free(meta.families);
    if (path)
        free(path);

end:
    if (p)
        xml_free(p);
    if (f)
        fclose(f);
}

static ASS_FontProviderFuncs androidfonts_callbacks = {
    .check_postscript   = check_postscript_ft,
    .check_glyph        = check_glyph_ft,
    .destroy_font       = destroy_font_ft,
    .get_substitutions  = get_substitutions,
    .destroy_provider   = destroy_provider,
};

ASS_FontProvider *
ass_androidfonts_add_provider(ASS_Library *lib, ASS_FontSelector *selector,
                            const char *config)
{
    ProviderPrivate *af = NULL;
    ASS_FontProvider *provider = NULL;

    af = calloc(1, sizeof(ProviderPrivate));
    if (af == NULL)
        return NULL;

    // create font provider
    provider = ass_font_provider_new(selector, &androidfonts_callbacks, af);

    // build database from system fonts
    scan_fonts(provider, af);

    return provider;
}

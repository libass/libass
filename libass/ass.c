/*
 * Copyright (C) 2006 Evgeniy Stepanov <eugeni.stepanov@gmail.com>
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

#ifdef CONFIG_ICONV
#include <iconv.h>
#endif

#include "ass.h"
#include "ass_utils.h"
#include "ass_library.h"
#include "ass_priv.h"
#include "ass_shaper.h"
#include "ass_string.h"

#define ass_atof(STR) (ass_strtod((STR),NULL))

static const ASS_StringView ass_style_format = ASS_SV(
    "Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
    "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
    "Alignment, MarginL, MarginR, MarginV, Encoding"
);
static const ASS_StringView ass_event_format = ASS_SV(
    "Layer, Start, End, Style, Name, "
    "MarginL, MarginR, MarginV, Effect, Text"
);
static const ASS_StringView ssa_style_format = ASS_SV(
    "Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, "
    "Shadow, Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding"
);
static const ASS_StringView ssa_event_format = ASS_SV(
    "Marked, Start, End, Style, Name, "
    "MarginL, MarginR, MarginV, Effect, Text"
);

static inline bool gather_prefix(ASS_StringView *str, ASS_StringView prefix)
{
    if (!ass_sv_istartswith(*str, prefix))
        return false;

    ass_sv_get(str, prefix.len);
    return true;
}

#define GATHER_PREFIX(str, lit) gather_prefix(&str, ASS_SV(lit))

#define ASS_STYLES_ALLOC 20

int ass_library_version(void)
{
    return LIBASS_VERSION;
}

void ass_free_track(ASS_Track *track)
{
    int i;

    if (!track)
        return;

    if (track->parser_priv) {
        free(track->parser_priv->read_order_bitmap);
        free(track->parser_priv->fontname);
        free(track->parser_priv->fontdata);
        free(track->parser_priv->process_data_buf);
        free(track->parser_priv);
    }
    free(track->style_format);
    free(track->event_format);
    free(track->Language);
    if (track->styles) {
        for (i = 0; i < track->n_styles; ++i)
            ass_free_style(track, i);
    }
    free(track->styles);
    if (track->events) {
        for (i = 0; i < track->n_events; ++i)
            ass_free_event(track, i);
    }
    free(track->events);
    free(track->name);
    free(track);
}

/// \brief Allocate a new style struct
/// \param track track
/// \return style id or negative value on failure
int ass_alloc_style(ASS_Track *track)
{
    int sid;

    assert(track->n_styles <= track->max_styles);

    if (track->n_styles == track->max_styles) {
        if (track->max_styles >= FFMIN(SIZE_MAX, INT_MAX) - ASS_STYLES_ALLOC)
            return -1;
        int new_max = track->max_styles + ASS_STYLES_ALLOC;
        if (!ASS_REALLOC_ARRAY(track->styles, new_max))
            return -1;
        track->max_styles = new_max;
    }

    sid = track->n_styles++;
    memset(track->styles + sid, 0, sizeof(ASS_Style));
    return sid;
}

/// \brief Allocate a new event struct
/// \param track track
/// \return event id or negative value on failure
int ass_alloc_event(ASS_Track *track)
{
    int eid;

    assert(track->n_events <= track->max_events);

    if (track->n_events == track->max_events) {
        if (track->max_events >= FFMIN(SIZE_MAX, INT_MAX) / 2)
            return -1;
        int new_max = track->max_events * 2 + 1;
        if (!ASS_REALLOC_ARRAY(track->events, new_max))
            return -1;
        track->max_events = new_max;
    }

    eid = track->n_events++;
    memset(track->events + eid, 0, sizeof(ASS_Event));
    return eid;
}

void ass_free_event(ASS_Track *track, int eid)
{
    ASS_Event *event = track->events + eid;

    free(event->Name);
    free(event->Effect);
    free(event->Text);
    free(event->render_priv);
}

void ass_free_style(ASS_Track *track, int sid)
{
    ASS_Style *style = track->styles + sid;

    free(style->Name);
    free(style->FontName);
}

static int resize_read_order_bitmap(ASS_Track *track, int max_id)
{
    // Don't allow malicious files to OOM us easily. Also avoids int overflows.
    if (max_id < 0 || max_id >= 10 * 1024 * 1024 * 8)
        goto fail;
    assert(track->parser_priv->read_order_bitmap || !track->parser_priv->read_order_elems);
    if (max_id >= track->parser_priv->read_order_elems * 32) {
        int oldelems = track->parser_priv->read_order_elems;
        int elems = ((max_id + 31) / 32 + 1) * 2;
        assert(elems >= oldelems);
        track->parser_priv->read_order_elems = elems;
        void *new_bitmap =
            realloc(track->parser_priv->read_order_bitmap, elems * 4);
        if (!new_bitmap)
            goto fail;
        track->parser_priv->read_order_bitmap = new_bitmap;
        memset(track->parser_priv->read_order_bitmap + oldelems, 0,
               (elems - oldelems) * 4);
    }
    return 0;

fail:
    free(track->parser_priv->read_order_bitmap);
    track->parser_priv->read_order_bitmap = NULL;
    track->parser_priv->read_order_elems = 0;
    return -1;
}

static int test_and_set_read_order_bit(ASS_Track *track, int id)
{
    if (resize_read_order_bitmap(track, id) < 0)
        return -1;
    int index = id / 32;
    uint32_t bit = 1u << (id % 32);
    if (track->parser_priv->read_order_bitmap[index] & bit)
        return 1;
    track->parser_priv->read_order_bitmap[index] |= bit;
    return 0;
}

// ==============================================================================================

/**
 * \brief Set up default style
 * \param style style to edit to defaults
 * The parameters are mostly taken directly from VSFilter source for
 * best compatibility.
 */
static void set_default_style(ASS_Style *style)
{
    style->Name             = strdup("Default");
    style->FontName         = strdup("Arial");
    style->FontSize         = 18;
    style->PrimaryColour    = 0xffffff00;
    style->SecondaryColour  = 0x00ffff00;
    style->OutlineColour    = 0x00000000;
    style->BackColour       = 0x00000080;
    style->Bold             = 200;
    style->ScaleX           = 1.0;
    style->ScaleY           = 1.0;
    style->Spacing          = 0;
    style->BorderStyle      = 1;
    style->Outline          = 2;
    style->Shadow           = 3;
    style->Alignment        = 2;
    style->MarginL = style->MarginR = style->MarginV = 20;
}

static long long string2timecode(ASS_Library *library, ASS_StringView p)
{
    int h, m, s, ms;
    long long tm;

    // This is never the last field in a line, so this can't overflow;
    // TIMEVAL ensures this
    int res = sscanf(p.str, "%d:%d:%d.%d", &h, &m, &s, &ms);
    if (res < 4) {
        ass_msg(library, MSGL_WARN, "Bad timestamp");
        return 0;
    }
    tm = ((h * 60LL + m) * 60 + s) * 1000 + ms * 10LL;
    return tm;
}

#define NEXT(s,token) \
    token = next_token(&s); \
    if (!token.str) break;


#define ALIAS(alias,name) \
        if (ass_string_equal(tname, ASS_SV(#alias))) {tname = ASS_SV(#name);}

/* One section started with PARSE_START and PARSE_END parses a single token
 * (contained in the variable named token) for the header indicated by the
 * variable tname. It does so by chaining a number of else-if statements, each
 * of which checks if the tname variable indicates that this header should be
 * parsed. The first parameter of the macro gives the name of the header.
 *
 * The string that is passed is in str. str is advanced to the next token if
 * a header could be parsed. The parsed results are stored in the variable
 * target, which has the type ASS_Style* or ASS_Event*.
 */
#define PARSE_START if (0) {
#define PARSE_END   }

#define ANYVAL(name,func) \
    } else if (ASS_SV_IEQ(tname, #name)) { \
        target->name = func(token);

#define STRVAL(name) \
    } else if (ASS_SV_IEQ(tname, #name)) { \
        char *new_str = ass_copy_string(token); \
        if (new_str) { \
            free(target->name); \
            target->name = new_str; \
        }

#define STARREDSTRVAL(name) \
    } else if (ASS_SV_IEQ(tname, #name)) { \
        while (token.len && *token.str == '*') ++token.str, --token.len; \
        char *new_str = ass_copy_string(token); \
        if (new_str) { \
            free(target->name); \
            target->name = new_str; \
        }

static float sv_atof(ASS_StringView val)
{
    // Header lines always end in a newline or \0, so this can't overflow
    return ass_atof(val.str);
}

#define COLORVAL(name) ANYVAL(name,parse_color_header)
#define INTVAL(name) ANYVAL(name,parse_int_header)
#define FPVAL(name) ANYVAL(name,sv_atof)
#define TIMEVAL(name) \
    } else if (ASS_SV_IEQ(tname, #name)) { \
        if (!p.len) \
            return 0; \
        target->name = string2timecode(track->library, token);

#define STYLEVAL(name) \
    } else if (ASS_SV_IEQ(tname, #name)) { \
        target->name = lookup_style(track, token);

// skip spaces in str beforehand, or trim leading spaces afterwards
static ASS_StringView next_token(ASS_StringView *str)
{
    vskip_spaces(str);
    if (!str->len)
        return (ASS_StringView){ NULL, 0 };

    ASS_StringView ret = (ASS_StringView){ str->str, 0 };

    while (str->len && *str->str != ',')
        ++ret.len, ++str->str, --str->len;

    // Chop the comma, if we ended on one
    if (str->len)
        ++str->str, --str->len;

    vrskip_spaces(&ret);

    return ret;
}

/**
 * \brief Parse the tail of Dialogue line
 * \param track track
 * \param event parsed data goes here
 * \param str string to parse, zero-terminated
 * \param n_ignored number of format options to skip at the beginning
*/
static int process_event_tail(ASS_Track *track, ASS_Event *event,
                              ASS_StringView p, int n_ignored)
{
    ASS_StringView token;
    ASS_StringView tname;
    int i;
    ASS_Event *target = event;

    ASS_StringView q = { track->event_format, strlen(track->event_format) }; // format scanning pointer

    for (i = 0; i < n_ignored; ++i) {
        NEXT(q, tname);
    }

    while (1) {
        NEXT(q, tname);
        if (ass_string_equal(tname, ASS_SV("Text"))) {
            char *last;
            event->Text = ass_copy_string(p);
            if (event->Text && *event->Text != 0) {
                last = event->Text + strlen(event->Text) - 1;
                if (last >= event->Text && *last == '\r')
                    *last = 0;
            }
            event->Duration -= event->Start;
            return event->Text ? 0 : -1;           // "Text" is always the last
        }
        NEXT(p, token);

        ALIAS(End, Duration)    // temporarily store end timecode in event->Duration
        PARSE_START
            INTVAL(Layer)
            STYLEVAL(Style)
            STRVAL(Name)
            STRVAL(Effect)
            INTVAL(MarginL)
            INTVAL(MarginR)
            INTVAL(MarginV)
            TIMEVAL(Start)
            TIMEVAL(Duration)
        PARSE_END
    }
    return 1;
}

/**
 * \brief Parse command line style overrides (--ass-force-style option)
 * \param track track to apply overrides to
 * The format for overrides is [StyleName.]Field=Value
 */
void ass_process_force_style(ASS_Track *track)
{
    char **list = track->library->style_overrides;

    if (!list)
        return;

    for (char **item = list; *item; ++item) {
        ASS_StringView name = { *item, 0 };
        const char *eq = strrchr(name.str, '=');
        if (!eq)
            continue;
        name.len = (eq - name.str);

        ASS_StringView token = { eq + 1, strlen(eq + 1) };

        if (ASS_SV_IEQ(name, "PlayResX"))
            track->PlayResX = parse_int_header(token);
        else if (ASS_SV_IEQ(name, "PlayResY"))
            track->PlayResY = parse_int_header(token);
        else if (ASS_SV_IEQ(name, "Timer"))
            track->Timer = ass_atof(token.str);
        else if (ASS_SV_IEQ(name, "WrapStyle"))
            track->WrapStyle = parse_int_header(token);
        else if (ASS_SV_IEQ(name, "ScaledBorderAndShadow"))
            track->ScaledBorderAndShadow = parse_bool(token);
        else if (ASS_SV_IEQ(name, "Kerning"))
            track->Kerning = parse_bool(token);
        else if (ASS_SV_IEQ(name, "YCbCr Matrix"))
            track->YCbCrMatrix = parse_ycbcr_matrix(token);

        size_t dt;
        for (dt = name.len; dt; dt--) {
            if (name.str[dt - 1] == '.')
                break;
        }

        ASS_StringView tname, style;
        if (dt) {
            style = (ASS_StringView){ name.str, dt - 1};
            tname = (ASS_StringView){ name.str + dt, name.len - dt };
        } else {
            style = (ASS_StringView){ NULL, 0 };
            tname = name;
        }
        for (int sid = 0; sid < track->n_styles; ++sid) {
            if (!style.str || ass_sv_iequal_cstr(style, track->styles[sid].Name)) {
                ASS_Style *target = track->styles + sid;
                PARSE_START
                    STRVAL(FontName)
                    COLORVAL(PrimaryColour)
                    COLORVAL(SecondaryColour)
                    COLORVAL(OutlineColour)
                    COLORVAL(BackColour)
                    FPVAL(FontSize)
                    INTVAL(Bold)
                    INTVAL(Italic)
                    INTVAL(Underline)
                    INTVAL(StrikeOut)
                    FPVAL(Spacing)
                    FPVAL(Angle)
                    INTVAL(BorderStyle)
                    INTVAL(Alignment)
                    INTVAL(Justify)
                    INTVAL(MarginL)
                    INTVAL(MarginR)
                    INTVAL(MarginV)
                    INTVAL(Encoding)
                    FPVAL(ScaleX)
                    FPVAL(ScaleY)
                    FPVAL(Outline)
                    FPVAL(Shadow)
                    FPVAL(Blur)
                PARSE_END
            }
        }
    }
}

/**
 * \brief Parse the Style line
 * \param track track
 * \param str string to parse, zero-terminated
 * Allocates a new style struct.
*/
static int process_style(ASS_Track *track, ASS_StringView p)
{
    ASS_StringView token;
    ASS_StringView tname;
    int sid;
    ASS_Style *style;
    ASS_Style *target;

    if (!track->style_format) {
        // no style format header
        // probably an ancient script version
        if (track->track_type == TRACK_TYPE_SSA)
            track->style_format = ass_copy_string(ssa_style_format);
        else
            track->style_format = ass_copy_string(ass_style_format);
        if (!track->style_format)
            return -1;
    }

    ASS_StringView q = { track->style_format, strlen(track->style_format) }; // format scanning pointer

    ass_msg(track->library, MSGL_V, "[%p] Style: %.*s", track, (int)p.len, p.str);

    sid = ass_alloc_style(track);
    if (sid < 0)
        return -1;

    style = track->styles + sid;
    target = style;

    // fill style with some default values
    style->ScaleX = 100.;
    style->ScaleY = 100.;

    while (1) {
        NEXT(q, tname);
        NEXT(p, token);

        PARSE_START
            STARREDSTRVAL(Name)
            STRVAL(FontName)
            COLORVAL(PrimaryColour)
            COLORVAL(SecondaryColour)
            COLORVAL(OutlineColour) // TertiaryColor
            COLORVAL(BackColour)
            // SSA uses BackColour for both outline and shadow
            // this will destroy SSA's TertiaryColour, but i'm not going to use it anyway
            if (track->track_type == TRACK_TYPE_SSA)
                target->OutlineColour = target->BackColour;
            FPVAL(FontSize)
            INTVAL(Bold)
            INTVAL(Italic)
            INTVAL(Underline)
            INTVAL(StrikeOut)
            FPVAL(Spacing)
            FPVAL(Angle)
            INTVAL(BorderStyle)
            INTVAL(Alignment)
            if (track->track_type == TRACK_TYPE_ASS)
                target->Alignment = numpad2align(target->Alignment);
            // VSFilter compatibility
            else if (target->Alignment == 8)
                target->Alignment = 3;
            else if (target->Alignment == 4)
                target->Alignment = 11;
            INTVAL(MarginL)
            INTVAL(MarginR)
            INTVAL(MarginV)
            INTVAL(Encoding)
            FPVAL(ScaleX)
            FPVAL(ScaleY)
            FPVAL(Outline)
            FPVAL(Shadow)
        PARSE_END
    }
    style->ScaleX = FFMAX(style->ScaleX, 0.) / 100.;
    style->ScaleY = FFMAX(style->ScaleY, 0.) / 100.;
    style->Spacing = FFMAX(style->Spacing, 0.);
    style->Outline = FFMAX(style->Outline, 0.);
    style->Shadow = FFMAX(style->Shadow, 0.);
    style->Bold = !!style->Bold;
    style->Italic = !!style->Italic;
    style->Underline = !!style->Underline;
    style->StrikeOut = !!style->StrikeOut;
    if (!style->Name)
        style->Name = strdup("Default");
    if (!style->FontName)
        style->FontName = strdup("Arial");
    if (!style->Name || !style->FontName) {
        ass_free_style(track, sid);
        track->n_styles--;
        return -1;
    }
    if (strcmp(target->Name, "Default") == 0)
        track->default_style = sid;
    return 0;

}

static bool format_line_compare(ASS_StringView fmt1, ASS_StringView fmt2)
{
    while (true) {
        vskip_spaces(&fmt1);
        vskip_spaces(&fmt2);
        if (!fmt1.len || !fmt2.len)
            break;

        ASS_StringView tk1 = next_token(&fmt1);
        ASS_StringView tk2 = next_token(&fmt2);

        if (!ass_sv_iequal(tk1, tk2))
            return false;
    }
    return !fmt1.len && !fmt2.len;
}


/**
 * \brief Set SBAS=1 if not set explicitly in case of custom format line
 * \param track track
 * \param fmt   format line of file
 * \param std   standard format line
 *
 * As of writing libass is the only renderer accepting custom format lines.
 * For years libass defaultet SBAS to yes instead of no.
 * To avoid breaking released scripts with custom format lines,
 * keep SBAS=1 default for custom format files.
 */
static void custom_format_line_compatibility(ASS_Track *track,
                                             ASS_StringView fmt,
                                             ASS_StringView std)
{
    if (!(track->parser_priv->header_flags & SINFO_SCALEDBORDER)
        && !format_line_compare(fmt, std)) {
        ass_msg(track->library, MSGL_INFO,
               "Track has custom format line(s). "
                "'ScaledBorderAndShadow' will default to 'yes'.");
        track->ScaledBorderAndShadow = 1;
    }
}

static int process_styles_line(ASS_Track *track, ASS_StringView str)
{
    int ret = 0;
    if (GATHER_PREFIX(str, "Format:")) {
        vskip_spaces(&str);
        free(track->style_format);
        track->style_format = ass_copy_string(str);
        if (!track->style_format)
            return -1;
        ass_msg(track->library, MSGL_DBG2, "Style format: %s",
               track->style_format);
        if (track->track_type == TRACK_TYPE_ASS)
            custom_format_line_compatibility(track, str, ass_style_format);
        else
            custom_format_line_compatibility(track, str, ssa_style_format);
    } else if (GATHER_PREFIX(str, "Style:")) {
        vskip_spaces(&str);
        ret = process_style(track, str);
    }
    return ret;
}

static inline void check_duplicate_info_line(const ASS_Track *const track,
                                             const ScriptInfo si,
                                             const char *const name)
{
    if (track->parser_priv->header_flags & si)
        ass_msg(track->library, MSGL_WARN,
            "Duplicate Script Info Header '%s'. Previous value overwritten!",
            name);
    else
        track->parser_priv->header_flags |= si;
}

static int process_info_line(ASS_Track *track, ASS_StringView str)
{
    if (GATHER_PREFIX(str, "PlayResX:")) {
        check_duplicate_info_line(track, SINFO_PLAYRESX, "PlayResX");
        track->PlayResX = parse_int_header(str);
    } else if (GATHER_PREFIX(str, "PlayResY:")) {
        check_duplicate_info_line(track, SINFO_PLAYRESY, "PlayResY");
        track->PlayResY = parse_int_header(str);
    } else if (GATHER_PREFIX(str, "Timer:")) {
        check_duplicate_info_line(track, SINFO_TIMER, "Timer");
        // Not parsed, as this doesn't actually do anything
    } else if (GATHER_PREFIX(str, "WrapStyle:")) {
        check_duplicate_info_line(track, SINFO_WRAPSTYLE, "WrapStyle");
        track->WrapStyle = parse_int_header(str);
    } else if (GATHER_PREFIX(str, "ScaledBorderAndShadow:")) {
        check_duplicate_info_line(track, SINFO_SCALEDBORDER,
                                    "ScaledBorderAndShadow");
        track->ScaledBorderAndShadow = parse_bool(str);
    } else if (GATHER_PREFIX(str, "Kerning:")) {
        check_duplicate_info_line(track, SINFO_KERNING, "Kerning");
        track->Kerning = parse_bool(str);
    } else if (GATHER_PREFIX(str, "YCbCr Matrix:")) {
        check_duplicate_info_line(track, SINFO_COLOURMATRIX, "YCbCr Matrix");
        track->YCbCrMatrix = parse_ycbcr_matrix(str);
    } else if (GATHER_PREFIX(str, "Language:")) {
        check_duplicate_info_line(track, SINFO_LANGUAGE, "Language");
        vskip_spaces(&str);
        free(track->Language);
        track->Language = strndup(str.str, FFMIN(str.len, 2));
    } else if (GATHER_PREFIX(str, "; Script generated by ")) {
        if (ASS_SV_STARTSWITH(str, "FFmpeg/Lavc"))
            track->parser_priv->header_flags |= GENBY_FFMPEG;
    }
    return 0;
}

static void event_format_fallback(ASS_Track *track)
{
    track->parser_priv->state = PST_EVENTS;
    if (track->track_type == TRACK_TYPE_SSA)
        track->event_format = ass_copy_string(ssa_event_format);
    else
        track->event_format = ass_copy_string(ass_event_format);
    ass_msg(track->library, MSGL_V,
            "No event format found, using fallback");
}

/**
 * \brief Return if track is post-signature and pre-SBAS ffmpeg track
 * \param track track
*/
static bool detect_legacy_conv_subs(ASS_Track *track)
{
    /*
     * FFmpeg and libav convert srt subtitles to ass.
     * In legacy versions, they did not set the 'ScaledBorderAndShadow' header,
     * but expected it to default to yes (which libass did).
     * To avoid breaking them, we try to detect these
     * converted subs by common properties of ffmpeg/libav's converted subs.
     * Since files with custom format lines (-2014.10.11) default to SBAS=1
     * regardless of being ffmpeg generated or not, we are only concerned with
     * post-signature and pre-SBAS ffmpeg-files (2014.10.11-2020.04.17).
     * We want to avoid matching modified ffmpeg files though.
     *
     * Relevant ffmpeg commits are:
     *  2c77c90684e24ef16f7e7c4462e011434cee6a98  2010.12.29
     *    Initial conversion format.
     *    Style "Format:" line is mix of SSA and ASS
     *    Event "Format:" line
     *     "Format: Layer, Start, End, Text\r\n"
     *    Only Header in ScriptInfo is "ScriptType: v4.00+"
     *  0e7782c08ec77739edb0b98ba5d896b45e98235f  2012.06.15
     *    Adds 'Style' to Event "Format:" line
     *  5039aadf68deb9ad6dd0737ea11259fe53d3727b  2014.06.18
     *    Adds PlayerRes(X|Y) (384x288)
     *    (moved below ScriptType: a few minutes later)
     *  40b9f28641b696c6bb73ce49dc97c2ce2700cbdb  2014.10.11 14:31:23 +0200
     *    Regular full ASS Event and Style "Format:" lines
     *  52b0a0ecaa02e17f7e01bead8c3f215f1cfd48dc  2014.10.11 18:37:43 +0200 <==
     *    Signature comment
     *  56bc0a6736cdc7edab837ff8f304661fd16de0e4  2015.02.08
     *    Allow custom PlayRes(X|Y)
     *  a8ba2a2c1294a330a0e79ae7f0d3a203a7599166  2020.04.17
     *    Set 'ScaledBorderAndShadow: yes'
     *
     * libav outputs initial ffmpeg format. (no longer maintained)
     */

    // GENBY_FFMPEG and exact ffmpeg headers required
    // Note: If there's SINFO_SCRIPTTYPE in the future this needs to be updated
    if (track->parser_priv->header_flags
            ^ (SINFO_PLAYRESX | SINFO_PLAYRESY | GENBY_FFMPEG))
        return false;

    // Legacy ffmpeg only ever has one style
    // Check 2 not 1 because libass also adds a def style
    if (track->n_styles != 2
        || strncmp(track->styles[1].Name, "Default", 7))
        return false;

    return true;
}


static int process_events_line(ASS_Track *track, ASS_StringView str)
{
    if (GATHER_PREFIX(str, "Format:")) {
        vskip_spaces(&str);
        free(track->event_format);
        track->event_format = ass_copy_string(str);
        if (!track->event_format)
            return -1;
        ass_msg(track->library, MSGL_DBG2, "Event format: %s", track->event_format);
        if (track->track_type == TRACK_TYPE_ASS)
            custom_format_line_compatibility(track, str, ass_event_format);
        else
            custom_format_line_compatibility(track, str, ssa_event_format);

        // Guess if we are dealing with legacy ffmpeg subs and change accordingly
        // If file has no event format it was probably not created by ffmpeg/libav
        if (detect_legacy_conv_subs(track)) {
            track->ScaledBorderAndShadow = 1;
            ass_msg(track->library, MSGL_INFO,
                    "Track treated as legacy ffmpeg sub.");
        }
    } else if (GATHER_PREFIX(str, "Dialogue:")) {
        // This should never be reached for embedded subtitles.
        // They have slightly different format and are parsed in ass_process_chunk,
        // called directly from demuxer
        int eid;
        ASS_Event *event;

        // We can't parse events without event_format
        if (!track->event_format) {
            event_format_fallback(track);
            if (!track->event_format)
                return -1;
        }

        vskip_spaces(&str);

        eid = ass_alloc_event(track);
        if (eid < 0)
            return -1;
        event = track->events + eid;

        return process_event_tail(track, event, str, 0);
    } else {
        ass_msg(track->library, MSGL_V, "Not understood: '%.*s'", (int)FFMIN(str.len, 30), str.str);
    }
    return 0;
}

static unsigned char *decode_chars(const unsigned char *src,
                                   unsigned char *dst, size_t cnt_in)
{
    uint32_t value = 0;
    for (size_t i = 0; i < cnt_in; i++)
        value |= (uint32_t) ((src[i] - 33u) & 63) << 6 * (3 - i);

    *dst++ = value >> 16;
    if (cnt_in >= 3)
        *dst++ = value >> 8 & 0xff;
    if (cnt_in >= 4)
        *dst++ = value & 0xff;
    return dst;
}

static void reset_embedded_font_parsing(ASS_ParserPriv *parser_priv)
{
    free(parser_priv->fontname);
    free(parser_priv->fontdata);
    parser_priv->fontname = NULL;
    parser_priv->fontdata = NULL;
    parser_priv->fontdata_size = 0;
    parser_priv->fontdata_used = 0;
}

static int decode_font(ASS_Track *track)
{
    unsigned char *p;
    unsigned char *q;
    size_t i;
    size_t size;                   // original size
    size_t dsize;                  // decoded size
    unsigned char *buf = 0;

    ass_msg(track->library, MSGL_V, "Font: %zu bytes encoded data",
            track->parser_priv->fontdata_used);
    size = track->parser_priv->fontdata_used;
    if (size % 4 == 1) {
        ass_msg(track->library, MSGL_ERR, "Bad encoded data size");
        goto error_decode_font;
    }
    buf = malloc(size / 4 * 3 + FFMAX(size % 4, 1) - 1);
    if (!buf)
        goto error_decode_font;
    q = buf;
    for (i = 0, p = (unsigned char *) track->parser_priv->fontdata;
         i < size / 4; i++, p += 4) {
        q = decode_chars(p, q, 4);
    }
    if (size % 4 == 2) {
        q = decode_chars(p, q, 2);
    } else if (size % 4 == 3) {
        q = decode_chars(p, q, 3);
    }
    dsize = q - buf;
    assert(dsize == size / 4 * 3 + FFMAX(size % 4, 1) - 1);

    if (track->library->extract_fonts) {
        ass_add_font(track->library, track->parser_priv->fontname,
                     (char *) buf, dsize);
    }

error_decode_font:
    free(buf);
    reset_embedded_font_parsing(track->parser_priv);
    return 0;
}

static int process_fonts_line(ASS_Track *track, ASS_StringView str)
{
    if (GATHER_PREFIX(str, "fontname:")) {
        vskip_spaces(&str);
        if (track->parser_priv->fontname) {
            decode_font(track);
        }
        track->parser_priv->fontname = ass_copy_string(str);
        if (!track->parser_priv->fontname)
            return -1;
        ass_msg(track->library, MSGL_V, "Fontname: %s",
                track->parser_priv->fontname);
        return 0;
    }

    if (!track->parser_priv->fontname) {
        ass_msg(track->library, MSGL_V, "Not understood: '%.*s'", (int)str.len, str.str);
        return 1;
    }

    if (track->parser_priv->fontdata_used >=
        SIZE_MAX - FFMAX(str.len, 100 * 1024)) {
        goto mem_fail;
    } else if (track->parser_priv->fontdata_used + str.len >
               track->parser_priv->fontdata_size) {
        size_t new_size =
                track->parser_priv->fontdata_size + FFMAX(str.len, 100 * 1024);
        if (!ASS_REALLOC_ARRAY(track->parser_priv->fontdata, new_size))
            goto mem_fail;
        track->parser_priv->fontdata_size = new_size;
    }
    memcpy(track->parser_priv->fontdata + track->parser_priv->fontdata_used,
           str.str, str.len);
    track->parser_priv->fontdata_used += str.len;

    return 0;

mem_fail:
    reset_embedded_font_parsing(track->parser_priv);
    return -1;
}

/**
 * \brief Parse a header line
 * \param track track
 * \param str string to parse, zero-terminated
*/
static int process_line(ASS_Track *track, ASS_StringView str)
{
    vskip_spaces(&str);
    if (ASS_SV_ISTARTSWITH(str, "[Script Info]")) {
        track->parser_priv->state = PST_INFO;
    } else if (ASS_SV_ISTARTSWITH(str, "[V4 Styles]")) {
        track->parser_priv->state = PST_STYLES;
        track->track_type = TRACK_TYPE_SSA;
    } else if (ASS_SV_ISTARTSWITH(str, "[V4+ Styles]")) {
        track->parser_priv->state = PST_STYLES;
        track->track_type = TRACK_TYPE_ASS;
    } else if (ASS_SV_ISTARTSWITH(str, "[Events]")) {
        track->parser_priv->state = PST_EVENTS;
    } else if (ASS_SV_ISTARTSWITH(str, "[Fonts]")) {
        track->parser_priv->state = PST_FONTS;
    } else {
        switch (track->parser_priv->state) {
        case PST_INFO:
            process_info_line(track, str);
            break;
        case PST_STYLES:
            process_styles_line(track, str);
            break;
        case PST_EVENTS:
            process_events_line(track, str);
            break;
        case PST_FONTS:
            process_fonts_line(track, str);
            break;
        default:
            break;
        }
    }
    return 0;
}

static int process_text(ASS_Track *track, ASS_StringView *str)
{
    while (1) {
        while (1) {
            if (ASS_SV_STARTSWITH(*str, "\r")  || ASS_SV_STARTSWITH(*str, "\n"))
                ass_sv_get(str, 1);
            else if (ASS_SV_STARTSWITH(*str, "\xef\xbb\xbf"))
                ass_sv_get(str, 3); // U+FFFE (BOM)
            else
                break;
        }

        size_t len;
        for (len = 0; len < str->len &&
                      str->str[len] != '\r' &&
                      str->str[len] != '\n';
             len++)
             ;

        if (!len || len == str->len)
            break;

        process_line(track, ass_sv_get(str, len));
    }
    // there is no explicit end-of-font marker in ssa/ass
    if (track->parser_priv->fontname)
        decode_font(track);
    return 0;
}

static int process_text_full(ASS_Track *track, ASS_StringView str)
{
    int ret = process_text(track, &str);
    if (ret < 0)
        return ret;

    if (!str.len)
        return 0;

    // Absurd? Yes.
    if (str.len >= SIZE_MAX - 1)
        return -ENOMEM;

    // If there was a partial line left over, alloc a buffer and parse it again
    // with a \n tacked on the end
    char *buf = malloc(str.len + 1);
    if (!buf)
        return -ENOMEM;

    memcpy(buf, str.str, str.len);
    buf[str.len] = '\n';

    str.str = buf;
    str.len++;

    ret = process_text(track, &str);

    free(buf);
    return ret;
}

/**
 * \brief Process a chunk of subtitle stream data.
 * \param track track
 * \param data string to parse
 * \param size length of data
*/
void ass_process_data(ASS_Track *track, const char *data, int size)
{
    ass_msg(track->library, MSGL_DBG2, "Event: %.*s", size, data);

    if (track->parser_priv->process_data_buf) {
        int pos;
        for (pos = 0; pos < size; pos++)
            if (data[pos] == '\r' ||
                data[pos] == '\n')
                break;

        if (pos == size) {
            if (track->parser_priv->process_data_buf_size >= SIZE_MAX - size)
                goto wipe;

            char *newbuf = realloc(track->parser_priv->process_data_buf, track->parser_priv->process_data_buf_size + size);
            if (!newbuf)
                goto wipe;

            memcpy(newbuf + track->parser_priv->process_data_buf_size, data, size);
            track->parser_priv->process_data_buf = newbuf;
            track->parser_priv->process_data_buf_size += size;
        }

        if (track->parser_priv->process_data_buf_size >= SIZE_MAX - pos - 1)
            goto wipe;

        size_t alloc_size = track->parser_priv->process_data_buf_size + pos + 1;
        char *alloced = malloc(alloc_size);
        if (!alloced)
            goto wipe;

        memcpy(alloced, track->parser_priv->process_data_buf, track->parser_priv->process_data_buf_size);
        memcpy(alloced + track->parser_priv->process_data_buf_size, data, pos + 1);

        ASS_StringView buf_str = { alloced, alloc_size };
        process_text(track, &buf_str);
        free(alloced);
        free(track->parser_priv->process_data_buf);
        track->parser_priv->process_data_buf = NULL;

        data += pos + 1;
        size -= pos + 1;
    }

    ASS_StringView str = { data, size };
    process_text(track, &str);

    if (str.len) {
        track->parser_priv->process_data_buf = ass_copy_string(str);
        track->parser_priv->process_data_buf_size = str.len;
    }

    return;

wipe:
    free(track->parser_priv->process_data_buf);
    track->parser_priv->process_data_buf = NULL;
    return; // ENOMEM
}

/**
 * \brief Process CodecPrivate section of subtitle stream
 * \param track track
 * \param data string to parse
 * \param size length of data
 CodecPrivate section contains [Stream Info] and [V4+ Styles] ([V4 Styles] for SSA) sections
*/
void ass_process_codec_private(ASS_Track *track, const char *data, int size)
{
    process_text_full(track, (ASS_StringView){ data, size });

    // probably an mkv produced by ancient mkvtoolnix
    // such files don't have [Events] and Format: headers
    if (!track->event_format)
        event_format_fallback(track);

    ass_process_force_style(track);
}

static int check_duplicate_event(ASS_Track *track, int ReadOrder)
{
    if (track->parser_priv->read_order_bitmap)
        return test_and_set_read_order_bit(track, ReadOrder) > 0;
    // ignoring last event, it is the one we are comparing with
    for (int i = 0; i < track->n_events - 1; i++)
        if (track->events[i].ReadOrder == ReadOrder)
            return 1;
    return 0;
}

void ass_set_check_readorder(ASS_Track *track, int check_readorder)
{
    track->parser_priv->check_readorder = check_readorder == 1;
}

/**
 * \brief Process a chunk of subtitle stream data. In Matroska, this contains exactly 1 event (or a commentary).
 * \param track track
 * \param data string to parse
 * \param size length of data
 * \param timecode starting time of the event (milliseconds)
 * \param duration duration of the event (milliseconds)
*/
void ass_process_chunk(ASS_Track *track, const char *data, int size,
                       long long timecode, long long duration)
{
    int eid;
    ASS_StringView p = { data, size };
    ASS_StringView token;
    ASS_Event *event;
    int check_readorder = track->parser_priv->check_readorder;

    if (check_readorder && !track->parser_priv->read_order_bitmap) {
        for (int i = 0; i < track->n_events; i++) {
            if (test_and_set_read_order_bit(track, track->events[i].ReadOrder) < 0)
                break;
        }
    }

    if (!track->event_format) {
        ass_msg(track->library, MSGL_WARN, "Event format header missing");
        return;
    }

    ass_msg(track->library, MSGL_V, "Event at %" PRId64 ", +%" PRId64 ": %.*s",
           (int64_t) timecode, (int64_t) duration, (int)p.len, p.str);

    eid = ass_alloc_event(track);
    if (eid < 0)
        return;
    event = track->events + eid;

    do {
        NEXT(p, token);
        if (!p.len)
            break;
        event->ReadOrder = atoi(token.str);
        if (check_readorder && check_duplicate_event(track, event->ReadOrder))
            break;

        NEXT(p, token);
        if (!p.len)
            break;
        event->Layer = atoi(token.str);

        process_event_tail(track, event, p, 3);

        event->Start = timecode;
        event->Duration = duration;

        return;
//              dump_events(tid);
    } while (0);
    // some error
    ass_free_event(track, eid);
    track->n_events--;
}

/**
 * \brief Flush buffered events.
 * \param track track
*/
void ass_flush_events(ASS_Track *track)
{
    if (track->events) {
        int eid;
        for (eid = 0; eid < track->n_events; eid++)
            ass_free_event(track, eid);
        track->n_events = 0;
    }
    free(track->parser_priv->read_order_bitmap);
    track->parser_priv->read_order_bitmap = NULL;
    track->parser_priv->read_order_elems = 0;
}

#ifdef CONFIG_ICONV
/** \brief recode buffer to utf-8
 * constraint: codepage != 0
 * \param data pointer to text buffer
 * \param size buffer size
 * \return a pointer to recoded buffer, caller is responsible for freeing it
**/
static char *sub_recode(ASS_Library *library, const char *data, size_t size,
                        const char *codepage, size_t *outsize)
{
    iconv_t icdsc;
    const char *tocp = "UTF-8";
    char *outbuf;
    assert(codepage);
    assert(outsize);

    if ((icdsc = iconv_open(tocp, codepage)) != (iconv_t) (-1)) {
        ass_msg(library, MSGL_V, "Opened iconv descriptor");
    } else {
        ass_msg(library, MSGL_ERR, "Error opening iconv descriptor");
        return NULL;
    }

    {
        size_t osize = size;
        size_t ileft = size;
        size_t oleft = size - 1;
        char *ip;
        char *op;
        size_t rc;
        int clear = 0;

        outbuf = malloc(osize);
        if (!outbuf)
            goto out;
        ip = (char*)data; // iconv takes a char**, but doesn't modify the pointed data
        op = outbuf;

        while (1) {
            if (ileft)
                rc = iconv(icdsc, &ip, &ileft, &op, &oleft);
            else {              // clear the conversion state and leave
                clear = 1;
                rc = iconv(icdsc, NULL, NULL, &op, &oleft);
            }
            if (rc == (size_t) (-1)) {
                if (errno == E2BIG) {
                    size_t offset = op - outbuf;
                    char *nbuf = realloc(outbuf, osize + size);
                    if (!nbuf) {
                        free(outbuf);
                        outbuf = 0;
                        goto out;
                    }
                    outbuf = nbuf;
                    op = outbuf + offset;
                    osize += size;
                    oleft += size;
                } else {
                    ass_msg(library, MSGL_WARN, "Error recoding file");
                    free(outbuf);
                    outbuf = NULL;
                    goto out;
                }
            } else if (clear)
                break;
        }
        outbuf[osize - oleft - 1] = 0;

        *outsize = osize - oleft - 1;
    }

out:
    if (icdsc != (iconv_t) (-1)) {
        (void) iconv_close(icdsc);
        ass_msg(library, MSGL_V, "Closed iconv descriptor");
    }

    return outbuf;
}
#endif                          // ICONV

/**
 * \brief read file contents into newly allocated buffer
 * \param fname file name
 * \param bufsize out: file size
 * \return pointer to file contents. Caller is responsible for its deallocation.
 */
char *read_file(ASS_Library *library, const char *fname, size_t *bufsize)
{
    int res;
    long sz;
    long bytes_read;
    char *buf;

    FILE *fp = fopen(fname, "rb");
    if (!fp) {
        ass_msg(library, MSGL_WARN,
                "ass_read_file(%s): fopen failed", fname);
        return 0;
    }
    res = fseek(fp, 0, SEEK_END);
    if (res == -1) {
        ass_msg(library, MSGL_WARN,
                "ass_read_file(%s): fseek failed", fname);
        fclose(fp);
        return 0;
    }

    sz = ftell(fp);
    rewind(fp);

    ass_msg(library, MSGL_V, "File size: %ld", sz);

    buf = sz < SIZE_MAX ? malloc(sz + 1) : NULL;
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    assert(buf);
    bytes_read = 0;
    do {
        res = fread(buf + bytes_read, 1, sz - bytes_read, fp);
        if (res <= 0) {
            ass_msg(library, MSGL_INFO, "Read failed, %d: %s", errno,
                    strerror(errno));
            fclose(fp);
            free(buf);
            return 0;
        }
        bytes_read += res;
    } while (sz - bytes_read > 0);
    buf[sz] = '\0';
    fclose(fp);

    if (bufsize)
        *bufsize = sz;
    return buf;
}

/*
 * \param buf pointer to subtitle text in utf-8
 */
static ASS_Track *parse_memory(ASS_Library *library, const char *buf, size_t size)
{
    ASS_Track *track;
    int i;

    track = ass_new_track(library);
    if (!track)
        return NULL;

    // process header
    if (process_text_full(track, (ASS_StringView){ buf, size }) < 0)
        goto fail;

    // external SSA/ASS subs does not have ReadOrder field
    for (i = 0; i < track->n_events; ++i)
        track->events[i].ReadOrder = i;

    if (track->track_type == TRACK_TYPE_UNKNOWN)
        goto fail;

    ass_process_force_style(track);

    return track;

fail:
    ass_free_track(track);
    return NULL;
}

/**
 * \brief Read subtitles from memory.
 * \param library libass library object
 * \param buf pointer to subtitles text
 * \param bufsize size of buffer
 * \param codepage recode buffer contents from given codepage
 * \return newly allocated track
*/
ASS_Track *ass_read_memory(ASS_Library *library, const char *buf,
                           size_t bufsize, const char *codepage)
{
    ASS_Track *track;
    char *alloced = NULL;

    if (!buf)
        return 0;

#ifdef CONFIG_ICONV
    if (codepage) {
        buf = alloced = sub_recode(library, buf, bufsize, codepage, &bufsize);
        if (!buf)
            return 0;
    }
#endif
    track = parse_memory(library, buf, bufsize);
    free(alloced);
    if (!track)
        return 0;

    ass_msg(library, MSGL_INFO, "Added subtitle file: "
            "<memory> (%d styles, %d events)",
            track->n_styles, track->n_events);
    return track;
}

static char *read_file_recode(ASS_Library *library, const char *fname,
                              const char *codepage, size_t *size)
{
    char *buf;

    buf = read_file(library, fname, size);
    if (!buf)
        return 0;
#ifdef CONFIG_ICONV
    if (codepage) {
        char *tmpbuf = sub_recode(library, buf, *size, codepage, size);
        free(buf);
        buf = tmpbuf;
    }
    if (!buf)
        return 0;
#endif
    return buf;
}

/**
 * \brief Read subtitles from file.
 * \param library libass library object
 * \param fname file name
 * \param codepage recode buffer contents from given codepage
 * \return newly allocated track
*/
ASS_Track *ass_read_file(ASS_Library *library, const char *fname,
                         const char *codepage)
{
    char *buf;
    ASS_Track *track;
    size_t bufsize;

    buf = read_file_recode(library, fname, codepage, &bufsize);
    if (!buf)
        return 0;
    track = parse_memory(library, buf, bufsize);
    free(buf);
    if (!track)
        return 0;

    track->name = strdup(fname);

    ass_msg(library, MSGL_INFO,
            "Added subtitle file: '%s' (%d styles, %d events)",
            fname, track->n_styles, track->n_events);

    return track;
}

/**
 * \brief read styles from file into already initialized track
 */
int ass_read_styles(ASS_Track *track, const char *fname, const char *codepage)
{
    char *buf;
    ParserState old_state;
    size_t sz;

    buf = read_file(track->library, fname, &sz);
    if (!buf)
        return 1;
#ifdef CONFIG_ICONV
    if (codepage) {
        char *tmpbuf;
        tmpbuf = sub_recode(track->library, buf, sz, codepage, &sz);
        free(buf);
        buf = tmpbuf;
    }
    if (!buf)
        return 1;
#endif

    old_state = track->parser_priv->state;
    track->parser_priv->state = PST_STYLES;
    process_text_full(track, (ASS_StringView){buf, sz});
    free(buf);
    track->parser_priv->state = old_state;

    return 0;
}

long long ass_step_sub(ASS_Track *track, long long now, int movement)
{
    int i;
    ASS_Event *best = NULL;
    long long target = now;
    int direction = (movement > 0 ? 1 : -1) * !!movement;

    if (track->n_events == 0)
        return 0;

    do {
        ASS_Event *closest = NULL;
        long long closest_time = now;
        for (i = 0; i < track->n_events; i++) {
            if (direction < 0) {
                long long end =
                    track->events[i].Start + track->events[i].Duration;
                if (end < target) {
                    if (!closest || end > closest_time) {
                        closest = &track->events[i];
                        closest_time = end;
                    }
                }
            } else if (direction > 0) {
                long long start = track->events[i].Start;
                if (start > target) {
                    if (!closest || start < closest_time) {
                        closest = &track->events[i];
                        closest_time = start;
                    }
                }
            } else {
                long long start = track->events[i].Start;
                if (start < target) {
                    if (!closest || start >= closest_time) {
                        closest = &track->events[i];
                        closest_time = start;
                    }
                }
            }
        }
        target = closest_time + direction;
        movement -= direction;
        if (closest)
            best = closest;
    } while (movement);

    return best ? best->Start - now : 0;
}

ASS_Track *ass_new_track(ASS_Library *library)
{
    int def_sid = -1;
    ASS_Track *track = calloc(1, sizeof(ASS_Track));
    if (!track)
        goto fail;
    track->library = library;
    track->ScaledBorderAndShadow = 0;
    track->parser_priv = calloc(1, sizeof(ASS_ParserPriv));
    if (!track->parser_priv)
        goto fail;
    def_sid = ass_alloc_style(track);
    if (def_sid < 0)
        goto fail;
    set_default_style(track->styles + def_sid);
    track->default_style = def_sid;
    if (!track->styles[def_sid].Name || !track->styles[def_sid].FontName)
        goto fail;
    track->parser_priv->check_readorder = 1;
    return track;

fail:
    if (track) {
        if (def_sid >= 0)
            ass_free_style(track, def_sid);
        free(track->parser_priv);
        free(track);
    }
    return NULL;
}

int ass_track_set_feature(ASS_Track *track, ASS_Feature feature, int enable)
{
    switch (feature) {
    case ASS_FEATURE_INCOMPATIBLE_EXTENSIONS:
        //-fallthrough
#ifdef USE_FRIBIDI_EX_API
    case ASS_FEATURE_BIDI_BRACKETS:
        track->parser_priv->bidi_brackets = !!enable;
#endif
        return 0;
    default:
        return -1;
    }
}

/**
 * \brief Prepare track for rendering
 */
void ass_lazy_track_init(ASS_Library *lib, ASS_Track *track)
{
    if (track->PlayResX > 0 && track->PlayResY > 0)
        return;
    if (track->PlayResX <= 0 && track->PlayResY <= 0) {
        ass_msg(lib, MSGL_WARN,
               "Neither PlayResX nor PlayResY defined. Assuming 384x288");
        track->PlayResX = 384;
        track->PlayResY = 288;
    } else {
        if (track->PlayResY <= 0 && track->PlayResX == 1280) {
            track->PlayResY = 1024;
            ass_msg(lib, MSGL_WARN,
                   "PlayResY undefined, setting to %d", track->PlayResY);
        } else if (track->PlayResY <= 0) {
            track->PlayResY = FFMAX(1, track->PlayResX * 3LL / 4);
            ass_msg(lib, MSGL_WARN,
                   "PlayResY undefined, setting to %d", track->PlayResY);
        } else if (track->PlayResX <= 0 && track->PlayResY == 1024) {
            track->PlayResX = 1280;
            ass_msg(lib, MSGL_WARN,
                   "PlayResX undefined, setting to %d", track->PlayResX);
        } else if (track->PlayResX <= 0) {
            track->PlayResX = FFMAX(1, track->PlayResY * 4LL / 3);
            ass_msg(lib, MSGL_WARN,
                   "PlayResX undefined, setting to %d", track->PlayResX);
        }
    }
}

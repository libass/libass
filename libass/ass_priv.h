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

#ifndef LIBASS_PRIV_H
#define LIBASS_PRIV_H

#include <stdbool.h>
#include <stdint.h>

#include "ass_shaper.h"

typedef enum {
    PST_UNKNOWN = 0,
    PST_INFO,
    PST_STYLES,
    PST_EVENTS,
    PST_FONTS
} ParserState;

typedef enum {
    SINFO_LANGUAGE     = 1 << 0,
    SINFO_PLAYRESX     = 1 << 1,
    SINFO_PLAYRESY     = 1 << 2,
    SINFO_TIMER        = 1 << 3,
    SINFO_WRAPSTYLE    = 1 << 4,
    SINFO_SCALEDBORDER = 1 << 5,
    SINFO_COLOURMATRIX = 1 << 6,
    SINFO_KERNING      = 1 << 7,
    // for legacy detection
    GENBY_FFMPEG       = 1 << 8
    // max 32 enumerators
} ScriptInfo;

typedef enum ASS_FormatToken {
    // cases must match field names in ASS_Track
    ASS_FMT_UNKNOWN,
    ASS_FMT_Name,            // ASS Style, ASS Event, SSA Style, SSA Event
    ASS_FMT_FontName,        // ASS Style, SSA Style
    ASS_FMT_FontSize,        // ASS Style, SSA Style
    ASS_FMT_PrimaryColour,   // ASS Style, SSA Style
    ASS_FMT_SecondaryColour, // ASS Style, SSA Style
    ASS_FMT_OutlineColour,   // ASS Style
    ASS_FMT_BackColour,      // ASS Style, SSA Style
    ASS_FMT_Bold,            // ASS Style, SSA Style
    ASS_FMT_Italic,          // ASS Style, SSA Style
    ASS_FMT_Underline,       // ASS Style
    ASS_FMT_StrikeOut,       // ASS Style
    ASS_FMT_ScaleX,          // ASS Style
    ASS_FMT_ScaleY,          // ASS Style
    ASS_FMT_Spacing,         // ASS Style
    ASS_FMT_Angle,           // ASS Style
    ASS_FMT_BorderStyle,     // ASS Style, SSA Style
    ASS_FMT_Outline,         // ASS Style, SSA Style
    ASS_FMT_Shadow,          // ASS Style, SSA Style
    ASS_FMT_Alignment,       // ASS Style, SSA Style
    ASS_FMT_MarginL,         // ASS Style, ASS Event, SSA Style, SSA Event
    ASS_FMT_MarginR,         // ASS Style, ASS Event, SSA Style, SSA Event
    ASS_FMT_MarginV,         // ASS Style, ASS Event, SSA Style, SSA Event
    ASS_FMT_Encoding,        // ASS Style, SSA Style
    ASS_FMT_Layer,           // ASS Event
    ASS_FMT_Start,           // ASS Event, SSA Event
    ASS_FMT_End,             // ASS Event, SSA Event
    ASS_FMT_Style,           // ASS Event, SSA Event
    ASS_FMT_Effect,          // ASS Event, SSA Event
    ASS_FMT_Text,            // ASS Event, SSA Event
    // SSA exclusive (currently ignored)
    ASS_FMT_TertiaryColour,  // SSA Style
    ASS_FMT_AlphaLevel,      // SSA Style
    ASS_FMT_Marked,          // SSA Event
    // TODO: check v4++-FormatToken's actual in-file names
    ASS_FMT_RelativeTo,      // v4++ Extension (not sure what RelativeTo is supposed to do)
    ASS_FMT_MarginT,         // v4++ Extension (margin top;    replaces MarginV)
    ASS_FMT_MarginB          // v4++ Extension (margin bottom; replaces MarginV)
} ASS_FormatToken;

typedef struct {
    size_t n_tokens;
    size_t max_tokens;
    ASS_FormatToken *tokens;
    char *string; // to check for changes in public track fields
} ASS_FormatLine;

struct parser_priv {
    ParserState state;
    char *fontname;
    char *fontdata;
    size_t fontdata_size;
    size_t fontdata_used;

    // contains bitmap of ReadOrder IDs of all read events
    uint32_t *read_order_bitmap;
    int read_order_elems; // size in uint32_t units of read_order_bitmap
    int check_readorder;

    // tracks [Script Info] headers set by the script
    uint32_t header_flags;

#ifdef USE_FRIBIDI_EX_API
    bool bidi_brackets;
#endif

    ASS_FormatLine style_format;
    ASS_FormatLine event_format;
};

#endif /* LIBASS_PRIV_H */

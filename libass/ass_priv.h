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
    SINFO_SCRIPTTYPE   = 1 << 8,
    SINFO_LAYOUTRESX   = 1 << 9,
    SINFO_LAYOUTRESY   = 1 << 10,
    // for legacy detection
    GENBY_FFMPEG       = 1 << 14
    // max 32 enumerators
} ScriptInfo;

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

    uint32_t feature_flags;

    long long prune_delay;
    long long prune_next_ts;
};

#endif /* LIBASS_PRIV_H */

/*
 * Copyright (C) 2024 libass contributors
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

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "writeout.h"


static const char *ycbcr_to_str(ASS_YCbCrMatrix ycbcr_val)
{
    switch (ycbcr_val) {
        case YCBCR_NONE:         return "None";
        case YCBCR_BT601_TV:     return "TV.601";
        case YCBCR_BT601_PC:     return "PC.601";
        case YCBCR_BT709_TV:     return "TV.709";
        case YCBCR_BT709_PC:     return "PC.709";
        case YCBCR_SMPTE240M_TV: return "TV.240m";
        case YCBCR_SMPTE240M_PC: return "PC.240m";
        case YCBCR_FCC_TV:       return "TV.fcc";
        case YCBCR_FCC_PC:       return "PC.fcc";
        case YCBCR_DEFAULT:      return "ThereWasNoHeader";
        case YCBCR_UNKNOWN:
        default:
            return "InvalidUnknownValue";
    }
}

static bool time_to_str(long long time, char **buf, size_t *buf_size)
{
    time /= 10; // ASS files can only have centi-second precision
    int sign = time < 0 ? -1 : 1;
    time = labs(time);

    // Without chars for hours, except for sign indicator
    size_t min_size = sign == -1 ? 14 : 10;

    long long cs = time % 100;  time /= 100;
    long long  s = time %  60;  time /=  60;
    long long  m = time %  60;  time /=  60;

    min_size += time >= 10 ? ceil(log10(time + 1)) : 1;
    if (*buf_size < min_size) {
        // +14 are arbitrary padding to avoid too many reallocs
        char *nbuf = realloc(*buf, min_size + 14);
        if (!nbuf)
            return false;
        *buf = nbuf;
        *buf_size = min_size + 14;
    }

    int ret = snprintf(*buf, *buf_size, "%lld:%02lld:%02lld.%02lld",
                       time * sign, m * sign, s * sign, cs * sign);
    return ret >= 0 && ret < *buf_size;
}

static int ssa2ass_align(int ssa_align)
{
    return ((ssa_align & 0xC) >> 2) * 3 + (ssa_align & 0x3);
}

static const char *tracktype_to_str(int track_type)
{
    switch (track_type) {
    case TRACK_TYPE_ASS:
        return "ASS";
    case TRACK_TYPE_SSA:
        return "SSA";
    case TRACK_TYPE_UNKNOWN:
        return "other";
    }
    return "(oops, track type list out of date)";
}


static void write_header(FILE *f, ASS_Track *track, const char *originalformat_name)
{
    // format version is normalised to ASS
    fprintf(f, "[Script Info]\n");
    fprintf(f, "; Original Format: %s\n", originalformat_name);
    fprintf(f, "ScriptType: v4.00+\n");

    #define HEADER_INT(name)  fprintf(f, "%s: %d\n",   #name, track->name)
    #define HEADER_FLT(name)  fprintf(f, "%s: %.3f\n", #name, track->name)
    #define HEADER_STR(name)  if (track->name) fprintf(f, "%s: %s\n",   #name, track->name)
    #define HEADER_BOOL(name) fprintf(f, "%s: %s\n",   #name, track->name ? "yes" : "no")

    HEADER_INT(PlayResX);
    HEADER_INT(PlayResY);
    HEADER_INT(LayoutResX);
    HEADER_INT(LayoutResY);
    HEADER_FLT(Timer);
    HEADER_INT(WrapStyle);
    HEADER_BOOL(ScaledBorderAndShadow);
    HEADER_BOOL(Kerning);
    HEADER_STR(Language);
    if (track->YCbCrMatrix != YCBCR_DEFAULT) /* Or normalise this to TV.601? */
        fprintf(f, "YCbCrMatrix: %s\n", ycbcr_to_str(track->YCbCrMatrix));

    #undef HEADER_INT
    #undef HEADER_FLT
    #undef HEADER_STR
    #undef HEADER_BOOL
}

static void write_styles(FILE *f, ASS_Track *track)
{
    fprintf(f, "\n[V4+ Styles]\n");
    fprintf(f, "Format: "
        "Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n");

    // don't print out builtin fallback
    for (int i = 1; i < track->n_styles; i++) {
        ASS_Style *s = track->styles + i;
        fprintf(f, "Style: "
                   "%s,%s,%.3f,&H%08X,&H%08X,&H%08X,&H%08X,%d,%d,%d,%d,"
                   "%.3f,%.3f,%.3f,%.3f,%d,%.3f,%.3f,%d,%d,%d,%d,%d\n",
            s->Name, s->FontName, s->FontSize, s->PrimaryColour, s->SecondaryColour,
            s->OutlineColour, s->BackColour, s->Bold, s->Italic, s->Underline, s->StrikeOut,
            s->ScaleX, s->ScaleY, s->Spacing, s->Angle, s->BorderStyle, s->Outline, s->Shadow,
            ssa2ass_align(s->Alignment), s->MarginL, s->MarginR, s->MarginV, s->Encoding);
    }
}

static void write_events(FILE *f, ASS_Track *track)
{
    size_t start_size = 128;
    char *start = calloc(1, start_size);
    if (!start) {
        printf("Failure to alloc start time buffer!\n");
        return;
    }

    size_t end_size = 128;
    char *end = calloc(1, end_size);
    if (!start) {
        printf("Failure to alloc end time buffer!\n");
        return;
    }

    fprintf(f, "\n[Events]\n");
    fprintf(f, "Format: "
        "Layer, Start, End, Style, Name, "
        "MarginL, MarginR, MarginV, Effect, Text\n");

    for (int i = 0; i < track->n_events; i++) {
        ASS_Event *e = track->events + i;
        bool flag = time_to_str(e->Start, &start, &start_size);
        flag = flag && time_to_str(e->Start + e->Duration, &end, &end_size);
        if (!flag) {
            printf("Ommiting event %d due to timestamp failure!\n", i);
            fprintf(f, "Comment: Skipped event\n");
            continue;
        }
        fprintf(f, "Dialogue: %d,%s,%s,%s,%s,%03d,%03d,%03d,%s,%s\n",
            e->Layer, start, end, (track->styles + e->Style)->Name, e->Name,
            e->MarginL, e->MarginR, e->MarginV, e->Effect, e->Text);
    }
}

void write_out_track(ASS_Track *track, const char *outpath)
{
    const char *originalformat_name = tracktype_to_str(track->track_type);

    printf("Parsed sub stats:\n");
    printf("  Format Version:  %s\n", originalformat_name);
    printf("         #Styles:  %d\n", track->n_styles - 1); // exclude builtin fallback
    printf("         #Events:  %d\n", track->n_events);

    FILE *f = NULL;
    if (outpath) {
        f = fopen(outpath, "w");
        printf("Parsed File will be written to:  %s\n", outpath);
    } else {
        char filename[] = "/tmp/parsedSubs_XXXXXX";
        int fd = mkstemp(filename);
        if (fd == -1) {
            printf("Failed to acquire temporary file!\n");
            return;
        }
        f = fdopen(fd, "w");
        if (!f) {
            printf("Failed to associate fd with a stream!\n");
            return;
        }
        printf("Parsed File will be written to:  %.*s\n", (int) sizeof(filename), filename);
    }

    // Write content normalised to ASS (v4+)
    write_header(f, track, originalformat_name);
    fprintf(f, "\n; Potential embedded fonts are lost in write out\n");
    write_styles(f, track);
    write_events(f, track);

    fclose(f);
}

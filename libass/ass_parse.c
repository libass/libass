/*
 * Copyright (C) 2009 Grigori Goronzy <greg@geekmind.org>
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
#include <math.h>

#include "ass_library.h"
#include "ass_render.h"
#include "ass_parse.h"

#define MAX_VALID_NARGS 7
#define MAX_BE 127
#define NBSP 0xa0   // unicode non-breaking space character

struct arg {
    char *start, *end;
};

static inline int32_t argtoi32(struct arg arg)
{
    int32_t value;
    mystrtoi32(&arg.start, 10, &value);
    return value;
}

static inline double argtod(struct arg arg)
{
    double value;
    mystrtod(&arg.start, &value);
    return value;
}

static inline void push_arg(struct arg *args, int *nargs, char *start, char *end)
{
    if (*nargs <= MAX_VALID_NARGS) {
        rskip_spaces(&end, start);
        if (end > start) {
            args[*nargs] = (struct arg) {start, end};
            ++*nargs;
        }
    }
}

/**
 * \brief Check if starting part of (*p) matches sample.
 * If true, shift p to the first symbol after the matching part.
 */
static inline int mystrcmp(char **p, const char *sample)
{
    char *p2;
    for (p2 = *p; *sample != 0 && *p2 == *sample; p2++, sample++)
        ;
    if (*sample == 0) {
        *p = p2;
        return 1;
    }
    return 0;
}

/**
 * \brief Change current font, using setting from render_priv->state.
 */
void ass_update_font(RenderContext *state)
{
    unsigned val;
    ASS_FontDesc desc;

    desc.family = state->family;
    if (!desc.family.str)
        return;
    if (desc.family.len && desc.family.str[0] == '@') {
        desc.vertical = 1;
        desc.family.str++;
        desc.family.len--;
    } else {
        desc.vertical = 0;
    }

    val = state->bold;
    // 0 = normal, 1 = bold, >1 = exact weight
    if (val == 1 || val == -1)
        val = 700;               // bold
    else if (val <= 0)
        val = 400;               // normal
    desc.bold = val;

    val = state->italic;
    if (val == 1)
        val = 100;              // italic
    else if (val <= 0)
        val = 0;                // normal
    desc.italic = val;

    ass_cache_dec_ref(state->font);
    state->font = ass_font_new(state->renderer, &desc);
}

/**
 * \brief Convert double to int32_t without UB
 * on out-of-range values; match x86 behavior
 */
static inline int32_t dtoi32(double val)
{
    if (isnan(val) || val <= INT32_MIN || val >= INT32_MAX + 1LL)
        return INT32_MIN;
    return val;
}

static double calc_anim(double new, double old, double pwr)
{
   return (1 - pwr) * old + new * pwr;
}

static int32_t calc_anim_int32(uint32_t new, uint32_t old, double pwr)
{
    return dtoi32(calc_anim(new, old, pwr));
}

/**
 * \brief Calculate a weighted average of two colors
 * calculates c1*(1-a) + c2*a, but separately for each component except alpha
 */
static void change_color(uint32_t *var, uint32_t new, double pwr)
{
    uint32_t co = ass_bswap32(*var);
    uint32_t cn = ass_bswap32(new);

    uint32_t cc = (calc_anim_int32(cn & 0xff0000, co & 0xff0000, pwr) & 0xff0000) |
                  (calc_anim_int32(cn & 0x00ff00, co & 0x00ff00, pwr) & 0x00ff00) |
                  (calc_anim_int32(cn & 0x0000ff, co & 0x0000ff, pwr) & 0x0000ff);

    (*var) = (ass_bswap32(cc & 0xffffff)) | _a(*var);
}

// like change_color, but for alpha component only
static inline void change_alpha(uint32_t *var, int32_t new, double pwr)
{
    *var = (*var & 0xFFFFFF00) | (uint8_t)calc_anim_int32(new, _a(*var), pwr);
}

/**
 * \brief Multiply two alpha values
 * \param a first value
 * \param b second value
 * \return result of multiplication
 * At least one of the parameters must be less than or equal to 0xFF.
 * The result is less than or equal to max(a, b, 0xFF).
 */
static inline uint32_t mult_alpha(uint32_t a, uint32_t b)
{
    return a - ((uint64_t) a * b + 0x7F) / 0xFF + b;
}

void ass_apply_fade(uint32_t *clr, int fade)
{
    // VSFilter compatibility: apply fade only when it's positive
    if (fade > 0)
        change_alpha(clr, mult_alpha(_a(*clr), fade), 1);
}

/**
 * \brief Calculate alpha value by piecewise linear function
 * Used for \fad, \fade implementation.
 */
static int
interpolate_alpha(long long now, int32_t t1, int32_t t2, int32_t t3,
                  int32_t t4, int a1, int a2, int a3)
{
    int a;
    double cf;

    if (now < t1) {
        a = a1;
    } else if (now < t2) {
        cf = ((double) (int32_t) ((uint32_t) now - t1)) /
                (int32_t) ((uint32_t) t2 - t1);
        a = a1 * (1 - cf) + a2 * cf;
    } else if (now < t3) {
        a = a2;
    } else if (now < t4) {
        cf = ((double) (int32_t) ((uint32_t) now - t3)) /
                (int32_t) ((uint32_t) t4 - t3);
        a = a2 * (1 - cf) + a3 * cf;
    } else {                    // now >= t4
        a = a3;
    }

    return a;
}

/**
 * Parse a vector clip into an outline, using the proper scaling
 * parameters.  Translate it to correct for screen borders, if needed.
 */
static bool parse_vector_clip(RenderContext *state,
                              struct arg *args, int nargs)
{
    if (nargs != 1 && nargs != 2)
        return false;

    int scale = 1;
    if (nargs == 2)
        scale = argtoi32(args[0]);

    struct arg text = args[nargs - 1];
    state->clip_drawing_text.str = text.start;
    state->clip_drawing_text.len = text.end - text.start;
    state->clip_drawing_scale = scale;
    return true;
}

static int32_t parse_alpha_tag(char *str)
{
    int32_t alpha = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &alpha);
    return alpha;
}

static uint32_t parse_color_tag(char *str)
{
    int32_t color = 0;

    while (*str == '&' || *str == 'H')
        ++str;

    mystrtoi32(&str, 16, &color);
    return ass_bswap32((uint32_t) color);
}

/**
 * \brief find style by name as in \r
 * \param track track
 * \param name style name
 * \param len style name length
 * \return style in track->styles
 * Returns NULL if no style has the given name.
 */
static ASS_Style *lookup_style_strict(ASS_Track *track, char *name, size_t len)
{
    int i;
    for (i = track->n_styles - 1; i >= 0; --i) {
        if (strncmp(track->styles[i].Name, name, len) == 0 &&
            track->styles[i].Name[len] == '\0')
            return track->styles + i;
    }
    ass_msg(track->library, MSGL_WARN,
            "[%p]: Warning: no style named '%.*s' found",
            track, (int) len, name);
    return NULL;
}

/**
 * \brief Parse style override tags.
 * \param p string to parse
 * \param end end of string to parse, which must be '}', ')', or the first
 *            of a number of spaces immediately preceding '}' or ')'
 * \param pwr multiplier for some tag effects (comes from \t tags)
 */
char *ass_parse_tags(RenderContext *state, char *p, char *end, double pwr,
                     bool nested)
{
    ASS_Renderer *render_priv = state->renderer;
    for (char *q; p < end; p = q) {
        while (*p != '\\' && p != end)
            ++p;
        if (*p != '\\')
            break;
        ++p;
        if (p != end)
            skip_spaces(&p);

        q = p;
        while (*q != '(' && *q != '\\' && q != end)
            ++q;
        if (q == p)
            continue;

        char *name_end = q;

        // Store one extra element to be able to detect excess arguments
        struct arg args[MAX_VALID_NARGS + 1];
        int nargs = 0;
        bool has_backslash_arg = false;
        for (int i = 0; i <= MAX_VALID_NARGS; ++i)
            args[i].start = args[i].end = "";

        // Split parenthesized arguments. Do this for all tags and before
        // any non-parenthesized argument because that's what VSFilter does.
        if (*q == '(') {
            ++q;
            while (1) {
                if (q != end)
                    skip_spaces(&q);

                // Split on commas. If there is a backslash, ignore any
                // commas following it and lump everything starting from
                // the last comma, through the backslash and all the way
                // to the end of the argument string into a single argument.

                char *r = q;
                while (*r != ',' && *r != '\\' && *r != ')' && r != end)
                    ++r;

                if (*r == ',') {
                    push_arg(args, &nargs, q, r);
                    q = r + 1;
                } else {
                    // Swallow the rest of the parenthesized string. This could
                    // be either a backslash-argument or simply the last argument.
                    if (*r == '\\') {
                        has_backslash_arg = true;
                        while (*r != ')' && r != end)
                            ++r;
                    }
                    push_arg(args, &nargs, q, r);
                    q = r;
                    // The closing parenthesis could be missing.
                    if (q != end)
                        ++q;
                    break;
                }
            }
        }

#define tag(name) (mystrcmp(&p, (name)) && (push_arg(args, &nargs, p, name_end), 1))
#define complex_tag(name) mystrcmp(&p, (name))

        // New tags introduced in vsfilter 2.39
        if (tag("xbord")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                val = state->border_x * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->Outline;
            state->border_x = val;
        } else if (tag("ybord")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                val = state->border_y * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->Outline;
            state->border_y = val;
        } else if (tag("xshad")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                val = state->shadow_x * (1 - pwr) + val * pwr;
            } else
                val = state->style->Shadow;
            state->shadow_x = val;
        } else if (tag("yshad")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                val = state->shadow_y * (1 - pwr) + val * pwr;
            } else
                val = state->style->Shadow;
            state->shadow_y = val;
        } else if (tag("fax")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->fax =
                    val * pwr + state->fax * (1 - pwr);
            } else
                state->fax = 0.;
        } else if (tag("fay")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->fay =
                    val * pwr + state->fay * (1 - pwr);
            } else
                state->fay = 0.;
        } else if (complex_tag("iclip")) {
            if (nargs == 4) {
                int32_t x0, y0, x1, y1;
                x0 = argtoi32(args[0]);
                y0 = argtoi32(args[1]);
                x1 = argtoi32(args[2]);
                y1 = argtoi32(args[3]);
                state->clip_x0 =
                    state->clip_x0 * (1 - pwr) + x0 * pwr;
                state->clip_x1 =
                    state->clip_x1 * (1 - pwr) + x1 * pwr;
                state->clip_y0 =
                    state->clip_y0 * (1 - pwr) + y0 * pwr;
                state->clip_y1 =
                    state->clip_y1 * (1 - pwr) + y1 * pwr;
                state->clip_mode = 1;
            } else if (!state->clip_drawing_text.str) {
                if (parse_vector_clip(state, args, nargs))
                    state->clip_drawing_mode = 1;
            }
        } else if (tag("blur")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                val = state->blur * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
                val = (val > BLUR_MAX_RADIUS) ? BLUR_MAX_RADIUS : val;
                state->blur = val;
            } else
                state->blur = 0.0;
            // ASS standard tags
        } else if (tag("fscx")) {
            double val;
            if (nargs) {
                val = argtod(*args) / 100;
                val = state->scale_x * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->ScaleX;
            state->scale_x = val;
        } else if (tag("fscy")) {
            double val;
            if (nargs) {
                val = argtod(*args) / 100;
                val = state->scale_y * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = state->style->ScaleY;
            state->scale_y = val;
        } else if (tag("fsc")) {
            state->scale_x = state->style->ScaleX;
            state->scale_y = state->style->ScaleY;
        } else if (tag("fsp")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->hspacing =
                    state->hspacing * (1 - pwr) + val * pwr;
            } else
                state->hspacing = state->style->Spacing;
        } else if (tag("fs")) {
            double val = 0;
            if (nargs) {
                val = argtod(*args);
                if (*args->start == '+' || *args->start == '-')
                    val = state->font_size * (1 + pwr * val / 10);
                else
                    val = state->font_size * (1 - pwr) + val * pwr;
            }
            if (val <= 0)
                val = state->style->FontSize;
            state->font_size = val;
        } else if (tag("bord")) {
            double val, xval, yval;
            if (nargs) {
                val = argtod(*args);
                xval = state->border_x * (1 - pwr) + val * pwr;
                yval = state->border_y * (1 - pwr) + val * pwr;
                xval = (xval < 0) ? 0 : xval;
                yval = (yval < 0) ? 0 : yval;
            } else
                xval = yval = state->style->Outline;
            state->border_x = xval;
            state->border_y = yval;
        } else if (complex_tag("move")) {
            double x1, x2, y1, y2;
            int32_t t1, t2, delta_t, t;
            double x, y;
            double k;
            if (nargs == 4 || nargs == 6) {
                x1 = argtod(args[0]);
                y1 = argtod(args[1]);
                x2 = argtod(args[2]);
                y2 = argtod(args[3]);
                t1 = t2 = 0;
                if (nargs == 6) {
                    t1 = argtoi32(args[4]);
                    t2 = argtoi32(args[5]);
                    if (t1 > t2) {
                        long long tmp = t2;
                        t2 = t1;
                        t1 = tmp;
                    }
                }
            } else
                continue;
            if (t1 <= 0 && t2 <= 0) {
                t1 = 0;
                t2 = state->event->Duration;
            }
            delta_t = (uint32_t) t2 - t1;
            t = render_priv->time - state->event->Start;
            if (t <= t1)
                k = 0.;
            else if (t >= t2)
                k = 1.;
            else
                k = ((double) (int32_t) ((uint32_t) t - t1)) / delta_t;
            x = k * (x2 - x1) + x1;
            y = k * (y2 - y1) + y1;
            if (!(state->evt_type & EVENT_POSITIONED)) {
                state->pos_x = x;
                state->pos_y = y;
                state->detect_collisions = 0;
                state->evt_type |= EVENT_POSITIONED;
            }
        } else if (tag("frx")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->frx =
                    val * pwr + state->frx * (1 - pwr);
            } else
                state->frx = 0.;
        } else if (tag("fry")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->fry =
                    val * pwr + state->fry * (1 - pwr);
            } else
                state->fry = 0.;
        } else if (tag("frz") || tag("fr")) {
            double val;
            if (nargs) {
                val = argtod(*args);
                state->frz =
                    val * pwr + state->frz * (1 - pwr);
            } else
                state->frz =
                    state->style->Angle;
        } else if (tag("fn")) {
            char *start = args->start;
            if (nargs && strncmp(start, "0", args->end - start)) {
                skip_spaces(&start);
                state->family.str = start;
                state->family.len = args->end - start;
            } else {
                state->family.str = state->style->FontName;
                state->family.len = strlen(state->style->FontName);
            }
            ass_update_font(state);
        } else if (tag("alpha")) {
            int i;
            if (nargs) {
                int32_t a = parse_alpha_tag(args->start);
                for (i = 0; i < 4; ++i)
                    change_alpha(&state->c[i], a, pwr);
            } else {
                change_alpha(&state->c[0],
                             _a(state->style->PrimaryColour), 1);
                change_alpha(&state->c[1],
                             _a(state->style->SecondaryColour), 1);
                change_alpha(&state->c[2],
                             _a(state->style->OutlineColour), 1);
                change_alpha(&state->c[3],
                             _a(state->style->BackColour), 1);
            }
            // FIXME: simplify
        } else if (tag("an")) {
            int32_t val = argtoi32(*args);
            if ((state->parsed_tags & PARSED_A) == 0) {
                if (val >= 1 && val <= 9)
                    state->alignment = numpad2align(val);
                else
                    state->alignment =
                        state->style->Alignment;
                state->parsed_tags |= PARSED_A;
            }
        } else if (tag("a")) {
            int32_t val = argtoi32(*args);
            if ((state->parsed_tags & PARSED_A) == 0) {
                if (val >= 1 && val <= 11)
                    // take care of a vsfilter quirk:
                    // handle illegal \a8 and \a4 like \a5
                    state->alignment = ((val & 3) == 0) ? 5 : val;
                else
                    state->alignment =
                        state->style->Alignment;
                state->parsed_tags |= PARSED_A;
            }
        } else if (complex_tag("pos")) {
            double v1, v2;
            if (nargs == 2) {
                v1 = argtod(args[0]);
                v2 = argtod(args[1]);
            } else
                continue;
            if (state->evt_type & EVENT_POSITIONED) {
                ass_msg(render_priv->library, MSGL_V, "Subtitle has a new \\pos "
                       "after \\move or \\pos, ignoring");
            } else {
                state->evt_type |= EVENT_POSITIONED;
                state->detect_collisions = 0;
                state->pos_x = v1;
                state->pos_y = v2;
            }
        } else if (complex_tag("fade") || complex_tag("fad")) {
            int32_t a1, a2, a3;
            int32_t t1, t2, t3, t4;
            if (nargs == 2) {
                // 2-argument version (\fad, according to specs)
                a1 = 0xFF;
                a2 = 0;
                a3 = 0xFF;
                t1 = -1;
                t2 = argtoi32(args[0]);
                t3 = argtoi32(args[1]);
                t4 = -1;
            } else if (nargs == 7) {
                // 7-argument version (\fade)
                a1 = argtoi32(args[0]);
                a2 = argtoi32(args[1]);
                a3 = argtoi32(args[2]);
                t1 = argtoi32(args[3]);
                t2 = argtoi32(args[4]);
                t3 = argtoi32(args[5]);
                t4 = argtoi32(args[6]);
            } else
                continue;
            if (t1 == -1 && t4 == -1) {
                t1 = 0;
                t4 = state->event->Duration;
                t3 = (uint32_t) t4 - t3;
            }
            if ((state->parsed_tags & PARSED_FADE) == 0) {
                state->fade =
                    interpolate_alpha(render_priv->time -
                            state->event->Start, t1, t2,
                            t3, t4, a1, a2, a3);
                state->parsed_tags |= PARSED_FADE;
            }
        } else if (complex_tag("org")) {
            double v1, v2;
            if (nargs == 2) {
                v1 = argtod(args[0]);
                v2 = argtod(args[1]);
            } else
                continue;
            if (!state->have_origin) {
                state->org_x = v1;
                state->org_y = v2;
                state->have_origin = 1;
                state->detect_collisions = 0;
            }
        } else if (complex_tag("t")) {
            double accel;
            int cnt = nargs - 1;
            int32_t t1, t2, t, delta_t;
            double k;
            // VSFilter compatibility (because we can): parse the
            // timestamps differently depending on argument count.
            if (cnt == 3) {
                t1 = argtoi32(args[0]);
                t2 = argtoi32(args[1]);
                accel = argtod(args[2]);
            } else if (cnt == 2) {
                t1 = dtoi32(argtod(args[0]));
                t2 = dtoi32(argtod(args[1]));
                accel = 1.;
            } else if (cnt == 1) {
                t1 = 0;
                t2 = 0;
                accel = argtod(args[0]);
            } else {
                t1 = 0;
                t2 = 0;
                accel = 1.;
            }
            state->detect_collisions = 0;
            if (t2 == 0)
                t2 = state->event->Duration;
            delta_t = (uint32_t) t2 - t1;
            t = render_priv->time - state->event->Start;        // FIXME: move to render_context
            if (t < t1)
                k = 0.;
            else if (t >= t2)
                k = 1.;
            else {
                assert(delta_t != 0.);
                k = pow((double) (int32_t) ((uint32_t) t - t1) / delta_t, accel);
            }
            if (nested)
                pwr = k;
            if (cnt < 0 || cnt > 3)
                continue;
            // If there's no backslash in the arguments, there are no
            // override tags, so it's pointless to try to parse them.
            if (!has_backslash_arg)
                continue;
            p = args[cnt].start;
            if (args[cnt].end < end) {
                assert(!nested);
                p = ass_parse_tags(state, p, args[cnt].end, k, true);
            } else {
                assert(q == end);
                // No other tags can possibly follow this \t tag,
                // so we don't need to restore pwr after parsing \t.
                // The recursive call is now essentially a tail call,
                // so optimize it away.
                pwr = k;
                nested = true;
                q = p;
            }
        } else if (complex_tag("clip")) {
            if (nargs == 4) {
                int32_t x0, y0, x1, y1;
                x0 = argtoi32(args[0]);
                y0 = argtoi32(args[1]);
                x1 = argtoi32(args[2]);
                y1 = argtoi32(args[3]);
                state->clip_x0 =
                    state->clip_x0 * (1 - pwr) + x0 * pwr;
                state->clip_x1 =
                    state->clip_x1 * (1 - pwr) + x1 * pwr;
                state->clip_y0 =
                    state->clip_y0 * (1 - pwr) + y0 * pwr;
                state->clip_y1 =
                    state->clip_y1 * (1 - pwr) + y1 * pwr;
                state->clip_mode = 0;
            } else if (!state->clip_drawing_text.str) {
                if (parse_vector_clip(state, args, nargs))
                    state->clip_drawing_mode = 0;
            }
        } else if (tag("c") || tag("1c")) {
            if (nargs) {
                uint32_t val = parse_color_tag(args->start);
                change_color(&state->c[0], val, pwr);
            } else
                change_color(&state->c[0],
                             state->style->PrimaryColour, 1);
        } else if (tag("2c")) {
            if (nargs) {
                uint32_t val = parse_color_tag(args->start);
                change_color(&state->c[1], val, pwr);
            } else
                change_color(&state->c[1],
                             state->style->SecondaryColour, 1);
        } else if (tag("3c")) {
            if (nargs) {
                uint32_t val = parse_color_tag(args->start);
                change_color(&state->c[2], val, pwr);
            } else
                change_color(&state->c[2],
                             state->style->OutlineColour, 1);
        } else if (tag("4c")) {
            if (nargs) {
                uint32_t val = parse_color_tag(args->start);
                change_color(&state->c[3], val, pwr);
            } else
                change_color(&state->c[3],
                             state->style->BackColour, 1);
        } else if (tag("1a")) {
            if (nargs) {
                uint32_t val = parse_alpha_tag(args->start);
                change_alpha(&state->c[0], val, pwr);
            } else
                change_alpha(&state->c[0],
                             _a(state->style->PrimaryColour), 1);
        } else if (tag("2a")) {
            if (nargs) {
                uint32_t val = parse_alpha_tag(args->start);
                change_alpha(&state->c[1], val, pwr);
            } else
                change_alpha(&state->c[1],
                             _a(state->style->SecondaryColour), 1);
        } else if (tag("3a")) {
            if (nargs) {
                uint32_t val = parse_alpha_tag(args->start);
                change_alpha(&state->c[2], val, pwr);
            } else
                change_alpha(&state->c[2],
                             _a(state->style->OutlineColour), 1);
        } else if (tag("4a")) {
            if (nargs) {
                uint32_t val = parse_alpha_tag(args->start);
                change_alpha(&state->c[3], val, pwr);
            } else
                change_alpha(&state->c[3],
                             _a(state->style->BackColour), 1);
        } else if (tag("r")) {
            if (nargs) {
                int len = args->end - args->start;
                ass_reset_render_context(state,
                        lookup_style_strict(render_priv->track, args->start, len));
            } else
                ass_reset_render_context(state, NULL);
        } else if (tag("be")) {
            double dval;
            if (nargs) {
                int32_t val;
                dval = argtod(*args);
                // VSFilter always adds +0.5, even if the value is negative
                val = dtoi32(state->be * (1 - pwr) + dval * pwr + 0.5);
                // Clamp to a safe upper limit, since high values need excessive CPU
                val = (val < 0) ? 0 : val;
                val = (val > MAX_BE) ? MAX_BE : val;
                state->be = val;
            } else
                state->be = 0;
        } else if (tag("b")) {
            int32_t val = argtoi32(*args);
            if (!nargs || !(val == 0 || val == 1 || val >= 100))
                val = state->style->Bold;
            state->bold = val;
            ass_update_font(state);
        } else if (tag("i")) {
            int32_t val = argtoi32(*args);
            if (!nargs || !(val == 0 || val == 1))
                val = state->style->Italic;
            state->italic = val;
            ass_update_font(state);
        } else if (tag("kt")) {
            // v4++
            double val = 0;
            if (nargs)
                val = argtod(*args) * 10;
            state->effect_skip_timing = dtoi32(val);
            state->effect_timing = 0;
            state->reset_effect = true;
        } else if (tag("kf") || tag("K")) {
            double val = 100;
            if (nargs)
                val = argtod(*args);
            state->effect_type = EF_KARAOKE_KF;
            state->effect_skip_timing +=
                    (uint32_t) state->effect_timing;
            state->effect_timing = dtoi32(val * 10);
        } else if (tag("ko")) {
            double val = 100;
            if (nargs)
                val = argtod(*args);
            state->effect_type = EF_KARAOKE_KO;
            state->effect_skip_timing +=
                    (uint32_t) state->effect_timing;
            state->effect_timing = dtoi32(val * 10);
        } else if (tag("k")) {
            double val = 100;
            if (nargs)
                val = argtod(*args);
            state->effect_type = EF_KARAOKE;
            state->effect_skip_timing +=
                    (uint32_t) state->effect_timing;
            state->effect_timing = dtoi32(val * 10);
        } else if (tag("shad")) {
            double val, xval, yval;
            if (nargs) {
                val = argtod(*args);
                xval = state->shadow_x * (1 - pwr) + val * pwr;
                yval = state->shadow_y * (1 - pwr) + val * pwr;
                // VSFilter compatibility: clip for \shad but not for \[xy]shad
                xval = (xval < 0) ? 0 : xval;
                yval = (yval < 0) ? 0 : yval;
            } else
                xval = yval = state->style->Shadow;
            state->shadow_x = xval;
            state->shadow_y = yval;
        } else if (tag("s")) {
            int32_t val = argtoi32(*args);
            if (!nargs || !(val == 0 || val == 1))
                val = state->style->StrikeOut;
            if (val)
                state->flags |= DECO_STRIKETHROUGH;
            else
                state->flags &= ~DECO_STRIKETHROUGH;
        } else if (tag("u")) {
            int32_t val = argtoi32(*args);
            if (!nargs || !(val == 0 || val == 1))
                val = state->style->Underline;
            if (val)
                state->flags |= DECO_UNDERLINE;
            else
                state->flags &= ~DECO_UNDERLINE;
        } else if (tag("pbo")) {
            double val = argtod(*args);
            state->pbo = val;
        } else if (tag("p")) {
            int32_t val = argtoi32(*args);
            val = (val < 0) ? 0 : val;
            state->drawing_scale = val;
        } else if (tag("q")) {
            int32_t val = argtoi32(*args);
            if (!nargs || !(val >= 0 && val <= 3))
                val = render_priv->track->WrapStyle;
            state->wrap_style = val;
        } else if (tag("fe")) {
            int32_t val;
            if (nargs)
                val = argtoi32(*args);
            else
                val = state->style->Encoding;
            state->font_encoding = val;
        }
    }

    return p;
}

void ass_apply_transition_effects(RenderContext *state)
{
    ASS_Renderer *render_priv = state->renderer;
    int v[4];
    int cnt;
    ASS_Event *event = state->event;
    char *p = event->Effect;

    if (!p || !*p)
        return;

    cnt = 0;
    while (cnt < 4 && (p = strchr(p, ';'))) {
        v[cnt++] = atoi(++p);
    }

    ASS_Vector layout_res = ass_layout_res(render_priv);
    if (strncmp(event->Effect, "Banner;", 7) == 0) {
        double delay;
        if (cnt < 1) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        if (cnt >= 2 && v[1])   // left-to-right
            state->scroll_direction = SCROLL_LR;
        else                    // right-to-left
            state->scroll_direction = SCROLL_RL;

        delay = v[0];
        // VSF works in storage coordinates, but scales delay to PlayRes canvas
        // before applying max(scaled_ delay, 1). This means, if scaled_delay < 1
        // (esp. delay=0) we end up with 1 ms per _storage pixel_ without any
        // PlayRes scaling.
        // The way libass deals with delay, it is automatically relative to the
        // PlayRes canvas, so we only want to "unscale" the small delay values.
        //
        // VSF also casts the scaled delay to int, which if not emulated leads to
        // easily noticeable deviations from VSFilter as the effect goes on.
        // To achieve both we need to keep our Playres-relative delay with high precision,
        // but must temporarily convert to storage-relative and truncate and take the
        // maxuimum there, before converting back.
        double scale_x = ((double) layout_res.x) / render_priv->track->PlayResX;
        delay = ((int) FFMAX(delay / scale_x, 1)) * scale_x;
        state->scroll_shift =
            (render_priv->time - event->Start) / delay;
        state->evt_type |= EVENT_HSCROLL;
        state->detect_collisions = 0;
        state->wrap_style = 2;
        return;
    }

    if (strncmp(event->Effect, "Scroll up;", 10) == 0) {
        state->scroll_direction = SCROLL_BT;
    } else if (strncmp(event->Effect, "Scroll down;", 12) == 0) {
        state->scroll_direction = SCROLL_TB;
    } else {
        ass_msg(render_priv->library, MSGL_DBG2,
                "Unknown transition effect: '%s'", event->Effect);
        return;
    }
    // parse scroll up/down parameters
    {
        double delay;
        int y0, y1;
        if (cnt < 3) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        delay = v[2];
        // See explanation for Banner
        double scale_y = ((double) layout_res.y) / render_priv->track->PlayResY;
        delay = ((int) FFMAX(delay / scale_y, 1)) * scale_y;
        state->scroll_shift =
            (render_priv->time - event->Start) / delay;
        if (v[0] < v[1]) {
            y0 = v[0];
            y1 = v[1];
        } else {
            y0 = v[1];
            y1 = v[0];
        }
        state->scroll_y0 = y0;
        state->scroll_y1 = y1;
        state->evt_type |= EVENT_VSCROLL;
        state->detect_collisions = 0;
    }

}

/**
 * \brief determine karaoke effects
 * Karaoke effects cannot be calculated during parse stage (ass_get_next_char()),
 * so they are done in a separate step.
 * Parse stage: when karaoke style override is found, its parameters are stored in the next glyph's
 * (the first glyph of the karaoke word)'s effect_type and effect_timing.
 * This function:
 * 1. sets effect_type for all glyphs in the word (_karaoke_ word)
 * 2. sets effect_timing for all glyphs to x coordinate of the border line between the left and right karaoke parts
 * (left part is filled with PrimaryColour, right one - with SecondaryColour).
 */
void ass_process_karaoke_effects(RenderContext *state)
{
    TextInfo *text_info = state->text_info;
    long long tm_current = state->renderer->time - state->event->Start;

    int32_t timing = 0, skip_timing = 0;
    Effect effect_type = EF_NONE;
    GlyphInfo *last_boundary = NULL;
    bool has_reset = false;
    for (int i = 0; i <= text_info->length; i++) {
        if (i < text_info->length &&
            !text_info->glyphs[i].starts_new_run) {

            if (text_info->glyphs[i].reset_effect) {
                has_reset = true;
                skip_timing = 0;
            }

            // VSFilter compatibility: if we have \k12345\k0 without a run
            // break, subsequent text is still part of the same karaoke word,
            // the current word's starting and ending time stay unchanged,
            // but the starting time of the next karaoke word is advanced.
            skip_timing += (uint32_t) text_info->glyphs[i].effect_skip_timing;
            continue;
        }

        GlyphInfo *start = last_boundary;
        GlyphInfo *end = text_info->glyphs + i;
        last_boundary = end;
        if (!start)
            continue;

        if (start->effect_type != EF_NONE)
            effect_type = start->effect_type;
        if (effect_type == EF_NONE)
            continue;

        if (start->reset_effect)
            timing = 0;

        long long tm_start = timing + start->effect_skip_timing;
        long long tm_end = tm_start + start->effect_timing;
        timing = !has_reset * tm_end + skip_timing;
        skip_timing = 0;
        has_reset = false;

        if (effect_type != EF_KARAOKE_KF)
            tm_end = tm_start;

        int x;
        if (tm_current < tm_start)
            x = -100000000;
        else if (tm_current >= tm_end)
            x = 100000000;
        else {
            GlyphInfo *first_visible = start, *last_visible = end - 1;
            while (first_visible < last_visible && first_visible->skip)
                ++first_visible;
            while (first_visible < last_visible && last_visible->skip)
                --last_visible;

            int x_start = first_visible->pos.x;
            int x_end = last_visible->pos.x + last_visible->advance.x;
            double dt = (double) (tm_current - tm_start) / (tm_end - tm_start);
            double frz = fmod(start->frz, 360);
            if (frz > 90 && frz < 270) {
                // Fill from right to left
                dt = 1 - dt;
                for (GlyphInfo *info = start; info < end; info++) {
                    uint32_t tmp = info->c[0];
                    info->c[0] = info->c[1];
                    info->c[1] = tmp;
                }
            }
            x = x_start + lrint((x_end - x_start) * dt);
        }

        for (GlyphInfo *info = start; info < end; info++) {
            info->effect_type = effect_type;
            info->effect_timing = x - info->pos.x;
        }
    }
}


/**
 * \brief Get next ucs4 char from string, parsing UTF-8 and escapes
 * \param str string pointer
 * \return ucs4 code of the next char
 * On return str points to the unparsed part of the string
 */
unsigned ass_get_next_char(RenderContext *state, char **str)
{
    char *p = *str;
    unsigned chr;
    if (*p == '\t') {
        ++p;
        *str = p;
        return ' ';
    }
    if (*p == '\\') {
        if ((p[1] == 'N') || ((p[1] == 'n') &&
                              (state->wrap_style == 2))) {
            p += 2;
            *str = p;
            return '\n';
        } else if (p[1] == 'n') {
            p += 2;
            *str = p;
            return ' ';
        } else if (p[1] == 'h') {
            p += 2;
            *str = p;
            return NBSP;
        } else if (p[1] == '{') {
            p += 2;
            *str = p;
            return '{';
        } else if (p[1] == '}') {
            p += 2;
            *str = p;
            return '}';
        }
    }
    chr = ass_utf8_get_char((char **) &p);
    *str = p;
    return chr;
}

// Return 1 if the event contains tags that will apply overrides the selective
// style override code should not touch. Return 0 otherwise.
int ass_event_has_hard_overrides(char *str)
{
    // look for \pos and \move tags inside {...}
    // mirrors ass_get_next_char, but is faster and doesn't change any global state
    while (*str) {
        if (str[0] == '\\' && str[1] != '\0') {
            str += 2;
        } else if (str[0] == '{') {
            str++;
            while (*str && *str != '}') {
                if (*str == '\\') {
                    char *p = str + 1;
                    if (mystrcmp(&p, "pos") || mystrcmp(&p, "move") ||
                        mystrcmp(&p, "clip") || mystrcmp(&p, "iclip") ||
                        mystrcmp(&p, "org") || mystrcmp(&p, "pbo") ||
                        mystrcmp(&p, "p"))
                        return 1;
                }
                str++;
            }
        } else {
            str++;
        }
    }
    return 0;
}

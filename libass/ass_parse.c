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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ass_render.h"
#include "ass_parse.h"

#define MAX_BE 127
#define NBSP 0xa0   // unicode non-breaking space character

#define skip_to(x) while ((*p != (x)) && (*p != '}') && (*p != 0)) { ++p;}
#define skip(x) if (*p == (x)) ++p; else { return p; }
#define skipopt(x) if (*p == (x)) { ++p; }

/**
 * \brief Check if starting part of (*p) matches sample.
 * If true, shift p to the first symbol after the matching part.
 */
static inline int mystrcmp(char **p, const char *sample)
{
    int len = strlen(sample);
    if (strncmp(*p, sample, len) == 0) {
        (*p) += len;
        return 1;
    } else
        return 0;
}

double ensure_font_size(ASS_Renderer *priv, double size)
{
    if (size < 1)
        size = 1;
    else if (size > priv->height * 2)
        size = priv->height * 2;

    return size;
}

static void change_font_size(ASS_Renderer *render_priv, double sz)
{
    render_priv->state.font_size = sz;
}

/**
 * \brief Change current font, using setting from render_priv->state.
 */
void update_font(ASS_Renderer *render_priv)
{
    unsigned val;
    ASS_FontDesc desc;

    if (render_priv->state.family[0] == '@') {
        desc.vertical = 1;
        desc.family = strdup(render_priv->state.family + 1);
    } else {
        desc.vertical = 0;
        desc.family = strdup(render_priv->state.family);
    }

    val = render_priv->state.bold;
    // 0 = normal, 1 = bold, >1 = exact weight
    if (val == 1 || val == -1)
        val = 700;               // bold
    else if (val <= 0)
        val = 400;               // normal
    desc.bold = val;

    val = render_priv->state.italic;
    if (val == 1)
        val = 110;              // italic
    else if (val <= 0)
        val = 0;                // normal
    desc.italic = val;

    render_priv->state.font =
        ass_font_new(render_priv->cache.font_cache, render_priv->library,
                     render_priv->ftlibrary, render_priv->fontselect,
                     &desc);
    free(desc.family);

    if (render_priv->state.font)
        change_font_size(render_priv, render_priv->state.font_size);
}

/**
 * \brief Change border width
 *
 * \param render_priv renderer state object
 * \param info glyph state object
 */
void change_border(ASS_Renderer *render_priv, double border_x, double border_y)
{
    int bord = 64 * border_x * render_priv->border_scale;

    if (bord > 0 && border_x == border_y) {
        if (!render_priv->state.stroker) {
            int error;
            error =
                FT_Stroker_New(render_priv->ftlibrary,
                               &render_priv->state.stroker);
            if (error) {
                ass_msg(render_priv->library, MSGL_V,
                        "failed to get stroker");
                render_priv->state.stroker = 0;
            }
            render_priv->state.stroker_radius = -1.0;
        }
        if (render_priv->state.stroker && render_priv->state.stroker_radius != bord) {
            FT_Stroker_Set(render_priv->state.stroker, bord,
                           FT_STROKER_LINECAP_ROUND,
                           FT_STROKER_LINEJOIN_ROUND, 0);
            render_priv->state.stroker_radius = bord;
        }
    } else {
        FT_Stroker_Done(render_priv->state.stroker);
        render_priv->state.stroker = 0;
    }
}

/**
 * \brief Calculate a weighted average of two colors
 * calculates c1*(1-a) + c2*a, but separately for each component except alpha
 */
static void change_color(uint32_t *var, uint32_t new, double pwr)
{
    (*var) = ((uint32_t) (_r(*var) * (1 - pwr) + _r(new) * pwr) << 24) +
        ((uint32_t) (_g(*var) * (1 - pwr) + _g(new) * pwr) << 16) +
        ((uint32_t) (_b(*var) * (1 - pwr) + _b(new) * pwr) << 8) + _a(*var);
}

// like change_color, but for alpha component only
inline void change_alpha(uint32_t *var, uint32_t new, double pwr)
{
    *var =
        (_r(*var) << 24) + (_g(*var) << 16) + (_b(*var) << 8) +
        (uint32_t) (_a(*var) * (1 - pwr) + _a(new) * pwr);
}

/**
 * \brief Multiply two alpha values
 * \param a first value
 * \param b second value
 * \return result of multiplication
 * Parameters and result are limited by 0xFF.
 */
inline uint32_t mult_alpha(uint32_t a, uint32_t b)
{
    return 0xFF - (0xFF - a) * (0xFF - b) / 0xFF;
}

/**
 * \brief Calculate alpha value by piecewise linear function
 * Used for \fad, \fade implementation.
 */
static unsigned
interpolate_alpha(long long now, long long t1, long long t2, long long t3,
                  long long t4, unsigned a1, unsigned a2, unsigned a3)
{
    unsigned a;
    double cf;

    if (now < t1) {
        a = a1;
    } else if (now < t2) {
        cf = ((double) (now - t1)) / (t2 - t1);
        a = a1 * (1 - cf) + a2 * cf;
    } else if (now < t3) {
        a = a2;
    } else if (now < t4) {
        cf = ((double) (now - t3)) / (t4 - t3);
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
static char *parse_vector_clip(ASS_Renderer *render_priv, char *p)
{
    int scale = 1;
    int res = 0;
    ASS_Drawing *drawing = render_priv->state.clip_drawing;

    ass_drawing_free(drawing);
    render_priv->state.clip_drawing =
        ass_drawing_new(render_priv->library, render_priv->ftlibrary);
    drawing = render_priv->state.clip_drawing;
    skipopt('(');
    res = mystrtoi(&p, &scale);
    skipopt(',')
    if (!res)
        scale = 1;
    drawing->scale = scale;
    drawing->scale_x = render_priv->font_scale_x * render_priv->font_scale;
    drawing->scale_y = render_priv->font_scale;
    while (*p != ')' && *p != '}' && *p != 0)
        ass_drawing_add_char(drawing, *p++);
    skipopt(')');

    return p;
}

/**
 * \brief Parse style override tag.
 * \param p string to parse
 * \param pwr multiplier for some tag effects (comes from \t tags)
 */
char *parse_tag(ASS_Renderer *render_priv, char *p, double pwr)
{
    skip_to('\\');
    skip('\\');
    if ((*p == '}') || (*p == 0))
        return p;

    // New tags introduced in vsfilter 2.39
    if (mystrcmp(&p, "xbord")) {
        double val;
        if (mystrtod(&p, &val)) {
            val = render_priv->state.border_x * (1 - pwr) + val * pwr;
            val = (val < 0) ? 0 : val;
        } else
            val = render_priv->state.style->Outline;
        render_priv->state.border_x = val;
    } else if (mystrcmp(&p, "ybord")) {
        double val;
        if (mystrtod(&p, &val)) {
            val = render_priv->state.border_y * (1 - pwr) + val * pwr;
            val = (val < 0) ? 0 : val;
        } else
            val = render_priv->state.style->Outline;
        render_priv->state.border_y = val;
    } else if (mystrcmp(&p, "xshad")) {
        double val;
        if (mystrtod(&p, &val))
            val = render_priv->state.shadow_x * (1 - pwr) + val * pwr;
        else
            val = render_priv->state.style->Shadow;
        render_priv->state.shadow_x = val;
    } else if (mystrcmp(&p, "yshad")) {
        double val;
        if (mystrtod(&p, &val))
            val = render_priv->state.shadow_y * (1 - pwr) + val * pwr;
        else
            val = render_priv->state.style->Shadow;
        render_priv->state.shadow_y = val;
    } else if (mystrcmp(&p, "fax")) {
        double val;
        if (mystrtod(&p, &val))
            render_priv->state.fax =
                val * pwr + render_priv->state.fax * (1 - pwr);
        else
            render_priv->state.fax = 0.;
    } else if (mystrcmp(&p, "fay")) {
        double val;
        if (mystrtod(&p, &val))
            render_priv->state.fay =
                val * pwr + render_priv->state.fay * (1 - pwr);
        else
            render_priv->state.fay = 0.;
    } else if (mystrcmp(&p, "iclip")) {
        int x0, y0, x1, y1;
        int res = 1;
        char *start = p;
        skip('(');
        res &= mystrtoi(&p, &x0);
        skipopt(',');
        res &= mystrtoi(&p, &y0);
        skipopt(',');
        res &= mystrtoi(&p, &x1);
        skipopt(',');
        res &= mystrtoi(&p, &y1);
        skipopt(')');
        if (res) {
            render_priv->state.clip_x0 =
                render_priv->state.clip_x0 * (1 - pwr) + x0 * pwr;
            render_priv->state.clip_x1 =
                render_priv->state.clip_x1 * (1 - pwr) + x1 * pwr;
            render_priv->state.clip_y0 =
                render_priv->state.clip_y0 * (1 - pwr) + y0 * pwr;
            render_priv->state.clip_y1 =
                render_priv->state.clip_y1 * (1 - pwr) + y1 * pwr;
            render_priv->state.clip_mode = 1;
        } else if (!render_priv->state.clip_drawing) {
            p = parse_vector_clip(render_priv, start);
            render_priv->state.clip_drawing_mode = 1;
        }
    } else if (mystrcmp(&p, "blur")) {
        double val;
        if (mystrtod(&p, &val)) {
            val = render_priv->state.blur * (1 - pwr) + val * pwr;
            val = (val < 0) ? 0 : val;
            val = (val > BLUR_MAX_RADIUS) ? BLUR_MAX_RADIUS : val;
            render_priv->state.blur = val;
        } else
            render_priv->state.blur = 0.0;
        // ASS standard tags
    } else if (mystrcmp(&p, "fsc")) {
        char tp = *p++;
        double val;
        if (tp == 'x') {
            if (mystrtod(&p, &val)) {
                val /= 100;
                val = render_priv->state.scale_x * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = render_priv->state.style->ScaleX;
            render_priv->state.scale_x = val;
        } else if (tp == 'y') {
            if (mystrtod(&p, &val)) {
                val /= 100;
                val = render_priv->state.scale_y * (1 - pwr) + val * pwr;
                val = (val < 0) ? 0 : val;
            } else
                val = render_priv->state.style->ScaleY;
            render_priv->state.scale_y = val;
        } else {
            --p;
            render_priv->state.scale_x = render_priv->state.style->ScaleX;
            render_priv->state.scale_y = render_priv->state.style->ScaleY;
        }
    } else if (mystrcmp(&p, "fsp")) {
        double val;
        if (mystrtod(&p, &val))
            render_priv->state.hspacing =
                render_priv->state.hspacing * (1 - pwr) + val * pwr;
        else
            render_priv->state.hspacing = render_priv->state.style->Spacing;
    } else if (mystrcmp(&p, "fs+")) {
        double val;
        mystrtod(&p, &val);
        val = render_priv->state.font_size * (1 + pwr * val / 10);
        if (val <= 0)
            val = render_priv->state.style->FontSize;
        if (render_priv->state.font)
            change_font_size(render_priv, val);
    } else if (mystrcmp(&p, "fs-")) {
        double val;
        mystrtod(&p, &val);
        val = render_priv->state.font_size * (1 - pwr * val / 10);
        if (val <= 0)
            val = render_priv->state.style->FontSize;
        if (render_priv->state.font)
            change_font_size(render_priv, val);
    } else if (mystrcmp(&p, "fs")) {
        double val;
        if (mystrtod(&p, &val))
            val = render_priv->state.font_size * (1 - pwr) + val * pwr;
        if (val <= 0)
            val = render_priv->state.style->FontSize;
        if (render_priv->state.font)
            change_font_size(render_priv, val);
    } else if (mystrcmp(&p, "bord")) {
        double val, xval, yval;
        if (mystrtod(&p, &val)) {
            xval = render_priv->state.border_x * (1 - pwr) + val * pwr;
            yval = render_priv->state.border_y * (1 - pwr) + val * pwr;
            xval = (xval < 0) ? 0 : xval;
            yval = (yval < 0) ? 0 : yval;
        } else
            xval = yval = render_priv->state.style->Outline;
        render_priv->state.border_x = xval;
        render_priv->state.border_y = yval;
    } else if (mystrcmp(&p, "move")) {
        double x1, x2, y1, y2;
        long long t1, t2, delta_t, t;
        double x, y;
        double k;
        skip('(');
        mystrtod(&p, &x1);
        skip(',');
        mystrtod(&p, &y1);
        skip(',');
        mystrtod(&p, &x2);
        skip(',');
        mystrtod(&p, &y2);
        t1 = t2 = 0;
        if (*p == ',') {
            skip(',');
            mystrtoll(&p, &t1);
            skip(',');
            mystrtoll(&p, &t2);
            // VSFilter
            if (t1 > t2) {
                double tmp = t2;
                t2 = t1;
                t1 = tmp;
            }
        }
        if (t1 <= 0 && t2 <= 0) {
            t1 = 0;
            t2 = render_priv->state.event->Duration;
        }
        skipopt(')');
        delta_t = t2 - t1;
        t = render_priv->time - render_priv->state.event->Start;
        if (t < t1)
            k = 0.;
        else if (t > t2)
            k = 1.;
        else
            k = ((double) (t - t1)) / delta_t;
        x = k * (x2 - x1) + x1;
        y = k * (y2 - y1) + y1;
        if (render_priv->state.evt_type != EVENT_POSITIONED) {
            render_priv->state.pos_x = x;
            render_priv->state.pos_y = y;
            render_priv->state.detect_collisions = 0;
            render_priv->state.evt_type = EVENT_POSITIONED;
        }
    } else if (mystrcmp(&p, "frx")) {
        double val;
        if (mystrtod(&p, &val)) {
            val *= M_PI / 180;
            render_priv->state.frx =
                val * pwr + render_priv->state.frx * (1 - pwr);
        } else
            render_priv->state.frx = 0.;
    } else if (mystrcmp(&p, "fry")) {
        double val;
        if (mystrtod(&p, &val)) {
            val *= M_PI / 180;
            render_priv->state.fry =
                val * pwr + render_priv->state.fry * (1 - pwr);
        } else
            render_priv->state.fry = 0.;
    } else if (mystrcmp(&p, "frz") || mystrcmp(&p, "fr")) {
        double val;
        if (mystrtod(&p, &val)) {
            val *= M_PI / 180;
            render_priv->state.frz =
                val * pwr + render_priv->state.frz * (1 - pwr);
        } else
            render_priv->state.frz =
                M_PI * render_priv->state.style->Angle / 180.;
    } else if (mystrcmp(&p, "fn")) {
        char *start = p;
        char *family;
        skip_to('\\');
        if (p > start && strncmp(start, "0", p - start)) {
            family = malloc(p - start + 1);
            strncpy(family, start, p - start);
            family[p - start] = '\0';
        } else
            family = strdup(render_priv->state.style->FontName);
        free(render_priv->state.family);
        render_priv->state.family = family;
        update_font(render_priv);
    } else if (mystrcmp(&p, "alpha")) {
        uint32_t val;
        int i;
        int hex = render_priv->track->track_type == TRACK_TYPE_ASS;
        if (strtocolor(render_priv->library, &p, &val, hex)) {
            unsigned char a = val >> 24;
            for (i = 0; i < 4; ++i)
                change_alpha(&render_priv->state.c[i], a, pwr);
        } else {
            change_alpha(&render_priv->state.c[0],
                         render_priv->state.style->PrimaryColour, 1);
            change_alpha(&render_priv->state.c[1],
                         render_priv->state.style->SecondaryColour, 1);
            change_alpha(&render_priv->state.c[2],
                         render_priv->state.style->OutlineColour, 1);
            change_alpha(&render_priv->state.c[3],
                         render_priv->state.style->BackColour, 1);
        }
        // FIXME: simplify
    } else if (mystrcmp(&p, "an")) {
        int val;
        mystrtoi(&p, &val);
        if ((render_priv->state.parsed_tags & PARSED_A) == 0) {
            if (val >= 1 && val <= 9) {
                int v = (val - 1) / 3;      // 0, 1 or 2 for vertical alignment
                if (v != 0)
                    v = 3 - v;
                val = ((val - 1) % 3) + 1;  // horizontal alignment
                val += v * 4;
                render_priv->state.alignment = val;
            } else
                render_priv->state.alignment =
                    render_priv->state.style->Alignment;
            render_priv->state.parsed_tags |= PARSED_A;
        }
    } else if (mystrcmp(&p, "a")) {
        int val;
        mystrtoi(&p, &val);
        if ((render_priv->state.parsed_tags & PARSED_A) == 0) {
            if (val >= 1 && val <= 11)
                // take care of a vsfilter quirk:
                // handle illegal \a8 and \a4 like \a5
                render_priv->state.alignment = ((val & 3) == 0) ? 5 : val;
            else
                render_priv->state.alignment =
                    render_priv->state.style->Alignment;
            render_priv->state.parsed_tags |= PARSED_A;
        }
    } else if (mystrcmp(&p, "pos")) {
        double v1, v2;
        skip('(');
        mystrtod(&p, &v1);
        skip(',');
        mystrtod(&p, &v2);
        skipopt(')');
        if (render_priv->state.evt_type == EVENT_POSITIONED) {
            ass_msg(render_priv->library, MSGL_V, "Subtitle has a new \\pos "
                   "after \\move or \\pos, ignoring");
        } else {
            render_priv->state.evt_type = EVENT_POSITIONED;
            render_priv->state.detect_collisions = 0;
            render_priv->state.pos_x = v1;
            render_priv->state.pos_y = v2;
        }
    } else if (mystrcmp(&p, "fad")) {
        int a1, a2, a3;
        long long t1, t2, t3, t4;
        if (*p == 'e')
            ++p;                // either \fad or \fade
        skip('(');
        mystrtoi(&p, &a1);
        skip(',');
        mystrtoi(&p, &a2);
        if (*p == ')') {
            // 2-argument version (\fad, according to specs)
            // a1 and a2 are fade-in and fade-out durations
            t1 = -1;
            t2 = a1;
            t3 = a2;
            t4 = -1;
            a1 = 0xFF;
            a2 = 0;
            a3 = 0xFF;
        } else {
            // 7-argument version (\fade)
            // a1 and a2 (and a3) are opacity values
            skip(',');
            mystrtoi(&p, &a3);
            skip(',');
            mystrtoll(&p, &t1);
            skip(',');
            mystrtoll(&p, &t2);
            skip(',');
            mystrtoll(&p, &t3);
            skip(',');
            mystrtoll(&p, &t4);
        }
        skipopt(')');
        if (t1 == -1 && t4 == -1) {
            t1 = 0;
            t4 = render_priv->state.event->Duration;
            t3 = t4 - t3;
        }
        if ((render_priv->state.parsed_tags & PARSED_FADE) == 0) {
            render_priv->state.fade =
                interpolate_alpha(render_priv->time -
                        render_priv->state.event->Start, t1, t2,
                        t3, t4, a1, a2, a3);
            render_priv->state.parsed_tags |= PARSED_FADE;
        }
    } else if (mystrcmp(&p, "org")) {
        double v1, v2;
        skip('(');
        mystrtod(&p, &v1);
        skip(',');
        mystrtod(&p, &v2);
        skipopt(')');
        if (!render_priv->state.have_origin) {
            render_priv->state.org_x = v1;
            render_priv->state.org_y = v2;
            render_priv->state.have_origin = 1;
            render_priv->state.detect_collisions = 0;
        }
    } else if (mystrcmp(&p, "t")) {
        double v[3];
        double accel;
        int cnt;
        long long t1, t2, t, delta_t;
        double k;
        skip('(');
        for (cnt = 0; cnt < 3; ++cnt) {
            if (!mystrtod(&p, &v[cnt]))
                break;
            skip(',');
        }
        if (cnt == 3) {
            t1 = v[0];
            t2 = v[1];
            accel = v[2];
        } else if (cnt == 2) {
            t1 = v[0];
            t2 = v[1];
            accel = 1.;
        } else if (cnt == 1) {
            t1 = 0;
            t2 = 0;
            accel = v[0];
        } else {                // cnt == 0
            t1 = 0;
            t2 = 0;
            accel = 1.;
        }
        render_priv->state.detect_collisions = 0;
        if (t2 == 0)
            t2 = render_priv->state.event->Duration;
        delta_t = t2 - t1;
        t = render_priv->time - render_priv->state.event->Start;        // FIXME: move to render_context
        if (t <= t1)
            k = 0.;
        else if (t >= t2)
            k = 1.;
        else {
            assert(delta_t != 0.);
            k = pow(((double) (t - t1)) / delta_t, accel);
        }
        while (*p != ')' && *p != '}' && *p != '\0')
            p = parse_tag(render_priv, p, k);   // maybe k*pwr ? no, specs forbid nested \t's
        skip_to(')');           // in case there is some unknown tag or a comment
        skipopt(')');
    } else if (mystrcmp(&p, "clip")) {
        char *start = p;
        int x0, y0, x1, y1;
        int res = 1;
        skip('(');
        res &= mystrtoi(&p, &x0);
        skipopt(',');
        res &= mystrtoi(&p, &y0);
        skipopt(',');
        res &= mystrtoi(&p, &x1);
        skipopt(',');
        res &= mystrtoi(&p, &y1);
        skipopt(')');
        if (res) {
            render_priv->state.clip_x0 =
                render_priv->state.clip_x0 * (1 - pwr) + x0 * pwr;
            render_priv->state.clip_x1 =
                render_priv->state.clip_x1 * (1 - pwr) + x1 * pwr;
            render_priv->state.clip_y0 =
                render_priv->state.clip_y0 * (1 - pwr) + y0 * pwr;
            render_priv->state.clip_y1 =
                render_priv->state.clip_y1 * (1 - pwr) + y1 * pwr;
        // Might be a vector clip
        } else if (!render_priv->state.clip_drawing) {
            p = parse_vector_clip(render_priv, start);
            render_priv->state.clip_drawing_mode = 0;
        }
    } else if (mystrcmp(&p, "c")) {
        uint32_t val;
        int hex = render_priv->track->track_type == TRACK_TYPE_ASS;
        if (strtocolor(render_priv->library, &p, &val, hex))
            change_color(&render_priv->state.c[0], val, pwr);
        else
            change_color(&render_priv->state.c[0],
                         render_priv->state.style->PrimaryColour, 1);
    } else if ((*p >= '1') && (*p <= '4') && (++p)
               && (mystrcmp(&p, "c") || mystrcmp(&p, "a"))) {
        char n = *(p - 2);
        int cidx = n - '1';
        char cmd = *(p - 1);
        uint32_t val;
        int hex = render_priv->track->track_type == TRACK_TYPE_ASS;
        assert((n >= '1') && (n <= '4'));
        if (!strtocolor(render_priv->library, &p, &val, hex)) {
            switch (n) {
            case '1':
                val = render_priv->state.style->PrimaryColour;
                break;
            case '2':
                val = render_priv->state.style->SecondaryColour;
                break;
            case '3':
                val = render_priv->state.style->OutlineColour;
                break;
            case '4':
                val = render_priv->state.style->BackColour;
                break;
            default:
                val = 0;
                break;          // impossible due to assert; avoid compilation warning
            }
            pwr = 1;
        }
        switch (cmd) {
        case 'c':
            change_color(render_priv->state.c + cidx, val, pwr);
            break;
        case 'a':
            change_alpha(render_priv->state.c + cidx, val >> 24, pwr);
            break;
        default:
            ass_msg(render_priv->library, MSGL_WARN, "Bad command: %c%c",
                    n, cmd);
            break;
        }
    } else if (mystrcmp(&p, "r")) {
        char *start = p;
        char *style;
        skip_to('\\');
        if (p > start) {
            style = malloc(p - start + 1);
            strncpy(style, start, p - start);
            style[p - start] = '\0';
            reset_render_context(render_priv,
                    lookup_style_strict(render_priv->track, style));
            free(style);
        } else
            reset_render_context(render_priv, NULL);
    } else if (mystrcmp(&p, "be")) {
        int val;
        if (mystrtoi(&p, &val)) {
            // Clamp to a safe upper limit, since high values need excessive CPU
            val = (val < 0) ? 0 : val;
            val = (val > MAX_BE) ? MAX_BE : val;
            render_priv->state.be = val;
        } else
            render_priv->state.be = 0;
    } else if (mystrcmp(&p, "b")) {
        int val;
        if (!mystrtoi(&p, &val) || !(val == 0 || val == 1 || val >= 100))
            val = render_priv->state.style->Bold;
        render_priv->state.bold = val;
        update_font(render_priv);
    } else if (mystrcmp(&p, "i")) {
        int val;
        if (!mystrtoi(&p, &val) || !(val == 0 || val == 1))
            val = render_priv->state.style->Italic;
        render_priv->state.italic = val;
        update_font(render_priv);
    } else if (mystrcmp(&p, "kf") || mystrcmp(&p, "K")) {
        double val;
        if (!mystrtod(&p, &val))
            val = 100;
        render_priv->state.effect_type = EF_KARAOKE_KF;
        if (render_priv->state.effect_timing)
            render_priv->state.effect_skip_timing +=
                render_priv->state.effect_timing;
        render_priv->state.effect_timing = val * 10;
    } else if (mystrcmp(&p, "ko")) {
        double val;
        if (!mystrtod(&p, &val))
            val = 100;
        render_priv->state.effect_type = EF_KARAOKE_KO;
        if (render_priv->state.effect_timing)
            render_priv->state.effect_skip_timing +=
                render_priv->state.effect_timing;
        render_priv->state.effect_timing = val * 10;
    } else if (mystrcmp(&p, "k")) {
        double val;
        if (!mystrtod(&p, &val))
            val = 100;
        render_priv->state.effect_type = EF_KARAOKE;
        if (render_priv->state.effect_timing)
            render_priv->state.effect_skip_timing +=
                render_priv->state.effect_timing;
        render_priv->state.effect_timing = val * 10;
    } else if (mystrcmp(&p, "shad")) {
        double val, xval, yval;
        if (mystrtod(&p, &val)) {
            xval = render_priv->state.shadow_x * (1 - pwr) + val * pwr;
            yval = render_priv->state.shadow_y * (1 - pwr) + val * pwr;
            // VSFilter compatibility: clip for \shad but not for \[xy]shad
            xval = (xval < 0) ? 0 : xval;
            yval = (yval < 0) ? 0 : yval;
        } else
            xval = yval = render_priv->state.style->Shadow;
        render_priv->state.shadow_x = xval;
        render_priv->state.shadow_y = yval;
    } else if (mystrcmp(&p, "s")) {
        int val;
        if (!mystrtoi(&p, &val) || !(val == 0 || val == 1))
            val = render_priv->state.style->StrikeOut;
        if (val)
            render_priv->state.flags |= DECO_STRIKETHROUGH;
        else
            render_priv->state.flags &= ~DECO_STRIKETHROUGH;
    } else if (mystrcmp(&p, "u")) {
        int val;
        if (!mystrtoi(&p, &val) || !(val == 0 || val == 1))
            val = render_priv->state.style->Underline;
        if (val)
            render_priv->state.flags |= DECO_UNDERLINE;
        else
            render_priv->state.flags &= ~DECO_UNDERLINE;
    } else if (mystrcmp(&p, "pbo")) {
        double val;
        mystrtod(&p, &val);
        render_priv->state.pbo = val;
    } else if (mystrcmp(&p, "p")) {
        int val;
        mystrtoi(&p, &val);
        val = (val < 0) ? 0 : val;
        render_priv->state.drawing_scale = val;
    } else if (mystrcmp(&p, "q")) {
        int val;
        if (!mystrtoi(&p, &val) || !(val >= 0 && val <= 3))
            val = render_priv->track->WrapStyle;
        render_priv->state.wrap_style = val;
    } else if (mystrcmp(&p, "fe")) {
        int val;
        if (!mystrtoi(&p, &val))
            val = render_priv->state.style->Encoding;
        render_priv->state.font_encoding = val;
    }

    return p;
}

void apply_transition_effects(ASS_Renderer *render_priv, ASS_Event *event)
{
    int v[4];
    int cnt;
    char *p = event->Effect;

    if (!p || !*p)
        return;

    cnt = 0;
    while (cnt < 4 && (p = strchr(p, ';'))) {
        v[cnt++] = atoi(++p);
    }

    if (strncmp(event->Effect, "Banner;", 7) == 0) {
        int delay;
        if (cnt < 1) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        if (cnt >= 2 && v[1] == 0)      // right-to-left
            render_priv->state.scroll_direction = SCROLL_RL;
        else                    // left-to-right
            render_priv->state.scroll_direction = SCROLL_LR;

        delay = v[0];
        if (delay == 0)
            delay = 1;          // ?
        render_priv->state.scroll_shift =
            (render_priv->time - render_priv->state.event->Start) / delay;
        render_priv->state.evt_type = EVENT_HSCROLL;
        return;
    }

    if (strncmp(event->Effect, "Scroll up;", 10) == 0) {
        render_priv->state.scroll_direction = SCROLL_BT;
    } else if (strncmp(event->Effect, "Scroll down;", 12) == 0) {
        render_priv->state.scroll_direction = SCROLL_TB;
    } else {
        ass_msg(render_priv->library, MSGL_DBG2,
                "Unknown transition effect: '%s'", event->Effect);
        return;
    }
    // parse scroll up/down parameters
    {
        int delay;
        int y0, y1;
        if (cnt < 3) {
            ass_msg(render_priv->library, MSGL_V,
                    "Error parsing effect: '%s'", event->Effect);
            return;
        }
        delay = v[2];
        if (delay == 0)
            delay = 1;          // ?
        render_priv->state.scroll_shift =
            (render_priv->time - render_priv->state.event->Start) / delay;
        if (v[0] < v[1]) {
            y0 = v[0];
            y1 = v[1];
        } else {
            y0 = v[1];
            y1 = v[0];
        }
        if (y1 == 0)
            y1 = render_priv->track->PlayResY;  // y0=y1=0 means fullscreen scrolling
        render_priv->state.clip_y0 = y0;
        render_priv->state.clip_y1 = y1;
        render_priv->state.evt_type = EVENT_VSCROLL;
        render_priv->state.detect_collisions = 0;
    }

}

/**
 * \brief determine karaoke effects
 * Karaoke effects cannot be calculated during parse stage (get_next_char()),
 * so they are done in a separate step.
 * Parse stage: when karaoke style override is found, its parameters are stored in the next glyph's
 * (the first glyph of the karaoke word)'s effect_type and effect_timing.
 * This function:
 * 1. sets effect_type for all glyphs in the word (_karaoke_ word)
 * 2. sets effect_timing for all glyphs to x coordinate of the border line between the left and right karaoke parts
 * (left part is filled with PrimaryColour, right one - with SecondaryColour).
 */
void process_karaoke_effects(ASS_Renderer *render_priv)
{
    GlyphInfo *cur, *cur2;
    GlyphInfo *s1, *e1;      // start and end of the current word
    GlyphInfo *s2;           // start of the next word
    int i;
    int timing;                 // current timing
    int tm_start, tm_end;       // timings at start and end of the current word
    int tm_current;
    double dt;
    int x;
    int x_start, x_end;

    tm_current = render_priv->time - render_priv->state.event->Start;
    timing = 0;
    s1 = s2 = 0;
    for (i = 0; i <= render_priv->text_info.length; ++i) {
        cur = render_priv->text_info.glyphs + i;
        if ((i == render_priv->text_info.length)
            || (cur->effect_type != EF_NONE)) {
            s1 = s2;
            s2 = cur;
            if (s1) {
                e1 = s2 - 1;
                tm_start = timing + s1->effect_skip_timing;
                tm_end = tm_start + s1->effect_timing;
                timing = tm_end;
                x_start = 1000000;
                x_end = -1000000;
                for (cur2 = s1; cur2 <= e1; ++cur2) {
                    x_start = FFMIN(x_start, d6_to_int(cur2->bbox.xMin + cur2->pos.x));
                    x_end = FFMAX(x_end, d6_to_int(cur2->bbox.xMax + cur2->pos.x));
                }

                dt = (tm_current - tm_start);
                if ((s1->effect_type == EF_KARAOKE)
                    || (s1->effect_type == EF_KARAOKE_KO)) {
                    if (dt >= 0)
                        x = x_end + 1;
                    else
                        x = x_start;
                } else if (s1->effect_type == EF_KARAOKE_KF) {
                    dt /= (tm_end - tm_start);
                    x = x_start + (x_end - x_start) * dt;
                } else {
                    ass_msg(render_priv->library, MSGL_ERR,
                            "Unknown effect type");
                    continue;
                }

                for (cur2 = s1; cur2 <= e1; ++cur2) {
                    cur2->effect_type = s1->effect_type;
                    cur2->effect_timing = x - d6_to_int(cur2->pos.x);
                }
                s1->effect = 1;
            }
        }
    }
}


/**
 * \brief Get next ucs4 char from string, parsing UTF-8 and escapes
 * \param str string pointer
 * \return ucs4 code of the next char
 * On return str points to the unparsed part of the string
 */
unsigned get_next_char(ASS_Renderer *render_priv, char **str)
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
                              (render_priv->state.wrap_style == 2))) {
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

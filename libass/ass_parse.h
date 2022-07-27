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

#ifndef LIBASS_PARSE_H
#define LIBASS_PARSE_H

#include "ass_render.h"

#define BLUR_MAX_RADIUS 100.0

#define _r(c)   ((c) >> 24)
#define _g(c)   (((c) >> 16) & 0xFF)
#define _b(c)   (((c) >> 8) & 0xFF)
#define _a(c)   ((c) & 0xFF)

void ass_update_font(RenderContext *state);
void ass_apply_transition_effects(RenderContext *state);
void ass_process_karaoke_effects(RenderContext *state);
unsigned ass_get_next_char(RenderContext *state, char **str);
char *ass_parse_tags(RenderContext *state, char *p, char *end, double pwr,
                     bool nested);
int ass_event_has_hard_overrides(char *str);
void ass_apply_fade(uint32_t *clr, int fade);


#endif /* LIBASS_PARSE_H */

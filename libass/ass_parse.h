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

#define BLUR_MAX_RADIUS 100.0

#define _r(c)   ((c) >> 24)
#define _g(c)   (((c) >> 16) & 0xFF)
#define _b(c)   (((c) >> 8) & 0xFF)
#define _a(c)   ((c) & 0xFF)

void update_font(ASS_Renderer *render_priv);
double ensure_font_size(ASS_Renderer *priv, double size);
void apply_transition_effects(ASS_Renderer *render_priv, ASS_Event *event);
void process_karaoke_effects(ASS_Renderer *render_priv);
unsigned get_next_char(ASS_Renderer *render_priv, char **str);
char *parse_tags(ASS_Renderer *render_priv, char *p, char *end, double pwr,
                 bool nested);
int event_has_hard_overrides(char *str);
int event_is_sign(char *str);
extern void change_alpha(uint32_t *var, int32_t new, double pwr);
extern uint32_t mult_alpha(uint32_t a, uint32_t b);


#endif /* LIBASS_PARSE_H */

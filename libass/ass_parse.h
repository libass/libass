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

#include <linebreakdef.h>
#include <linebreak.h>

#define BLUR_MAX_RADIUS 100.0

#define _r(c)   ((c) >> 24)
#define _g(c)   (((c) >> 16) & 0xFF)
#define _b(c)   (((c) >> 8) & 0xFF)
#define _a(c)   ((c) & 0xFF)

#define CJK_START 0x4E00
#define CJK_END   0x9FFF

#define KANA_START 0x3040
#define KANA_END   0x30FF

#define CYRILLIC_START 0x0400
#define CYRILLIC_END   0x04FF

#define HANGUL_SYLLABLES_START  0xAC00
#define HANGUL_SYLLABLES_END    0xD7AF
#define HANGUL_JAMO_START       0x1100
#define HANGUL_JAMO_END         0x11FF
#define HANGUL_COMP_JAMO_START  0x3130
#define HANGUL_COMP_JAMO_END    0x318F
#define HANGUL_EXTENDED_A_START 0xA960
#define HANGUL_EXTENDED_A_END   0xA97F
#define HANGUL_EXTENDED_B_START 0xD7B0
#define HANGUL_EXTENDED_B_END   0xD7FF

void update_font(ASS_Renderer *render_priv);
double ensure_font_size(ASS_Renderer *priv, double size);
void apply_transition_effects(ASS_Renderer *render_priv, ASS_Event *event);
void process_karaoke_effects(ASS_Renderer *render_priv);
unsigned get_next_char(ASS_Renderer *render_priv, char **str);
utf32_t text_info_get_next_char_utf32(TextInfo *text_info, size_t len, size_t *ip);
char *text_info_choose_ub_lang(TextInfo *text_info, char *track_lang);
char *parse_tags(ASS_Renderer *render_priv, char *p, char *end, double pwr,
                 bool nested);
int event_has_hard_overrides(char *str);
extern void change_alpha(uint32_t *var, int32_t new, double pwr);
extern uint32_t mult_alpha(uint32_t a, uint32_t b);


#endif /* LIBASS_PARSE_H */

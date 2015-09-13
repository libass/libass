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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ass.h"
#include "ass_library.h"
#include "ass_utils.h"
#include "ass_string.h"

static void ass_msg_handler(int level, const char *fmt, va_list va, void *data)
{
    if (level > MSGL_INFO)
        return;
    fprintf(stderr, "[ass] ");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
}

ASS_Library *ass_library_init(void)
{
    ASS_Library* lib = calloc(1, sizeof(*lib));
    if (lib)
        lib->msg_callback = ass_msg_handler;
    return lib;
}

void ass_library_done(ASS_Library *priv)
{
    if (priv) {
        ass_set_fonts_dir(priv, NULL);
        ass_set_style_overrides(priv, NULL);
        ass_clear_fonts(priv);
        free(priv);
    }
}

void ass_set_fonts_dir(ASS_Library *priv, const char *fonts_dir)
{
    free(priv->fonts_dir);

    priv->fonts_dir = fonts_dir ? strdup(fonts_dir) : 0;
}

void ass_set_extract_fonts(ASS_Library *priv, int extract)
{
    priv->extract_fonts = !!extract;
}

void ass_set_style_overrides(ASS_Library *priv, char **list)
{
    char **p;
    char **q;
    int cnt;

    if (priv->style_overrides) {
        for (p = priv->style_overrides; *p; ++p)
            free(*p);
    }
    free(priv->style_overrides);
    priv->style_overrides = NULL;

    if (!list)
        return;

    for (p = list, cnt = 0; *p; ++p, ++cnt) {
    }

    priv->style_overrides = calloc(cnt + 1, sizeof(char *));
    if (!priv->style_overrides)
        return;
    for (p = list, q = priv->style_overrides; *p; ++p, ++q)
        *q = strdup(*p);
}

static int grow_array(void **array, int nelem, size_t elsize)
{
    if (!(nelem & 31)) {
        void *ptr = realloc(*array, (nelem + 32) * elsize);
        if (!ptr)
            return 0;
        *array = ptr;
    }
    return 1;
}

void ass_add_font(ASS_Library *priv, char *name, char *data, int size)
{
    int idx = priv->num_fontdata;
    if (!name || !data || !size)
        return;
    if (!grow_array((void **) &priv->fontdata, priv->num_fontdata,
                    sizeof(*priv->fontdata)))
        return;

    priv->fontdata[idx].name = strdup(name);
    priv->fontdata[idx].data = malloc(size);

    if (!priv->fontdata[idx].name || !priv->fontdata[idx].data)
        goto error;

    memcpy(priv->fontdata[idx].data, data, size);

    priv->fontdata[idx].size = size;

    priv->num_fontdata++;
    return;

error:
    free(priv->fontdata[idx].name);
    free(priv->fontdata[idx].data);
}

void ass_clear_fonts(ASS_Library *priv)
{
    int i;
    for (i = 0; i < priv->num_fontdata; ++i) {
        free(priv->fontdata[i].name);
        free(priv->fontdata[i].data);
    }
    free(priv->fontdata);
    priv->fontdata = NULL;
    priv->num_fontdata = 0;
}

/*
 * Register a message callback function with libass.  Without setting one,
 * a default handler is used which prints everything with MSGL_INFO or
 * higher to the standard output.
 *
 * \param msg_cb the callback function
 * \param data additional data that will be passed to the callback
 */
void ass_set_message_cb(ASS_Library *priv,
                        void (*msg_cb)(int, const char *, va_list, void *),
                        void *data)
{
    if (msg_cb) {
        priv->msg_callback = msg_cb;
        priv->msg_callback_data = data;
    }
}

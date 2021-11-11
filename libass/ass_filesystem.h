/*
 * Copyright (C) 2021 libass contributors
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

#include <stdio.h>
#include <stdbool.h>

#ifndef LIBASS_FILESYSTEM_H
#define LIBASS_FILESYSTEM_H

typedef enum {
    FN_EXTERNAL,
    FN_DIR_LIST,
} FileNameSource;

FILE *ass_open_file(const char *filename, FileNameSource hint);

typedef struct {
    void *handle;
    char *path;
    size_t prefix, max_path;
    const char *name;
} ASS_Dir;

bool ass_open_dir(ASS_Dir *dir, const char *path);
const char *ass_read_dir(ASS_Dir *dir);
const char *ass_current_file_path(ASS_Dir *dir);
void ass_close_dir(ASS_Dir *dir);

#endif /* LIBASS_FILESYSTEM_H */

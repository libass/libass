/*
 * Copyright (C) 2017 Vabishchevich Nikolay <vabnick@gmail.com>
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

#include "image.h"
#include "../libass/ass.h"
#include "../libass/ass_filesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif


#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

static void blend_image(Image8 *frame, int32_t x0, int32_t y0,
                        const ASS_Image *img)
{
    int32_t x1 = img->dst_x, x_min = FFMAX(x0, x1);
    int32_t y1 = img->dst_y, y_min = FFMAX(y0, y1);
    x0 = x_min - x0;  x1 = x_min - x1;
    y0 = y_min - y0;  y1 = y_min - y1;

    int32_t w = FFMIN(x0 + frame->width,  x1 + img->w);
    int32_t h = FFMIN(y0 + frame->height, y1 + img->h);
    if (w <= 0 || h <= 0)
        return;

    uint8_t r = img->color >> 24;
    uint8_t g = img->color >> 16;
    uint8_t b = img->color >>  8;
    uint8_t a = img->color >>  0;

    int32_t mul = 129 * (255 - a);
    const int32_t offs = (int32_t) 1 << 22;

    int32_t stride = 4 * frame->width;
    uint8_t *dst = frame->buffer + y0 * stride + 4 * x0;
    const uint8_t *src = img->bitmap + y1 * img->stride + x1;
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            int32_t k = src[x] * mul;
            dst[4 * x + 0] -= ((dst[4 * x + 0] - r) * k + offs) >> 23;
            dst[4 * x + 1] -= ((dst[4 * x + 1] - g) * k + offs) >> 23;
            dst[4 * x + 2] -= ((dst[4 * x + 2] - b) * k + offs) >> 23;
            dst[4 * x + 3] -= ((dst[4 * x + 3] - 0) * k + offs) >> 23;
        }
        dst += stride;
        src += img->stride;
    }
}

static void blend_all(Image8 *frame, int32_t x0, int32_t y0,
                      const ASS_Image *img)
{
    uint8_t *dst = frame->buffer;
    size_t size = (size_t) frame->width * frame->height;
    for (size_t i = 0; i < size; i++) {
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = 255;
        dst += 4;
    }
    for (; img; img = img->next)
        blend_image(frame, x0, y0, img);
}

inline static uint16_t abs_diff(uint16_t a, uint16_t b)
{
    return a > b ? a - b : b - a;
}

inline static uint16_t abs_diff4(const uint16_t a[4], const uint16_t b[4])
{
    uint16_t res = 0;
    for (int k = 0; k < 4; k++) {
        uint16_t diff = abs_diff(a[k], b[k]);
        res = FFMAX(res, diff);
    }
    return res;
}

// Calculate error visibility scale according to formula:
// max_pixel_value / 255 + max(max_side_gradient / 4, max_diagonal_gradient / 8).
static void calc_grad(const Image16 *target, uint16_t *grad)
{
    const int base = 257;
    const int border = base + 65535 / 4;

    int32_t w = target->width;
    int32_t h = target->height;
    int32_t stride = 4 * target->width;

    for (int32_t x = 0; x < w; x++)
        *grad++ = border;
    const uint16_t *tg = target->buffer + stride + 4;
    for (int32_t y = 1; y < h - 1; y++) {
        *grad++ = border;
        for (int32_t x = 1; x < w - 1; x++) {
            uint16_t g[8];
            g[0] = abs_diff4(tg, tg - 4) / 4;
            g[1] = abs_diff4(tg, tg + 4) / 4;
            g[2] = abs_diff4(tg, tg - stride) / 4;
            g[3] = abs_diff4(tg, tg + stride) / 4;
            g[4] = abs_diff4(tg, tg - stride - 4) / 8;
            g[5] = abs_diff4(tg, tg - stride + 4) / 8;
            g[6] = abs_diff4(tg, tg + stride - 4) / 8;
            g[7] = abs_diff4(tg, tg + stride + 4) / 8;
            uint16_t gg = g[0];
            for (int k = 1; k < 8; k++)
                gg = FFMAX(gg, g[k]);
            *grad++ = base + gg;
            tg += 4;
        }
        *grad++ = border;
        tg += 8;
    }
    for (int32_t x = 0; x < w; x++)
        *grad++ = border;
}

static int compare1(const Image16 *target, const uint16_t *grad,
                    const ASS_Image *img, const char *path, double *result)
{
    Image8 frame;
    frame.width  = target->width;
    frame.height = target->height;
    size_t size = (size_t) frame.width * frame.height;
    frame.buffer = malloc(4 * size);
    if (!frame.buffer)
        return 0;

    blend_all(&frame, 0, 0, img);

    double max_err = 0;
    const uint8_t *ptr = frame.buffer;
    const uint16_t *tg = target->buffer;
    for (size_t i = 0; i < size; i++) {
        uint16_t cmp[4];
        for (int k = 0; k < 4; k++)
            cmp[k] = 257u * ptr[k];
        double err = (double) abs_diff4(cmp, tg) / *grad++;
        if (max_err < err)
            max_err = err;
        ptr += 4;
        tg += 4;
    }
    int flag = path && !write_png8(path, &frame) ? -1 : 1;
    free(frame.buffer);
    *result = max_err;
    return flag;
}

static int compare(const Image16 *target, const uint16_t *grad,
                   const ASS_Image *img, const char *path,
                   double *result, int scale_x, int scale_y)
{
    if (scale_x == 1 && scale_y == 1)
        return compare1(target, grad, img, path, result);
    int scale_area = scale_x * scale_y;

    Image16 frame;
    frame.width  = target->width;
    frame.height = target->height;
    size_t size = (size_t) frame.width * frame.height;
    frame.buffer = malloc(8 * size);
    if (!frame.buffer)
        return 0;

    Image8 temp;
    temp.width  = scale_x * target->width;
    temp.height = scale_y * target->height;
    temp.buffer = malloc(4 * scale_area * size);
    if (!temp.buffer) {
        free(frame.buffer);
        return 0;
    }
    blend_all(&temp, 0, 0, img);

    uint16_t *dst = frame.buffer;
    const uint8_t *src = temp.buffer;
    int32_t stride = 4 * temp.width;
    const uint32_t offs = ((uint32_t) 1 << 19) - 1;
    uint32_t mul = ((uint32_t) 257 << 20) / scale_area;
    for (int32_t y = 0; y < frame.height; y++) {
        for (int32_t x = 0; x < frame.width; x++) {
            uint16_t res[4] = {0};
            const uint8_t *ptr = src;
            for (int i = 0; i < scale_y; i++) {
                for (int j = 0; j < scale_x; j++)
                    for (int k = 0; k < 4; k++)
                        res[k] += ptr[4 * j + k];
                ptr += stride;
            }
            for (int k = 0; k < 4; k++)
                // equivalent to (257 * res[k] + (scale_area - 1) / 2) / scale_area;
                *dst++ = (res[k] * (uint64_t) mul + offs) >> 20;
            src += 4 * scale_x;
        }
        src += (scale_y - 1) * stride;
    }

    free(temp.buffer);

    double max_err = 0;
    const uint16_t *ptr = frame.buffer;
    const uint16_t *tg = target->buffer;
    for (size_t i = 0; i < size; i++) {
        double err = (double) abs_diff4(ptr, tg) / *grad++;
        if (max_err < err)
            max_err = err;
        ptr += 4;
        tg += 4;
    }
    int flag = path && !write_png16(path, &frame) ? -1 : 1;
    free(frame.buffer);
    *result = max_err;
    return flag;
}


static bool load_font(ASS_Library *lib, const char *dir, const char *file)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    if (fseek(fp, 0, SEEK_END) == -1) {
        fclose(fp);
        return false;
    }

    long size = ftell(fp);
    if (size <= 0 || size > (1l << 30)) {
        fclose(fp);
        return false;
    }
    rewind(fp);

    char *buf = malloc(size);
    if (!buf) {
        fclose(fp);
        return false;
    }

    long pos = 0;
    while (pos < size) {
        size_t n = fread(buf + pos, 1, size - pos, fp);
        if (!n) {
            free(buf);
            fclose(fp);
            return false;
        }
        pos += n;
    }
    fclose(fp);

    printf("Loading font '%s'.\n", file);
    ass_add_font(lib, file, buf, size);
    free(buf);
    return true;
}

static ASS_Track *load_track(ASS_Library *lib,
                             const char *dir, const char *file)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    ASS_Track *track = ass_read_file(lib, path, NULL);
    if (!track) {
        printf("Cannot load subtitle file '%s'!\n", file);
        return NULL;
    }
    printf("Processing '%s':\n", file);
    return track;
}

static bool out_of_memory(void)
{
    printf("Not enough memory!\n");
    return false;
}

typedef enum {
    R_SAME, R_GOOD, R_BAD, R_FAIL, R_ERROR
} Result;

static const char *result_text[R_ERROR] = {
    "SAME", "GOOD", "BAD", "FAIL"
};

Result classify_result(double error)
{
    if (error == 0)
        return R_SAME;
    else if (error < 2)
        return R_GOOD;
    else if (error < 4)
        return R_BAD;
    else
        return R_FAIL;
}

static Result process_image(ASS_Renderer *renderer, ASS_Track *track,
                            const char *input, const char *output,
                            const char *file, int64_t time,
                            int scale_x, int scale_y)
{
    uint64_t tm = time;
    unsigned msec = tm % 1000;  tm /= 1000;
    unsigned sec  = tm %   60;  tm /=   60;
    unsigned min  = tm %   60;  tm /=   60;
    printf("  Time %u:%02u:%02u.%03u - ", (unsigned) tm, min, sec, msec);

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", input, file);

    Image16 target;
    if (!read_png(path, &target)) {
        printf("PNG reading failed!\n");
        return R_ERROR;
    }

    uint16_t *grad = malloc(2 * target.width * target.height);
    if (!grad) {
        free(target.buffer);
        out_of_memory();
        return R_ERROR;
    }
    calc_grad(&target, grad);

    ass_set_storage_size(renderer, target.width, target.height);
    ass_set_frame_size(renderer, scale_x * target.width, scale_y * target.height);
    ASS_Image *img = ass_render_frame(renderer, track, time, NULL);

    const char *out_file = NULL;
    if (output) {
        snprintf(path, sizeof(path), "%s/%s", output, file);
        out_file = path;
    }
    double max_err;
    int res = compare(&target, grad, img, out_file, &max_err, scale_x, scale_y);
    free(target.buffer);
    free(grad);
    if (!res) {
        out_of_memory();
        return R_ERROR;
    }
    Result flag = classify_result(max_err);
    printf("%.3f %s\n", max_err, result_text[flag]);
    if (res < 0)
        printf("Cannot write PNG to file '%s'!\n", path);
    return flag;
}


typedef struct {
    char *name;
    size_t prefix;
    const char *dir;
    int64_t time;
} Item;

typedef struct {
    size_t n_items, max_items;
    Item *items;
} ItemList;

static bool add_item(ItemList *list)
{
    if (list->n_items < list->max_items)
        return true;

    size_t n = list->max_items ? 2 * list->max_items : 256;
    Item *next = realloc(list->items, n * sizeof(Item));
    if (!next)
        return out_of_memory();
    list->max_items = n;
    list->items = next;
    return true;
}

static void delete_items(ItemList *list)
{
    for (size_t i = 0; i < list->n_items; i++)
        free(list->items[i].name);
    free(list->items);
}

static int item_compare(const void *ptr1, const void *ptr2)
{
    const Item *e1 = ptr1, *e2 = ptr2;

    int cmp_len = 0;
    size_t len = e1->prefix;
    if (len > e2->prefix) {
        cmp_len = +1;
        len = e2->prefix;
    } else if (len < e2->prefix) {
        cmp_len = -1;
    }
    int cmp = memcmp(e1->name, e2->name, len);
    if (cmp)
        return cmp;
    if (cmp_len)
        return cmp_len;
    if (e1->time > e2->time)
        return +1;
    if (e1->time < e2->time)
        return -1;
    return 0;
}


static bool add_sub_item(ItemList *list, const char *dir, const char *file, size_t len)
{
    if (!add_item(list))
        return false;

    Item *item = &list->items[list->n_items];
    item->name = strdup(file);
    if (!item->name)
        return out_of_memory();
    item->prefix = len;
    item->dir = dir;
    item->time = -1;
    list->n_items++;
    return true;
}

static bool add_img_item(ItemList *list, const char *dir, const char *file, size_t len)
{
    // Parse image name:
    // <subtitle_name>-<time_in_msec>.png

    size_t pos = len, first = len;
    while (true) {
        if (!pos--)
            return true;
        if (file[pos] == '-')
            break;
        if (file[pos] < '0' || file[pos] > '9')
            return true;
        if (file[pos] != '0')
            first = pos;
    }
    if (pos + 1 == len || first + 15 < len)
        return true;

    if (!add_item(list))
        return false;

    Item *item = &list->items[list->n_items];
    item->name = strdup(file);
    if (!item->name)
        return out_of_memory();
    item->prefix = pos;
    item->dir = dir;
    item->time = 0;
    for (size_t i = first; i < len; i++)
        item->time = 10 * item->time + (file[i] - '0');
    list->n_items++;
    return true;
}

static bool process_input(ItemList *list, const char *path, ASS_Library *lib)
{
    ASS_Dir dir;
    if (!ass_open_dir(&dir, path)) {
        printf("Cannot open input directory '%s'!\n", path);
        return false;
    }

    const char *name;
    while ((name = ass_read_dir(&dir))) {
        if (name[0] == '.')
            continue;
        const char *ext = strrchr(name + 1, '.');
        if (!ext)
            continue;

        char ext_lc[5];
        size_t pos = 0;
        while (pos < sizeof(ext_lc) - 1) {
            char c = ext[pos + 1];
            if (!c)
                break;
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            ext_lc[pos] = c;
            pos++;
        }
        ext_lc[pos] = '\0';

        if (!strcmp(ext_lc, "png")) {
            if (add_img_item(list, path, name, ext - name))
                continue;
        } else if (!strcmp(ext_lc, "ass")) {
            if (add_sub_item(list, path, name, ext - name))
                continue;
        } else if (!strcmp(ext_lc, "ttf") ||
                   !strcmp(ext_lc, "otf") ||
                   !strcmp(ext_lc, "pfb")) {
            if (load_font(lib, path, name))
                continue;
            printf("Cannot load font '%s'!\n", name);
        } else {
            continue;
        }
        ass_close_dir(&dir);
        return false;
    }
    ass_close_dir(&dir);
    return true;
}


enum {
    OUTPUT, SCALE, LEVEL, INPUT
};

static int *parse_cmdline(int argc, char *argv[])
{
    int *pos = calloc(INPUT + argc, sizeof(int));
    if (!pos) {
        out_of_memory();
        return NULL;
    }
    int input = INPUT;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            pos[input++] = i;
            continue;
        }
        int index;
        switch (argv[i][1]) {
        case 'i':  index = input++;  break;
        case 'o':  index = OUTPUT;   break;
        case 's':  index = SCALE;    break;
        case 'p':  index = LEVEL;    break;
        default:   goto fail;
        }
        if (argv[i][2] || ++i >= argc || pos[index])
            goto fail;
        pos[index] = i;
    }
    if (pos[INPUT])
        return pos;

fail:
    free(pos);
    const char *fmt =
        "Usage: %s ([-i] <input-dir>)+ [-o <output-dir>] [-s <scale:1-8>[x<scale:1-8>]] [-p <pass-level:0-3>]\n"
        "\n"
        "Scale can be a single uniform scaling factor or a pair of independent horizontal and vertical factors. -s N is equivalent to -s NxN.\n";
    printf(fmt, argv[0] ? argv[0] : "compare");
    return NULL;
}

static bool parse_scale(const char *arg,  int *scale_x, int *scale_y)
{
    if (arg[0] < '1' || arg[0] > '8')
        return false;
    *scale_x = *scale_y = arg[0] - '0';
    if (!arg[1])
        return true;
    if (arg[1] != 'x' || arg[2] < '1' || arg[2] > '8' || arg[3])
        return false;
    *scale_y = arg[2] - '0';
    return true;
}

void msg_callback(int level, const char *fmt, va_list va, void *data)
{
    if (level > 3)
        return;
    fprintf(stderr, "libass: ");
    vfprintf(stderr, fmt, va);
    fprintf(stderr, "\n");
}

int main(int argc, char *argv[])
{
    int *pos = parse_cmdline(argc, argv);
    if (!pos)
        return R_ERROR;

    ASS_Library *lib = NULL;
    ItemList list = {0};
    int result = R_ERROR;

    int scale_x = 1, scale_y = 1;
    if (pos[SCALE] && !parse_scale(argv[pos[SCALE]], &scale_x, &scale_y)) {
        printf("Invalid scale value, should be 1-8[x1-8]!\n");
        goto end;
    }

    int level = R_BAD;
    if (pos[LEVEL]) {
        const char *arg = argv[pos[LEVEL]];
        if (arg[0] < '0' || arg[0] > '3' || arg[1]) {
            printf("Invalid pass level value, should be 0-3!\n");
            goto end;
        }
        level = arg[0] - '0';
    }

    const char *output = NULL;
    if (pos[OUTPUT]) {
        output = argv[pos[OUTPUT]];
        struct stat st;
        if (stat(output, &st)) {
            if (mkdir(output, 0755)) {
                printf("Cannot create output directory '%s'!\n", output);
                goto end;
            }
        } else if (!(st.st_mode & S_IFDIR)) {
            printf("Invalid output directory '%s'!\n", output);
            goto end;
        }
    }

    lib = ass_library_init();
    if (!lib) {
        printf("ass_library_init failed!\n");
        goto end;
    }
    ass_set_message_cb(lib, msg_callback, NULL);
    ass_set_extract_fonts(lib, true);

    for (int *input = pos + INPUT; *input; input++) {
        if (!process_input(&list, argv[*input], lib))
            goto end;
    }

    ASS_Renderer *renderer = ass_renderer_init(lib);
    if (!renderer) {
        printf("ass_renderer_init failed!\n");
        goto end;
    }
    ass_set_fonts(renderer, NULL, NULL, ASS_FONTPROVIDER_NONE, NULL, 0);

    result = 0;
    size_t prefix = 0;
    const char *prev = "";
    ASS_Track *track = NULL;
    unsigned total = 0, good = 0;
    if (list.n_items)
        qsort(list.items, list.n_items, sizeof(Item), item_compare);
    for (size_t i = 0; i < list.n_items; i++) {
        char *name = list.items[i].name;
        size_t len = list.items[i].prefix;
        if (prefix != len || memcmp(prev, name, len)) {
            if (track) {
                ass_free_track(track);
                track = NULL;
            }
            prev = name;
            prefix = len;
            if (list.items[i].time >= 0) {
                printf("Missing subtitle file '%.*s.ass'!\n", (int) len, name);
                total++;
            } else if (i + 1 < list.n_items && list.items[i + 1].time >= 0)
                track = load_track(lib, list.items[i].dir, prev);
            continue;
        }
        if (list.items[i].time < 0) {
            printf("Multiple subtitle files '%.*s.ass'!\n", (int) len, name);
            continue;
        }
        total++;
        if (!track)
            continue;
        Result res = process_image(renderer, track, list.items[i].dir, output,
                                   name, list.items[i].time,
                                   scale_x, scale_y);
        result = FFMAX(result, res);
        if (res <= level)
            good++;
    }
    if (track)
        ass_free_track(track);
    ass_renderer_done(renderer);

    if (!total) {
        printf("No images found!\n");
        result = R_ERROR;
    } else if (good < total) {
        printf("Only %u of %u images have passed test (%s or better)\n",
               good, total, result_text[level]);
    } else {
        printf("All %u images have passed test (%s or better)\n",
               total, result_text[level]);
        result = 0;
    }

end:
    delete_items(&list);
    if (lib)
        ass_library_done(lib);
    free(pos);
    return result;
}

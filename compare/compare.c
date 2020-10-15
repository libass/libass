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
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>


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
                   double *result, int scale)
{
    if (scale == 1)
        return compare1(target, grad, img, path, result);
    int scale2 = scale * scale;

    Image16 frame;
    frame.width  = target->width;
    frame.height = target->height;
    size_t size = (size_t) frame.width * frame.height;
    frame.buffer = malloc(8 * size);
    if (!frame.buffer)
        return 0;

    Image8 temp;
    temp.width  = scale * target->width;
    temp.height = scale * target->height;
    temp.buffer = malloc(4 * scale2 * size);
    if (!temp.buffer) {
        free(frame.buffer);
        return 0;
    }
    blend_all(&temp, 0, 0, img);

    uint16_t *dst = frame.buffer;
    const uint8_t *src = temp.buffer;
    int32_t stride = 4 * temp.width;
    const uint32_t offs = ((uint32_t) 1 << 18) - 1;
    uint32_t mul = ((uint32_t) 257 << 19) / scale2;
    for (int32_t y = 0; y < frame.height; y++) {
        for (int32_t x = 0; x < frame.width; x++) {
            uint16_t res[4] = {0};
            const uint8_t *ptr = src;
            for (int i = 0; i < scale; i++) {
                for (int j = 0; j < scale; j++)
                    for (int k = 0; k < 4; k++)
                        res[k] += ptr[4 * j + k];
                ptr += stride;
            }
            for (int k = 0; k < 4; k++)
                // equivalent to (257 * res[k] + (scale2 - 1) / 2) / scale2;
                *dst++ = (res[k] * (uint64_t) mul + offs) >> 19;
            src += 4 * scale;
        }
        src += (scale - 1) * stride;
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
    ass_add_font(lib, (char *) file, buf, size);
    free(buf);
    return true;
}

static ASS_Track *load_track(ASS_Library *lib,
                             const char *dir, const char *file)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.ass", dir, file);
    ASS_Track *track = ass_read_file(lib, path, NULL);
    if (!track) {
        printf("Cannot load subtitle file '%s.ass'!\n", file);
        return NULL;
    }
    printf("Processing '%s.ass':\n", file);
    return track;
}

static bool out_of_memory()
{
    printf("Not enough memory!\n");
    return false;
}

static bool process_image(ASS_Renderer *renderer, ASS_Track *track,
                          const char *input, const char *output,
                          const char *file, int64_t time, int scale)
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
        return false;
    }

    uint16_t *grad = malloc(2 * target.width * target.height);
    if (!grad) {
        free(target.buffer);
        return out_of_memory();
    }
    calc_grad(&target, grad);

    ass_set_storage_size(renderer, target.width, target.height);
    ass_set_frame_size(renderer, scale * target.width, scale * target.height);
    ASS_Image *img = ass_render_frame(renderer, track, time, NULL);

    const char *out_file = NULL;
    if (output) {
        snprintf(path, sizeof(path), "%s/%s", output, file);
        out_file = path;
    }
    double max_err;
    int res = compare(&target, grad, img, out_file, &max_err, scale);
    free(target.buffer);
    free(grad);
    if (!res)
        return out_of_memory();
    bool flag = max_err < 4;
    printf("%.3f %s\n", max_err, flag ? (max_err < 2 ? "OK" : "BAD") : "FAIL");
    if (res < 0)
        printf("Cannot write PNG to file '%s'!\n", path);
    return flag;
}


typedef struct {
    char *name;
    int64_t time;
} Item;

typedef struct {
    size_t n_items, max_items;
    Item *items;
} ItemList;

static bool init_items(ItemList *list)
{
    int n = 256;
    list->n_items = list->max_items = 0;
    list->items = malloc(n * sizeof(Item));
    if (!list->items)
        return out_of_memory();
    list->max_items = n;
    return true;
}

static bool add_item(ItemList *list)
{
    if (list->n_items < list->max_items)
        return true;

    int n = 2 * list->max_items;
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
    int cmp = strcmp(e1->name, e2->name);
    if (cmp)
        return cmp;
    if (e1->time > e2->time)
        return +1;
    if (e1->time < e2->time)
        return -1;
    return 0;
}


static bool add_sub_item(ItemList *list, const char *file, int len)
{
    if (!add_item(list))
        return false;

    Item *item = &list->items[list->n_items];
    item->name = strndup(file, len);
    if (!item->name)
        return out_of_memory();
    item->time = -1;
    list->n_items++;
    return true;
}

static bool add_img_item(ItemList *list, const char *file, int len)
{
    // Parse image name:
    // <subtitle_name>-<time_in_msec>.png

    int pos = len, first = len;
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
    item->name[pos] = '\0';
    item->time = 0;
    for (int i = first; i < len; i++)
        item->time = 10 * item->time + (file[i] - '0');
    list->n_items++;
    return true;
}


static int print_usage(const char *program)
{
    const char *fmt =
        "Usage: %s [-i] <input-dir> [-o <output-dir>] [-s <scale:1-8>]\n";
    printf(fmt, program);
    return 1;
}

void msg_callback(int level, const char *fmt, va_list va, void *data)
{
    if (level > 3)
        return;
    printf("libass: ");
    vprintf(fmt, va);
    printf("\n");
}

int main(int argc, char *argv[])
{
    enum {
        INPUT, OUTPUT, SCALE
    };
    int pos[3] = {0};
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (pos[INPUT])
                return print_usage(argv[0]);
            pos[INPUT] = i;
            continue;
        }
        int index;
        switch (argv[i][1]) {
        case 'i':  index = INPUT;   break;
        case 'o':  index = OUTPUT;  break;
        case 's':  index = SCALE;   break;
        default:  return print_usage(argv[0]);
        }
        if (argv[i][2] || ++i >= argc || pos[index])
            return print_usage(argv[0]);
        pos[index] = i;
    }
    if (!pos[INPUT])
        return print_usage(argv[0]);

    int scale = 1;
    if (pos[SCALE]) {
        const char *arg = argv[pos[SCALE]];
        if (arg[0] < '1' || arg[0] > '8' || arg[1]) {
            printf("Invalid scale value, should be 1-8!\n");
            return 1;
        }
        scale = arg[0] - '0';
    }

    const char *input = argv[pos[INPUT]];
    DIR *dir = opendir(input);
    if (!dir) {
        printf("Cannot open input directory '%s'!\n", input);
        return 1;
    }

    const char *output = NULL;
    if (pos[OUTPUT]) {
        output = argv[pos[OUTPUT]];
        struct stat st;
        if (stat(output, &st)) {
            if (mkdir(output, 0755)) {
                printf("Cannot create output directory '%s'!\n", output);
                closedir(dir);
                return 1;
            }
        } else if (!(st.st_mode & S_IFDIR)) {
            printf("Invalid output directory '%s'!\n", output);
            closedir(dir);
            return 1;
        }
    }

    ASS_Library *lib = ass_library_init();
    if (!lib) {
        printf("ass_library_init failed!\n");
        closedir(dir);
        return 1;
    }
    ass_set_message_cb(lib, msg_callback, NULL);

    ItemList list;
    if (!init_items(&list)) {
        ass_library_done(lib);
        closedir(dir);
        return 1;
    }

    while (true) {
        struct dirent *file = readdir(dir);
        if (!file)
            break;
        const char *name = file->d_name;
        if (name[0] == '.')
            continue;
        const char *ext = strrchr(name + 1, '.');
        if (!ext)
            continue;

        if (!strcmp(ext, ".png")) {
            if (add_img_item(&list, name, ext - name))
                continue;
        } else if (!strcmp(ext, ".ass")) {
            if (add_sub_item(&list, name, ext - name))
                continue;
        } else if (!strcmp(ext, ".ttf") || !strcmp(ext, ".otf") || !strcmp(ext, ".pfb")) {
            if (load_font(lib, input, name))
                continue;
            printf("Cannot load font '%s'!\n", name);
        } else {
            continue;
        }
        delete_items(&list);
        ass_library_done(lib);
        closedir(dir);
        return 1;
    }
    closedir(dir);

    ASS_Renderer *renderer = ass_renderer_init(lib);
    if (!renderer) {
        printf("ass_renderer_init failed!\n");
        delete_items(&list);
        ass_library_done(lib);
        return 1;
    }
    ass_set_fonts(renderer, NULL, NULL, ASS_FONTPROVIDER_NONE, NULL, 0);

    int prefix;
    const char *prev = "";
    ASS_Track *track = NULL;
    unsigned total = 0, good = 0;
    qsort(list.items, list.n_items, sizeof(Item), item_compare);
    for (size_t i = 0; i < list.n_items; i++) {
        if (strcmp(prev, list.items[i].name)) {
            if (track)
                ass_free_track(track);
            prev = list.items[i].name;
            prefix = strlen(prev);
            if (list.items[i].time < 0)
                track = load_track(lib, input, prev);
            else {
                printf("Missing subtitle file '%s.ass'!\n", prev);
                track = NULL;
                total++;
            }
            continue;
        }

        total++;
        if (!track)
            continue;
        char *name = list.items[i].name;
        name[prefix] = '-';  // restore initial filename
        if (process_image(renderer, track, input, output,
                          name, list.items[i].time, scale))
            good++;
    }
    if (track)
        ass_free_track(track);
    delete_items(&list);
    ass_renderer_done(renderer);
    ass_library_done(lib);

    if (good < total) {
        printf("Only %u of %u images have passed test\n", good, total);
        return 1;
    }
    printf("All %u images have passed test\n", total);
    return 0;
}

#ifndef __MPUTILS_H__
#define __MPUTILS_H__

#define MSGL_FATAL 0
#define MSGL_ERR 1
#define MSGL_WARN 2
#define MSGL_INFO 4
#define MSGL_V 6
#define MSGL_DBG2 7

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))

#include "help_mp.h"

unsigned ass_utf8_get_char(char **str);
void ass_msg(int lvl, char *fmt, ...);
void ass_gauss_blur(unsigned char *buffer, unsigned short *tmp2,
                    int width, int height, int stride, int *m2,
                    int r, int mwidth);
void *ass_guess_buffer_cp(unsigned char *buffer, int buflen,
                          char *preferred_language, char *fallback);

#endif

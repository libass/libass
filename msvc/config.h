#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#define _USE_MATH_DEFINES
#include <math.h>

#define inline __inline

#define CONFIG_DIRECTWRITE	1
#define CONFIG_RASTERIZER	1
#define CONFIG_ASM			1
#define CONFIG_HARFBUZZ     1
#define CONFIG_WINUTF16     1

#ifdef _WIN64
#define __x86_64__
#else
#define __i386__
#endif

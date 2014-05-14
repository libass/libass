cmake_minimum_required(VERSION 2.8)

project(libass)

if (NOT WIN32 OR (MINGW OR MSYS OR CYGWIN))
  message(FATAL_ERROR
    "This CMake project is intended only for native WIN32 compilation.  "
    "For any other platforms please use the autotools ./configure script.")
endif (NOT WIN32 OR (MINGW OR MSYS OR CYGWIN))

set (THIRD_PARTY_LIBS)

set(FREETYPE_DIR "$ENV{FREETYPE_DIR}"
  CACHE PATH "root path for freetype lib/ and include/ folders"
  )
find_path(FREETYPE_INCLUDE_DIR
  ft2build.h freetype2/freetype/freetype.h
  PATHS ${FREETYPE_DIR}/include
  )
find_library(FREETYPE_LIBRARY
  freetype libfreetype
  PATHS ${FREETYPE_DIR}/lib
  DOC "freetype library"
  )
if (FREETYPE_INCLUDE_DIR)
  include_directories(AFTER ${FREETYPE_INCLUDE_DIR} ${FREETYPE_INCLUDE_DIR}/freetype2)
endif (FREETYPE_INCLUDE_DIR)

if (FREETYPE_INCLUDE_DIR AND FREETYPE_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${FREETYPE_LIBRARY})
  set (FOUND_FREETYPE 1)
endif (FREETYPE_INCLUDE_DIR AND FREETYPE_LIBRARY)


set(HARFBUZZ_DIR "$ENV{HARFBUZZ_DIR}"
  CACHE PATH "root path for harfbuzz lib/ and include/ folders"
  )
find_path(HARFBUZZ_INCLUDE_DIR
  harfbuzz/hb.h
  PATHS ${HARFBUZZ_DIR}/include
  )
find_library(HARFBUZZ_LIBRARY
  harfbuzz libharfbuzz
  PATHS ${HARFBUZZ_DIR}/lib
  DOC "harfbuzz-ng library"
  )
if (HARFBUZZ_INCLUDE_DIR)
  include_directories(AFTER ${HARFBUZZ_INCLUDE_DIR} ${HARFBUZZ_INCLUDE_DIR}/harfbuzz)
endif (HARFBUZZ_INCLUDE_DIR)

if (HARFBUZZ_INCLUDE_DIR AND HARFBUZZ_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${HARFBUZZ_LIBRARY})
  set (FOUND_HARFBUZZ 1)
endif (HARFBUZZ_INCLUDE_DIR AND HARFBUZZ_LIBRARY)


set(FRIBIDI_DIR "$ENV{FRIBIDI_DIR}"
  CACHE PATH "root path for fribidi lib/ and include/ folders"
  )
find_path(FRIBIDI_INCLUDE_DIR
  fribidi/fribidi.h
  PATHS ${FRIBIDI_DIR}/include
  )
find_library(FRIBIDI_LIBRARY
  fribidi libfribidi
  PATHS ${FRIBIDI_DIR}/lib
  DOC "fribidi library"
  )
if (FRIBIDI_INCLUDE_DIR)
  include_directories(AFTER ${FRIBIDI_INCLUDE_DIR} ${FRIBIDI_INCLUDE_DIR}/fribidi)
endif (FRIBIDI_INCLUDE_DIR)

if (FRIBIDI_INCLUDE_DIR AND FRIBIDI_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${FRIBIDI_LIBRARY})
  set (FOUND_FRIBIDI 1)
endif (FRIBIDI_INCLUDE_DIR AND FRIBIDI_LIBRARY)


set(FONTCONFIG_DIR "$ENV{FONTCONFIG_DIR}"
  CACHE PATH "root path for fontconfig lib/ and include/ folders"
  )
find_path(FONTCONFIG_INCLUDE_DIR
  fontconfig/fontconfig.h
  PATHS ${FONTCONFIG_DIR}/include
  )
find_library(FONTCONFIG_LIBRARY
  fontconfig libfontconfig
  PATHS ${FONTCONFIG_DIR}/lib
  DOC "fontconfig library"
  )
if (FONTCONFIG_INCLUDE_DIR)
  include_directories(AFTER ${FONTCONFIG_INCLUDE_DIR})
endif (FONTCONFIG_INCLUDE_DIR)

if (FONTCONFIG_INCLUDE_DIR AND FONTCONFIG_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${FONTCONFIG_LIBRARY})
  set (FOUND_FONTCONFIG 1)
endif (FONTCONFIG_INCLUDE_DIR AND FONTCONFIG_LIBRARY)


set(ENCA_DIR "$ENV{ENCA_DIR}"
  CACHE PATH "root path for enca lib/ and include/ folders"
  )
find_path(ENCA_INCLUDE_DIR
  enca.h
  PATHS ${ENCA_DIR}/include
  )
find_library(ENCA_LIBRARY
  enca libenca
  PATHS ${ENCA_DIR}/lib
  DOC "enca library"
  )
if (ENCA_INCLUDE_DIR)
  include_directories(AFTER ${ENCA_INCLUDE_DIR})
endif (ENCA_INCLUDE_DIR)

if (ENCA_INCLUDE_DIR AND ENCA_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${ENCA_LIBRARY})
  set (FOUND_ENCA 1)
endif (ENCA_INCLUDE_DIR AND ENCA_LIBRARY)


set(ICONV_DIR "$ENV{ICONV_DIR}"
  CACHE PATH "root path for iconv lib/ and include/ folders"
  )
find_path(ICONV_INCLUDE_DIR
  iconv.h
  PATHS ${ICONV_DIR}/include
  )
find_library(ICONV_LIBRARY
  iconv libiconv
  PATHS ${ICONV_DIR}/lib
  DOC "iconv library"
  )
if (ICONV_INCLUDE_DIR)
  include_directories(AFTER ${ICONV_INCLUDE_DIR})
endif (ICONV_INCLUDE_DIR)

if (ICONV_INCLUDE_DIR AND ICONV_LIBRARY)
  set (THIRD_PARTY_LIBS ${THIRD_PARTY_LIBS} ${ICONV_LIBRARY})
  set (FOUND_ICONV 1)
endif (ICONV_INCLUDE_DIR AND ICONV_LIBRARY)


set(project_sources
  ${CONFIG_H}
  strings.c

  ../libass/ass.c
  ../libass/ass_cache.c
  ../libass/ass_font.c
  ../libass/ass_fontconfig.c
  ../libass/ass_render.c
  ../libass/ass_utils.c
  ../libass/ass_bitmap.c
  ../libass/ass_library.c
  ../libass/ass_drawing.c
  ../libass/ass_parse.c
  ../libass/ass_render_api.c
  ../libass/ass_shaper.c
  ../libass/ass_strtod.c
  ../libass/ass_rasterizer.c
  ../libass/ass_rasterizer_c.c
  )

set(FOUND_ASSEMBLER 0)
#enable_language(ASM-NASM)
#set(project_sources
#  ${project_sources}
#
#  ../libass/x86/blend_bitmaps.asm
#  ../libass/x86/cpuid.asm
#  ../libass/x86/rasterizer.asm
#  )

set (CONFIG_H_IN "${PROJECT_SOURCE_DIR}/../msvcport/config.h.in")
set (CONFIG_H "${PROJECT_BINARY_DIR}/config.h")
set_source_files_properties("${CONFIG_H}" PROPERTIES GENERATED true)
configure_file("${CONFIG_H_IN}" "${CONFIG_H}.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${CONFIG_H}.tmp"
  "${CONFIG_H}")
file(REMOVE "${CONFIG_H}.tmp")
#install(FILES "${CONFIG_H}" DESTINATION lib/pkgconfig)

add_definitions(-DHAVE_CONFIG_H)
add_definitions(-Dinline=__inline)
add_definitions(-D_USE_MATH_DEFINES -DNOMINMAX)

include_directories(AFTER
  ${PROJECT_BINARY_DIR}
  ${PROJECT_SOURCE_DIR}/..
  ${PROJECT_SOURCE_DIR}/../libass
  ${PROJECT_SOURCE_DIR}/../msvcport
  )

add_library(libass STATIC ${project_sources})
target_link_libraries(libass ${THIRD_PARTY_LIBS})

install(TARGETS libass DESTINATION lib)
install(FILES
  ../libass/ass.h
  ../libass/ass_types.h

  DESTINATION
  include/ass)

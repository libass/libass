#!/usr/bin/env python3

from pathlib import Path
from urllib.parse import urlparse
from urllib.request import urlretrieve


LICENSE_HEADER = """\
/*
 * Copyright (C) 2026
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

// WARNING - THIS FILE IS AUTO-GENERATED. DO NOT EDIT IT MANUALLY.
// Regenerate with: python3 gen_arabic_charmap.py
"""


def render_function(file_content: list[str], function_name: str, unicode_mapping_file: Path):
    file_content.append(f"uint32_t {function_name}(uint32_t symbol)")
    file_content.append("{")
    file_content.append("    switch (symbol) {")

    with open(unicode_mapping_file, encoding="utf-8") as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith("#"):
                continue
            pua_code, unicode, _ = line.split("\t")
            file_content.append(f"        case {unicode}:")
            file_content.append(f"            return {pua_code};")

    file_content.append("    }")
    file_content.append("")
    file_content.append("    return symbol;")
    file_content.append("}")
    file_content.append("")


def render_header(function_names: list[str]) -> list[str]:
    file_content = []
    guard = "LIBASS_ASS_ARABIC_CHARMAP_H"

    file_content.append(LICENSE_HEADER)
    file_content.append(f"#ifndef {guard}")
    file_content.append(f"#define {guard}")
    file_content.append("")
    file_content.append("#include <stdint.h>")
    file_content.append("")
    for name in function_names:
        file_content.append(f"uint32_t {name}(uint32_t symbol);")
    file_content.append("")
    file_content.append(f"#endif /* {guard} */")
    file_content.append("")
    return file_content


def main():
    HARFBUZZ_BASE = "https://raw.githubusercontent.com/harfbuzz/harfbuzz/refs/heads/main/src"
    SOURCES = [
        {
            "url": f"{HARFBUZZ_BASE}/ArabicPUASimplified.txt",
            "function": "ass_font_charmap_arabic_simplified",
        },
        {
            "url": f"{HARFBUZZ_BASE}/ArabicPUATraditional.txt",
            "function": "ass_font_charmap_arabic_traditional",
        },
    ]

    c_file_content = []
    c_file_content.append(LICENSE_HEADER)
    c_file_content.append("#include \"ass_arabic_charmap.h\"")
    c_file_content.append("")

    for source in SOURCES:
        unicode_file_mapping_path = Path(Path(urlparse(source["url"]).path).name)
        urlretrieve(source["url"], unicode_file_mapping_path)
        render_function(c_file_content, source["function"], unicode_file_mapping_path)
        unicode_file_mapping_path.unlink()

    c_file_path = Path(__file__).parent.joinpath("libass", "ass_arabic_charmap.c")
    c_file_path.write_text("\n".join(c_file_content), encoding="utf-8")

    h_file_content = render_header([source["function"] for source in SOURCES])
    h_file_path = Path(__file__).parent.joinpath("libass", "ass_arabic_charmap.h")
    h_file_path.write_text("\n".join(h_file_content), encoding="utf-8")


if __name__ == "__main__":
    main()

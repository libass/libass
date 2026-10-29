#!/usr/bin/env python3

from abc import ABC, abstractmethod
from pathlib import Path
from typing import Self
import textwrap


class HashFunc(ABC):
    @abstractmethod
    def __call__(self, value: int) -> int: ...
    @abstractmethod
    def text(self) -> str: ...

class Identity(HashFunc):
    def __call__(self : Self, value : int) -> int:
        return value
    def text(self : Self) -> str:
        return f"symbol"

class Shift(HashFunc):
    shift : int
    def __init__(self : Self, shift : int):
        self.shift = shift
    def __call__(self : Self, value : int) -> int:
        return value >> self.shift
    def text(self : Self) -> str:
        return f"symbol >> {self.shift}"

class XorShift(HashFunc):
    shift : int
    def __init__(self : Self, shift : int):
        self.shift = shift
    def __call__(self : Self, value : int) -> int:
        return value ^ (value >> self.shift)
    def text(self : Self) -> str:
        return f"symbol ^ symbol >> {self.shift}"

def test_hash(table : list[list[int]], func : HashFunc, n : int) -> bool:
    mask = (1 << n) - 1
    check = [False] * (1 << n)
    for code, _ in table:
        index = func(code) & mask
        if check[index]:
            return False
        check[index] = True
    return True

def hash_functions():
    yield Identity()
    for shift in range(1, 16):
        yield Shift(shift)
    for shift in range(1, 16):
        yield XorShift(shift)

def find_hash(table : list[list[int]]) -> tuple[HashFunc, int]:
    m = len(table).bit_length()
    for n in range(m, m + 3):
        for func in hash_functions():
            if test_hash(table, func, n):
                return func, n
    raise Exception("Cannot find suitable hash function!")


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
    table = []
    res_min = 1 << 32
    with open(unicode_mapping_file, encoding="utf-8") as f:
        lines = f.readlines()
        for line in lines:
            if line.startswith("#"):
                continue
            pua_code_str, unicode_str, _ = line.split("\t")
            unicode = int(unicode_str, 0)
            pua_code = int(pua_code_str, 0)
            table.extend([[unicode, pua_code]])
            res_min = min(res_min, pua_code)

    func, n = find_hash(table)

    n1 = n - 1
    mask = (1 << n) - 1
    checks = [1] * (1 << n1) + [0] * (1 << n1)
    results = [0] * (1 << n)
    for unicode, pua_code in table:
        hash_val = func(unicode)
        index = hash_val & mask
        checks[index] = hash_val >> n1
        results[index] = pua_code - res_min
        if checks[index] > 255:
            raise Exception("uint8_t isn't enough for check table!")
        if results[index] > 255:
            raise Exception("uint8_t isn't enough for result table!")

    file_content.append(f"static const uint8_t {function_name}_check[{1 << n}] = {{")
    for line in textwrap.wrap(", ".join(map(str, checks)), width = 100):
        file_content.append(f"    {line}")
    file_content.append("};")
    file_content.append("")

    file_content.append(f"static const uint8_t {function_name}_result[{1 << n}] = {{")
    for line in textwrap.wrap(", ".join(map(str, results)), width = 100):
        file_content.append(f"    {line}")
    file_content.append("};")
    file_content.append("")

    expr = func.text()
    file_content.append(f"uint32_t {function_name}(uint32_t symbol)")
    file_content.append("{")
    file_content.append(f"    unsigned hash = {expr}, index = hash & {mask}, test = hash >> {n1};")
    file_content.append(f"    return test == {function_name}_check[index] ?")
    file_content.append(f"      {res_min} + {function_name}_result[index] : symbol;")
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
    SOURCES = [
        {
            "file": Path(__file__).parent.joinpath("ArabicPUASimplified.txt"),
            "function": "ass_font_charmap_arabic_simplified",
        },
        {
            "file": Path(__file__).parent.joinpath("ArabicPUATraditional.txt"),
            "function": "ass_font_charmap_arabic_traditional",
        },
        {
            "file": Path(__file__).parent.joinpath("HebrewPUA.txt"),
            "function": "ass_font_charmap_hebrew",
        },
    ]

    c_file_content = []
    c_file_content.append(LICENSE_HEADER)
    c_file_content.append("#include \"ass_arabic_charmap.h\"")
    c_file_content.append("")

    for source in SOURCES:
        render_function(c_file_content, source["function"], source["file"])

    c_file_path = Path(__file__).parent.parent.joinpath("libass", "ass_arabic_charmap.c")
    c_file_path.write_text("\n".join(c_file_content), encoding="utf-8")

    h_file_content = render_header([source["function"] for source in SOURCES])
    h_file_path = Path(__file__).parent.parent.joinpath("libass", "ass_arabic_charmap.h")
    h_file_path.write_text("\n".join(h_file_content), encoding="utf-8")


if __name__ == "__main__":
    main()

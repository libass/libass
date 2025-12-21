#!/usr/bin/env python3

import sys

# GNU-style version script, for bfd, lld, mold and others
def gen_mapfile(symfile_path: str, mapfile_path: str):
    with open(symfile_path) as symfile:
        lines = symfile.readlines()
        output = '{\n  global:'
        for line in lines:
            output += f'\n    {line.strip()};'
        output += '\n  local:\n    *;\n};\n'
        with open(mapfile_path, 'w') as mapfile:
            mapfile.writelines(output)

# Exported symbols file, for Apple linker
def gen_expsym(symfile_path: str, expsymfile_path: str):
    with open(symfile_path) as symfile:
        lines = symfile.readlines()
        output = ''
        for line in lines:
            output += f'_{line.strip()}\n'
        with open(expsymfile_path, 'w') as expsymfile:
            expsymfile.writelines(output)

# DEF file, for Microsoft link.exe
def gen_deffile(symfile_path: str, deffile_path: str):
    with open(symfile_path) as symfile:
        lines = symfile.readlines()
        lines.insert(0, 'EXPORTS\n')
        with open(deffile_path, 'w') as deffile:
            deffile.writelines(lines)

if sys.argv[1] == '--mapfile':
    gen_mapfile(sys.argv[2], sys.argv[3])
elif sys.argv[1] == '--expsym':
    gen_expsym(sys.argv[2], sys.argv[3])
else:
    gen_deffile(sys.argv[1], sys.argv[2])

#!/usr/bin/env python3

import sys

symfile_path = sys.argv[1]
deffile_path = sys.argv[2]

with open(symfile_path) as symfile:
    lines = symfile.readlines()
    lines.insert(0, 'EXPORTS\n')
    with open(deffile_path, 'w') as deffile:
        deffile.writelines(lines)

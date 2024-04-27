#!/usr/bin/env python3

import sys
import re

symfile_path = sys.argv[1]
deffile_path = sys.argv[2]

with open(deffile_path, 'w') as deffile:
    deffile.write("EXPORTS\n")
    for line in open(symfile_path, 'r'):
        a = re.search('(ass_\w+)', line)
        if a is not None:
            deffile.write(a.group(1) + "\n")

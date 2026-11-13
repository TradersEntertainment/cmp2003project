# Usage: splice.py template.cpp region.hpp out.cpp
# region.hpp holds everything that goes between "// BEGIN STUDENT IMPLEMENTATION" and "IndexPtr make_index"
# (it must open "namespace findfast {" itself, exactly like the template does).
import sys
t = open(sys.argv[1]).read(); r = open(sys.argv[2]).read()
B, M = '// BEGIN STUDENT IMPLEMENTATION', 'IndexPtr make_index(string_view name) {'
b = t.index(B) + len(B); m = t.index(M)
if not r.startswith('\n'): r = '\n' + r
if not r.endswith('\n\n'): r = r.rstrip('\n') + '\n\n'
open(sys.argv[3], 'w').write(t[:b] + r + t[m:])

# Verifies a candidate keeps everything outside the student region byte-identical
# to the template, and flags banned constructs inside the region.
import sys, re
tpl = open(sys.argv[1]).read(); cand = open(sys.argv[2]).read()
B, E, M = '// BEGIN STUDENT IMPLEMENTATION', '// END STUDENT IMPLEMENTATION', 'IndexPtr make_index(string_view name) {'
ok = True
def split(t):
    b = t.index(B) + len(B); m = t.index(M); return t[:b], t[b:m], t[m:]
tp, _, tt = split(tpl); cp, region, ct = split(cand)
if tp != cp: print('RULE: text before BEGIN marker changed'); ok = False
if tt != ct: print('RULE: make_index / text after region changed'); ok = False
code = re.sub(r'//[^\n]*', '', region); code = re.sub(r'/\*.*?\*/', '', code, flags=re.S)
code = re.sub(r'"(\\.|[^"\\])*"', '""', code)
banned = [r'\bvector\b', r'\bmap\b', r'\bset\b', r'\bunordered_\w+', r'\blist\s*<', r'\bdeque\b', r'\bstack\b', r'\bqueue\b',
          r'\barray\s*<', r'\bpair\b', r'\btuple\b', r'\bstring\b(?!_view)', r'\bsort\b', r'\bfind\s*\(\s*\w+\.begin', r'\blower_bound\b', r'\bupper_bound\b',
          r'\bbinary_search\b', r'\bstd::', r'(?<!::)\bnew\b(?!\s*\()', r'\bdelete\b(?!\s*;)', r'\bmalloc\b', r'\bcalloc\b', r'\brealloc\b', r'\bfree\s*\(',
          r'\bthread\b', r'\bmutex\b', r'\bcout\b', r'\bcerr\b', r'\bcin\b', r'\bprintf\b', r'\bfopen\b', r'\bmemcpy\b', r'\bmemcmp\b', r'\bmemset\b', r'\bmemmove\b',
          r'\bhash\s*<', r'\bshared_ptr\b', r'\bunique_ptr\b', r'\bbitset\b', r'\bspan\b', r'\bvalarray\b', r'\boptional\b', r'\bvariant\b', r'\bany\b',
          r'\bmemory::live\b', r'\bmemory::peak\b', r'\bHeader\b', r'\bchrono\b', r'\bgetenv\b', r'\bsystem\s*\(', r'\bmutable\b', r'\bconst_cast\b', r'\brand\b', r'\bmt19937\b',
          r'\bmin\s*\(', r'\bmax\s*\(', r'\bswap\s*\(', r'\bfill\s*\(', r'(?<!::)\bcopy\s*\(', r'(?<!::)\bequal\s*\(', r'\bmismatch\s*\(', r'\bsearch\s*\(', r'\bcount\s*\(']
for pat in banned:
    for m in re.finditer(pat, code):
        line = code[:m.start()].count('\n') + 1
        print(f'WARN region line {line}: matches {pat!r}: {code.splitlines()[line-1].strip()[:100]}'); ok = False
for m in re.finditer(r'\bstatic\b[^;{(]*[;=]', code):
    s = m.group(0)
    if 'constexpr' not in s: print('WARN static data (possible cache):', s.strip()[:80]); ok = False
print('RULES OK' if ok else 'RULES: see above')

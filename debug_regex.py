import re
s = '    /* ---- foo ---- */'
a = re.sub(r'/\*\s*-+\s*', '/* ', s)
print(repr(a))
b = re.sub(r'\s*-+\s*\*/', ' */', a)
print(repr(b))

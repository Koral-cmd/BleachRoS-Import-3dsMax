# Каждый TR("...") в исходниках должен иметь перевод в src/lang_en.inc,
# и в английском тексте не должно быть кириллицы.
import glob, re, sys
inc = open('src/lang_en.inc', encoding='utf-8').read()
pairs = dict(re.findall(r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)" \}', inc))
miss = []
for p in sorted(glob.glob('src/*.cpp')):
    for m in re.finditer(r'TR\("((?:[^"\\]|\\.)*)"\)', open(p, encoding='utf-8').read()):
        if m.group(1) not in pairs: miss.append((p, m.group(1)))
cyr = [e for e in pairs.values() if re.search('[Ѐ-ӿ]', e)]
for p, s in miss: print('  нет перевода:', p, s)
for e in cyr: print('  кириллица в переводе:', e)
if miss or cyr: sys.exit(1)
print('  перевод: %d строк, все TR() переведены' % len(pairs))

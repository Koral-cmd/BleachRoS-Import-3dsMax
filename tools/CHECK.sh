#!/bin/sh
# ============================================================
#  Проверка без Windows и без Max:
#   1. код плагина против заглушки SDK для всех MAX_RELEASE
#   2. окна (Win32) и .rc против настоящих заголовков Windows (zig, если есть)
#   3. сборка bleachdump и сравнение с эталоном на синтетических файлах
#      (с AddressSanitizer + UBSan)
# ============================================================
cd "$(dirname "$0")/.." || exit 1
FAIL=0
for R in 18000 19000 20000 21000 22000 23000 24000 25000 26000 27000 28000 29000; do
  Y=$((2016 + (R - 18000) / 1000))
  if g++ -std=c++17 -Wall -Wextra -Wshadow -Werror=shadow -D_WIN32 -DMAX_RELEASE=$R \
       -Isrc -Ivendor/miniz -Itools/sdkstub -include tools/sdkstub/win_shim.h \
       -fsyntax-only src/bleach_max.cpp src/bleach_dll.cpp 2>/tmp/stub.txt; then
    printf "  Max %d  ok\n" "$Y"
  else
    printf "  Max %d  ОШИБКА\n" "$Y"; head -5 /tmp/stub.txt; FAIL=1
  fi
done
[ "$FAIL" = 0 ] || exit 1
python3 tools/langcheck.py || exit 1

ZIG=""
if python3 -m ziglang version >/dev/null 2>&1; then ZIG="python3 -m ziglang"; fi
if command -v zig >/dev/null 2>&1; then ZIG="zig"; fi
if [ -n "$ZIG" ]; then
  $ZIG c++ -target x86_64-windows-gnu -std=c++17 -Wall -Wextra -Wshadow -DUNICODE -D_UNICODE \
       -Isrc -Ivendor/miniz -c src/bleach_ui.cpp -o /tmp/bleach_ui.obj || exit 1
  $ZIG c++ -target x86_64-windows-gnu -std=c++17 -Wall -Wextra -Isrc -Ivendor/miniz -c src/bleach_core.cpp -o /tmp/bleach_core.obj || exit 1
  $ZIG c++ -target x86_64-windows-gnu -std=c++17 -Wall -Wextra -Isrc -c src/lang.cpp -o /tmp/lang.obj || exit 1
  (cd src && $ZIG rc /i . -- bleach.rc /tmp/bleach.res) || exit 1
  echo "  окна и ресурсы: ok (настоящие заголовки Windows)"
else
  echo "  zig не найден - проверка окон пропущена (pip install ziglang)"
fi

B=/tmp/bleach_check; mkdir -p $B
gcc -O1 -w -Ivendor/miniz -c vendor/miniz/miniz_tinfl.c -o $B/tinfl.o || exit 1
g++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wshadow \
    -Isrc -Ivendor/miniz src/tam_formats.cpp src/tam_assemble.cpp src/tam_anim.cpp src/bleach_core.cpp src/lang.cpp \
    tools/bleachdump.cpp $B/tinfl.o -o $B/bleachdump || exit 1
python3 tests/make_samples.py $B/samples >/dev/null || exit 1
S=$B/samples
{
  $B/bleachdump $S/pl999_cos00_00.tmd2 $S/pl999_hair00_00.tmd2 $S/pl999_wep00_00.tmd2
  $B/bleachdump $S/pl998_cos00_00.tmd2 $S/pl999_wep00_00.tmd2
  $B/bleachdump $S/pl999_cos00_00.tmd2 $S/pl999_face00_00.tmd2 $S/pl999_hair01_00.tmd2
  $B/bleachdump $S/pl996_cos00_00.tmd2 $S/pl996_wep00_00.tmd2 | grep -v "^  bone\|layout\|mat #\|model "
  $B/bleachdump $S/pl999_cos00_00.lds
  $B/bleachdump $S/pl999.tactpkg idle_delta $S/pl999_cos00_00.tmd2
  $B/bleachdump $S/pl999.tactpkg pose_abs $S/pl999_cos00_00.tmd2
  $B/bleachdump $S/pl999.tactpkg
  $B/bleachdump $S/garganta.cat
  $B/bleachdump $S/single.tmo
  $B/bleachdump $S/unknown.bin
  $B/bleachdump $S/pl997.tactpkg
  $B/bleachdump $S/garganta.cat raw.tmo $S/pl999_cos00_00.tmd2 | tail -4
  $B/bleachdump $S/unknown.bin unknown#1 $S/pl999_cos00_00.tmd2 | tail -4
  $B/bleachdump --find $S $S/pl999_cos00_00.tmd2
  $B/bleachdump $S/garganta.cat "*" $S/pl999_cos00_00.tmd2 | grep "MERGED\|кадров\|F0"
  cp $S/unknown.bin $S/pl999_fx.bin; $B/bleachdump --find $S $S/pl999_cos00_00.tmd2 | grep "pl999_fx\|FIND"; rm -f $S/pl999_fx.bin
} > $B/out.txt 2>&1 || { cat $B/out.txt; exit 1; }
# английский режим: в выводе не должно остаться кириллицы
BLEACH_LANG=en $B/bleachdump $S/pl999.tactpkg idle_delta $S/pl999_cos00_00.tmd2 $S/pl999_face00_00.tmd2 > $B/en.txt 2>&1
if grep -q '[А-Яа-я]' $B/en.txt; then echo "  КИРИЛЛИЦА В АНГЛИЙСКОМ ВЫВОДЕ:"; grep '[А-Яа-я]' $B/en.txt | head; exit 1; fi
echo "  английский вывод: ok"
if [ "$1" = "--update" ]; then cp $B/out.txt tests/expected.txt; echo "  эталон обновлён"; exit 0; fi
if cmp -s $B/out.txt tests/expected.txt; then echo "  читатели: вывод совпал с эталоном ($(wc -l < tests/expected.txt) строк)"
else echo "  ВЫВОД ОТЛИЧАЕТСЯ:"; diff tests/expected.txt $B/out.txt | head -20; exit 1; fi
echo "Всё в порядке."

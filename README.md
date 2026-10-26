# BLEACH RoS Import — импорт BLEACH Rebirth of Souls в 3ds Max

Плагин для **3ds Max 2016–2027** (C++, `.dlu`), который читает файлы игры
**BLEACH Rebirth of Souls** (Tamsoft, TamEngine) напрямую из папки игры:

- **модель**: меш, скелет в bind-позе, Skin с весами (до 8 влияний на вершину,
  модели с >256 костями через таблицы индексов), UV, цвет вершин;
- **текстуры** сразу при импорте: `.lds` рядом с `.tmd2` распаковывается в DDS и
  подключается к Physical Material (у волос — цвет из параметров шейдера);
- **части персонажа** (`cos` тело, `face`, `hair`, `wep` оружие) собираются на
  **один скелет** — общие кости сливаются по хешу имени;
- **оружие**: если у него свой скелет без общих костей, оно прицепляется к кости
  оружия/руки (авто или выбранная кость);
- **LOD**: те же модели из `01MIDDLE` / `02LOW` — колонка «LOD» показывает
  треугольники каждого уровня (`H 12.3k / M 4.1k`; `(=)` — геометрия одинаковая,
  отличаются только текстуры, это не LOD). Галочка «Импортировать LOD» кладёт
  настоящие LOD на тот же скелет в отдельные скрытые слои, одинаковые пропускает;
- **анимации** из `.tactpkg` (клипы `tmo1`) на импортированный скелет:
  один клип или несколько подряд на таймлайн, **линейные контроллеры**,
  note track + `NeoDexSequenceData` (последовательности для экспортёров).

> Статус 1.1.1: код читателей проверен на синтетических файлах того же формата
> (и сверён с открытым Blender‑аддоном BlenderTMD2). На **настоящих файлах игры
> ещё не прогонялся** — если что-то не так, пришли `Downloads\Import bleach.log`
> и/или вывод `bleachdump` (см. ниже).

## Установка

### Готовая сборка (Visual Studio не нужна)
Плагин собирается автоматически на GitHub (Max 2025/2026/2027) — скачай из
[Releases](https://github.com/Koral-cmd/BleachRoS-Import-3dsMax/releases/latest):
- **`BleachRoSImport-<версия>-Setup.exe`** — закрой 3ds Max и запусти. Ставит пакет в
  `C:\ProgramData\Autodesk\ApplicationPlugins\BleachRoSImport` и Visual C++ Runtime.
  Новая версия ставится **поверх старой** (тот же AppId), старые `.dlu` удаляются.
- **`BleachRoSImport-<версия>.zip`** — распакуй и запусти `INSTALL.bat` (сам попросит
  права администратора; без них поставит в `%AppData%`). Повторный запуск новой версии = обновление.

После запуска Max появится меню **Bleach Tool**.

### Сборка у себя (если нужно)
Нужно: Visual Studio 2022/2026 с C++ и CMake, 3ds Max SDK в
`C:\Program Files\Autodesk\3ds Max <год> SDK\maxsdk`.

```
BUILD.bat          собрать для всех найденных SDK и сразу установить
BUILD.bat 2027     только для Max 2027
INSTALL.bat        переустановить уже собранное (output\<год>\)
UNINSTALL.bat      удалить
installer\BUILD_RELEASE.bat   ZIP + Setup.exe (Inno Setup 6) + SHA256SUMS
```

## Использование

**Bleach Tool → Import BLEACH RoS**
1. «Найти в Steam» или «Обзор…» — папка `BLEACH Rebirth of Souls`.
2. Слева персонаж (`pl000  Ichigo Kurosaki  [LOD x3]`), справа его части.
   По умолчанию отмечены `*00_00` (cos/face/hair/wep). Колонка **LOD** — `H M L`.
3. Параметры: текстуры, цвет вершин, сварка швов, Skin, альфа → прозрачность,
   вид костей (Bone/Point), единицы игры (авто м/см), масштаб сцены,
   кость для оружия (`auto`, `none` или имя кости), LOD.
4. **Импорт**. Итог — в окне и в `Downloads\Import bleach.log`.

**Bleach Tool → BLEACH Animations** (окно не модальное — таймлайн работает)
1. Скелет из сцены (берётся и из выделения), пакет `.tactpkg` персонажа
   находится сам (`Motion\pl000*.tactpkg`), можно открыть любой файл.
2. Загрузка — клипы **добавляются после уже загруженных** (через «Паузу» кадров);
   первый клип начинается после кадра 0 (кадр 0 = bind-поза):
   - **Добавить выбранные** / двойной щелчок по клипу;
   - **Все из пакета (фильтр)** — все клипы открытого пакета (или по поиску);
   - **ВСЕ анимации персонажа** — все клипы из всех пакетов персонажа за раз;
   - «Сначала удалить загруженные» — заменить, а не дописать.
   Клип, который не подходит к скелету (кости не совпали), отбрасывается — сцена
   не меняется; повторы по имени пропускаются. Контроллеры — **линейные**.
3. **Загруженные клипы = Sequences WhiteoutDex**: «Проиграть» (или двойной щелчок)
   ставит диапазон на клип, «Весь таймлайн», «Удалить клип» — удаляет только его
   ключи (остальные клипы остаются на местах).
   **Имя** — переименовать в стандартное имя Warcraft 3 (`Stand`, `Walk`,
   `Attack - 2`, `Death`...; Attack/Spell/Death/Birth/Decay сразу отмечаются
   NonLooping).
4. **Режим** значений клипа: `Авто` (по первым ключам), `Дельта от bind`,
   `Абсолютные`; **Эйлер** — порядок поворотов (XYZ). Если анимация «ломает»
   скелет — попробуй другой режим/порядок и пришли лог.
5. «Bind-поза (удалить все)» — снять всю анимацию и последовательности.

### В Warcraft 3 через WhiteoutDex
Каждый клип сразу пишется туда, где его ищут Sequence Manager и MDX-экспортёр
[WhiteoutDex](https://github.com/FernandoS27/WhiteoutDex):
- WhiteoutDex установлен → его блок `WhiteoutDexSequenceData` на `rootNode`
  (у оставшихся клипов сохраняются Rarity / MoveSpeed / Extents);
- нет → note track на `rootNode` в формате WC3 (`"Stand"`, `"Death"\r\nNonLooping`),
  WhiteoutDex переносит его сам при открытии Sequence Manager.

Что поменяешь в Sequence Manager (имя, кадры, NonLooping, удаление), плагин
читает обратно. Копия списка лежит на note track корневого helper'а скелета.
Веса Skin могут быть до 8 на вершину — экспортёр WhiteoutDex сам оставляет 4
самых сильных.

**MAXScript**
```maxscript
BleachCpp.showDialog()
BleachCpp.showAnimDialog()
BleachCpp.version()
BleachCpp.lodInfo @"D:\...\00HIGH\Model\chara\pl000_cos00_00.tmd2"  -- "LOD есть, уровней 3: 00HIGH 01MIDDLE 02LOW"
```

## Диагностика без Max: `bleachdump`
```
bleachdump pl000_cos00_00.tmd2 pl000_face00_00.tmd2      модели, кости, меши
bleachdump pl000.tactpkg                                  список клипов
bleachdump pl000.tactpkg <клип> pl000_cos00_00.tmd2       совпадение костей, режим
bleachdump pl000_cos00_00.lds                             текстуры (формат, альфа)
```
Собирается вместе с проектом (`-DBLEACH_BUILD_TOOL=ON`) или на Linux:
`sh tools/CHECK.sh` (там же проверка кода плагина против заглушки SDK и окон
против настоящих заголовков Windows).

## Форматы
Коротко — в [docs/FORMATS.md](docs/FORMATS.md).

## Благодарности
- **Al-Hydra / HydraKame** — [BlenderTMD2](https://github.com/Al-Hydra/BlenderTMD2):
  разбор `.tmd2`, `.lds`, `.tactpkg`, `tmo1` (код здесь написан заново).
- **Durik256, DKDave** и тема BLEACH Rebirth of Souls на ResHax — PZZE, LDS, веса.
- **DennisHerrm** — [SWBF2-Import-3dsMax](https://github.com/DennisHerrm/SWBF2-Import-3dsMax):
  схема сборки/установки пакета, правила работы с SDK, основа заглушки SDK.
- [miniz](https://github.com/richgel999/miniz) (MIT) — распаковка zlib.

Неофициальный фан-проект, не связан с Bandai Namco, Tamsoft, Tite Kubo/Shueisha
или Autodesk. Данных игры в репозитории нет — плагин читает твою установку.

Лицензия: GPL-3.0 + [исключение для Max SDK](LICENSE-EXCEPTION.md).

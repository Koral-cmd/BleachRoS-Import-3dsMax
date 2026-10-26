# BLEACH RoS Import — BLEACH Rebirth of Souls importer for 3ds Max

[Русский README](README.md)

A C++ plugin (`.dlu`) for 3ds Max that reads **BLEACH Rebirth of Souls** files
(Tamsoft / TamEngine) directly from the game folder.

- **Models** (`.tmd2`): mesh, skeleton in bind pose, Skin weights, UVs, vertex colors.
  Character parts (body / face / hair / weapon) are assembled on one skeleton.
- **Textures** on import: `.lds` is unpacked to DDS and wired into Physical Materials.
- **LOD**: shows which models have real LOD levels (`00HIGH` / `01MIDDLE` / `02LOW`)
  and can import them on hidden layers.
- **Animations** from `.tactpkg`, `.tmo2`, `.cat` onto the imported skeleton with
  **linear controllers**. Load one clip, a selection, all clips of a pack, or all
  animations of a character. Clips that don't fit the skeleton are rejected.
  Object animations made of several parts (e.g. `adv_object\garganta_*.tmo2`)
  are loaded at the same time as one sequence.
- **Search by bones** through the whole game folder for animations that fit
  the selected skeleton.
- **WhiteoutDex**: loaded clips appear as Sequences (Sequence Manager / MDX export
  for Warcraft III).
- **UI language**: English / Russian (selector in the import window; "Auto" follows Windows).
- Not supported yet: effects (particles, glow, material/UV animation).

## Install
Download from [Releases](https://github.com/Koral-cmd/BleachRoS-Import-3dsMax/releases/latest)
(prebuilt for 3ds Max 2025 / 2026 / 2027):
- `BleachRoSImport-<version>-Setup.exe` — close 3ds Max and run it, or
- `BleachRoSImport-<version>.zip` — unzip and run `INSTALL.bat`.

A new version installs over the old one. After starting Max, use the **Bleach Tool** menu.

## Usage
1. **Bleach Tool → Import BLEACH RoS**: set the game folder (or *Find in Steam*),
   pick a character, tick parts, *Import*.
2. **Bleach Tool → BLEACH Animations**: pick the skeleton and the pack, load clips.
   *Find by bones* searches all game files for matching animations.

If something goes wrong, post `Downloads\Import bleach.log`.

## Credits
- Al-Hydra / HydraKame — [BlenderTMD2](https://github.com/Al-Hydra/BlenderTMD2)
- Durik256, DKDave and the ResHax BLEACH Rebirth of Souls thread — PZZE, LDS, weights
- DennisHerrm — [SWBF2-Import-3dsMax](https://github.com/DennisHerrm/SWBF2-Import-3dsMax)
- [miniz](https://github.com/richgel999/miniz) (MIT)

Fan project, not affiliated with Bandai Namco, Tamsoft, Tite Kubo/Shueisha or Autodesk.
No game data included. License: GPL-3.0 + [Max SDK exception](LICENSE-EXCEPTION.md).

# Quick Audio Sliders

SKSE plugin for Skyrim SE/AE that puts every sound category flagged
"Should appear on menu" as a volume slider into an
[SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
section, grouped by the plugin that defines each category, so the volume can be
changed in-game without Escape → Settings → Audio.

- Changes apply live through `BSISoundCategory::SetCategoryVolume`, the same
  path the Vanilla slider uses. No INI reload tricks.
- An "All Sounds (global)" slider on top scales every root sound category (menu-visible or not); saved in the plugin INI and applied on startup. The Vanilla master is left to the Vanilla menu.
- Mods contributing a single slider are pooled into a collapsed "Other Mods" group with a mod-name prefix.
- "Reset" per slider and a two-click "Reset All" button restore the SNCT default menu values (UNAM) and global to 100%.
- Groups are flat, always-visible section headings (Vanilla first, "Other Mods" last).
- Values are persisted to `SkyrimPrefs.ini` `[AudioMenu]`
  (`fVal<n>` / `uID<n>` per category slot, keyed by
  the category's runtime FormID like the Vanilla menu does), so they survive
  restarts and stay consistent with the Vanilla menu.
- The engine only keeps eight `[AudioMenu]` slots (`uID0..uID7`). Categories
  beyond that are saved by the plugin in
  `Data/SKSE/Plugins/QuickAudioSliders.ini` (`[Volumes]`, key
  `<plugin file>|<local FormID>`, e.g. `Audio Overhaul Skyrim.esp|00AA6E=0.5000`)
  and re-applied at `kDataLoaded`. Load-order changes do not affect the key.
  Under MO2 the file is created in `overwrite/SKSE/Plugins/`.
- Labels come from the game's own Scaleform translation table, so `$Effects`
  shows as "Effects" / "Effekte" in the player's language.

## Requirements

- Skyrim SE 1.5.97 or AE 1.6.x (Address Library build, one DLL)
- SKSE64, Address Library for SKSE Plugins
- SKSE Menu Framework 3.x (soft dependency: without it the plugin loads but
  shows nothing)

## Layout

- `src/` — plugin source (`main.cpp` lifecycle, `AudioSliders.*` audio +
  persistence, `MenuIntegration.*` ImGui page, `Translation.*` label lookup)
- `extern/commonlibsse-ng/` — CommonLibSSE-NG submodule (alandtse/CommonLibVR `ng`)
- `external/skse-menu-framework/` — SKSE Menu Framework consumer header
- `tools/build.ps1` — configure + build (see `AGENTS.md` "Build")
- `build/` — generated output (gitignored)
- `docs/` — design notes

## Build

```powershell
.\tools\build.ps1
```

Output: `build\release\QuickAudioSliders.dll`. Install as
`SKSE\Plugins\QuickAudioSliders.dll` in a mod folder.

Logs: `My Games\Skyrim Special Edition\SKSE\QuickAudioSliders.log`.

## Localization

UI strings are English by default and loaded from
`Data/Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt` (SKSE
translation format, UTF-16 LE, `$Key<TAB>Text`). English and German files are
in `config/Interface/Translations/`. Category names come from the game's own
translation table and follow the game language.

## Settings

All options live in `Data/SKSE/Plugins/QuickAudioSliders.ini` (under MO2 the
file is created in `overwrite/SKSE/Plugins/`). The INI is self-documenting:
it is created with every option, its default and an explaining comment, and
missing sections or keys are re-added on startup without touching existing
values or comments.

## Custom labels

Rename a slider in `Data/SKSE/Plugins/QuickAudioSliders.ini`:

```ini
[Labels]
Audio Overhaul Skyrim.esp|00AA6E=Weapons
Immersive Sounds - Compendium.esp|34924A=Armor
```

Key = `<plugin file>|<local FormID>` (the log lists every category with its
key at startup). ALL-CAPS names are shown in title case and a mod's own
prefix ("AOS_") is dropped inside that mod's group automatically.

## Per-mod sliders for mods without their own category (page "Extra Sliders")

Many sound mods route their sounds through Vanilla categories (Effects,
Ambience). By default the plugin creates, at startup and only in memory, one
sound category per such mod (at least `MinSounds` descriptors, default 3) and
shows it on the "Extra Sliders" page. Nothing is written to disk or to save games;
engine-managed categories and Sound Record Distributor assignments are left
alone, and mods that already have a slider on the Volume page are not
duplicated. Opt out with `[ModCategories] Enabled=0` in
`QuickAudioSliders.ini` (the "Extra Sliders" page then disappears after a restart),
or keep single mods off the page with `Exclude=Some Mod.esp,Other.esp`.

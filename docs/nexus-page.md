# Quick Audio Sliders — Nexus page text

Copy-paste source for the Nexus mod page. Written for the 1.0.0 release.

---

## Short description (summary field)

In-game volume panel for every sound category, via SKSE Menu Framework. Global slider, per-slider resets, persistence beyond the 8 vanilla slots, and automatic per-mod sliders for mods without their own category.

## Description

**Quick Audio Sliders** adds an in-game audio panel (rendered through SKSE Menu Framework) so you can adjust volumes while playing — no more Escape → Settings → Audio, no restarts, no INI editing.

**Page "Volume"** shows every sound category flagged "Should appear on menu" — the same sliders the vanilla audio menu has, plus all the ones mods add (Audio Overhaul, ISC, ambience mods, …). Sliders are grouped by the mod that owns each category, with the vanilla game first. Changes apply live through the same engine path the vanilla slider uses, including sounds that are already playing.

**Page "Extra Sliders"** gives you sliders that don't exist anywhere else: many sound mods route their sounds through vanilla categories (Effects, Ambience) and never get their own slider. Quick Audio Sliders detects those mods at startup and creates one in-memory sound category per mod, so each one gets its own volume slider. Nothing is written to your plugins or save games — the categories exist only at runtime and are verified save-neutral.

**Features**

- **"All Sounds (global)" slider** — one knob that scales *everything*, including mod categories that sit outside the vanilla master tree (which the vanilla master slider does not reach). Saved and restored across sessions.
- **Reset buttons** — every slider has a Reset to its record default (the value the mod author chose); "Reset All" (two-click confirmation) restores every category, the global slider, and the vanilla master.
- **Real persistence** — the engine can only remember 8 slider values in `SkyrimPrefs.ini` (`[AudioMenu]` has exactly eight slots — a vanilla limitation). With a modded load order there are easily 20+ categories, so the rest silently reset on every restart. This plugin stores every value in its own INI, keyed by plugin + FormID, so values survive restarts *and* load-order changes.
- **Readable labels** — category names come from the game's own translation table and follow your game language; ALL-CAPS names are shown in title case, redundant mod prefixes are stripped, and you can rename any slider via INI.
- **Self-documenting INI** — `Data/SKSE/Plugins/QuickAudioSliders.ini` is created with every option, its default and an explaining comment. Missing options are re-added on update without touching your values. (Under MO2 the file appears in `overwrite/SKSE/Plugins/`.)
- **Localized UI** — English and German included; translators only need to add a standard SKSE translation file (`Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt`).

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [SKSE Menu Framework 3](https://www.nexusmods.com/skyrimspecialedition/mods/120352) — renders the panel (open it with the framework's hotkey, default F1)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Supported runtimes

One DLL for Skyrim SE **1.5.97** and AE **1.6.x** (including 1.6.1170) via Address Library; the build also carries AE 1.7.x support from CommonLibSSE-NG. Skyrim VR is untested.

## Installation

Install with your mod manager (MO2 / Vortex). The archive contains:

```
SKSE/Plugins/QuickAudioSliders.dll
Interface/Translations/QuickAudioSliders_ENGLISH.txt
Interface/Translations/QuickAudioSliders_GERMAN.txt
```

No ESP, no FOMOD, no patches needed. Open the SKSE Menu Framework menu in-game and pick the "Quick Audio Sliders" section.

## Configuration

Everything lives in `Data/SKSE/Plugins/QuickAudioSliders.ini`, created on first launch with all options documented inline:

- `[ModCategories]` — turn the "Extra Sliders" page off (`Enabled=0`), set the minimum sound count per mod (`MinSounds`), or exclude specific plugins (`Exclude=`).
- `[Labels]` — rename any slider (`<plugin file>|<local FormID>=My Name`; the log lists every category with its key at startup).
- `[Volumes]` — the persistence store; managed by the plugin.

## FAQ

**Why does the global slider start at a value below 100%?**
It remembers its last value across sessions (stored in the plugin INI). If you lowered it last time, you start quiet — the slider at the top of the panel shows the current value, so nothing is hidden. Set it back to 100% (or use Reset All) and the plugin releases all control back to the game.

**My game was completely silent before I even installed this — can it fix that?**
Yes, one known cause: a vanilla master volume stuck at 0 in `SkyrimPrefs.ini`. The plugin repairs that once at startup (restores the vanilla master to 100%) and "Reset All" also restores it. If you deliberately set the vanilla master to 0 afterwards, the plugin respects it.

**The intro video (Bethesda logo) is still loud!**
Known limitation: the Bink intro video plays before any mod code runs and outside the game's sound-category system, so no SKSE plugin can touch its volume. Use `sIntroSequence=` in `Skyrim.ini` to skip it, or lower your system volume for startup.

**Does the "Extra Sliders" page bloat my save?**
No. The runtime categories are created in memory only, never flagged for the vanilla menu, and never enter the save file (verified with ReSaver: no created/changed forms, no co-save growth).

**Does it conflict with the vanilla audio menu or other audio mods?**
No. Values for the first eight categories go through the same `[AudioMenu]` slots the vanilla menu uses, so both stay consistent. Sound Record Distributor assignments are detected and left alone; mods that bring their own sound category keep it.

**Where is the log?**
`My Games/Skyrim Special Edition/SKSE/QuickAudioSliders.log` — it lists every category with its INI key, every restored value, and every `$Key → label` translation at startup.

## Credits

- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) by alandtse, CharmedBaryon and the CommonLibSSE contributors (MIT)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) by QTR-Modding / Thiago Kaique (MIT) — and through it [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut and contributors (MIT)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) by meh321
- The SKSE team

## Source

The plugin is written in C++ against CommonLibSSE-NG and licensed under the MIT License (see the LICENSE file in the archive docs / repository). Source is currently kept in a private repository — ask on the Posts tab if you want it for a port or a fork and I'll share it.

## Changelog

**1.0.0**
- Initial release.
- Page "Volume": every menu-flagged sound category, grouped by owning mod, live changes, per-slider reset, two-click Reset All.
- Page "Extra Sliders": automatic runtime per-mod categories for mods without their own slider (save-neutral, configurable, opt-out).
- "All Sounds (global)" slider over every root category, persisted.
- Persistence for every category beyond the engine's 8 `[AudioMenu]` slots, keyed load-order-independently.
- One-time vanilla master repair (stuck-at-0 fix).
- Self-documenting INI; English and German translations.

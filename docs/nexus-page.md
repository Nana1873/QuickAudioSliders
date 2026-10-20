# Quick Audio Sliders: Nexus page text

Copy-paste source for the Nexus mod page. Written for the 1.0.0 release.
Style: casual, first person, no em dashes.

---

## Short description (summary field)

In-game volume panel for every sound category, via SKSE Menu Framework. Global slider, per-slider resets, persistence beyond the 8 vanilla slots, and automatic per-mod sliders for mods that never got their own.

## Description

**Full disclosure up front:** this mod was developed with AI (Claude). I tested it myself in-game, every feature, over many sessions. If that's not for you, no hard feelings.

**Why this exists:** I got tired of hitting Escape, then Settings, then Audio, every single time I just wanted to turn the music down a bit. There was no mod that put the audio sliders into a quick in-game panel, so I built one.

**What it does:** Quick Audio Sliders adds an audio panel to the SKSE Menu Framework menu, so you can change volumes while playing. No reloads, no INI editing, changes apply instantly, even to sounds that are already playing.

**Page "Volume"** has every sound category that is flagged to appear on the audio menu. That's the vanilla sliders plus everything your sound mods add (Audio Overhaul, ISC, ambience mods and so on), grouped by the mod each category belongs to, vanilla first. Changes go through the same engine path the vanilla slider uses.

**Page "Extra Sliders"** is the part I'm most happy with: lots of sound mods route their sounds through vanilla categories like Effects or Ambience and never get a slider of their own. The plugin finds those mods at startup and builds one in-memory sound category per mod, so each of them gets its own volume slider. Nothing is written to your plugins or saves, the categories only exist at runtime, and I verified with ReSaver that saves stay clean.

**Features**

- **"All Sounds (global)" slider.** One knob that scales everything, including mod categories that sit outside the vanilla master tree and that the vanilla master slider never reaches. Saved and restored across sessions.
- **Reset buttons.** Every slider has a Reset to its record default (the value the mod author intended). "Reset All" (click twice to confirm) restores every category, the global slider and the vanilla master.
- **Real persistence.** The engine can only remember 8 slider values in `SkyrimPrefs.ini`. `[AudioMenu]` has exactly eight slots, that's a vanilla limitation. With a modded load order you easily have 20+ categories, so the rest silently reset on every restart. This plugin stores every value in its own INI, keyed by plugin and FormID, so values survive restarts and even load-order changes.
- **Readable labels.** Category names come from the game's own translation table and follow your game language. ALL-CAPS names are shown in title case, redundant mod prefixes are stripped, and you can rename any slider via INI.
- **Self-documenting INI.** `Data/SKSE/Plugins/QuickAudioSliders.ini` is created with every option, its default and a comment explaining it. Updates re-add missing options without touching your values. (Under MO2 the file shows up in `overwrite/SKSE/Plugins/`.)
- **Localized UI.** English and German included. Translators only need a standard SKSE translation file (`Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt`).

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [SKSE Menu Framework 3](https://www.nexusmods.com/skyrimspecialedition/mods/120352) (renders the panel, open it with the framework's hotkey, default F1)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Supported runtimes

One DLL for Skyrim SE **1.5.97** and AE **1.6.x** (including 1.6.1170) via Address Library. The build also carries AE 1.7.x support from CommonLibSSE-NG. Skyrim VR is untested.

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

- `[ModCategories]` turns the "Extra Sliders" page off (`Enabled=0`), sets the minimum sound count per mod (`MinSounds`) or excludes specific plugins (`Exclude=`).
- `[Labels]` renames any slider (`<plugin file>|<local FormID>=My Name`, the log lists every category with its key at startup).
- `[Volumes]` is the persistence store, managed by the plugin.

## FAQ

**Why does the global slider start below 100%?**
It remembers its last value across sessions (stored in the plugin INI). If you lowered it last time, you start quiet. The slider sits at the top of the panel and shows the current value, so nothing is hidden. Set it back to 100% (or use Reset All) and the plugin hands control back to the game.

**The intro video (Bethesda logo) is still loud!**
Known limitation: the Bink intro video plays before any mod code runs and outside the game's sound-category system, so no SKSE plugin can touch its volume. Use `sIntroSequence=` in `Skyrim.ini` to skip it, or lower your system volume for startup.

**Does the "Extra Sliders" page bloat my save?**
No. The runtime categories are created in memory only, never flagged for the vanilla menu, and never enter the save file. Verified with ReSaver: no created or changed forms, no co-save growth.

**Does it conflict with the vanilla audio menu or other audio mods?**
No. Values for the first eight categories go through the same `[AudioMenu]` slots the vanilla menu uses, so both stay consistent. Sound Record Distributor assignments are detected and left alone, and mods that bring their own sound category keep it.

**Where is the log?**
`My Games/Skyrim Special Edition/SKSE/QuickAudioSliders.log`. It lists every category with its INI key, every restored value and every label translation at startup.

## Permissions

Downloads are available exclusively here on Nexus Mods, please don't re-upload the files to other sites. The source code is licensed under GPL-3.0, so ports and forks are welcome under the same license with credit.

## Credits

- **AI development:** built with Claude by Anthropic; all in-game testing done by me.
- [SKSE](https://skse.silverlock.org/) by the SKSE team, the foundation everything here stands on.
- [SKSE Menu Framework 3](https://www.nexusmods.com/skyrimspecialedition/mods/120352) by QTR-Modding (Thiago Kaique), MIT. It renders the panel and embeds [Dear ImGui](https://github.com/ocornut/imgui) by Omar Cornut and contributors, MIT.
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) by alandtse, CharmedBaryon and the CommonLibSSE contributors, GPL-3.0-or-later (with modding exceptions; originally MIT).
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) by meh321.
- Build dependencies via vcpkg: [spdlog](https://github.com/gabime/spdlog) (logging) and, through CommonLibSSE-NG, [fmt](https://github.com/fmtlib/fmt), Microsoft DirectXTK / DirectXMath and [rapidcsv](https://github.com/d99kris/rapidcsv).
- **Research / tools:** engine analysis (volume broadcast, master volume path, translation table) performed with [Ghidra](https://ghidra-sre.org/) by the NSA, [Bethesda Ghidra Scripts](https://github.com/doodlum/BethesdaGhidraScripts) by doodlum and [GhidrAssistMCP](https://github.com/jtang613/GhidrAssistMCP) by Jason Tang, MIT. No code from these projects is included in the mod.

## Source

The plugin is written in C++ against CommonLibSSE-NG and licensed under **GPL-3.0** (see the LICENSE file). Downloads live only here on Nexus; the source is planned for GitHub. Until it's up, ask on the Posts tab and I'll share it.

## Changelog

**1.0.0**
- Initial release.
- Page "Volume": every menu-flagged sound category, grouped by owning mod, live changes, per-slider reset, two-click Reset All.
- Page "Extra Sliders": automatic runtime per-mod categories for mods without their own slider (save-neutral, configurable, opt-out).
- "All Sounds (global)" slider over every root category, persisted.
- Persistence for every category beyond the engine's 8 `[AudioMenu]` slots, keyed load-order-independently.
- One-time vanilla master repair (stuck-at-0 fix).
- Self-documenting INI; English and German translations.

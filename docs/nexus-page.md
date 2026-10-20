# Quick Audio Sliders: Nexus page text

Copy-paste source for the Nexus mod page. Written for the 1.0.0 release.
Style: casual, first person, no em dashes.

---

## Short description (summary field)

In-game volume panel for every sound category, via SKSE Menu Framework. Global slider, per-slider resets, persistence beyond the 8 vanilla slots, and automatic per-mod sliders for mods that never got their own.

## Description

**Full disclosure up front:** this mod was developed with AI (Claude). I tested everything myself in-game.

I got tired of hitting Escape, then Settings, then Audio, every time I just wanted to turn the music down a bit. No mod put the audio sliders into a quick in-game panel, so I built one: an audio panel in the SKSE Menu Framework menu. Changes apply instantly, even to sounds already playing.

**Page "Volume"**: every sound category that appears on the audio menu, vanilla and modded (Audio Overhaul, ISC, ambience mods and so on), grouped by the mod it belongs to.

**Page "Extra Sliders"**: my favorite part. Many sound mods route their sounds through vanilla categories like Effects or Ambience and never get their own slider. The plugin detects them at startup and gives each one its own slider, via in-memory categories. Nothing touches your plugins or saves (verified clean with ReSaver).

**Also in the box**

- **"All Sounds (global)" slider** that scales everything, including categories the vanilla master slider never reaches.
- **Reset per slider** (back to the mod author's default) and a two-click **Reset All**.
- **Real persistence.** Vanilla can only remember 8 slider values; with a modded load order the rest silently reset every restart. This plugin saves them all, keyed so even load-order changes don't matter.
- **Readable labels** in your game language, custom names via INI, self-documenting config file, English and German UI included.

## Supported runtimes

One DLL for Skyrim SE **1.5.97** and AE **1.6.x** (including 1.6.1170) via Address Library. The build also carries AE 1.7.x support from CommonLibSSE-NG. Skyrim VR is untested.

## Installation

Install with your mod manager (MO2 / Vortex). The archive contains:

```
SKSE/Plugins/QuickAudioSliders.dll
Interface/Translations/QuickAudioSliders_ENGLISH.txt
Interface/Translations/QuickAudioSliders_GERMAN.txt
```

No ESP, no FOMOD, no patches needed. Requirements are listed at the top of this page (SKSE64, SKSE Menu Framework 3, Address Library). Open the SKSE Menu Framework menu in-game (its hotkey, default F1) and pick the "Quick Audio Sliders" section.

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

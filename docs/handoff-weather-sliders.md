# Handoff: weather sliders without ESP patches

Status: investigation only, nothing implemented or tested in-game.
Origin: Nexus user report (phbd01, 2026-11-01):

> Hi bro can you add support for cathedral weathers? I tried this mod
> https://www.nexusmods.com/skyrimspecialedition/mods/49010 but it doesnt work
> with "cathedral weathers unofficial update". Currently your sliders also
> doesnt change weather sounds.

Mod 49010 is "Volume Sliders for Weather" (Mugnum). It ships ESP patches per
weather mod (Obsidian, Azurite, Aequinoctium, VA II, ...) that re-point rain /
snow / interior / thunder sound descriptors to its own menu-flagged SNCTs.
Cathedral Weathers Unofficial Update has no such patch, so it does nothing
there. Not our bug, but our plugin also has no weather handling.

## Why Quick Audio Sliders misses weather today

1. Skyrim.esm defines no weather category (see the 18-entry table in
   `VanillaFullName`, `src/AudioSliders.cpp`). Vanilla weather sounds hang
   on the ambience routing (`AudioCategoryAMB` / `AMBr`, both under Effects).
   Without AOS, Ambience is not menu-flagged, so weather is only reachable
   through "Effects".
2. `PlanModCategories` attributes a descriptor to `sourceFiles->front()` (the
   defining file) and skips Vanilla-defined descriptors. Weather mods often
   override the Vanilla rain / wind / thunder SNDRs or reuse them in their WTHR
   records, so those sounds count as Vanilla and never get an Extra slider.
3. Mod-defined weather SNDRs on a Vanilla category (>= `MinSounds`) already
   get a per-mod slider, mixed with that mod's other sounds. Ones on a
   pause / ducking / submerged category are skipped on purpose
   (`IsEngineManagedVanillaCategory`).

Unverified: which categories the Vanilla and Cathedral Weathers weather SNDRs
actually use. Check with houseCARL (Skyrim.esm + CW + CWUU) and ask the
reporter for `My Games\Skyrim Special Edition\SKSE\QuickAudioSliders.log`.

## Proposed design: runtime weather categories

Same mechanism as `[ModCategories]` (in-memory `BGSSoundCategory` from the
form factory, 0xFF FormID, `kShouldAppearOnMenu` reset, re-pointed one frame
after kDataLoaded), but grouped by weather sound type instead of by mod.
Works for any weather mod, no ESP, save-neutral.

1. At kDataLoaded, iterate `GetFormArray<RE::TESWeather>()` and its sound list
   (`TESWeather::sounds`, entries with a sound FormID + type
   Default / Precip / Wind / Thunder; verify the exact struct and enum names in
   `extern/commonlibsse-ng/include/RE/T/TESWeather.h`, written from memory).
   Resolve each FormID with `RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>`.
2. Bucket the descriptors: Precipitation (optionally split Rain / Snow via the
   weather's `data.flags` kRainy / kSnow; a descriptor used by both stays in
   Precipitation), Wind, Thunder, Other (type Default). Deduplicate per
   bucket; a descriptor seen in several buckets goes to the first by a fixed
   priority (Thunder > Precip > Wind > Default) and is logged.
3. Skip a descriptor when:
   - its category is engine-managed (`IsEngineManagedVanillaCategory`),
     otherwise menu pause / dialogue ducking / submerged muting breaks;
   - its category is mod-owned (e.g. Volume Sliders for Weather's own SNCTs,
     AOS categories): that mod already gives a slider, do not steal it;
   - Sound Record Distributor assigns it (`SrdAssignsCategory`).
   Log every skip with its reason, like `PlanModCategories` does.
4. One created category per (bucket, original parent), parent = original
   category, copy `kMuteWhenSubmerged` from the parent (see
   `CreateModCategories`). Label "Weather: Rain" etc. via new `$QAS_*`
   translation keys (ENGLISH + GERMAN files). If one bucket has several
   parents, use one created category per parent but a single slider driving
   all of them, or add "(Ambience)" suffixes; decide when you see real data.
5. Persist under `[Volumes]` with a stable key, e.g. `weather:precip`,
   `weather:wind`, `weather:thunder`, `weather:other` (add the parent's
   local FormID when there are several parents).
6. Order against `[ModCategories]`: plan weather first and exclude the
   weather descriptors from `PlanModCategories`, so a sound is never in two
   plans. Re-point in the same deferred step as `RepointModDescriptors`,
   keeping the `descriptor->category == parent` guard.
7. UI: a "Weather" group on the "Extra Sliders" page (or the Volume page;
   user decision). Reset restores 100% like other synthetic entries.
8. INI: new `[WeatherCategories]` section in `kIniTemplate` with `Enabled=1`
   and a comment, plus its constants in `PluginIni.h` if `MenuIntegration`
   needs them at kPostLoad (as `[ModCategories] Enabled` does).

## Known risks

- Shared descriptors: a wind / rain SNDR also used by ambient sound markers or
  region sounds would follow the weather slider too. Accept and document, or
  scan `BGSAcousticSpace` / region sound data to detect and log it.
- Interior weather sounds (rain on the roof) may not come from WTHR sound
  lists; check where Vanilla and "Interior Weather Sounds Restored" put them
  before promising an interior slider.
- Weather sounds already playing keep their old category until restarted;
  re-pointing happens before the first gameplay frame, so this should not show,
  but confirm after a save load during rain.
- Volume Sliders for Weather installed together: its re-pointed SNDRs land
  on its own categories and are skipped by rule 3 (mod-owned), so both coexist.
  Verify.

## Test plan (MO2, `..\..\tools\agent\launch-skyrim-skse.ps1`)

1. Log lists the weather plans, skip reasons, and re-pointed counts.
2. `fw <rain weather id>` in the console: Rain slider changes rain live,
   Effects / Ambience still scale it (parent chain).
3. `fw` a thunderstorm: Thunder slider works independently.
4. Dive under water during rain: still muted. Open a menu: still pauses.
5. Restart: the values are restored from `[Volumes]`.
6. With Cathedral Weathers + Unofficial Update loaded (the reporter's setup),
   and with Volume Sliders for Weather + a supported weather mod.
7. `[WeatherCategories] Enabled=0`: no re-pointing, no group.

## Reply drafted for the reporter

Thanks, you're right: the sliders don't treat weather as its own group yet, and
weather sounds from the base game are deliberately skipped on the "Extra
Sliders" page. I'm looking into rain / wind / thunder sliders that work with
any weather mod without patches. Could you send me your
`Documents\My Games\Skyrim Special Edition\SKSE\QuickAudioSliders.log`?

# Design notes

## Why a native plugin

No existing mod exposes the audio sliders through SKSE Menu Framework. dMenu NG
can show INI pages, but a write to `SkyrimPrefs.ini` does not change the
running mixer: the engine reads `[AudioMenu]` once at startup. The Vanilla
Audio tab changes the volume by calling `BSISoundCategory::SetCategoryVolume`
on the category form and `fAudioMasterVolume` for the master, so the plugin
does exactly that.

## What the Vanilla audio menu persists

`SkyrimPrefs.ini` `[AudioMenu]` (observed on this lab's profile):

```
fAudioMasterVolume=1.0000
fVal0=1.0000 ... fVal7=1.0000
uID0=94881      ; 0x000172A1 AudioCategorySFX        ($Effects)
uID1=1007612    ; 0x000F5FFC AudioCategoryFST        ($Footsteps)
uID2=554685     ; 0x000876BD AudioCategoryVOCGeneral ($Voice)
uID3=466532     ; 0x00071E64 AudioCategoryMUS        ($Music)
uID4=503329576  ; 0x1E000F28 (mod category from an older load order)
```

So each slot `n` pairs `uID<n>` (runtime FormID, decimal) with `fVal<n>`
(volume 0..1). The plugin keeps that mapping: it reads the existing `uID<n>`
assignment at `kDataLoaded`, matches categories by FormID, assigns the next
free slot to categories that have none, and writes `uID<n>` + `fVal<n>` on
slider release. `fAudioMasterVolume:AudioMenu` is a registered engine setting
(found as a literal in `SkyrimSE.exe`); `fVal<n>` / `uID<n>` are built
dynamically (`:AudioMenu` suffix literal only), so the plugin checks
`INIPrefSettingCollection::GetSetting` first and falls back to writing the INI
line directly into the file the collection points at (`subKey`).

Only categories with the SNCT flag `ShouldAppearOnMenu` are listed, in
`TESDataHandler` form-array order (load order). This lab's order has 20 such
categories (Vanilla 8 + AOS, ISC, NAT, and several single-slider mods).

## Threading

SKSE Menu Framework renders on the D3D present hook. Slider reads are plain
memory reads (`BGSSoundCategory::volumeMult`, `BSAudioManager::masterVolume`).
Every write (`SetCategoryVolume`, setting updates, INI writes) is marshalled to
the game thread with `SKSE::GetTaskInterface()->AddTask`. The `pending` flag
keeps the knob at the dragged value until the task has run.

## Multi-runtime

One DLL for SE 1.5.97 and AE 1.6.x via Address Library
(`add_commonlibsse_plugin` default). No `RELOCATION_ID` of our own; every
engine call goes through CommonLibSSE-NG accessors. `BGSSoundCategory`
(0x58) and `BSAudioManager` (0x190) have the same layout on SE and AE in NG.

## Verification status

See the hand-over note / session log. A clean build proves the code compiles;
load, slider effect, and persistence are checked in-game through
`QuickAudioSliders.log` (category volumes are logged at `kDataLoaded`, so a
value changed in one session shows up in the next session's log).

## Open questions

- Whether `BSAudioManager::masterVolume` is the live master gain the mixer
  reads, or whether the Vanilla master slider also pushes the value into the
  XAudio2 mastering voice. If the master slider turns out not to be audible
  live, the next step is a Ghidra look at the audio settings callback (the lab
  Ghidra project has a stale lock from 2026-08-10 blocking the MCP server).

## Grouping by origin (follow-up 2026-10-19)

Sliders are grouped by the plugin that *defines* the category
(`TESForm::GetFile(0)`, the first entry of `sourceFiles`). Skyrim.esm, the
official DLC masters, `_ResourcePack.esl` and `cc*.esm/.esl` form the
"Vanilla Game" group, rendered first and open by default; every other defining
plugin gets its own `CollapsingHeader` labelled with the file name minus
extension, with `TESFile::createdBy` (TES4 author) shown below it when set.
Groups are sorted alphabetically (case-insensitive), entries inside a group by
translated label, then FormID. Master stays above all groups.

## Engine slot limit (found in the first in-game log)

The engine registers exactly `fVal0..fVal7` / `uID0..uID7` as preference
settings; an unused slot holds `uID=0`. Only those eight slots are restored
at startup. With 21 menu categories in this load order, 13 of them cannot be
persisted through `[AudioMenu]` at all — a Vanilla limitation. The plugin now
treats `uID=0` as free and takes the lowest free slot first. Categories that
still end up beyond the registered slots are persisted by the plugin itself
(next section); no `uID<n>` / `fVal<n>` lines beyond the engine's slots are
written any more, because the engine ignores them.

## Plugin-owned persistence (follow-up 2026-10-19)

`Data/SKSE/Plugins/QuickAudioSliders.ini`, written with the Win32 profile API
(no extra vcpkg package), resolved to an absolute path from the game CWD
because `WritePrivateProfileString` looks in the Windows directory for a bare
file name. Under MO2/USVFS a newly created file lands in `overwrite/`.

```ini
[Volumes]
Audio Overhaul Skyrim.esp|00AA6E=0.5000
```

- Key = defining plugin file (`TESForm::GetFile(0)`) + `|` + local FormID
  (`TESForm::GetLocalFormID`, 6 hex digits, ESL-aware), so a load-order change
  neither detaches a value from its category nor lets another category inherit
  it. Value = volume 0..1 with four decimals.
- `Persist()` writes the key for **every** category (so the value follows a
  category that a later load order pushes out of an engine slot) and writes
  `[AudioMenu]` only for categories with `slot < registered engine slots`
  (eight here; if no `uID<n>` setting is registered at all, eight is assumed
  rather than treating every category as slot-less).
- `Initialize()` (`kDataLoaded`) reads the file for every category with
  `slot >= engine slots`, snapshots the value into `uiVolume`, and applies it
  with `SetCategoryVolume` through `SKSE::GetTaskInterface()`; the same task
  logs each restored value (`Restored <FormID> <EditorID> '<label>' (slot n)
  volume x from '<path>'`) and only then sets `IsReady()`. Categories in engine
  slots keep the engine's own restore; a value in the plugin INI is ignored
  for them.
- A category without a defining file (runtime-created) cannot be keyed; it is
  logged as a warning and simply not persisted.
- Injected records are keyed to the file their FormID range belongs to, not
  to the plugin that carries them: AOS's `AOS_FootstepsNPC` (`00000F72`) gets
  `Skyrim.esm|000F72`, because `GetFile(0)` is Skyrim.esm. Still stable.

Verified in-game 2026-10-19 (run through `run-skyrim-test.ps1`): a seeded
`Audio Overhaul Skyrim.esp|065871=0.1500` was restored for `AOS_Waterfalls`
(slot 9) on the game thread before `IsReady()`; the twelve other slot-less
categories logged their keys with "no stored volume". The write path (slider
release) still needs a manual check.

## "Alle Sounds (global)" slider (follow-up 2026-10-19)

User report: Master at 0% does not mute everything. Our Master writes
`BSAudioManager::masterVolume` + `fAudioMasterVolume`; whatever the engine does
with that, mod categories with no parent (`_SPPF_SFX` from SkyParkour,
`WordWalls` from Voiced Word Walls) sit outside the `_AudioCategoryMaster`
tree, and categories without the menu flag are not on any slider.

Design of the global slider:

- It is a multiplier applied to every **root** sound category
  (`parentCategory == nullptr`), menu-visible or not. Children multiply their
  parent's volume, so scaling only the roots scales each sound exactly once
  (no G² on nested chains) and reaches categories without the menu flag.
  In this load order the roots are `_AudioCategoryMaster`, `_SPPF_SFX`, and
  `WordWalls`; they are logged at `kDataLoaded`.
- Applied as `root.volume = baseline × G` via `SetCategoryVolume` on the game
  thread. The per-category sliders and their `[AudioMenu]` INI lines are never
  touched. The baseline is captured the moment G leaves 100% and written back
  exactly when G returns to 100%, after which the plugin releases the roots,
  so engine or other-mod changes to a root while G is at 100% are respected.
- **Persistence (user decision 2026-10-19):** originally not persisted (a
  game starting muted with no trace in the Vanilla menu seemed confusing).
  The user asked for it to be saved, so the value is written to the plugin
  INI (`[Volumes] GlobalMultiplier`) on slider release and re-applied at
  `kDataLoaded` after the category restores. It is deliberately NOT written to
  `SkyrimPrefs.ini [AudioMenu]`, which belongs to the Vanilla master. The
  slider on top shows the current value, so a quieter start is visible.
- Blocklist: a root whose EditorID contains `PausedDuringMenu`,
  `FadeDuringDialogue`, `MuteSubmerged` or `MuteWhenSubmerged` is skipped
  (none of these is a root today; the check is defensive).

Open: whether the Vanilla master slider in fact drives `_AudioCategoryMaster`
rather than `BSAudioManager::masterVolume`. If the global slider mutes
everything and Master still does not, that is the likely answer and the
Master slider should then target `_AudioCategoryMaster` too.

## Reset (follow-up 2026-10-19)

- **Default value** of a category = the SNCT record's UNAM "Default Menu
  Value" (`BGSSoundCategory::defaultMenuValue / 65535`), which is exactly
  what the engine initialises `volumeMult` from when no `[AudioMenu]` slot
  carries the category (verified against the first in-game log: every
  unassigned category started at its UNAM). VNAM (static multiplier) is not a
  slider default — it is always applied on top. A record without UNAM (0)
  falls back to 100%. Master default is 100%.
- **Per-slider reset**: a `SmallButton("Reset")` in front of every slider,
  tooltip shows the target value. Applies live and persists like a slider
  release (the override is rewritten to the default rather than removed, so
  the `[AudioMenu]` slot and the plugin INI stay consistent with Vanilla).
- **Reset all**: button in the Global section; two-click confirmation — the
  first click arms it for 4 s (label changes to "Wirklich alle
  zuruecksetzen?"), the second click within that window resets every
  category, the master, and the global multiplier to 100%. Chosen over a
  Ctrl-modifier because it needs no keyboard and is discoverable.
- Labels use "ue" instead of "ü" because the Menu Framework's default font
  (Futura Condensed EN) only covers ASCII unless Cyrillic/… ranges are
  enabled in its INI; the game's own translated category names already
  showed "RÜSTUNG" fine, so this is a precaution for our own strings only.

## Pooled "Sonstige Mods" group (follow-up 2026-10-19)

Eleven groups were too many. A mod plugin keeps its own group only with at
least `kMinGroupSize` (2, in `AudioSliders.h`) menu sliders; smaller ones are
pooled into "Sonstige Mods", rendered last and collapsed by default. Pooled
sliders are relabelled "<mod name>: <category>" (the mod name is the file name
without extension) so the origin stays visible; sorting inside the pool is
alphabetical by that prefixed label. "Vanilla Game" is never pooled. With this
load order that yields Vanilla, Audio Overhaul Skyrim, Immersive Sounds -
Compendium, NAT-ENB, and Sonstige Mods (7 sliders) = 5 groups.

## Ghidra findings (SkyrimSE 1.6.1170, lab project, 2026-10-19)

Read with the lab's GhidrAssistMCP after the stale project lock was removed
with the user's OK. Addresses are AE 1.6.1170 (unpacked exe).

- `BGSSoundCategory::GetCategoryVolume` @ 140323210 =
  `staticMult/65535 × volumeMult × parent->GetCategoryVolume()` (recursive,
  1.0 at the root). Confirms the parent-chain model behind the global slider:
  scaling the roots scales every sound exactly once.
- `BGSSoundCategory::SetCategoryVolume` @ 140323270 stores `volumeMult` and,
  for menu categories, copies the value into the matching `[AudioMenu]` slot
  (`fVal<n>` setting data) via the slot lookup @ 140323790. The lookup walks
  the 8-entry table `gIniPref_uID0_AudioMenu` (0x50 bytes per slot, 8 slots)
  and **assigns the first free slot (uID == 0) to an unknown FormID** — the
  engine hands out slots itself; it just never writes them back to disk until
  `WriteAllSettings` (closing the Vanilla settings menu).
- `BGSSoundCategory::InitItemImpl` @ 140322fe0: on first call validates all
  8 slots (a uID that is not a menu SNCT is cleared and its fVal set to 1.0);
  then a menu category with a slot takes `volumeMult = fVal<n>`, otherwise
  `SetCategoryVolume(defaultMenuValue / 65535)` (UNAM). That is the whole
  Vanilla restore path: 8 slots, UNAM as default. Consequence for us: write
  `uID<n>` together with every `fVal<n>` (done since build 8), otherwise a
  stale on-disk uID makes the engine discard the value on the next start.
- `BSXAudio2GameSound::SetVolumeImpl` @ 140cc0be0 → volume helper @ 140cbb290:
  output = `BSAudioManager::masterVolume (+0x170) × sound volume (+0x70) ×
  category->GetCategoryVolume()`, then attenuations, clamped, converted to
  dB. So `masterVolume` is the live master gain; our Master slider writes the
  right field.
- **But** `BSGameSound::Update` @ 140cbb970 does not recompute volume. A
  volume change reaches playing sounds only through an audio-thread message.
  `papyrus::SoundCategory::SetVolume` @ 140a06370 → helper @ 140cba940 does
  `category->SetCategoryVolume(v)` and then
  `BSAudioManager::ComposeMessage(SOUND_MSG::VolumeChanged /*0x10*/, 0, 0, 0)`.
  That broadcast is what re-applies volumes to every active sound. It was
  missing from this plugin until build 9 — the reason Master (and category
  sliders, for sounds already playing) seemed not to "mute everything".
  `BroadcastVolumeChanged()` now follows every category, master and global
  change.
- Not found: a dedicated vanilla master-slider handler (data xrefs to the
  `fAudioMasterVolume:AudioMenu` Setting @ 142011840 are absent in the
  project). Given the volume formula above, the master slider can only act
  through `masterVolume` plus the same VolumeChanged broadcast.

## UI simplification (user decisions 2026-10-19, build 10)

- **Master slider removed.** In-game test (build 7): the global slider at 0%
  mutes everything audibly, the Master slider does not (mod categories with no
  parent, `_SPPF_SFX` and `WordWalls`, sit outside `_AudioCategoryMaster`, and
  the plugin did not yet broadcast VolumeChanged). The user considers Master
  redundant next to "Alle Sounds"; `fAudioMasterVolume` and
  `BSAudioManager::masterVolume` are no longer touched — the Vanilla menu owns
  them. The Ghidra findings above stay for reference.
- **Flat layout.** Collapsing groups were no faster than ESC → Settings →
  Audio, so every group is now an always-visible `SeparatorText` heading
  (Vanilla first, mods with several sliders, "Sonstige Mods" last); the page
  scrolls.
- "Alle zuruecksetzen" resets every category to UNAM and the global multiplier
  to 100% (and persists both).

## Vanilla-master safety net (build 11, 2026-10-20)

Build-7 test runs persisted `fAudioMasterVolume = 0` through the then-existing
Master slider; after the Master slider was removed, "Alle zuruecksetzen" could
no longer undo that, and the game stayed silent (log 00:03: `fAudioMasterVolume
= 0.0000` in memory). Two measures:

- "Alle zuruecksetzen" also restores the Vanilla master to 100% (Setting +
  `BSAudioManager::masterVolume` + VolumeChanged broadcast + INI write).
- One-time startup repair: if `fAudioMasterVolume` is 0 at `kDataLoaded` and
  the plugin INI has no `[Volumes] MasterRepaired` marker, it is set to 100%
  and the marker written. A master the user later sets to 0 in the Vanilla menu
  on purpose is left alone.
- Reset order fixed: Global is released to 100% before the category resets,
  so roots that are also menu categories (`_SPPF_SFX`, `WordWalls`) end at
  their default instead of the pre-reset baseline.

## Build 12: grouping by owning mod, English UI, compact layout (2026-10-20)

- **Grouping rule changed.** `GetFile(0)` (defining file) put AOS's renamed
  Vanilla categories (AMB → "$AOS_Ambience", FSTnpc, MAG) under Vanilla. Now a
  category belongs to the first file in its override chain
  (`TESForm::sourceFiles`) that is not an official file; only categories
  touched by nothing but Skyrim.esm, the DLCs, Creation Club, `_ResourcePack`
  and USSEP stay "Vanilla Game". (Winner-file grouping was rejected: for
  AudioCategoryAMB the winner is `AOS_ISC_Integration.esp`, not AOS.)
- **English by default, translatable.** All plugin strings have English
  defaults compiled in (`Translation::Strings`) and are overridden from
  `Data/Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt` via
  `SKSE::Translation::ParseTranslation` at `kDataLoaded`; keys `$QAS_*`.
  English and German files ship in `config/Interface/Translations/`
  (UTF-16 LE with BOM, tab-separated). Category labels still come from the
  game's own table. Tooltip strings carry printf placeholders (`%.0f%%`);
  translators must keep them.
- **Compact layout.** No "<file> by <author>" captions, no "Other Mods" hint
  line, Reset All sits on the Global slider's row. Pooled labels: mod name
  shortened (cut at " - ", strip "Volume Slider"/"SE"), and when the category
  name already names the mod (SkyParkour, Standing Sound Stones, Crackling
  Fire) only the category name is shown; otherwise "<short mod>: <category>".

## Build 13: grouping by renamed FULL, root sliders decoupled (2026-10-20)

- Build 12's "first non-official file in the chain" rule moved every Vanilla
  category AOS merely touches (Effects, Footsteps, Voice, Music, Menus) into
  the AOS group; the user expects only visibly taken-over categories there.
  New rule for official-defined categories: compare the displayed FULL name
  with the Skyrim.esm original (table of all 18 base-game SNCT names, read
  from Skyrim.esm via houseCARL; the DLCs define no SNCT). Unchanged → Vanilla
  Game; changed → first non-official file in the override chain. Mod-defined
  categories keep their defining mod. Note: Skyrim.esm flags only Effects,
  Footsteps, Voice and Music for the menu; Menus/Magic/Ambience/NPC Footsteps
  only appear because a mod (AOS) set the flag — Menus stays Vanilla because
  its name is unchanged, which matches the user's intuition.
- Root categories that are also sliders (SkyParkour, Word Walls) no longer
  display the global value: while the global multiplier is active, their
  slider edits the root baseline and the engine gets baseline × global.

## Build 14: label cosmetics (2026-10-20)

Display only; records are untouched. Applied in this order, each skipped for
a label the user overrode:

1. **Override map.** `QuickAudioSliders.ini` `[Labels]`, key =
   `<plugin file>|<local FormID>` (the same key the volume store uses, e.g.
   `Audio Overhaul Skyrim.esp|00AA6E=Weapons`), value = display name shown
   verbatim. Default: empty. Labels that are `$Keys` can alternatively be
   overridden in `Interface/Translations/QuickAudioSliders_<LANGUAGE>.txt`
   (`$AOS_Ambience<TAB>Ambience`), because FULL names are resolved through
   the same translation table.
2. **Mod prefix stripped** inside a mod group: a leading acronym of the mod's
   short name ("AOS" for "Audio Overhaul Skyrim") or the short name itself,
   followed by `_`, space, `-`, `:` or `.`, is removed ("AOS_Ambience" →
   "Ambience"). Generic; no mod names in code.
3. **Title case for ALL-CAPS labels** longer than three characters with at
   least one letter and no lower-case letter ("RÜSTUNG" → "Rüstung",
   "WAFFEN" → "Waffen"); first letter after space/`-`/`_`/`/`/`(` is upper.
   Uses Win32 `CharLowerBuffW`/`CharUpperBuffW` on UTF-16, so umlauts work.
   Mixed-case names and short acronyms are left alone.

## Build 15: runtime per-mod sound categories (opt-in, 2026-10-20)

User decision: runtime variant instead of an add-on ESL. Initially off by
default; since the two-page layout (build 17) the user wants it **on by
default** (build 18) — the sliders live on their own "Mods" page, so the
"panel flooding" reason for opt-in is gone. Opt-out via `Enabled=0`; an
existing INI with an explicit value is always respected, the default applies
only when the key is missing.

Config in `QuickAudioSliders.ini` (created with defaults on first start):

```ini
[ModCategories]
Enabled=1      ; 0 = do not create runtime categories (no "Mods" page)
MinSounds=3    ; a mod needs at least this many eligible descriptors
```

Mechanics (all in `AudioSliders.cpp`, `PlanModCategories` /
`CreateModCategories` / `RepointModDescriptors`):

- Eligible descriptor (SNDR): defined by a non-official plugin AND its
  category is defined by an official file AND that category is not
  engine-managed (PausedDuringMenu*, SFXFadeDuringDialogue, MuteSubmerged, by
  Skyrim.esm FormID and by name) AND no Sound Record Distributor config that
  mentions "category" names both the plugin and the descriptor's FormID
  (`Data/SKSE/Plugins/SoundRecordDistributor/**/*.json|ini|toml|yaml`).
  Mods with their own SNCT are untouched (their descriptors point at mod
  categories).
- One category per (mod plugin, original parent): parent chain is preserved
  exactly, with one node inserted, so pausing/ducking via `Matches()` and the
  global multiplier via the root chain still work. `kMuteWhenSubmerged` is
  copied from the parent. If a mod yields more than one category, labels read
  "<mod> (<parent>)".
- Forms come from `IFormFactory<BGSSoundCategory>::Create()` (dynamic 0xFF
  FormIDs). SNCT/SNDR have no change-form in the save system, so nothing is
  written to .ess/.skse. `kShouldAppearOnMenu` is never set: the Vanilla menu
  would otherwise write 0xFF IDs into the eight uID slots.
- Re-pointing (`BGSSoundDescriptor::category`, GNAM) runs one frame after
  kDataLoaded via `AddTask`, after other plugins' kDataLoaded handlers; only
  descriptors still pointing at the planned parent are changed. The engine
  copies the category pointer into `BSGameSound::category` per playback, so
  every sound started afterwards uses the new category.
- Persistence: `[Volumes] mod:<plugin file>|<parent local FormID>` in the
  plugin INI (FormIDs of dynamic forms are not stable). Restored before the
  sliders are ready; default 100%. Panel: grouped under the mod, reset to
  100%, no engine slot.

Verification checklist for the user (not yet done in-game):

1. Set `Enabled=1`, start the game, check `QuickAudioSliders.log` for
   "Mod categories: N plan(s)", one "Mod category '…'" line per slider and
   "… re-pointed to runtime mod categories".
2. In the panel: new sliders under the mod groups (e.g. Sounds of Towns and
   Cities, Wildwood Echoes, Whales Off The Coast, …); move one to 0% and
   listen in a place where that mod plays (Wildwood Echoes: forest).
3. Underwater: a mod sound under Ambience must still mute when submerged.
4. Open/close a menu with a mod ambience playing: it must still pause like
   before.
5. Save, quit, restart: slider values return (log "Restored … mod:…").
6. Save bloat: inspect the new save with ReSaver (lab tool) — the created
   forms must not appear as created/changed forms; the .skse co-save size
   must not grow with each save.

## Build 16: Menus grouping and German Vanilla labels (2026-10-20)

- `Menus` landed under AOS because AOS changes the FULL from the plain
  "Menus" to the key "$Menus" (same text, now translatable). The takeover
  comparison now strips the `$` and compares case-insensitively, so this is
  "unchanged" and Menus stays Vanilla.
- Vanilla labels showed English in a German game although
  `interface/translate_german.txt` (in Skyrim - Interface.bsa) has
  `$Effects → Effekte`. Two fixes: the translator lookup now tries the key
  with and without its `$` (engine-loaded and SKSE-loaded entries may be
  keyed differently), and labels that fell back to the bare key are
  re-resolved once on the first panel render (`RefreshTranslatedLabels`),
  because SKSE imports per-plugin translation files (e.g. AOS's
  `$AOS_Ambience → Ambiente`) after kDataLoaded. The startup log now prints
  every `$Key → label` resolution and marks misses.

## Build 17: two pages (2026-10-20)

Section "Quick Audio Sliders" now has page **Volume** (Global + Reset All,
then the categories Settings → Audio also shows: Vanilla and mod categories
with the menu flag, grouped as before) and page **Mods** (only the runtime
categories from `[ModCategories]`, same grouping, labels and reset). The Mods
page is registered at kPostLoad only when `Enabled=1` in
`QuickAudioSliders.ini` (read directly from `Data/SKSE/Plugins/…` through
the VFS). Global and Reset All keep acting on both pages; the tooltips and
the Mods page hint say so.

## Build 19: no duplicate mod sliders, canonical translation path (2026-10-20)

- **Eligibility refined.** A descriptor is skipped when its plugin already
  owns a slider on the Volume page (AOS, ISC, and mods with their own SNCT):
  their sounds are adjustable there, a runtime copy only duplicated them
  (AOS alone produced six). New `[ModCategories] Exclude=a.esp,b.esp`
  (comma-separated, case-insensitive substrings of plugin file names) for
  mods the user does not want on the Mods page, e.g. footstep packs that are
  effectively covered by the Footsteps slider. One-descriptor sub-plans of a
  mod with several parents are dropped (their sounds stay on the Vanilla
  slider). Parents may be any Vanilla-defined, non-engine-managed category,
  renamed by a mod or not; the label uses the parent's slider label with the
  owner prefix stripped ("Sounds of Towns and Cities (Ambience)"), and
  friendly names for routing nodes ("Ambience (region)" for AMBr).
- **Label translation.** Build 18's log showed every `$Key` lookup missing
  at kDataLoaded, Vanilla ones included, although `interface/translate_german.txt`
  holds them; `$Weapons`/`$Armor` resolved only because a plugin file (SkyUI)
  defines them. So `BSScaleformTranslator::translationMap` is not where the
  engine keeps its own table. `Translation::Translate` now calls
  `SKSE::Translation::Translate` first — the translator's virtual
  `Translate()`, exactly what Scaleform uses — and falls back to the map.
  Additionally the owning mod's translation file is imported via
  `SKSE::Translation::ParseTranslation(<plugin name>)` before its categories
  are labelled (SKSE does not import every plugin's file by itself; AOS's was
  missing). The first-render refresh stays as a safety net.

## Build 21: page 2 renamed to "Auto Sliders" (2026-10-20)

"Volume" vs "Mods" was misleading (mods appear on the Volume page too). Page
1 "Volume" = every regular menu slider; page 2 "Auto Sliders" = the categories
this plugin creates. Page names are registered at kPostLoad, before any
translation table exists, so they stay English; the page hint is translated
("Regler, die dieses Plugin für Mods erzeugt …").

## Rename: page 2 "Auto Sliders" -> "Extra Sliders" (2026-10-20)

"Auto" described the implementation (the plugin creates the categories
itself), "Extra" describes the benefit (additional sliders that would not
exist otherwise). Page names are still registered at kPostLoad and stay
English; no `$QAS_*` string mentioned the old name.

## Build 22: self-documenting INI (2026-10-20)

`QuickAudioSliders.ini` now documents itself. A template in `AudioSliders.cpp`
(`kIniTemplate`) holds every section and key with its default and an English
comment: `[ModCategories]` `Enabled=1` / `MinSounds=3` / `Exclude=` (with an
example), `[Labels]` with a commented-out override example, `[Volumes]` with a
"persistence store, do not edit manually" note. `EnsurePluginIni()` writes the
full template when the file is missing and retrofits an existing file
line-based: missing sections are appended whole, missing keys are inserted at
the end of their section with their comment block; every existing line (user
values, user comments) is kept verbatim. Reading/writing goes through text-mode
streams, so line endings stay CRLF. `WritePluginVolume()` lost its own
first-write header; if the file disappeared since startup it simply calls
`EnsurePluginIni()` before the Win32 profile write (the previous session's
attempt to template this path produced syntax errors - kept minimal on
purpose).

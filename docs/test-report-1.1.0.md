# Testbericht Quick Audio Sliders 1.1.0

Stand: 2026-11-24, Branch `ccr-8450ad3a-qboakf` (PR 3). Geschrieben von der
Lab-Session, die den Branch zum ersten Mal gebaut und gestartet hat.

## Wie getestet wurde

- **Laufzeiten:** eine Testinstanz mit Skyrim 1.6.1170 (wenige Mods, ohne
  Engine Fixes) und eine Modliste mit Skyrim 1.7.104 (mit Engine Fixes 7.x,
  Azurite Weathers III). Die große Hauptliste (1.7.104) lief nicht: dort
  blockiert ein Fehlerdialog einer anderen Mod (OSkin) den Start vor
  `kDataLoaded`, siehe Abschnitt 5.
- **Methode der meisten Läufe:** Store-Dateien (Plugin-INI, `SkyrimPrefs.ini`,
  `EngineFixes_SNCT.ini`, Ersatzdatei) in einen definierten Zustand bringen,
  Spiel über MO2 starten, `coc Riverwood`, beenden, `QuickAudioSliders.log`
  und die Dateien danach auswerten.
- **Bedienung:** Regler im Plugin-Menü wurden mit echter Maus gezogen. Die
  Regler im Vanilla-Menü (Einstellungen > Audio) ließen sich mit synthetischer
  Eingabe nicht bewegen; wo das eine Rolle spielt, steht es dabei.
- **Gehört wurde nichts.** Alle Aussagen stützen sich auf Logs und Dateien.
- **Builds:** Die Läufe „frische INI", „INI aus 1.0.2", „Engine Fixes",
  „schreibgeschützt", „Vanilla-Master" und die Wetterläufe außer Azurite und
  True Storms liefen mit `030b766` (Stand des PR vor den Fixes). Azurite, True
  Storms, Reset All und die Reglerzüge liefen mit dem Migrations-Fix, der
  letzte Lauf zum Wurzel-Regler mit beiden Fixes (`27ecf4e`).
- Die Log-Zeilen unten sind wörtlich, ohne Zeitstempel; wo `'...\`
  steht, ist nur der Pfad der Testinstanz gekürzt. In Abschnitt 4 sind die
  Slider aus den Zeilen `Mod category '...'` zusammengefasst.

„Bestanden" heißt: Das Log und die Dateien zeigen das beabsichtigte
Verhalten. Es heißt nicht, dass es sich richtig anhört.

## 0. Gesamttest mit dem Stand `aac0e0d` (zweiter Durchlauf, 2026-11-24)

Nach den Abschnitten 1 bis 5 kamen drei Commits dazu (`27ecf4e` Fixes für
Wurzel-Regler, `6614ab7` Wetter-Nacharbeit aus der Cloud-Session, `aac0e0d`
Seite „Now Playing"). Deshalb lief alles noch einmal mit einem Build dieses
Stands: 25 Spielstarts, **kein Crashlog, keine geänderten Saves**. Die
Abschnitte 1 bis 5 bleiben als Protokoll des ersten Durchlaufs stehen; wo der
zweite abweicht, gilt dieser Abschnitt. Gehört wurde weiterhin nichts.

Build: wieder ohne Fehler und Warnungen, auch `6614ab7`, das vorher nie
kompiliert worden war.

| Test | Laufzeit | Status | Beleg oder Begründung |
|---|---|---|---|
| Frische INI | 1.7.104 | bestanden | `Created '...QuickAudioSliders.ini' (wrote the self-documenting template)`, `0 of 0 stored volume(s) differed` |
| INI aus 1.0.2 (nachgebaut), ohne und mit Änderung | 1.7.104 | bestanden | `stored 0.3300 predates SkyrimPrefs.ini, taking over the current 0.8000 (stored)`, `1 of 4 stored volume(s) differed` |
| Engine Fixes, veraltete SNCT-Datei | 1.7.104 | bestanden | `Restored 000172A1  'Effekte' (slot 0) ... volume 0.6000 (was 0.2000)`; zweiter Start `0 of 4 ... differed` |
| Ohne Engine Fixes | 1.6.1170 | bestanden | alle Läufe der Testinstanz |
| QAS-INI schreibgeschützt | 1.7.104 | bestanden | `write failed, Windows error 5`, Ersatzdatei mit zwei Zeilen, zweiter Start `fallback '...' with 2 line(s)` |
| Vanilla-Master 50 % über Neustart | 1.7.104 | bestanden | `Vanilla master restored to 0.5000 from the volume store`; zweiter Start `fAudioMasterVolume:AudioMenu = 0.5000` |
| Globaler Regler mit Word Walls, Store 0.8 / Global 0.5, zwei Starts | 1.6.1170 | bestanden | beide Starts `Restored ... volume 0.8000`, danach `Restored global multiplier 0.5000`; `fVal4=0.4000` |
| Migration mit skaliertem Wurzel-Regler | 1.6.1170 | bestanden | `fVal4` 0.4: `volume 0.8000 (was 0.4000)`; `fVal4` 0.3: `taking over the current 0.6000 (found 0.3000 with the global multiplier 0.5000)` |
| Regler per Maus gezogen | beide | bestanden | 1.6.1170: Effekte 0.5, Word Walls 0.81, Global 0.51; 1.7.104 mit Engine Fixes: Word Walls 0.8, Global 0.5, jeweils `Persisted ... (ok)` |
| Verpasstes Loslassen | 1.6.1170 | bestanden | beim Schließen des Mod-Menüs keine Zeile; beim nächsten Menü `Storing 'Musik' now: it was moved but never released` |
| Änderung von außen, Journal schließen (Papyrus statt Vanilla-Regler) | beide | bestanden | 1.6.1170: `Adopted 000876BD 'Stimmen' ...: 1.0000 -> 0.3700` und Wurzel-Regler `0.8100 -> 0.5882` (0.3 / 0.51); 1.7.104 mit Engine Fixes: `0.8000 -> 0.6000`. Durchläufe ohne Änderung: keine Zeile |
| Neustart nach Übernahme, mit Engine Fixes | 1.7.104 | bestanden | Engine Fixes hatte noch 0.8 gespeichert: `Restored FE10D8A1  'Voiced Word Walls' ... volume 0.6000 (was 0.8000)` |
| Reset All und Neustart danach | 1.6.1170 | bestanden | `Reset all 8 slider(s) ...`; Neustart `8 slider(s) have a stored volume`, `0 of 8 stored volume(s) differed` |
| „Now Playing" | beide | bestanden | Hook installiert (`... DoResolve hooked`, 3297 bis 3395 Deskriptoren), Seite füllt sich, Regler-Spalte zeigt „Effekte", „Fußstapfen", „Thunder (weather)". Namen überall nur als `Plugin|FormID`, siehe unten |
| „Now Playing" abgeschaltet (`[NowPlaying] Enabled=0`) | 1.6.1170 | bestanden | `pages: Volume, Extra Sliders`, keine Zeile `Now Playing:` |
| Wetter, zehn Load Orders | 1.6.1170 | bestanden | Tabelle unten; erzwungene Wetterwechsel ohne Absturz |
| Änderung im echten Vanilla-Menü; SkyParkour; echte 1.0.2-INI; Nebenwirkungen des Umhängens | | nicht gelaufen | wie im ersten Durchlauf |

**Wetter-Slider nach `6614ab7`.** Ein einzelner übrig gebliebener Deskriptor
bekommt keinen eigenen Slider mehr, wenn eine Mod den Typ abdeckt; er wird
geloggt (`Weather sound 0010C442 (defined in 'Skyrim.esm') stays on its
Vanilla slider: the only one a mod's Thunder slider does not cover`). Jeder
Slider loggt jetzt seine Sounds, und „AMB" heißt „Ambience".

| Load Order | Slider (Deskriptoren, Elternkategorie) | Nicht umgehängter Rest |
|---|---|---|
| Nur Vanilla-Wetter | „Thunder (weather)" (3, Effekte); „Rain (weather)" (2, Ambience); „Wind (weather)" (1, Ambience (region)) | keiner |
| NAT | keiner | Donner `0010C442` |
| NAT + VSfW, ohne und mit Patch | keiner | Donner `0010C442` |
| True Storms, ohne und mit VSfW | keiner | Donner `0010C442` |
| Cathedral + Unofficial Update + VSfW mit Patch | keiner | Donner `0010C442` |
| Cathedral + Unofficial Update + VSfW ohne Patch | „Thunder (weather, other)" (4, Effekte) | Wind `0F00080A` (Cathedral Weathers.esp) |
| Cathedral + Unofficial Update | „Wind (weather, Ambience)" (2); „Thunder (weather, other)" (4, Effekte); „Wind (weather, Ambience (region))" (1) | keiner |
| Azurite Weathers III + Azure | „Wind (weather, Ambience)" (1); „Thunder (weather)" (14, Effekte); „Rain (weather)" (5, Effekte); „Wind (weather, Ambience (region))" (1) | keiner |
| 1.7.104-Liste mit Azurite III (nur Planung) | „Thunder (weather)" (17, Effekte); „Wind (weather, Ambience)" (1); „Rain (weather, Effekte)" (5); „Rain (weather, Ambience)" (1); „Wind (weather, Ambience (region))" (1) | keiner |

`Weather sounds:` in dieser Reihenfolge (NAT-, True-Storms- und die
Cathedral-Zeilen je einmal pro unterschiedlichem Ergebnis):

```
Weather sounds: 8 descriptor(s) listed by 110 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 0 single left-over(s) left alone, 6 get 3 weather slider(s)
Weather sounds: 33 descriptor(s) listed by 133 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 single left-over(s) left alone, 0 get 0 weather slider(s)
Weather sounds: 54 descriptor(s) listed by 122 weather(s) as precipitation / thunder / wind: 51 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 single left-over(s) left alone, 0 get 0 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 36 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 single left-over(s) left alone, 0 get 0 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 4 engine-managed, 0 excluded or without category, 1 single left-over(s) left alone, 4 get 1 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 28 on a mod's own category, 4 engine-managed, 0 excluded or without category, 0 single left-over(s) left alone, 7 get 3 weather slider(s)
Weather sounds: 23 descriptor(s) listed by 110 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 0 single left-over(s) left alone, 21 get 4 weather slider(s)
Weather sounds: 27 descriptor(s) listed by 214 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 0 single left-over(s) left alone, 25 get 5 weather slider(s)
```

Neu aufgefallen im zweiten Durchlauf:

- **„Now Playing" zeigte keine Sound-Namen**, auch nicht in der 1.7.104-Liste
  mit Native EditorID Fix: `GetFormEditorID()` liefert für Sound-Deskriptoren
  dort nichts, also stand überall `Skyrim.esm|03F8CE`. **Danach behoben:** Das
  Spiel behält die Namen nicht, das Plugin schreibt sie jetzt beim Laden der
  Records selbst mit. Nachgetestet auf beiden Laufzeiten:
  `Now Playing: 3297 of 3297 sound descriptor(s) have a name` (1.6.1170) und
  `3338 of 3338` (1.7.104); die Seite zeigt dort Namen wie
  `AMBrWindForestPineGustB01`, `NPCDogBarkDistantSD`,
  `FSTPlayerDirtJumpDownSD`. Diese zwei Läufe kamen nach den 25 Spielstarts
  dazu; die übrigen Tests wurden mit diesem letzten Stand nicht wiederholt.
- Derselbe Vanilla-Deskriptor `0003D09F` zählt mit Vanilla-Wetter als Regen
  („Rain (weather)") und mit Cathedral als Wind („Wind (weather, Ambience)"),
  weil die Wetter-Records ihn unterschiedlich einstufen.
- Bleibt aus dem ersten Durchlauf: uneinheitliche Zusätze in den Namen
  („other" gegen Elternkategorie), zwei Regen-Slider in der 1.7.104-Liste,
  kein Slider „Snow" in irgendeiner Load Order.
- Engine Fixes speichert den Wert einer Wurzel-Kategorie nicht bei jeder
  Änderung; nach der Übernahme stand in seiner Datei noch der alte Wert. Der
  Store hat das beim Neustart korrigiert.
- Das Mod-Menü merkt sich seine Fensterposition; ein versehentlich
  verschobenes Fenster blieb über Neustarts verschoben (Framework, nicht QAS).

## 1. Build

`tools/build.ps1` (Preset `release`, MSVC, CommonLibSSE-NG `12d61f3`): 528 von
528 Schritten, **keine Fehler und keine Warnungen** im Plugin-Code. Für den
Build wurde nichts geändert. Die DLL lädt auf 1.6.1170 und 1.7.104.

Geändert wurde später wegen zweier Laufzeitbefunde, nicht wegen des Builds
(Commit `27ecf4e`, `src/AudioSliders.cpp`): siehe Test „globaler Regler mit
Wurzel-Kategorie" und „Änderung im Vanilla-Menü".

## 2. Ergebnis pro Test

| Test | Status | Begründung |
|---|---|---|
| Frische INI | bestanden | Vorlage wird angelegt, Marker geschrieben, kein Wert angewendet, keine Warnung. |
| INI aus 1.0.2, ohne Änderungen im Vanilla-Menü | bestanden | Gespeicherte Werte, die zu `fVal<n>` passen, werden behalten. Nachgebaute INI, keine echte aus langer Nutzung. |
| INI aus 1.0.2, mit Änderungen im Vanilla-Menü | bestanden | Der abweichende Wert (Stimmen 0.33 gegen `fVal2` 0.8) wird durch den aktuellen ersetzt. Gleiche Einschränkung. |
| Änderung im Vanilla-Menü, Journal schließen, Neustart | nicht gelaufen | Die Vanilla-Regler reagierten nicht auf synthetische Eingabe. Ersatz mit Papyrus `SetVolume` statt Regler: bestanden, siehe unten. Die Übernahme des Vanilla-Masters lief gar nicht. |
| Mit Engine Fixes, veraltete `EngineFixes_SNCT.ini` | bestanden | Engine Fixes setzt Effekte auf den veralteten Wert 0.2, das Plugin danach auf 0.6 und spiegelt nach `fVal0`. Nur für eine Kategorie mit Engine-Slot geprüft. |
| Ohne Engine Fixes | bestanden | Alle Läufe der 1.6.1170-Instanz; ein auf 1.0 zurückgesetztes `fVal0` wird aus dem Store auf 0.37 korrigiert. |
| QAS-INI schreibgeschützt | bestanden | Schreiben scheitert mit Windows-Fehler 5, die Werte landen in der Ersatzdatei und werden beim zweiten Start von dort gelesen. Nicht geprüft: Entfernen aus der Ersatzdatei, sobald die INI wieder schreibbar ist. |
| Globaler Regler mit Wurzel-Kategorie, Word Walls | bestanden | Erst fehlgeschlagen (Migration skalierte doppelt), nach dem Fix bestanden; auch mit Engine Fixes auf 1.7.104. |
| Globaler Regler mit Wurzel-Kategorie, SkyParkour | nicht gelaufen | SkyParkour war in keiner Testumgebung aktiv; gleicher Codepfad wie Word Walls, aber nicht belegt. |
| Vanilla-Master bei 50 % über einen Neustart | bestanden | Wird aus dem Store angewendet, nach `fAudioMasterVolume` geschrieben und beim zweiten Start so vorgefunden. Ob es hörbar 50 % sind: offen. |
| Reset All | bestanden | Alle acht Regler auf ihre Standardwerte, Global und Master auf 100 %, Store und `SkyrimPrefs.ini` aktualisiert. Nur Seite „Volume", kein Neustart danach. |
| Wetter: nur Vanilla-Wetter | bestanden | Drei Slider entstehen, erzwungene Wetterwechsel ohne Absturz. |
| Wetter: NAT | bestanden | 30 Sounds bleiben auf NATs Kategorien, ein Rest-Slider. |
| Wetter: True Storms | bestanden | 51 Sounds bleiben auf True Storms' Kategorien, ein Rest-Slider. |
| Wetter: VSfW mit Patch | bestanden | Mit NAT und mit Cathedral je ein Rest-Slider. |
| Wetter: VSfW ohne Patch | bestanden | Mit NAT ein Rest-Slider, mit Cathedral zwei. |
| Wetter: Cathedral + Unofficial Update | bestanden | Drei Slider ohne VSfW. |
| Nebenwirkungen des Umhängens (Dialog-Dämpfung, Innenraum-Dämpfung) | nicht gelaufen | Nur nach Gehör prüfbar. |

### Belege

**Frische INI** (1.7.104):

```
[info] QuickAudioSliders v1-1-0-0 loaded (runtime 1-7-104-0)
[info] Created 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' (wrote the self-documenting template)
[info] 0 slider(s) have a stored volume, 12 have none yet (they keep the value found at startup)
[info] 0 of 0 stored volume(s) differed from what the game had loaded
[info] 0 volume(s) applied from the volume store; sliders ready
```

Danach steht `StoreOwnsAllVolumes=1.0000` in `[Volumes]`.

**INI aus 1.0.2** (1.7.104, ein Lauf; Effekte, Fußstapfen und Musik passten zu
`fVal<n>`, Stimmen nicht, dazu ein Extra-Regler ohne Engine-Slot):

```
[info] Updated 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' (appended missing parts of the self-documenting template)
[info] Category 000876BD 'Stimmen' (slot 2) key 'Skyrim.esm|0876BD': stored 0.3300 predates SkyrimPrefs.ini, taking over the current 0.8000 (stored)
[info] 4 slider(s) have a stored volume, 7 have none yet (they keep the value found at startup)
[info] Restored FF00161D (runtime: Helios.esp) 'Helios' (slot -1) key 'mod:Helios.esp|000172A1' volume 0.7000 (was 1.0000)
[info] 1 of 4 stored volume(s) differed from what the game had loaded
```

**Ersatztest zur Änderung im Vanilla-Menü** (1.6.1170). Effekte per Papyrus
`SoundCategory.SetVolume(0.37)` geändert, Journal geöffnet und geschlossen:

```
[info] Adopted 000172A1 'Effekte' key 'Skyrim.esm|0172A1': 1.0000 -> 0.3700, changed outside this plugin (stored)
[info] 1 value(s) changed outside this plugin taken into the volume store
```

Danach `fVal0` von Hand auf 1.0 zurückgesetzt (verlorene `SkyrimPrefs.ini`)
und neu gestartet:

```
[info] Category 000172A1  'Effekte' slot 0 volumeMult 1.0000 GetCategoryVolume 0.8100 staticMult 0.900
[info] Restored 000172A1  'Effekte' (slot 0) key 'Skyrim.esm|0172A1' volume 0.3700 (was 1.0000)
```

Einmal war das echte Menü Einstellungen > Audio offen und zeigte die im Plugin
gesetzten Werte (Effekte 50 %, Musik 66 %). Verstellen ließ sich dort nichts.

**Engine Fixes mit veraltetem Eintrag** (1.7.104; Store 0.6, SNCT-Datei 0.2,
`fVal0` 1.0):

```
[info] Category 000172A1  'Effekte' slot 0 volumeMult 0.2000 GetCategoryVolume 0.1620 staticMult 0.900
[info] Restored 000172A1  'Effekte' (slot 0) key 'Skyrim.esm|0172A1' volume 0.6000 (was 0.2000)
[info] Persisted fVal0:AudioMenu = 0.6000 via INIPrefSettingCollection
[info] 1 of 4 stored volume(s) differed from what the game had loaded
```

Zweiter Start: `0 of 4 stored volume(s) differed from what the game had loaded`;
Engine Fixes hatte seine Datei inzwischen selbst auf 0.6 aktualisiert.

**QAS-INI schreibgeschützt** (1.7.104):

```
[warning] Cannot write 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini'
[warning] [Volumes] Skyrim.esm|0876BD = 0.8000 was not stored in 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' (write failed, Windows error 5)
[warning] The plugin INI 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' does not accept values; storing them in 'D:\Dokumente\My Games\Skyrim Special Edition\SKSE\QuickAudioSliders.ini' instead
[warning] [Volumes] StoreOwnsAllVolumes = 1.0000 was not stored in 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' (write failed, Windows error 5)
```

Ersatzdatei danach: `Skyrim.esm|0876BD=0.8000`, `StoreOwnsAllVolumes=1.0000`.
Zweiter Start, INI weiter schreibgeschützt:

```
[info] Volume store: 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini' with 6 [Volumes] line(s); fallback 'D:\Dokumente\My Games\Skyrim Special Edition\SKSE\QuickAudioSliders.ini' with 2 line(s)
[info] 5 slider(s) have a stored volume, 7 have none yet (they keep the value found at startup)
```

**Globaler Regler mit Wurzel-Kategorie (Word Walls).**

Fehlgeschlagen mit `030b766` (1.6.1170; INI ohne Marker, Store 0.8, Global
0.5, vom Spiel gespeichertes `fVal4` 0.4):

```
[info] Category FE0418A1 'Word Walls' (slot 4) key 'Voiced Word Walls.esp|0008A1': stored 0.8000 predates SkyrimPrefs.ini, taking over the current 0.4000 (stored)
[info] Restored global multiplier 0.5000 from '...\QuickAudioSliders.ini'
```

Nach dem Lauf stand `fVal4=0.2000`: Die Basis war durch den skalierten Wert
ersetzt und noch einmal skaliert worden. Gleiche Ausgangslage mit Fix:

```
[info] Restored FE0418A1  'Voiced Word Walls' (slot 4) key 'Voiced Word Walls.esp|0008A1' volume 0.8000 (was 0.4000)
[info] Restored global multiplier 0.5000 from '...\QuickAudioSliders.ini'
```

Mit `fVal4=0.3` (im Vanilla-Menü geändert, während Global aktiv war):

```
[info] Category FE0418A1 'Word Walls' (slot 4) key 'Voiced Word Walls.esp|0008A1': stored 0.8000 predates SkyrimPrefs.ini, taking over the current 0.6000 (found 0.3000 with the global multiplier 0.5000) (stored)
[info] Restored FE0418A1  'Voiced Word Walls' (slot 4) key 'Voiced Word Walls.esp|0008A1' volume 0.6000 (was 0.3000)
```

Regler per Maus gezogen (1.6.1170): Nach dem Ziehen des Globalreglers steht
Word Walls im Store weiter mit der Basis.

```
[info] Persisted [Volumes] Voiced Word Walls.esp|0008A1 = 0.8100 (ok)
[info] Persisted [Volumes] GlobalMultiplier = 0.5100 (ok)
```

Mit Engine Fixes (1.7.104), Regler gezogen, beendet, neu gestartet:

```
[info] Category FE10D8A1  'Word Walls' slot 4 volumeMult 0.8100 GetCategoryVolume 0.8100 staticMult 1.000
[info] 0 of 2 stored volume(s) differed from what the game had loaded
[info] Restored global multiplier 0.5000 from 'E:\Modlisten\Skyrim\GameRoot\Data\SKSE\Plugins\QuickAudioSliders.ini'
```

In dieser Abfolge hatte Engine Fixes die Basis gespeichert, nicht den
skalierten Wert; der Fall „Engine Fixes speichert skaliert" wurde hier nicht
ausgelöst.

Übernahme einer Änderung von außen bei aktivem Globalregler (zweiter Fix,
1.6.1170; Word Walls 0.8, Global 0.5, per Papyrus auf 0.3 gesetzt, Journal
auf und zu, dann Neustart):

```
[info] Adopted FE0418A1 'Voiced Word Walls' key 'Voiced Word Walls.esp|0008A1': 0.8000 -> 0.6000, changed outside this plugin (stored)
[info] Restored FE0418A1  'Voiced Word Walls' (slot 4) key 'Voiced Word Walls.esp|0008A1' volume 0.6000 (was 0.3000)
```

Ein Journal-Durchlauf ohne Änderung davor und danach erzeugte keine Zeile.

**Vanilla-Master bei 50 %** (1.7.104; Store `VanillaMaster=0.5000`, Setting 1.0):

```
[info] Vanilla master: fAudioMasterVolume:AudioMenu = 1.0000
[info] Vanilla master restored to 0.5000 from the volume store
```

Danach `fAudioMasterVolume=0.5000` in `SkyrimPrefs.ini`. Zweiter Start:

```
[info] Vanilla master: fAudioMasterVolume:AudioMenu = 0.5000
```

und keine weitere Wiederherstellung. Zusätzlich in Ghidra (1.6.1170)
nachgelesen: Das Einstellungsmenü und die Audio-Initialisierung schreiben den
Wert des Settings unverändert nach `BSAudioManager::masterVolume`
(`docs/design-notes.md`).

**Reset All** (1.6.1170; vorher Effekte 50 %, Musik 66 %, Word Walls 81 %,
Global 51 %):

```
[info] Reset all 8 slider(s) to their defaults, the global multiplier and the Vanilla master to 100%
[info] Reset  'Effekte' to its default 1.0000
[info] Reset  'Fußstapfen' to its default 0.8000
[info] Reset  'Stimmen' to its default 0.8000
[info] Reset  'Musik' to its default 0.5000
[info] Reset  'Voiced Word Walls' to its default 1.0000
[info] Reset (runtime: Weather) 'Thunder (weather)' to its default 1.0000
[info] Reset (runtime: Weather) 'Rain (weather)' to its default 1.0000
[info] Reset (runtime: Weather) 'Wind (weather)' to its default 1.0000
[info] Persisted [Volumes] GlobalMultiplier = 1.0000 (ok)
[info] Vanilla master volume restored to 100% (reset all)
```

Der Store enthielt danach genau diese Werte und `VanillaMaster=1.0000`.

**Verpasstes Loslassen** (nicht in der Liste, aber Teil des PR; 1.6.1170).
Menü mitten im Zug geschlossen: zunächst keine Zeile, kein Store-Eintrag.
Beim nächsten Menü-Ereignis im Spiel:

```
[info] Storing 'Musik' now: it was moved but never released
[info] Persisted [Volumes] Skyrim.esm|071E64 = 0.6600 (ok)
```

### Nachtrag: Seite „Now Playing"

Nach dem ersten Durchlauf dazugekommen und **nicht Teil der Tabelle oben**;
das Ergebnis des Gesamttests steht in Abschnitt 0. Der erste Kurztest auf
1.6.1170 (nur Vanilla-Plugins, erzwungenes Gewitter):

```
[info] Registered SKSE Menu Framework section 'Quick Audio Sliders' (framework v3.8), pages: Volume, Extra Sliders, Now Playing
[info] Now Playing: 3297 sound descriptor(s) indexed; BGSStandardSoundDef::DoResolve hooked (keeps the last 64 distinct sounds for 120 s)
```

Die Seite zeigte danach zehn Zeilen mit Zähler, Plugin, Regler („Effekte",
einmal „Thunder (weather)") und Alter; kein Absturz. Namen erschienen als
`Skyrim.esm|<FormID>`, weil die Testinstanz keine Editor-IDs wiederherstellt.
Nicht gelaufen: 1.7.104, eine große Load Order, `Enabled=0`, längeres Spielen
mit dem Hook, und ob die gelisteten Sounds die sind, die man hört.

## 3. Nur nach Gehör prüfbar und noch offen

- Ob das Umhängen der Vanilla-Wettersounds von Ambience/Effekte auf die neuen
  Kategorien Nebenwirkungen hat: Dialog-Dämpfung (Ducking), Dämpfung in
  Innenräumen, Unterwasser.
- Ob die Wetter-Slider hörbar genau die Sounds regeln, die ihr Name verspricht,
  und ob „Thunder (weather, other)" überhaupt etwas Hörbares regelt.
- Ob der Vanilla-Master bei 50 % nach dem Neustart tatsächlich halb so laut
  ist (Log und Ghidra sprechen dafür).
- Ob eine Änderung an bereits spielenden Sounds sofort ankommt, wenn das Plugin
  beim Start Werte wiederherstellt.
- Der echte Weg durch Einstellungen > Audio: Regler verstellen, Journal
  schließen, Neustart; ebenso die Gesamtlautstärke dort.
- SkyParkour als zweite Wurzel-Kategorie.
- Eine echte, lange genutzte 1.0.2-INI. Die INI der Hauptliste wäre der
  passende Fall (Stimmen 0.33 im Store gegen `fVal2` 1.0), lief aber nicht.

## 4. Wetter-Slider pro Load Order

Alle Läufe außer dem letzten in der 1.6.1170-Instanz, mit `fw 10a241`
(Gewitter), `fw 4d7fb` (Schnee), `fw c8220` (Regen) nach dem Laden; in keinem
Lauf entstand ein Crashlog. Ob das Wetter sichtbar wechselte, wurde nicht
geprüft. „Elternkategorie" ist die Vanilla-Kategorie, unter der der neue
Slider hängt. Ein Slider „Snow" erschien in keiner Load Order.

| Load Order | Slider (Deskriptoren, Elternkategorie) |
|---|---|
| Nur Vanilla-Wetter | „Wind (weather)" (1, Ambience (region)); „Rain (weather)" (2, AMB); „Thunder (weather)" (3, Effekte) |
| NAT (`NAT-ENB.esp`) | „Thunder (weather, other)" (1, Effekte) |
| NAT + VSfW ohne Patch | „Thunder (weather, other)" (1, Effekte) |
| NAT + VSfW mit NAT-Patch | „Thunder (weather, other)" (1, Effekte) |
| True Storms | „Thunder (weather, other)" (1, Effekte) |
| True Storms + VSfW | „Thunder (weather, other)" (1, Effekte) |
| Cathedral + Unofficial Update + VSfW mit Patch | „Thunder (weather, other)" (1, Effekte) |
| Cathedral + Unofficial Update + VSfW ohne Patch | „Wind (weather, other)" (1, AMB); „Thunder (weather, other)" (4, Effekte) |
| Cathedral + Unofficial Update | „Wind (weather, AMB)" (2, AMB); „Thunder (weather, other)" (4, Effekte); „Wind (weather, Ambience (region))" (1, Ambience (region)) |
| Azurite Weathers III + Azure | „Wind (weather, AMB)" (1, AMB); „Thunder (weather)" (14, Effekte); „Wind (weather, Ambience (region))" (1, Ambience (region)); „Rain (weather)" (5, Effekte) |
| 1.7.104-Liste mit Azurite III (nur Planung, keine Wetterbefehle) | „Thunder (weather)" (17, Effekte); „Wind (weather, AMB)" (1, AMB); „Rain (weather, Effekte)" (5, Effekte); „Wind (weather, Ambience (region))" (1, Ambience (region)); „Rain (weather, AMB)" (1, AMB) |

Log-Zeilen `Weather sounds:` in derselben Reihenfolge:

```
Weather sounds: 8 descriptor(s) listed by 110 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 6 get 3 weather slider(s)
Weather sounds: 33 descriptor(s) listed by 133 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 33 descriptor(s) listed by 133 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 33 descriptor(s) listed by 133 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 54 descriptor(s) listed by 122 weather(s) as precipitation / thunder / wind: 51 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 54 descriptor(s) listed by 122 weather(s) as precipitation / thunder / wind: 51 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 36 on a mod's own category, 2 engine-managed, 0 excluded or without category, 1 get 1 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 30 on a mod's own category, 4 engine-managed, 0 excluded or without category, 5 get 2 weather slider(s)
Weather sounds: 39 descriptor(s) listed by 119 weather(s) as precipitation / thunder / wind: 28 on a mod's own category, 4 engine-managed, 0 excluded or without category, 7 get 3 weather slider(s)
Weather sounds: 23 descriptor(s) listed by 110 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 21 get 4 weather slider(s)
Weather sounds: 27 descriptor(s) listed by 214 weather(s) as precipitation / thunder / wind: 0 on a mod's own category, 2 engine-managed, 0 excluded or without category, 25 get 5 weather slider(s)
```

Die Mod-Regler, die daneben auf der Seite „Volume" standen: NAT „Rain",
„Thunder"; True Storms „True Storms - Rain", „- Thunder", „- Interior";
VSfW „Weather - Rain", „- Snow", „- Interior", „- Thunder"; Cathedral „Rain",
„Thunder", „Interior Rain" (mit VSfW-Patch versteckt, „Interior Rain" dann als
Extra-Regler).

## 5. Was sonst aufgefallen ist

**Abstürze:** keine, in keinem Lauf ein Crashlog.

**Fehler im Code, behoben in `27ecf4e`:**

- Migration skalierte die Basis einer Wurzel-Kategorie doppelt, wenn der
  Globalregler aktiv war (Beleg oben).
- Eine Änderung an einer Wurzel-Kategorie von außerhalb wurde bei aktivem
  Globalregler nicht übernommen. Der Fix ist nur mit Papyrus `SetVolume`
  geprüft, nur auf 1.6.1170 und ohne Engine Fixes.

**Verhalten, das überrascht hat:**

- In jeder Load Order mit Wetter-Mod bleibt ein Slider „Thunder (weather,
  other)" für genau einen Deskriptor. Das Log nennt weder dessen Editor-ID noch
  das Plugin.
- Die Namen sind uneinheitlich: „(weather)", „(weather, other)", „(weather,
  AMB)", „(weather, Effekte)", „(weather, Ambience (region))". „AMB" ist die
  rohe Kategorie, „Effekte" die übersetzte, und bei Cathedral ohne VSfW steht
  beim Wind die Elternkategorie statt „other".
- Vanilla-Regen hängt an AMB, Azurites Regen an Effekte; in der 1.7.104-Liste
  gibt es deshalb zwei Regen-Slider.
- Bei aktivem Globalregler und einer Wurzel-Kategorie mit Engine-Slot stellt
  das Plugin bei jedem Start einen Wert wieder her und schreibt `fVal<n>`,
  weil das Spiel beim Beenden den skalierten Wert speichert (`1 of 1 stored
  volume(s) differed` bei jedem Start). Funktioniert, erzeugt aber bei jedem
  Start Schreibzugriffe.
- Im Vanilla-Menü steht eine solche Wurzel-Kategorie bei aktivem Globalregler
  mit dem skalierten Wert (Word Walls etwa 41 % statt 81 %).
- Ein mitten im Zug abgebrochener Regler wird nicht beim Schließen des
  Mod-Menüs gespeichert, sondern erst beim nächsten Menü-Ereignis des Spiels.
  Wer direkt danach abstürzt oder den Prozess beendet, verliert den Wert.
- Mit schreibgeschützter INI kommt bei jedem Start `Cannot write`, weil die
  Vorlage den Schlüssel `Weather` nachtragen will.
- Ein Wert, der für „Interior Rain" gespeichert wurde, solange die Kategorie
  ein Extra-Regler war (1.0), wurde angewendet, als sie ohne VSfW-Patch wieder
  ein Menü-Regler war, und ersetzte den Standardwert 0.4 der Mod. Der Store
  stammte aus früheren Sitzungen; ob das gewollt ist, ist offen.
- Die Slot-Zuordnung der Engine wechselt mit der Load Order; Werte kamen
  deshalb mehrfach „falsch geladen" an und wurden vom Store korrigiert (`4 of 8
  stored volume(s) differed` bei NAT + VSfW). Das ist der Zweck des Umbaus,
  aber nach Gehör nicht geprüft.
- „Alle zurücksetzen" braucht zwei Klicks innerhalb kurzer Zeit. Bei 1280x720
  ist der Knopf rechts abgeschnitten, bei 2560x1440 nicht.

**Umgebung, nicht der PR:**

- Die Hauptliste startet nicht bis `kDataLoaded`: `OSkin.dll` zeigt einen
  Fehlerdialog (`OSkin_skinDistributionConfig.json` passt nicht zum Schema).
  Quick Audio Sliders wurde dort nie initialisiert.
- In der 1.7.104-Liste bricht der automatische Test-Runner nach dem Laden mit
  einem Null-Array-Fehler ab; die Plugin-Logs waren trotzdem vollständig.
- Im ersten Lauf dort meldete der Runner ein verschwundenes verwaistes Co-Save
  (`quicksave0_…000236….skse`); die Ursache ist nicht geklärt.
- Beim Ziehen per Maus wurde in der 1.7.104-Liste versehentlich „Musik" von
  50 % auf 80 % gestellt. `SkyrimPrefs.ini` und die Engine-Fixes-Datei wurden
  danach aus der Sicherung wiederhergestellt.

**Was ich anders bauen würde:**

- Für jeden Wetter-Slider die Editor-IDs und Plugins der Deskriptoren loggen;
  ohne das lässt sich „Thunder (weather, other)" nicht beurteilen.
- Für Wetter-Slider eine Mindestzahl wie `MinSounds`, oder einen einzelnen
  Rest-Deskriptor auf dem Vanilla-Regler lassen.
- Die Elternkategorie in den Namen einheitlich benennen (übersetzter Name oder
  gar nicht) und „other" unabhängig davon setzen.
- Geänderte, noch nicht gespeicherte Regler auch dann sichern, wenn das
  Mod-Menü selbst schließt.
- Die Migration und die Übernahme teilen sich jetzt dieselbe Rechnung
  (Wert durch Globalregler); das gehört in eine gemeinsame Funktion.
- `activeSounds` nicht für eine „Now playing"-Ansicht verwenden: Die Map
  gehört laut Ghidra dem Audio-Thread und hat keinen Lock.

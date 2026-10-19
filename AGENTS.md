# Quick Audio Sliders Workspace

This folder is the Quick Audio Sliders repo and the source of truth for its
development. Do not treat `E:/DevProjects/MO2 Dev Setup/` as the repo when
working on this project.

## Boundaries

- **Workspace root**: `E:/DevProjects/MO2 Dev Setup/workspaces/Quick Audio Sliders/`
- **Shared lab root**: `E:/DevProjects/MO2 Dev Setup/`
- **Canonical MO2 runtime**: `E:/Modlisten/SkyrimAE/`
- **MO2 profile**: `E:/Modlisten/SkyrimAE/profiles/Default/`
- **MO2 dev mod target**: `E:/Modlisten/SkyrimAE/mods/houseCARL - Quick Audio Sliders Dev/ (created by housecarl_place)`
- **Runtime logs**: `D:/Dokumente/My Games/Skyrim Special Edition/SKSE/` and `D:/Dokumente/My Games/Skyrim Special Edition/Logs/Script/`

Use this workspace for source, tests, docs, and build/deploy scripts. Use the
shared lab root for skills, build tools, and launch helpers; use SkyrimAE for
MO2, the active Game Root, and runtime deployment. Layout follows the lab
workspace rules: `src/` editable source, `vendor/original/` untouched release
files, `build/` generated output, `patches/` config experiments, `docs/` notes.

## Local Workflow

Start each agent session (Codex or Claude Code) in this workspace, then:

1. Read this file.
2. Check `git status --short --branch`.
3. Plan or implement the next slice.
4. Run the project's local checks before commit or push (add them to `tools/`
   as the project grows).

For runtime work, build and deploy from this workspace, then launch Skyrim
through the shared lab's MO2 helper:

```powershell
..\..\tools\agent\launch-skyrim-skse.ps1
```

Do not start `E:\Modlisten\SkyrimAE\Game Root\skse64_loader.exe` directly for
runtime tests â€” a direct launch bypasses MO2/USVFS.

For game-binary inspection (native crashes, SKSE hook targets, undocumented
engine behavior), follow the shared lab `AGENTS.md` "Shared Tools" Ghidra rules
and `tools/TOOLCHAIN.md`. The analyzed SkyrimSE 1.6.1170 project already exists.
Use the lab's `tools/agent/start-ghidra-mcp.ps1` for read-only inspection and
`tools/agent/stop-ghidra-mcp.ps1` to stop the session. It binds only to loopback;
respect existing listener/process/project locks. Stop the MCP session before
opening or rebuilding that same project. An inspection request does not call
for `python run.py`, a new import, or a Ghidra/LLVM download.

## Local-only Coordination

This workspace is deliberately local-only (no GitHub remote). Coordinate
through commits on `main` and short notes in `docs/`. Keep `build/` and the
deployed DLL out of git. If the mod is ever published, re-run the GitHub block
from `tools/shared-scripts/new-workspace.ps1` at the lab root.

## Build

Native SKSE plugin (CommonLibSSE-NG, `extern/commonlibsse-ng` submodule, SKSE
Menu Framework header under `external/skse-menu-framework`). Build with

```powershell
.	oolsuild.ps1
```

which wraps `VsDevCmd.bat` + `cmake --preset release` + `cmake --build`. The
first configure compiles CommonLib from source (~15 min); later builds are
seconds. Output: `build/release/QuickAudioSliders.dll` (+ `.pdb`).
In a fresh clone run `git submodule update --init --recursive` first.

Dev deployment target is the MO2 mod folder named in Boundaries; place the DLL
as `SKSE/Plugins/QuickAudioSliders.dll` (via houseCARL `housecarl_place` or
the lab helpers), then enable the mod in MO2. A running game keeps the PDB
open, so quit Skyrim before relinking.

## Git Safety

Never commit third-party payloads, generated builds, deployed runtime
artifacts, or MO2 profile backups. These paths must stay untracked:

- `vendor/`
- `docs/source-docs/`
- `build/`
- `*.pex`, `*.esp`, `*.esm`, `*.esl`, `*.bsa`, `*.ba2`, `*.dll`, `*.pdb`
- MO2 `modlist.txt`, `plugins.txt`, `loadorder.txt`, or backup copies

Useful safety check (should print nothing):

```powershell
git ls-files | rg '^(vendor/|docs/source-docs/|build/)'
```

## Runtime Test Loop

1. Build and deploy dev artifacts from workspace tools.
2. Prefer the DevBench-first unattended runner
   `..\..\tools\agent\run-skyrim-test.ps1`; it launches SkyrimAE through
   MO2/USVFS, loads without saving, runs scenarios, and exits cleanly.
3. For renderer, shader, visual-regression, or performance work, use the
   Community Shaders Remote Control tools registered in DevBench. Default to
   read-only `communityshaders.inspect` and `communityshaders.feature`
   `list`/`get`; follow the shared lab `AGENTS.md` and `tools/TOOLCHAIN.md`
   mutation/rollback rules for all other Community Shaders operations.
4. For a visual/manual test use
   `..\..\tools\agent\launch-skyrim-skse.ps1`, load a save, run the test, then
   quit with `qqq`.
5. Check logs (`..\..\tools\agent\check-runtime-logs.ps1` for shared checks).

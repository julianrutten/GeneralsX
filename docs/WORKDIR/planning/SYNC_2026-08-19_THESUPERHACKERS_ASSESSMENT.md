# Upstream Sync Assessment: TheSuperHackers (2026-08-19)

Pre-merge assessment for branch `thesuperhackers-sync-08-19-2026`. Written and
committed *before* any merge is attempted, so the measurement survives
independently of the merge itself.

## 1. Remote

- Remote `thesuperhackers` added as `https://github.com/TheSuperHackers/GeneralsGameCode.git`.
  HTTPS rather than the SSH URL named in `.github/prompts/sync-thesuperhackers-upstream.prompt.md`,
  because the environment this sync ran in holds no GitHub SSH key. The repository is
  public, so the fetch needs no credential.
- Upstream's published default branch is confirmed `refs/heads/main`
  (`git ls-remote --symref thesuperhackers HEAD`). Not assumed.

## 2. How far behind

| Measure | Value |
|---|---|
| Merge base | `ec0658a3578499dfd88f1239899c9096c40b0087` (2026-08-15 23:03 +1000) |
| Upstream tip | `33a3f8f00e9403610a61e41c3360ba3eb4fcda77` (2026-08-19 09:19 +0300) |
| Our tip | `19859e65dccc5b72359bcb3e8a6b2beb7ad813c9` (2026-08-19 15:22 +0200) |
| Commits upstream is ahead | **7** |
| Commits we are ahead | 583 |
| Upstream date range to import | 2026-08-16 → 2026-08-19 (4 days) |
| Files changed by the import | 31 |
| Lines changed by the import | +635 / -81 |

**This repository is not significantly behind upstream.** The previous sync
(`thesuperhackers-sync-08-16-2026`, merged as PR #257 on 2026-08-16) landed only
three days ago, so this is a small incremental sync rather than the large
divergence the playbook's briefing anticipates. The playbook's caution about
files migrating into a unified `Core/` directory does not apply to this window:
no file was added, deleted, renamed or moved by these seven commits except the
two brand-new `WWLib/utf8.*` files.

## 3. What is coming in

| Commit | Type | Subsystem | Files |
|---|---|---|---|
| `33a3f8f00` | bugfix (#3136) | GameLogic — contain modules / AI exit | 16 |
| `242f5a4c1` | refactor (#3162) | GameClient + W3DDevice — particle/smudge typing | 3 |
| `15b02ffe9` | test (#3160) | GameLogic — nuke missile debug draw | 2 |
| `c98fcafa5` | feat (#2528) | WWLib + Common/System + GameSpy — UTF-8 transcoding | 6 |
| `a40cdb66d` | bugfix (#3125) | GameLogic — `KINDOF_NO_SELECT` | 2 |
| `fba44b797` | bugfix (#3153) | W3DDevice — scorch diffuse colour | 1 |
| `d95a9f8c9` | perf (#3140) | W3DDevice — smudge render early-out | 1 |

Subsystems touched, in GeneralsX terms:

- **Shared engine (`Core/GameEngine`)** — `GameDefines.h`, `AsciiString.cpp`,
  `UnicodeString.cpp`, `ParticleSys.cpp`, GameSpy `ThreadUtils.cpp`.
- **Shared device layer (`Core/GameEngineDevice`)** — `W3DParticleSys.cpp`,
  `W3DScorch.cpp`, `W3DSmudge.cpp`. Renderer-side, but all of it is
  backend-agnostic W3D code, not DXVK/SDL3 code.
- **Shared libraries (`Core/Libraries/.../WWLib`)** — two new files plus a
  `CMakeLists.txt` source-list entry.
- **Both products' game logic (`Generals/` and `GeneralsMD/`)** — contain
  modules, `Object.cpp`, `AIUpdate.cpp`, `NeutronMissileSlowDeathUpdate.cpp`.
  Upstream applied every one of these to both trees symmetrically, which
  satisfies AGENTS.md golden rule 11 (backport to Generals) without extra work
  on our side.

Nothing in this window touches `.github/`, CI, presets, `CMakePresets.json`, the
SDL3/DXVK/MiniAudio/OpenAL/FFmpeg platform layers, the INI parser, or file load
order. The playbook's standing instruction to reject upstream CI changes has
nothing to act on here.

## 4. Conflict surface

`git merge-tree --write-tree HEAD thesuperhackers/main` reports **two** conflicted
files, both the same conflict duplicated across the two products:

- `Generals/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp`
- `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp`

Everything else auto-merges. Auto-merged files that still need a semantic
review, because both sides edited them:

- `Core/GameEngine/Include/Common/GameDefines.h` — we flipped every
  `RETAIL_COMPATIBLE_*` to `0`; upstream adds a new `PRESERVE_RETAIL_PARTICLES`.
- `Core/GameEngine/Source/Common/System/UnicodeString.cpp` — our POSIX
  `vswprintf` locale/format-normalisation work vs upstream's new `translate()`.
- `Core/Libraries/Source/WWVegas/WWLib/CMakeLists.txt` — our source-list edits
  vs upstream's two new entries.
- `Core/GameEngine/Source/GameClient/System/ParticleSys.cpp` and
  `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DParticleSys.cpp`.

## 5. Pre-existing baseline

Established *before* merging so that any later failure can be attributed
correctly:

- `cmake --preset linux64-deploy` from `/work`: **succeeds** (exit 0).
- `cmake --build build/linux64-deploy --target GeneralsXZH GeneralsX`: run from
  the same clean tree; result recorded in the conflict-resolution plan.

Runtime smoke testing (playbook step 11) is **not possible** in this
environment: it is a headless container with no display, no Vulkan device and no
retail game assets, and the assets are not in the repository. This will be
reported as not run rather than approximated.

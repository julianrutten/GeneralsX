# Upstream Sync Outcome: TheSuperHackers (2026-08-19)

Result of executing `docs/WORKDIR/planning/SYNC_2026-08-19_THESUPERHACKERS_PLAN.md`
on branch `thesuperhackers-sync-08-19-2026`.

## 1. What was merged

`thesuperhackers/main` at `33a3f8f00`, merged over `ec0658a`. Seven commits,
2026-08-16 → 2026-08-19, 31 files, +638/-178 as landed.

| Commit | Change | Landed |
|---|---|---|
| `33a3f8f00` | #3136 tunnel network passengers can exit any endpoint | as upstream, replacing our equivalent fix |
| `242f5a4c1` | #3162 retail smudge particle type cleanup | as upstream |
| `15b02ffe9` | #3160 nuke missile debug radius draw | as upstream, plus WWMath wrappers |
| `c98fcafa5` | #2528 UTF-8 transcoding in WWLib | as upstream |
| `a40cdb66d` | #3125 `KINDOF_NO_SELECT` blocks selection | as upstream |
| `fba44b797` | #3153 scorch diffuse colour fix | as upstream |
| `d95a9f8c9` | #3140 smudge render early-out | as upstream |

## 2. Conflicts and how each was resolved

### 2.1 `AIUpdate.cpp` `privateExit` / `privateExitInstantly` — HIGH RISK, resolved

The only textual conflict, in both products (one site in `Generals`, two in
`GeneralsMD`). Both projects independently fixed the same regression:
Caball009's 10/08/2026 guard rejected exit commands for tunnel and cave
*networks*, where a passenger is recorded as contained by the endpoint it
entered, not the one it was ordered out of.

**Resolved by taking upstream and deleting our `isCaveContainer` /
`isSharedNetworkExitContainer` helpers.** Justification, since this removes
GeneralsX code:

- `TunnelContain::getContainedItemsList()` returns the controlling player's
  tunnel-system list, so upstream's membership test already implies the
  same-player condition our helper checked explicitly.
- `CaveContain::getContainedItemsList()` returns the tracker for the cave index,
  which is the exact network identity our helper compared pointers on.
- Our helper matched "both tunnels or both caves"; upstream dispatches on the
  actual contain module, so it cannot mismatch types.
- `OpenContain::isContained` reduces to the pre-existing `getContainedBy`
  comparison, so no other container type changes behaviour.

The resolved regions are byte-identical to upstream. Our `WWMath::Sqrt*Origin`
determinism edits elsewhere in the same files are untouched. The
`#include "GameLogic/Module/BehaviorModule.h"` we added is kept — it is genuinely
used by two other sites in each file — and the include block it had split is
restored.

**Residual behavioural difference:** our helper required the same controlling
player for *caves* too; upstream's cave `isContained` checks only network
membership. A captured cave that keeps its cave index could, in principle, let a
passenger exit through an endpoint now controlled by another player. This is
upstream's design and cannot be exercised without game assets.

### 2.2 `GameDefines.h` — auto-merged, policy reviewed

Our seven `RETAIL_COMPATIBLE_*` / `PRESERVE_RETAIL_SCRIPTED_CAMERA` zeroes and
our `DEEP_CRC_TO_MEMORY` and display-default edits all survived. Upstream's new
`PRESERVE_RETAIL_PARTICLES` is **kept at its upstream default of `1`**: it is not
a CRC switch, it gates the fixup that lets retail particle templates be
recognised as smudges at all, and GeneralsX ships against retail data. Setting it
to `0` would silently delete smudge rendering.

### 2.3 `UnicodeString.cpp` — auto-merged, semantics verified

Upstream rewrote `translate(const AsciiString&)`. Our POSIX `vswprintf` locale
handling and `normalizeWidePrintfFormatForPosix` are in `format_va`, a different
function, and both survive intact. `WideChar` is `wchar_t`
(`Core/Libraries/Include/Lib/BaseType.h:36`), 32-bit on Linux and macOS;
`utf8.h` documents that it handles both wide widths, so Linux/macOS stay in
agreement, which is what cross-play requires.

### 2.4 `WWLib/CMakeLists.txt` — auto-merged, verified

Upstream's `utf8.cpp` / `utf8.h` entries landed; our `registry.cpp` relocation
out of the `if(WIN32)` block and our `PchCompat.h` PCH substitution are intact.
`utf8.cpp.o` is confirmed present in the built `core_wwlib`.

### 2.5 Two upstream changes taken as cross-platform wins

- #2528 removes `MultiByteToWideChar` / `WideCharToMultiByte` from shared GameSpy
  code in favour of platform-API-free helpers.
- #3162 removes `*((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53`,
  an unaligned 4-byte load over a `char*` that hard-coded little-endian order and
  read past the end of short type names. Bad on ARM64; now `isUsingSmudge()`.

## 3. Determinism work applied on top of the merge

Upstream #3160 arrived with bare `ceilf`, `cosf` and `sinf` in the new
`debugDrawBlastCircle` helper in both products. Per AGENTS.md these were replaced
with `WWMath::Ceil`, `WWMath::CosTrig` and `WWMath::SinTrig`
(commit `3ca6a1f8c`). No other bare libm call, NaN-to-int cast, FMA opportunity
or FPU-state boundary was introduced by the seven commits.

## 4. Validation performed

| Check | Result |
|---|---|
| No merge markers anywhere in the tree | pass |
| `cmake --preset linux64-deploy` from `/work` (pre-merge baseline) | pass |
| `cmake --build ... --target GeneralsXZH GeneralsX` (pre-merge baseline) | pass, 2389/2389, both binaries |
| Same configure + build after the merge | pass, both binaries relinked |
| Same again after the WWMath change | pass |
| clang++ compile of all 20 merged `.cpp` files | pass, 0 errors each |
| No untracked build/runtime artifacts staged | pass (`build*` and `logs/` are gitignored) |

The clang pass is a partial proxy for macOS, which cannot be built here: it
exercises the same compiler frontend, but not the macOS SDK, ARM64 codegen or
MoltenVK/DXVK-on-macOS.

## 5. Not done, and why

- **Runtime smoke test (playbook step 11) — NOT ACHIEVABLE; a partial launch was
  attempted and is reported for what it is.** Both binaries were run as
  `./build/linux64-deploy/<product> -headless`. Both start, create the
  `SDL3GameEngine`, initialise `TheLocalFileSystem`, `TheArchiveFileSystem` and
  `TheWritableGlobalData`, enter the INI loader, and then exit with status 1 at
  `[INI] ERROR: No files read from directory 'Data\INI\Default\GameData'` —
  the retail data files are not in the repository. No crash, no hang, no stack
  trace, deterministic exit at the missing-asset boundary.

  That is engine startup, **not** the main loop. Playbook step 11 asks that both
  products enter and exit the main loop; neither ever reaches it, so step 11 is
  **not satisfied**. `scripts/qa/smoke/docker-smoke-test-zh.sh` needs a Docker
  daemon and could not be run either. Nothing here should be read as a passing
  smoke test.

  Note for the next person: that launch writes an untracked `ReleaseCrashInfo.txt`
  into the repository root, and `.gitignore` does not cover it. It was deleted
  before committing (playbook step 12), but it will reappear on any run without
  assets.
- **macOS configure/build — NOT RUN.** Linux container.
- **`scripts/build/linux/docker-*.sh` — NOT RUN.** No Docker daemon inside the
  container. The native `cmake --preset linux64-deploy` path from `/work` was
  used instead: same preset, same `CMAKE_HOME_DIRECTORY`.
- **Push and pull request — NOT DONE, deliberately.** The host holds a read-only
  credential and pushing is a maintainer decision.

## 6. Pre-existing problems found while validating (NOT caused by this sync)

These were confirmed against files the merge does not touch, and are reported
separately so they are not mistaken for merge damage.

1. **`RTS_BUILD_OPTION_DEBUG=ON` does not compile on 64-bit Linux.** Two
   independent blockers:
   - `Core/GameEngine/Include/Common/Debug.h:226-229` declares `SimpleProfiler`
     members as MSVC-only `__int64` under `#ifdef DEBUG_PROFILE`, which
     `RTS_DEBUG_PROFILE=DEFAULT` enables for debug builds. Last touched
     2026-01-11; untouched by this merge.
   - With `RTS_DEBUG_PROFILE=OFF`, the build then fails in the `MAKE_DLINK`
     debug validation at `GameLogic/Include/GameLogic/Object.h` (`:780` in
     `GeneralsMD`, `:737` in `Generals`), which casts `Object*` to
     `unsigned int`. Untouched by this merge.

   **Consequence for this sync:** upstream's new debug-draw *call site* in
   `NeutronMissileSlowDeathUpdate.cpp` is guarded by `#if defined(RTS_DEBUG)` and
   therefore could not be compiled here at all. The helper functions themselves
   are unguarded and do compile in `linux64-deploy`, so the WWMath change above
   is compile-verified; the call site is not.

2. **`Object::xfer` dereferences a possibly-null `Drawable` on save/load.**
   Upstream #2712 (2026-05-17) added `if (draw && xfer->getXferMode() ==
   XFER_LOAD)`. Both GeneralsX products still have the unguarded
   `if( xfer->getXferMode() == XFER_LOAD ) { draw->setID( drawableID ); }`
   (`GeneralsMD/.../Object.cpp:4178`, `Generals/.../Object.cpp:3670`). The merge
   base already had upstream's guard, so this is a divergence introduced by an
   earlier sync, not by this one. **Deliberately not fixed here** — it is outside
   this merge window and the maintainer may know why it was dropped — but it is a
   real null dereference and should be restored.

3. **`ParticleSystemManager::update` can call `TheSmudgeManager->reset()` when
   `TheSmudgeManager` is null.** Our `drawSmudge` is false precisely when
   `TheSmudgeManager` is null, and the `else` branch added by GeneralsX
   (`fbraz`/`Bender` annotations) then dereferences it. Pre-existing GeneralsX
   code, unchanged by the merge.

4. **`linux64-testing` does not exist.** `AGENTS.md` and
   `.github/instructions/build.instructions.md` both list it as a preset;
   `CMakePresets.json` has no such entry. Already noted in the 19/08 diary.

## 7. What still needs a human

Ranked by risk:

1. **Tunnel and cave network unloading**, both products. Garrison a tunnel
   network, then order the unload from a *different* tunnel; repeat for caves,
   including a cave network with a captured endpoint.
2. **Non-ASCII text everywhere.** `AsciiString::translate` and
   `UnicodeString::translate` now do real UTF-8 transcoding instead of 7-bit
   truncation. Map names, player names, GameSpy chat, `.ini`-sourced strings and
   anything round-tripping through them change behaviour for non-ASCII input.
   This is also the file holding our POSIX `vswprintf` locale fix.
3. **`WWLib/utf8.cpp`** — 283 lines of new hand-rolled decoder entering shared
   engine code, running for the first time on a 32-bit-`wchar_t` platform that
   upstream does not build for.
4. **Smudge and scorch rendering** — three of the seven commits touch it and it
   is only observable at runtime.
5. **`KINDOF_NO_SELECT`** — check no unit that used to be selectable stopped
   being so.

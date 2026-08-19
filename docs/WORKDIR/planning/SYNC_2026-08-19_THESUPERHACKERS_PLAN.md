# Upstream Sync Conflict Resolution Plan: TheSuperHackers (2026-08-19)

Companion to `SYNC_2026-08-19_THESUPERHACKERS_ASSESSMENT.md`. Written and
committed **before** any conflict is resolved, so the reasoning is on record
independently of the outcome.

Scope: merge `thesuperhackers/main` (`33a3f8f00`) into
`thesuperhackers-sync-08-19-2026`. Seven commits, 31 files, two conflicted
files. See the assessment for the measurement.

## A. Guiding constraints for this window

The playbook's standing rules that actually bind here:

1. **Never blanket-resolve.** Each hunk is judged on its own.
2. **Do not sacrifice the cross-platform architecture.** Nothing in this window
   touches SDL3, DXVK, MiniAudio, OpenAL, FFmpeg or the platform abstraction, so
   this rule mostly acts as a filter on *incoming* code rather than a defence of
   ours.
3. **Reject upstream CI changes.** Nothing to act on: no `.github/` path is
   touched by the seven commits.
4. **Cross-platform determinism (AGENTS.md).** Every incoming line must be
   audited for `libm` calls that need `WWMath` wrappers, NaN-to-int casts, FMA,
   and FPU state leaks. Section E.
5. **Parity between the two products.** Upstream already applied every gameplay
   change to both `Generals/` and `GeneralsMD/`; the job is to not break that
   symmetry while resolving.

## B. Build-system conflicts

**None.** `CMakePresets.json`, `CMakeLists.txt` at any level bar one, `cmake/`,
`vcpkg.json` and the build scripts are untouched by upstream in this window.

The single build-system change is `Core/Libraries/Source/WWVegas/WWLib/CMakeLists.txt`,
where upstream appends `utf8.cpp` / `utf8.h` to `WWLIB_SRC`. We have our own
edits elsewhere in the same file; git auto-merges them because they are in
different hunks.

- **Decision:** accept upstream's two source-list entries verbatim.
- **Verification:** confirm after merge that our WWLib entries survive intact and
  that `utf8.cpp` actually compiles into `libwwlib` on the Linux preset.

## C. Platform abstraction conflicts

**None.** No conflict, and no upstream change, in
`Core/GameEngineDevice/Source/{SDL3,DXVK,MiniAudioDevice,OpenALAudioDevice,FFmpeg}`
or `Core/Libraries/Source/Platform/`.

Two upstream changes are nevertheless *cross-platform wins* and are wanted for
that reason on top of their stated purpose:

### C.1 `c98fcafa5` removes Win32 text API calls from GameSpy

`Core/GameEngine/Source/GameNetwork/GameSpy/Thread/ThreadUtils.cpp` currently
calls `MultiByteToWideChar` / `WideCharToMultiByte` (`CP_UTF8`). Upstream
replaces both with the new hand-rolled `WWLib/utf8.h` helpers, which use no
platform text API at all. That deletes two Win32 dependencies from shared engine
code, which is exactly the direction GeneralsX wants.

- **Decision:** accept in full. We have no local edits to this file, so it
  applies cleanly.
- **Watch:** `WideChar` is `wchar_t` (`Core/Libraries/Include/Lib/BaseType.h:36`),
  which is 32-bit on Linux and macOS and 16-bit on Windows. `utf8.h` documents
  that it handles both widths, including surrogate pairs. Linux/macOS agree with
  each other, which is what our cross-play determinism requires; Windows would
  differ in *wide-string length* for astral codepoints, which is a pre-existing
  property of `wchar_t` and not a regression this sync introduces.

### C.2 `242f5a4c1` removes an unaligned little-endian type-pun

Both `ParticleSys.cpp` and `W3DParticleSys.cpp` currently identify smudge
particles with
`*((DWORD *)sys->getParticleTypeName().str()) == 0x44554D53`
— an unaligned 4-byte load over a `char*` that also hard-codes little-endian
byte order and reads past the end of any type name shorter than four bytes.
Upstream replaces it with `sys->isUsingSmudge()`.

- **Decision:** accept in full. This is strictly better on ARM64 (macOS) and is
  UB-free.

## D. Gameplay / game-logic conflicts

### D.1 THE ONE REAL CONFLICT — `AIUpdate.cpp` `privateExit` / `privateExitInstantly`

Conflicted in both products:

- `Generals/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp` (1 site)
- `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp` (2 sites)

**What happened.** Upstream `ec0658a` (merge base) contained Caball009's
10/08/2026 guard `if (us->getContainedBy() != objectToExit) return;`. That guard
broke tunnel and cave *networks*, where a whole network shares one passenger
list and a passenger is recorded as contained by the endpoint it *entered*, not
the endpoint it was ordered to unload from. Both projects then fixed the same
regression independently:

- **GeneralsX** (`0c9e1d1bf`, PR #254, GitHubCopilot 15/08/2026) added two file-static
  helpers, `isCaveContainer()` and `isSharedNetworkExitContainer()`, near the top
  of `AIUpdate.cpp`, plus an inline same-controlling-player check at each call
  site. The helper accepts the exit when both containers are tunnels (or both are
  caves), are controlled by the same player, and return the *same*
  `ContainedItemsList*`.
- **Upstream** (`33a3f8f00`, #3136) added a virtual
  `ContainModuleInterface::isContained(const Object*)`, implemented as
  `obj->getContainedBy() == getObject()` in `OpenContain` and overridden in
  `TunnelContain` and `CaveContain` to also search the shared items list. The
  call site collapses to
  `const ContainModuleInterface *contain = objectToExit->getContain(); if (contain == nullptr || !contain->isContained(us)) return;`

**Decision: take upstream's version and delete our two helpers.**

This is a removal of GeneralsX code, so per the playbook it needs explicit
justification. The justification is that upstream's implementation *subsumes*
ours rather than dropping a capability:

- `TunnelContain::getContainedItemsList()` returns
  `getObject()->getControllingPlayer()->getTunnelSystem()->getContainedItemsList()`
  (`TunnelContain.cpp:328`). Membership of that list therefore already implies
  the same controlling player, which is the extra condition our helper checked
  explicitly. The check is not lost, it is implied.
- `CaveContain::getContainedItemsList()` returns the tracker for the cave index
  (`CaveContain.cpp:212`), i.e. the cave network — the same object identity our
  helper compared pointers on.
- Our helper matched "both are tunnels or both are caves"; upstream's dispatches
  on the actual contain module, so it cannot mismatch types by construction.
- For every non-network container, `OpenContain::isContained` reduces to exactly
  the pre-existing `us->getContainedBy() == objectToExit` test, so no other unit
  type changes behaviour.

Upstream's version is additionally maintainable (the knowledge lives in the
contain module, not in a file-static helper in the AI code) and keeps us aligned
with upstream so the next sync does not re-conflict here.

**Residual risk to record, not to block on.** Our helper required
`fromPlayer == toPlayer` for *caves* too; upstream's cave `isContained` checks
only network membership. A captured cave that keeps its cave index could, in
principle, let a passenger exit through an endpoint now controlled by another
player. This is upstream's design decision, applies equally to the upstream
project, and cannot be exercised without game assets. Flagged in section G.

**Mechanics of the resolution:**
1. Take upstream's hunk at all three call sites.
2. Delete `isCaveContainer()` and `isSharedNetworkExitContainer()` from both
   files — they become unreferenced, and AGENTS.md forbids leaving dead code.
3. Remove the `#include "GameLogic/Module/BehaviorModule.h"` we added for
   `isCaveContainer()`, *only if* nothing else in the file needs it.
4. Restore the include block we split when the helpers were inserted between
   `#include "GameLogic/Object.h"` and `#include "GameLogic/PartitionManager.h"`.
5. Keep our surrounding `WWMath::Sqrt*Origin` determinism edits in the same file
   untouched — they are in different functions and must not be reverted.

### D.2 Auto-merged game-logic changes to review, not resolve

| File | Upstream change | Decision |
|---|---|---|
| `{Generals,GeneralsMD}/.../Contain/{Cave,Open,Tunnel}Contain.cpp` + 4 headers | new `isContained` virtual | Accept. Only `OpenContain` derives directly from `ContainModuleInterface` in our tree, so the new pure virtual is satisfied. `OverlordContain` inherits `OpenContain::isContained`. |
| `{Generals,GeneralsMD}/.../Object.cpp` (`a40cdb66d`) | `KINDOF_NO_SELECT` now blocks selection | Accept. Pure gameplay bugfix, no platform surface. |
| `{Generals,GeneralsMD}/.../NeutronMissileSlowDeathUpdate.cpp` (`15b02ffe9`) | debug draw behind `RTS_DEBUG` | Accept. Compiled out of `linux64-deploy`; audit it for determinism anyway (section E). |
| `Core/GameEngine/Source/GameClient/System/ParticleSys.cpp` (`242f5a4c1`) | smudge typing + `PRESERVE_RETAIL_PARTICLES` fixup | Accept, see C.2. |

### D.3 `GameDefines.h` — a policy decision, not a textual conflict

GeneralsX flips every `RETAIL_COMPATIBLE_*` macro to `0`. Upstream adds a *new*
macro `PRESERVE_RETAIL_PARTICLES (1)`. Git will auto-merge, leaving the new macro
at upstream's default of `1`.

- **Decision: keep `1`.** It is not a `RETAIL_COMPATIBLE_*` CRC/determinism
  switch. It gates a compatibility *fixup* — retail's particle templates do not
  set `ParticleType::SMUDGE`, so with the macro off, retail data stops producing
  smudges at all. GeneralsX ships against retail data (AGENTS.md golden rule 6),
  so turning it off would be a visual regression, not a modernisation. Setting it
  to `0` is only correct for a project shipping corrected `.ini` data, which we
  do not.
- Confirm after merge that our seven `0` values survived the auto-merge.

## E. Determinism audit plan (AGENTS.md "Cross-Platform Determinism")

Every incoming line gets checked for the five rules. Concretely, after the merge
and before the build:

1. `grep` the merged diff for bare `sqrt|sqrtf|sin|cos|tan|acos|asin|atan|atan2|ceil|floor|pow`
   outside `WWMath::` and replace with the mandated wrappers.
2. Inspect `W3DScorch.cpp` (`fba44b797`, diffuse colour arithmetic) and
   `NeutronMissileSlowDeathUpdate.cpp` (`15b02ffe9`, radius maths) by hand —
   these are the two commits that do float work.
3. Confirm no new division that can produce NaN/Inf feeds an integer cast.
4. Confirm nothing introduces `-ffast-math`, `/fp:fast`, or an FMA-friendly
   rewrite.
5. `utf8.cpp` is integer/string code only — no FP surface — but confirm it makes
   no locale-dependent call, since `UnicodeString::format_va` already had to be
   patched for POSIX locale behaviour on our side.

## F. Validation plan

Baseline established on the clean pre-merge tree so failures can be attributed:

- `cmake --preset linux64-deploy` from `/work` — **passed** before the merge.
- `cmake --build build/linux64-deploy --target GeneralsXZH GeneralsX` —
  **passed** before the merge (2389/2389, both binaries linked).

After the merge, both are re-run from `/work` (never from the host path; the
`scripts/build/linux/docker-*.sh` scripts discard a `build/<preset>` whose
`CMAKE_HOME_DIRECTORY` is not `/work`). Because the object cache is warm, only
the merged translation units recompile, which makes any new failure directly
attributable to the merge.

Known limits of this environment, stated rather than worked around:

- **macOS configure/build cannot be run.** This is a Linux container. The
  playbook asks for macOS first; that ordering cannot be honoured here and macOS
  validation is deferred to a maintainer.
- **Runtime smoke test cannot be run.** Headless, no display, no Vulkan device,
  and the retail game assets are not in the repository. Playbook step 11 will be
  reported as **not run**, not approximated.
- **`scripts/build/linux/docker-*.sh` cannot be run** — no Docker daemon inside
  the container. The equivalent native `cmake --preset` / `cmake --build` path
  from `/work` is used instead, which is the same preset and the same
  `CMAKE_HOME_DIRECTORY`.

## G. Areas to flag for extra human review

1. `AIUpdate.cpp` exit path in both products — the only real conflict, and it
   deletes GeneralsX code in favour of upstream's. Needs a gameplay test:
   garrison a tunnel network, order an unload from a *different* tunnel.
2. Cave networks under capture — see the residual risk in D.1.
3. `AsciiString::translate` / `UnicodeString::translate` — these now do real
   UTF-8 transcoding instead of the old 7-bit truncation. Anything that round-trips
   through them (map names, player names, GameSpy chat, `.ini`-sourced strings)
   changes behaviour for non-ASCII input. This interacts with our POSIX
   `vswprintf` locale fix in `UnicodeString::format_va`, which is in the same
   file.
4. `WWLib/utf8.cpp` — 283 lines of brand-new hand-rolled decoder entering shared
   engine code, on a 32-bit-`wchar_t` platform upstream does not build for.
5. Smudge/scorch rendering — three of the seven commits touch it, and it is the
   part of the import that is only observable at runtime, which this environment
   cannot reach.

## H. Execution order

1. `git merge thesuperhackers/main` (expected: 2 conflicts).
2. Resolve `Generals/.../AIUpdate.cpp` per D.1; commit progress.
3. Resolve `GeneralsMD/.../AIUpdate.cpp` per D.1; commit progress.
4. Review every auto-merged file per D.2/D.3; fix up what the auto-merge got
   semantically wrong; commit.
5. Determinism audit per E; commit any fixes separately.
6. Assert no `<<<<<<<` / `=======` / `>>>>>>>` anywhere in the tree.
7. Configure + build per F; commit the merge.
8. Update `docs/WORKLOG/2026-08-DIARY.md`.
9. Stop. Do not push — this environment holds a read-only credential and the
   push is the maintainer's decision.

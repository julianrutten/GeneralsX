# Linux Dev Container (`Dockerfile.dev`)

Findings from an audit of the Linux build, and the container tooling that came out of it:
`resources/dockerbuild/Dockerfile.dev`, committed here, and `godmode.yaml`, which lives in the
private `devmastersbv/godmode-env` overlay rather than this repository.

There is no Compose file. This repository runs no services - it is a desktop game, with no
server, no database and nothing to keep up - and the image is named to godmode directly, by
path, through `agent.base_dockerfile`.

> **Status: unbuilt and unverified.** The image was written from repository evidence only.
> The author had no Docker socket and no `docker` CLI, so it has never been built or run, and
> neither has the godmode wiring been applied. The [Build and verify](#5-build-and-verify)
> section is a single pass that a person with Docker can run to settle that.

---

## 1. What the Linux build actually requires

### Trees and targets

| Tree | Meaning | CMake target | Binary |
|---|---|---|---|
| `GeneralsMD/` | Zero Hour - the primary target | `z_generals` | `build/<preset>/GeneralsMD/GeneralsXZH` |
| `Generals/` | base game | `g_generals` | `build/<preset>/Generals/GeneralsX` |
| `Core/` | libraries shared by both | - | - |
| `GeneralsZH/` | game data only (`Data/Window/Menus`), not a code tree | - | - |

Both games build out of one configure. `.github/workflows/build-linux.yml` builds them as a
two-entry matrix over the same preset.

### Presets

`linux64-deploy` is the only real Linux target: **Ninja**, `CMAKE_BUILD_TYPE=RelWithDebInfo`,
`SAGE_USE_SDL3=ON`, `SAGE_USE_OPENAL=ON`, `SAGE_USE_DX8=OFF`, `RTS_BUILD_OPTION_FFMPEG=ON`,
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`. `linux64-openal` is a legacy alias with identical
settings; `linux64-miniaudio` inherits from it and swaps the audio backend.

Two documentation drifts worth knowing:

- **`linux64-testing` does not exist.** `AGENTS.md`, `build.instructions.md` and the
  `workflow_dispatch` choice lists in `build-linux.yml` and `build-linux-flatpak.yml` all
  offer it. There is no such entry in `CMakePresets.json`. Selecting it fails at configure.
- **The CMake floor is 3.25, not 3.20.** `CMakeLists.txt:1` is
  `cmake_minimum_required(VERSION 3.25)` and `CMakePresets.json` is schema `"version": 6`
  with `cmakeMinimumRequired` 3.25.0. `docs/BUILD/LINUX.md` still says 3.20.

The compiler is GCC or Clang with C++20. `cmake/compilers.cmake` adds `-ffp-contract=off`
globally; per `AGENTS.md` that is load-bearing for cross-platform replay determinism and
must not be relaxed.

### The dependency split

This is the part that makes or breaks a build image. Three separate mechanisms are in play,
and only one of them is vcpkg.

**From vcpkg** (`vcpkg.json`, baseline `533a5fda5c0646d1771345fb572e759283444d5f`):
`zlib`, `glm`, `gli`, `stb`, `ffmpeg`, and - on non-Windows only - `freetype`, `fontconfig`,
`openal-soft`, `curl` (with the `ssl` feature). Consumed through
`CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`, set by the hidden
`default-vcpkg` preset, so **`VCPKG_ROOT` must be exported or configure fails**.
`vcpkg-lock.json` pins eight resolved ports. `triplets/` contains only `x86-windows.cmake`
and is irrelevant on Linux; the Linux triplet is the builtin `x64-linux`.

**Built from source at configure time**, needing no distro package of their own but needing
their own build dependencies:

- **SDL3 3.4.2 + SDL3_image 3.4.0** - `cmake/sdl3.cmake` prefers a system SDL3 >= 3.4.0 and
  otherwise fetches and compiles the release tarballs, with X11 and Wayland both on. That is
  why the CI package list is full of `libx*-dev`, `libwayland-dev`, `libdecor-0-dev`,
  `libpipewire-0.3-dev`, `libdbus-1-dev`, `libudev-dev` and friends: they are *SDL's* build
  dependencies, not the game's. `AGENTS.md` states this correctly ("SDL3 from source ...
  no system package needed"); `docs/BUILD/LINUX.md`'s `apt install libsdl3-dev` line is
  stale and would be ignored anyway unless the distro's SDL3 is >= 3.4.0.
- SDL3_image is forced to link the **system shared `libpng`**, explicitly bypassing vcpkg's
  static `libpng16.a` (`find_library(... NO_CMAKE_PATH NO_CMAKE_FIND_ROOT_PATH)`), so
  `libpng-dev` is mandatory. JPEG/TIFF/WebP are vendored inside the SDL3_image tarball,
  which is why CI installs no `-dev` package for them.
- **openal-soft 1.24.2** - `cmake/openal.cmake` tries `find_package(OpenAL)` first on Linux
  and falls back to FetchContent. With the vcpkg toolchain active it normally resolves to
  vcpkg's ALSA-only build, which the module's comment says avoids a SIGSEGV in Debian's
  `libopenal1` 1.25.1.
- `lzhl`, `GameMath` and `gamespy` are fetched from pinned git tags.

**Prebuilt, downloaded, not compiled**: **DXVK**. On Linux `cmake/dx8.cmake` fetches
`dxvk-native-2.6-steamrt-sniper.tar.gz` from the upstream release. Nothing about DXVK needs
a build toolchain on Linux - only the Vulkan loader at link and run time. (The Meson +
MoltenVK source build in that file is the macOS path.)

**From the distribution**, i.e. genuinely required as system `-dev` packages:

- **FFmpeg**: `Core/GameEngineDevice/CMakeLists.txt:305` and its Generals twin do
  `pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil
  libswscale)`. Even though `vcpkg.json` also lists `ffmpeg`, the CI image installs
  `libav*-dev` / `libsw*-dev`, and the pkg-config lookup is what the build depends on.
- **Vulkan**: `libvulkan-dev` at build time, `libvulkan1` plus an ICD at run time.
- **libpng, zlib**, and the whole SDL3 build-dependency set above.
- **Freetype and Fontconfig** are `find_package(... REQUIRED)` in
  `Core/Libraries/Source/WWVegas/WW3D2/CMakeLists.txt` but come from **vcpkg**, not from
  apt - CI installs no `libfreetype-dev` and the build passes. (They also happen to be the
  only two of the game's libraries that were already present in godmode's default agent
  image, which is misleading: their presence there does not mean a build would work.)

### Ground truth is CI, and the CI Linux path is Flatpak

`ci.yml` does **not** call `build-linux.yml`. Its Linux job is `build-linux-flatpak.yml`,
which builds inside the `org.freedesktop.Sdk//25.08` sandbox - so its apt list (`flatpak`,
`flatpak-builder`, `elfutils`) says nothing about the native toolchain.
`build-linux.yml` is still the authority for the **native** build: it is the only place
that installs a native dependency set and then runs `cmake --preset linux64-deploy` and
`cmake --build ... --target z_generals|g_generals` to a verified binary. `Dockerfile.dev`
takes its package list from there, verbatim.

`replay-tests.yml` supplies the rest of a working environment: `p7zip-full`,
`mesa-vulkan-drivers`, `libvulkan1`, `git lfs`, and the headless run environment
(`SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, `DXVK_WSI_DRIVER=SDL3`,
`DXVK_LOG_LEVEL=none`, `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`).

---

## 2. How this relates to the existing `resources/dockerbuild/` images

**Update 20/08/2026: `Dockerfile.linux` is deleted.** It sat alongside `Dockerfile.dev` for
one day and that was one day too many - see section 2b. `generalsx/linux-builder:latest` is
now built from `Dockerfile.dev`, and there is one Linux build image.

| File | What it is | Verdict |
|---|---|---|
| `Dockerfile` | Debian 12 + Wine + Visual Studio 6 portable, driven by `entrypoint.sh`. The legacy VC6 32-bit path. | Untouched. Nothing to do with Linux. |
| `Dockerfile.linux` | was `generalsx/linux-builder:latest`, the native Linux builder. | **Deleted 20/08/2026.** Its tag now points at `Dockerfile.dev`. |
| `Dockerfile.mingw` | `generalsx/mingw-builder:latest`, MinGW-w64 + wine64. | Untouched, and deliberately not folded in. |
| `Dockerfile.dev` | `generalsx/linux-builder:latest` - the one Linux build image, and godmode's agent base. | Native Linux build + dev + agent base. |

### Where `Dockerfile.linux` had drifted from CI

- It pinned `ubuntu:26.04`, a series newer than CI's `ubuntu-latest`. **This is the one that
  reached a user.** See section 2b.
- Its package list contains the `libgles2-mesa-dev` / `libegl1-mesa-dev` transitional
  packages, which are exactly the kind that get dropped between Ubuntu releases.
  `Dockerfile.dev` pins `ubuntu:24.04`.
- It **omits `libvulkan-dev`**, which `build-linux.yml` installs. DXVK links against the
  Vulkan loader.
- It ships no CMake pin - it takes whatever the base image has - and no `clang`, despite
  `README.md` and `DOCKER_WORKFLOW.md` both claiming the image contains "GCC, Clang" and
  "CMake 3.25.0" on "Ubuntu 22.04". All three statements are wrong about the current file.
- Its vcpkg story did not work off the original developer's machine. The image had no
  vcpkg; `docker-configure-linux.sh` and `docker-build-linux-*.sh` bind-mount
  `~/.generalsx/vcpkg` into `/opt/vcpkg` and used to clone into it on first run. Two problems:
  1. that clone was **unpinned** - a bare `git clone https://github.com/microsoft/vcpkg.git`
     with no `checkout`, where CI pins commit `ffc071e0c08432c60c9b64f00334c0227667931b`;
  2. `~/.generalsx/vcpkg` does not exist on a CI or godmode host, and the setup script the
     docs tell you to run for it - `scripts/docker-vcpkg-init.sh`, named in both
     `DOCKER_WORKFLOW.md` and `resources/dockerbuild/README.md` - **is not in the
     repository**.

  Both are fixed: the scripts now seed that mount from the image's pinned `/opt/vcpkg-dist`.
  See "The `/opt/vcpkg` mount conflict" below - it is the one place merging the two images
  actually collided.
- `resources/dockerbuild/README.md` is itself partly corrupted: several sections are
  duplicated and one code fence is spliced mid-sentence ("```bash in Dockerfiles:").

---

## 2b. The `ubuntu:26.04` base was not cosmetic: it shipped unstartable binaries

Reported from a desktop on 20/08/2026, after `./scripts/build/linux/run-linux.sh -win`:

```
/home/.../GeneralsX: /lib/x86_64-linux-gnu/libm.so.6: version `GLIBC_2.43' not found (required by .../GeneralsX)
/home/.../GeneralsX: /lib/x86_64-linux-gnu/libm.so.6: version `GLIBC_2.43' not found (required by .../libSDL3.so.0)
```

The build succeeded. The deploy succeeded. The binary could not `exec`.

glibc symbol versioning is forward-only: a binary that references `GLIBC_x.y` runs on
glibc >= x.y and on nothing older. So **the `FROM` line of the build image is the oldest
distribution the resulting binary will start on** - and `Dockerfile.linux` was
`ubuntu:26.04`, whose glibc is 2.43 (Launchpad, resolute: `2.43-2ubuntu2`). Every
`docker-build-linux-*.sh` used that image, so the build path the repository documents as
recommended produced binaries that only ran on Ubuntu 26.04 and later, while
`docs/HOWTO/INSTALLATION.md` presented 26.04 as a platform the project is *tested* on.

Measured floors, `objdump -T <file> | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`:

| Path | Base | Base glibc | Floor |
|---|---|---|---|
| `Dockerfile.dev` | `ubuntu:24.04` | 2.39 | **2.38** (game binary and the `libSDL3.so.0` built beside it) |
| `Dockerfile.linux` (deleted) | `ubuntu:26.04` | 2.43 | 2.43, per the report above |
| `build-linux.yml` | `ubuntu-24.04` (was `ubuntu-latest`) | 2.39 | 2.38 |
| Flatpak (`build-linux-flatpak.yml`, and what `release.yml` ships) | `org.freedesktop.Sdk//25.08` | 2.42, **inside the runtime** | host glibc irrelevant |

The floor is 2.38 rather than the base's own 2.39 because nothing in the tree references a
symbol first versioned in 2.39; the newest actually referenced are 2.38's
`__isoc23_strtol`/`__isoc23_sscanf` family (GCC 13 emits the C23 variants), `wcslcpy`, and the
re-versioned `fmod`/`fmodf`.

**The released binaries were never affected - measured, not inferred.**

`release.yml`'s Linux assets are `.flatpak` bundles from `build-linux-flatpak.yml`, which
builds inside `org.freedesktop.Sdk//25.08` and runs against `org.freedesktop.Platform//25.08`.
A Flatpak runtime carries its own glibc - 2.42, per `elements/bootstrap/include/glibc-source.yml`
at tag `freedesktop-sdk-25.08.16` - and is shipped inside the bundle, so the host's glibc never
enters into it.

Checked against the actual published artifact rather than left as an argument. Decompressing
the static-delta payload of `GeneralsX-linux.flatpak` from `GeneralsX-Beta-16` (published
2026-08-12) and running `objdump -T` over every ELF inside it:

| Object in the bundle | Floor |
|---|---|
| the game binary (11 MB PIE, RPATH `/run/build/generalsx/...`) | `GLIBC_2.38` |
| `libSDL3.so.0` | `GLIBC_2.38` |
| `libgamespy.so` | `GLIBC_2.38` |
| `libsage_patch.so` | `GLIBC_2.34` |
| `libdxvk_d3d9.so.0` | `GLIBC_2.27` |
| `libdxvk_d3d8.so.0`, `libSDL3_image.so.0` | `GLIBC_2.14` |

Nothing above 2.38, against a runtime that provides 2.42. The bundle's own metadata header
says `runtime=org.freedesktop.Platform/x86_64/25.08`.

`ci.yml` only exercises the Flatpak path too, which is precisely why nothing caught the broken
image: it was on a path CI does not run.

**AppImage is affected in principle.** `scripts/build/linux/build-linux-appimage-*.sh`
explicitly skip `libc.so.*`, `libm.so.*` and `ld-linux*` when bundling, so an AppImage
inherits its build host's floor exactly like a bare binary does. It is only produced by
`build-linux.yml` with `package_format: appimage`, which is `workflow_dispatch`-only and not
part of any release, so no shipped AppImage carries the 2.43 floor - but one built from the
old Docker image would have. The same applies to that workflow's gzip bundle, which skips the
same libraries.

### Guards added

- `build-linux.yml` names `ubuntu-24.04` instead of `ubuntu-latest`, so GitHub migrating that
  label cannot raise the floor silently.
- A *Verify glibc Baseline* step in the same workflow fails the build if the binary or any
  co-built `.so` exceeds `GENERALSX_MAX_GLIBC` (2.38).
- `scripts/build/linux/check-glibc-baseline.sh`, called from both `deploy-linux*.sh`, prints
  the deployed tree's floor and warns when the local machine is older than it.

---

### MinGW

Out of scope, and not nearly free. `Dockerfile.mingw` is a separate ~660 MB toolchain plus
`wine64`, `mingw-w64-i686` is described in `build.instructions.md` as exploratory, and
`windows64-deploy` is not active (issue #29). Folding it in would double the image for a
target this container is not meant to build. Use the existing MinGW image for that.

---

## 3. Design decisions in `Dockerfile.dev`

**Base**: `ubuntu:24.04` - the LTS series `ubuntu-latest` resolved to on 20/08/2026 (verified
against the `actions/runner-images` README; 26.04 was preview-only), so the CI package names
are known to resolve, **and it is the ABI contract for every binary built here** (section 2b).
Must stay glibc Debian/Ubuntu for godmode (below). Anything older than glibc 2.38 - Ubuntu
22.04 LTS, Debian 12, RHEL 9 - is served by the Flatpak, not by lowering this base; lowering it
would move the image off the series CI builds on, which is the drift this consolidation
removes.

**CMake**: pinned to **3.31.6** from Kitware, the same version `resources/dockerbuild/Dockerfile`
already pins via `ARG CMAKE_VERSION`. Pinning removes base-image drift as a failure mode;
the floor is 3.25. Symlinked into `/usr/local/bin`.

**vcpkg: baked, not mounted.** Pinned to CI's `ffc071e0...`, full clone (manifest mode has to
resolve the `vcpkg.json` baseline, which is a different commit).

| | Mounted (`Dockerfile.linux`, deleted) | Baked (`Dockerfile.dev`) |
|---|---|---|
| Image size | ~90 MB | ~600-800 MB larger |
| First run on a fresh host | needs a host-side init that does not exist here | works immediately |
| Version pinning | unpinned clone, drifts from CI | pinned to the CI commit |
| Updating vcpkg | `git pull` on the host | rebuild the image |

For an agent container, "works with zero host-side setup" decides it. Baking also removes the
need for a cache volume: the **binary-cache prewarm is an image layer**, so every container
started from the image already has the compiled ports. What is not persisted is anything
compiled *after* a container starts - a `vcpkg.json` bump, say. Those are rebuilt in each new
container until the image is rebuilt, and rebuilding is the designed path, because the
manifest `COPY` invalidates the prewarm layer exactly when the manifest changes.

**The `/opt/vcpkg` mount conflict.** Every `scripts/build/linux/docker-*.sh` runs
`-v "${VCPKG_DIR:-$HOME/.generalsx/vcpkg}:/opt/vcpkg"`, and a bind mount hides whatever the
image has underneath it. So the moment those scripts started using `Dockerfile.dev`, a baked
clone at `/opt/vcpkg` would have been shadowed by an empty host directory - worse than before,
and `/usr/local/bin/vcpkg` would have dangled inside the container too.

Resolved by baking at **`/opt/vcpkg-dist`** (`ENV VCPKG_ROOT=/opt/vcpkg-dist`) and leaving
`/opt/vcpkg` empty as the scripts' mount point. On first run the scripts `cp -a` the baked
tree into the mount and set `VCPKG_ROOT=/opt/vcpkg`. Why this way round:

- `/opt/vcpkg` is in the scripts' published contract and in the docs; `VCPKG_ROOT` is an
  environment variable nothing else hardcodes. Move the cheaper one.
- `cp -a` under `--user "$(id -u):$(id -g)"` produces caller-owned files, so the seeded `.git`
  is not "dubious ownership" to git and vcpkg can write its `buildtrees`.
- The developer's `~/.generalsx/vcpkg` ends up pinned to CI's commit, which the old unpinned
  clone never was.
- A bare `docker run` with no mount is unchanged: `VCPKG_ROOT` is `/opt/vcpkg-dist` and
  everything works with zero host-side setup, which is what godmode needs.

Cost: a one-time ~1 GB copy the first time a given `VCPKG_DIR` is used. Delete `VCPKG_DIR` to
re-seed after the image is rebuilt with a newer pin.

**Running as a non-root `--user`.** The build scripts always do, and `Dockerfile.dev` ships
three things `Dockerfile.linux` never had, all of which are root-owned by default and all of
which would have broken under `--user`:

- `/ccache` - and unlike the old image this one actually installs `ccache`, which
  `cmake/ccache.cmake` wires in automatically. ccache **fails the compile it is wrapping**
  when it cannot create its cache directory. Now mode 1777.
- `/opt/vcpkg-cache` and the subdirectories the prewarm creates in it - readable but not
  writable, so a new port could never be cached. Now 1777 (directories only; the cached
  payloads only need to be read).
- git's `safe.directory` - vcpkg shells out to git inside `VCPKG_ROOT` to resolve
  `vcpkg.json`'s `builtin-baseline`, and the bind-mounted `/work` hits the same rule from the
  other direction when the agent runs as root. Set to `*`; these are build containers.

**Layering.** No game source is ever `COPY`ed. The layers are: apt -> CMake -> vcpkg clone ->
`COPY vcpkg.json vcpkg-lock.json triplets/` -> binary-cache prewarm. A `.cpp` edit touches
none of them; only a dependency-manifest change invalidates the prewarm.

A root `.dockerignore` was added for this: the build context is the repository root - godmode
passes the worktree as the context for `agent.base_dockerfile`, and the hand build below runs
`docker build ... .` from the root - which is ~500k LOC plus `references/` and `build/`, and
the image needs exactly three paths out of it. It excludes everything and re-includes `vcpkg.json`, `vcpkg-lock.json` and
`triplets/`. The older images are built by `scripts/env/docker/docker-build-images.sh` with
`resources/dockerbuild/` as their context and are unaffected. (`.gitignore` ignores all
dotfiles by default, so `!.dockerignore` had to be added to its allowlist.)

The prewarm (`vcpkg install --x-manifest-root=... --triplet=x64-linux`) is the slow part of
the image build. It is **non-fatal by design** - a cold cache is a slow first configure, not
a broken image - so on failure it writes `/opt/vcpkg-cache/PREWARM_FAILED` instead of
aborting. Check for that file after building. Skip it entirely with
`--build-arg VCPKG_PREWARM=0`.

**Dev environment, not just a builder**: `clang-tidy` (for the root `.clang-tidy` and
`scripts/tooling/clang-tidy/run.py`) against the `compile_commands.json` every preset already
exports; `gdb` for the backtrace recipe in `platform-linux.instructions.md`; `ccache`, picked
up automatically by `cmake/ccache.cmake`; `git-lfs`, `p7zip-full` and `mesa-vulkan-drivers`
for the replay harness; `vulkan-tools` for the "DXVK needs Vulkan" pitfall in `AGENTS.md`.

---

## 4. godmode wiring

`godmode.yaml` is the whole of it, but it does not live in this repository: the authoritative
copy is `julianrutten/GeneralsX/godmode.yaml` in the private `devmastersbv/godmode-env`
overlay, which godmode reads and which takes precedence over anything committed here. What
follows is a copy of its contents, kept here for reference; edit the overlay, not this
repository, if it needs to change:

```yaml
version: 1
name: generalsx

agent:
  base_dockerfile: resources/dockerbuild/Dockerfile.dev
  workdir: /work
```

godmode builds that Dockerfile with the worktree as the build context, tags the result as this
repository's agent base image, and layers its own agent tooling (node, claude, codex, opencode,
tmux, playwright) on top. `base_dockerfile` takes **no build arguments and no separate build
context** - `base_args:` and `base_context:` do not exist and are refused by the decoder - so
the image must build as-is from the repository root. It is also mutually exclusive with
`agent.image` and `agent.base_service`; declaring two is refused.

**What this file may contain.** GeneralsX is a godmode *workspace* repository: it renders no
Compose project. Such a file is validated against a stricter schema that refuses, by name,
every section a repository with no Compose project cannot act on - `slug`, `compose.files`,
`routes`, `scope`, `shared`, `database`, `templates`, `bootstrap`, `health`, `inject`,
`tenancy`, `verify`, `uses`, `logins` and `agent.base_service`. Any one of them fails `up` for
the whole repository. `version` and `name` are required, and `name` must be lowercase
alphanumeric with dashes - `generalsx`, not `GeneralsX`.

Three constraints the image respects, all of them things that have broken before:

1. **glibc Debian/Ubuntu.** The layering step checks for `apt-get` and for
   `getconf GNU_LIBC_VERSION` and fails the whole `up`, naming the repository, if either is
   missing. Never move this image to Alpine or musl.
2. **No reliance on its own `ENTRYPOINT` or `USER`** - godmode resets both. The Dockerfile
   sets only `CMD` and runs as root.
3. **Tooling must be reachable without a login shell.** A Dockerfile `ENV PATH` does not
   survive Debian's `/etc/profile`; this fleet has already lost a Go toolchain that was
   installed, present and unusable that way. `cmake`, `ctest`, `cpack` and `vcpkg` are
   symlinked into `/usr/local/bin`; everything else is a distro package in `/usr/bin`.

(The fourth constraint this document used to carry - "the service must stay up" - is gone with
the Compose file. A workspace repository starts no services, so nothing has to be kept alive
with `sleep infinity`.)

`agent.workdir` is `/work` **deliberately, and it is load-bearing rather than cosmetic**:
every `scripts/build/linux/docker-*.sh` runs the build with `-v "$PWD:/work" -w /work`, and
each of them discards `build/<preset>` when that directory's `CMakeCache.txt` was not
generated with `CMAKE_HOME_DIRECTORY == /work`
(`docker-configure-linux.sh:74`, `docker-build-linux-zh.sh:80`,
`docker-build-linux-generals.sh:80`). Mount the checkout anywhere else and an agent's
configure output is thrown away by the next script run, and the script's by the next agent.
`Dockerfile.dev`'s `WORKDIR` is `/work` for the same reason.

**What the deleted Compose file used to supply, and what replaces it:**

| Compose provided | Now |
|---|---|
| the checkout at `/work` | godmode mounts the worktree at `agent.workdir` |
| named volume for the vcpkg binary cache | not needed for the prewarm, which is an image layer; ports compiled after container start are lost on container recreation (rebuild the image, or mount a volume by hand) |
| named volume for ccache | `/ccache` is container-local. It still pays inside one long-lived container; a recreated container starts cold. Mount `-v generalsx-ccache:/ccache` by hand to keep it |
| headless run env (`SDL_VIDEODRIVER=dummy`, ...) | exported per run, as `replay-tests.yml` does - see the block in section 5 |
| `platform: linux/amd64` | pass `--platform linux/amd64` on a hand build; a Dockerfile cannot pin its own platform |
| `ulimits: nofile` | dropped; it was container hygiene, not derived from any file in this repository |

Applying this is a person's job: `godmode up` / `godmode repo` were deliberately not run.

---

## 5. Build and verify

One pass, from the repository root, on a machine with Docker. No Compose is involved; this is
the same path godmode takes, plus a container to run the build in.

```bash
# 1. Build the image (slow: the vcpkg binary-cache prewarm dominates).
#    The context is the repository root - that is what .dockerignore is trimming - and
#    --platform matters on an Apple Silicon host: the presets target x86_64 only.
docker build --platform linux/amd64 \
    -f resources/dockerbuild/Dockerfile.dev \
    -t generalsx/linux-builder:latest .

# 2. Did the prewarm succeed? Absence of the marker means yes.
docker run --rm generalsx/linux-builder:latest \
    sh -c 'ls /opt/vcpkg-cache/PREWARM_FAILED 2>/dev/null && echo COLD_CACHE || echo PREWARM_OK'

# 3. Toolchain sanity, and the godmode preconditions
docker run --rm generalsx/linux-builder:latest \
    bash -lc 'cmake --version && ninja --version && gcc --version | head -1 \
              && clang-tidy --version | head -2 && vcpkg version | head -1 \
              && echo "VCPKG_ROOT=$VCPKG_ROOT" \
              && getconf GNU_LIBC_VERSION && command -v apt-get'

# 4. Start a long-lived container to work in. The two volumes are optional and only buy
#    persistence across `docker rm`: Docker seeds a fresh named volume from the image
#    content on first mount, so the prewarmed vcpkg cache is preserved rather than shadowed.
docker run -d --name generalsx-dev --platform linux/amd64 \
    -v "$PWD:/work" -w /work \
    -v generalsx-vcpkg-cache:/opt/vcpkg-cache \
    -v generalsx-ccache:/ccache \
    generalsx/linux-builder:latest sleep infinity

# 5. Configure (this is where vcpkg installs the manifest and DXVK/SDL3 are fetched)
docker exec generalsx-dev bash -lc 'cmake --preset linux64-deploy'

# 6. Build both games
docker exec generalsx-dev bash -lc 'cmake --build build/linux64-deploy --target z_generals -j"$(nproc)"'
docker exec generalsx-dev bash -lc 'cmake --build build/linux64-deploy --target g_generals -j"$(nproc)"'

# 7. Verify the artifacts, the same way build-linux.yml does
docker exec generalsx-dev bash -lc '
    file build/linux64-deploy/GeneralsMD/GeneralsXZH
    file build/linux64-deploy/Generals/GeneralsX
    ls -lh build/linux64-deploy/GeneralsMD/GeneralsXZH build/linux64-deploy/Generals/GeneralsX'

# 7b. The glibc floor of what was just built. This is the number that decides which
#     machines the binaries will start on; it must not exceed GENERALSX_MAX_GLIBC in
#     .github/workflows/build-linux.yml (2.38).
docker exec generalsx-dev bash -lc '
    for f in build/linux64-deploy/GeneralsMD/GeneralsXZH \
             build/linux64-deploy/Generals/GeneralsX \
             build/linux64-deploy/_deps/sdl3-build/libSDL3.so.0.*; do
        printf "%-60s %s\n" "$f" \
          "$(objdump -T "$f" | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)"
    done'

# 8. Dev-environment checks: compile_commands.json and clang-tidy on one real file
docker exec generalsx-dev bash -lc '
    test -f build/linux64-deploy/compile_commands.json && echo COMPILE_COMMANDS_OK
    clang-tidy -p build/linux64-deploy --quiet \
        Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp | head -20'

# 9. Software Vulkan is present, which is what the headless replay run needs
docker exec generalsx-dev bash -lc 'ls /usr/share/vulkan/icd.d/ && vulkaninfo --summary | head -20'

# Tear down (add `docker volume rm generalsx-vcpkg-cache generalsx-ccache` to drop the caches)
docker rm -f generalsx-dev
```

Two things to know about step 4:

- The container runs as **root**, so files it writes into the bind-mounted checkout - the
  whole of `build/<preset>` - are root-owned on the host. The repository's own scripts avoid
  that by passing `--user "$(id -u):$(id -g)" -e HOME=/tmp/generalsx-home`; do the same if
  that matters. The cache paths no longer need a `chown` to go with it - `/opt/vcpkg-cache`
  and `/ccache` are mode 1777 in the image for exactly this reason (see "Running as a
  non-root `--user`" above).
- The headless replay environment is **not** baked into the image, on purpose: it would break
  an interactive `run-linux-zh.sh -win` on a machine that does have a display. Export it per
  run, exactly as `replay-tests.yml` does:

  ```bash
  docker exec \
      -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy \
      -e DXVK_WSI_DRIVER=SDL3 -e DXVK_LOG_LEVEL=none \
      -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
      generalsx-dev bash -lc '<replay command>'
  ```

Steps 5-6 take a long time from cold: ~500k LOC plus SDL3, SDL3_image, openal-soft and the
vcpkg manifest. `ccache` and the vcpkg binary cache make the second run much cheaper.

### Least confident about

1. **Whether the prewarm command line is exactly right.** `vcpkg install --x-manifest-root
   --x-install-root --overlay-triplets --triplet` in standalone manifest mode is a documented
   but experimental-prefixed surface, and this is a specific pinned vcpkg commit. If it is
   wrong, step 2 prints `COLD_CACHE` and everything still works, just slowly.
2. **`ubuntu:24.04` vs. whatever `ubuntu-latest` is today.** If GitHub has already moved
   `ubuntu-latest` to 26.04 then CI's package names are being verified on 26.04, not 24.04.
   24.04 is still the safer pin - it is a superset for the transitional mesa `-dev`
   packages - but the exact CI/image equivalence is an inference, not a measurement.
3. **The pinned CMake download.** The URL follows the same pattern `Dockerfile.mingw` uses,
   but it is fetched without a checksum. Adding a SHA-256 pin is a good follow-up.
4. **`nasm`.** Carried over from `Dockerfile.linux`; `build-linux.yml` does not install it.
   The assumed reason is vcpkg's ffmpeg port needing an assembler. If that assumption is
   wrong it is 5 MB of dead weight, not a failure.
5. **Whether `agent.base_dockerfile` builds this file cleanly.** godmode passes no build
   arguments, so `VCPKG_PREWARM` takes its default of `1` and the agent base build carries the
   full prewarm. If that turns out to be too slow for `up`, the fallback is to change the
   `ARG` default in the Dockerfile - there is no way to override it from `godmode.yaml`.
6. **Nothing about Flatpak.** `ci.yml`'s Linux path is `build-linux-flatpak.yml`, and
   `flatpak-builder` inside an unprivileged container needs user namespaces and `bwrap`
   privileges this image does not attempt to arrange. Flatpak packaging remains a CI-only
   and host-only operation.

---

## References

- `.github/workflows/build-linux.yml` - the native Linux dependency list and build steps
- `.github/workflows/replay-tests.yml` - headless replay runtime
- `docs/WORKDIR/support/DOCKER_WORKFLOW.md` - the existing (partly stale) Docker workflow
- `docs/BUILD/LINUX.md` - Linux build instructions
- `resources/dockerbuild/README.md` - the existing images

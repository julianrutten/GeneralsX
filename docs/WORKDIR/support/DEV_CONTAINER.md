# Linux Dev Container (`Dockerfile.dev`)

Findings from an audit of the Linux build, and the container tooling that came out of it:
`resources/dockerbuild/Dockerfile.dev` and the repository-root `godmode.yaml`.

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

The repository already ships container tooling. Nothing here replaces it; the new image is a
corrected and consolidated Linux one that sits alongside.

| File | What it is | Verdict |
|---|---|---|
| `Dockerfile` | Debian 12 + Wine + Visual Studio 6 portable, driven by `entrypoint.sh`. The legacy VC6 32-bit path. | Untouched. Nothing to do with Linux. |
| `Dockerfile.linux` | `generalsx/linux-builder:latest`, the current native Linux builder. | Superseded for dev/agent use; still what `scripts/build/linux/docker-*.sh` invoke. See drift below. |
| `Dockerfile.mingw` | `generalsx/mingw-builder:latest`, MinGW-w64 + wine64. | Untouched, and deliberately not folded in. |
| `Dockerfile.dev` | **new** - `generalsx/linux-dev:latest`. | Native Linux dev + agent base. |

### Where `Dockerfile.linux` has drifted from CI

- It pins `ubuntu:26.04`, a series newer than CI's `ubuntu-latest`. Its package list contains
  the `libgles2-mesa-dev` / `libegl1-mesa-dev` transitional packages, which are exactly the
  kind that get dropped between Ubuntu releases. `Dockerfile.dev` pins `ubuntu:24.04`.
- It **omits `libvulkan-dev`**, which `build-linux.yml` installs. DXVK links against the
  Vulkan loader.
- It ships no CMake pin - it takes whatever the base image has - and no `clang`, despite
  `README.md` and `DOCKER_WORKFLOW.md` both claiming the image contains "GCC, Clang" and
  "CMake 3.25.0" on "Ubuntu 22.04". All three statements are wrong about the current file.
- Its vcpkg story does not work off the original developer's machine. The image has no
  vcpkg; `docker-configure-linux.sh` and `docker-build-linux-*.sh` bind-mount
  `~/.generalsx/vcpkg` into `/opt/vcpkg` and clone into it on first run. Two problems:
  1. that clone is **unpinned** - a bare `git clone https://github.com/microsoft/vcpkg.git`
     with no `checkout`, where CI pins commit `ffc071e0c08432c60c9b64f00334c0227667931b`;
  2. `~/.generalsx/vcpkg` does not exist on a CI or godmode host, and the setup script the
     docs tell you to run for it - `scripts/docker-vcpkg-init.sh`, named in both
     `DOCKER_WORKFLOW.md` and `resources/dockerbuild/README.md` - **is not in the
     repository**.
- `resources/dockerbuild/README.md` is itself partly corrupted: several sections are
  duplicated and one code fence is spliced mid-sentence ("```bash in Dockerfiles:").

### MinGW

Out of scope, and not nearly free. `Dockerfile.mingw` is a separate ~660 MB toolchain plus
`wine64`, `mingw-w64-i686` is described in `build.instructions.md` as exploratory, and
`windows64-deploy` is not active (issue #29). Folding it in would double the image for a
target this container is not meant to build. Use the existing MinGW image for that.

---

## 3. Design decisions in `Dockerfile.dev`

**Base**: `ubuntu:24.04` - the LTS series `ubuntu-latest` resolves to, so the CI package
names are known to resolve. Must stay glibc Debian/Ubuntu for godmode (below).

**CMake**: pinned to **3.31.6** from Kitware, the same version `resources/dockerbuild/Dockerfile`
already pins via `ARG CMAKE_VERSION`. Pinning removes base-image drift as a failure mode;
the floor is 3.25. Symlinked into `/usr/local/bin`.

**vcpkg: baked, not mounted.** Pinned to CI's `ffc071e0...`, full clone (manifest mode has to
resolve the `vcpkg.json` baseline, which is a different commit).

| | Mounted (`Dockerfile.linux` today) | Baked (`Dockerfile.dev`) |
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

`godmode.yaml` at the repository root is the whole of it:

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
    -t generalsx/linux-dev:latest .

# 2. Did the prewarm succeed? Absence of the marker means yes.
docker run --rm generalsx/linux-dev:latest \
    sh -c 'ls /opt/vcpkg-cache/PREWARM_FAILED 2>/dev/null && echo COLD_CACHE || echo PREWARM_OK'

# 3. Toolchain sanity, and the godmode preconditions
docker run --rm generalsx/linux-dev:latest \
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
    generalsx/linux-dev:latest sleep infinity

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
  that matters, but then also `chown` the two cache paths, because `/opt/vcpkg-cache` and
  `/ccache` are root-owned in the image and ccache fails the compile it wraps when it cannot
  write its cache directory.
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

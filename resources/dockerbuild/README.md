# Docker Build Images

This directory contains Dockerfiles for GeneralsX development environments.

## Images

### `Dockerfile.dev`
**Image**: `generalsx/linux-builder:latest` (also `generalsx/linux-dev:latest` by convention -
same image, two tags)
**Purpose**: **the** Linux build image. Used by every `scripts/build/linux/docker-*.sh`, by
`scripts/qa/smoke/docker-smoke-test-zh.sh`, and as the base image godmode layers agent tooling
onto (`agent.base_dockerfile`, in the private `devmastersbv/godmode-env` overlay rather than
this repository).
**Base**: Ubuntu 24.04 (linux/amd64) - the series CI's `ubuntu-latest` resolved to on
2026-08-20, and the one `.github/workflows/build-linux.yml` now names explicitly.
**glibc floor of its output**: `GLIBC_2.38`, measured with `objdump -T` on a `linux64-deploy`
build (see [glibc baseline](#glibc-baseline) below).

**Includes**:
- GCC + Clang, Ninja, CMake 3.31.6 (pinned; the repo floor is 3.25)
- vcpkg **baked into the image at `/opt/vcpkg-dist`**, pinned to the commit `build-linux.yml`
  checks out, with a prewarmed binary cache
- clang-tidy, gdb, ccache, git-lfs, p7zip-full, mesa-vulkan-drivers (headless replay)
- The full `build-linux.yml` package list, including `libvulkan-dev`

**Build** (from the repository **root** - the build context is the repository root, which is
what the root `.dockerignore` trims):
```bash
docker build --platform linux/amd64 \
    -f resources/dockerbuild/Dockerfile.dev \
    -t generalsx/linux-builder:latest .
# or, equivalently:
./scripts/env/docker/docker-build-images.sh linux
```

**Use** (the checkout is bind-mounted; `/work` is not optional - the build scripts discard
`build/<preset>` when its CMake cache was generated anywhere else):
```bash
docker run --rm -v "$PWD:/work" -w /work generalsx/linux-builder:latest \
    bash -lc 'cmake --preset linux64-deploy && cmake --build build/linux64-deploy --target z_generals'
```

There is no Compose file for this image, and it needs none: the repository runs no services.
godmode builds this Dockerfile directly through `agent.base_dockerfile` in `godmode.yaml`,
which is not committed here - it lives in the private `devmastersbv/godmode-env` overlay, at
`julianrutten/GeneralsX/godmode.yaml` - and layers its agent tooling on top.

See [docs/WORKDIR/support/DEV_CONTAINER.md](../../docs/WORKDIR/support/DEV_CONTAINER.md) for
the dependency analysis behind this image, the godmode wiring, and the full
build-and-verify sequence (including how to keep the vcpkg binary cache and ccache across
containers, which the deleted Compose file used to do with named volumes).

#### vcpkg: `/opt/vcpkg-dist` vs `/opt/vcpkg`

The image bakes a pinned vcpkg at **`/opt/vcpkg-dist`** and sets `VCPKG_ROOT` to it, so a bare
`docker run` needs no host-side setup.

`/opt/vcpkg` is left empty on purpose: it is where the build scripts bind-mount
`${VCPKG_DIR:-$HOME/.generalsx/vcpkg}`, and a bind mount hides whatever the image had at that
path. On first run those scripts copy `/opt/vcpkg-dist` into the (host-owned, writable) mount
and set `VCPKG_ROOT=/opt/vcpkg`. Two consequences worth knowing:

- your `~/.generalsx/vcpkg` is now pinned to the same commit CI uses, instead of whatever
  `git clone` produced on the day you first built;
- to pick up a newer pin after rebuilding the image, delete `~/.generalsx/vcpkg` and let the
  next build re-seed it.

#### glibc baseline

glibc symbol versioning is forward-only: a binary that references `GLIBC_x.y` starts on
glibc >= x.y and nowhere else. **The `FROM` line in `Dockerfile.dev` is therefore the oldest
distribution a locally built GeneralsX will run on.** Measured floors:

| Build path | Base | Base glibc | Floor emitted |
|---|---|---|---|
| `Dockerfile.dev` (this image) | `ubuntu:24.04` | 2.39 | **`GLIBC_2.38`** |
| `Dockerfile.linux` (deleted 20/08/2026) | `ubuntu:26.04` | 2.43 | `GLIBC_2.43` |
| `.github/workflows/build-linux.yml` | `ubuntu-24.04` | 2.39 | `GLIBC_2.38`, enforced by the *Verify glibc Baseline* step |
| Flatpak (what the releases ship) | `org.freedesktop.Sdk//25.08` | 2.42, **bundled in the runtime** | host glibc irrelevant |

The `ubuntu:26.04` row is why `Dockerfile.linux` is gone: it made the documented local build
path emit binaries that only start on Ubuntu 26.04 and later.

To check any binary yourself:

```bash
objdump -T build/linux64-deploy/Generals/GeneralsX | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail
# or, over a whole deployed tree:
./scripts/build/linux/check-glibc-baseline.sh --dir ~/GeneralsX/Generals
```

> **Note**: parts of the rest of this README are stale - `scripts/docker-vcpkg-init.sh`, which
> some sections tell you to run, does not exist in this repository.

### `Dockerfile.mingw`
**Image**: `generalsx/mingw-builder:latest`  
**Purpose**: Windows .exe cross-compilation (MinGW-w64)  
**Base**: Ubuntu 22.04 (linux/amd64)  
**Size**: ~660MB

**Includes**:
- MinGW-w64 (i686 + x86_64 targets)
- CMake 3.25.0
- Ninja, Git
- Wine64 (for testing Windows .exe)

**Note**: vcpkg NOT needed for MinGW builds (uses system libraries)

**Build**:
```bash
docker build -t generalsx/mingw-builder:latest -f Dockerfile.mingw .
# Or use: ./scripts/docker-build-images.sh mingw
```

## First-Time Setup

None. The image bakes a pinned vcpkg, so `./scripts/build/linux/docker-build-linux-zh.sh`
works on a machine that has never built this project. (There used to be a
`scripts/docker-vcpkg-init.sh` step here; that file does not exist in this repository.)

`~/.generalsx/vcpkg` is still bind-mounted at `/opt/vcpkg`, and the build scripts seed it from
the image's `/opt/vcpkg-dist` on first use, so it holds the same vcpkg commit CI does. It
persists vcpkg's package cache between builds.

**vcpkg location**

Default `~/.generalsx/vcpkg`; override with `VCPKG_DIR`:
```bash
export VCPKG_DIR="/custom/path/to/vcpkg"
```

### Updating vcpkg

The pin lives in `ARG VCPKG_COMMIT` in `Dockerfile.dev` and in the "Bootstrap vcpkg" step of
`.github/workflows/build-linux.yml`; change both together, then:
```bash
./scripts/env/docker/docker-build-images.sh linux   # rebuild the image
rm -rf ~/.generalsx/vcpkg                           # drop the old seed
```
The next build re-seeds from the image. Running `git pull` inside `~/.generalsx/vcpkg`
instead will silently put you back on an unpinned vcpkg - that drift is what the pin is for.

### Persists vcpkg package cache between builds

### Automated (Recommended)
Build scripts automatically check for images and vcpkg, build/initialize
### Automated (Recommended)
Build scripts automatically check for images and build if missing:
```bash
./scripts/docker-build-linux-zh.sh  # Auto-uses generalsx/linux-builder
./scripts/docker-build-mingw-zh.sh  # Auto-uses generalsx/mingw-builder
```

### Manual
Build images explicitly:
```bash in Dockerfiles:
```dockerfile
# Current (3.25.0)
RUN curl -sL https://github.com/Kitware/CMake/releases/download/v3.25.0/cmake-3.25.0-linux-x86_64.tar.gz | tar -xz ...

# New version (e.g., 3.28.0)
RUN curl -sL https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-x86_64.tar.gz | tar -xz ...
```

Then rebuild:
```bash
./scripts/docker-build-images.sh all
```dockerfile
# Current (3.25.0)
RUN curl -sL https://github.com/Kitware/CMake/releases/download/v3.25.0/cmake-3.25.0-linux-x86_64.tar.gz | tar -xz ...

# New version (e.g., 3.28.0)
RUN curl -sL https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-x86_64.tar.gz | tar -xz ...
```

Then rebuild:
```bash
./scripts/docker-build-images.sh all
```

### Updating vcpkg
vcpkg is cloned from GitHub at build time (always latest). To update:
```bash
# Just rebuild the image
./scripts/docker-build-images.sh linux

# Or manually update in running container
docker run --rm -it generalsx/linux-builder:latest bash
cd /opt/vcpkg
git pull
./bootstrap-vcpkg.sh -disableMetrics
```

## Troubleshooting

### Image Not Found
If build scripts complain about missing image:
```bash
./scripts/docker-build-images.sh all
```

### Image Taking Too Much Space
```bash
# Remove old images
docker rmi generalsx/linux-builder:latest
docker rmi generalsx/mingw-builder:latest

# Rebuild
./scvcpkg Not Found
If build scripts complain about vcpkg:
```bash
./scripts/docker-vcpkg-init.sh
```

Or check if it exists:
```bash
ls -la ~/.generalsx/vcpkg
```

### vcpkg Baseline Errors
If you see git baseline errors, vcpkg might be corrupted:
```bash
# Re-initialize
rm -rf ~/.generalsx/vcpkg
./scripts/docker-vcpkg-init.sh
```

### ripts/docker-build-images.sh all
```

### Full Cleanup
```bash
# WARNING: Removes ALL unused Docker data
docker system prune --all --volumes

# Rebuild GeneralsX images
./scripts/docker-build-images.sh all
```

## VS Code Tasks

Tasks available in VS Code (Cmd+Shift+P → "Tasks: Run Task"):
- **Docker: Build Images (All)** - Build both images
- **Docker: Build Linux Builder Image** - Build Linux only
- **Docker: Build MinGW Builder Image** - Build MinGW only

## References

- Documentation: `docs/WORKDIR/support/DOCKER_WORKFLOW.md`
- Build scripts: `scripts/docker-*.sh`
- Tasks: `.vscode/tasks.json`

---

**Note**: Docker layer caching means rebuilds are fast—only changed layers are rebuilt!

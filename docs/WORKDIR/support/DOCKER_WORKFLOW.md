# Docker Build Workflow for GeneralsX

This document describes the enhanced Docker workflow using pre-built images and vcpkg volumes for efficient builds.

## Overview

All Docker-based builds use:
1. **Pre-built images** - Base systems with compilers, CMake, build tools, and (for Linux)
   a pinned vcpkg baked in at `/opt/vcpkg-dist`
2. **vcpkg volume mount** - Local `~/.generalsx/vcpkg` mounted into containers at
   `/opt/vcpkg`, seeded from the image's baked copy on first use

This persists the package cache across builds while keeping every developer on the same
vcpkg commit CI uses.

## Docker Images

### Linux Native Builder (`generalsx/linux-builder:latest`)
- **Dockerfile**: `resources/dockerbuild/Dockerfile.dev` - the same image godmode uses as its
  agent base. `Dockerfile.linux` was deleted on 20/08/2026; see the glibc note below.
- **Base**: Ubuntu 24.04 (linux/amd64) - the series `.github/workflows/build-linux.yml` builds on
- **Toolchain**: GCC, Clang, Ninja, ccache, clang-tidy, gdb
- **CMake**: 3.31.6 (pinned from Kitware)
- **vcpkg**: baked at `/opt/vcpkg-dist`, pinned to CI's commit, with a prewarmed binary cache;
  seeded into the `~/.generalsx/vcpkg` mount on first run
- **Purpose**: Native Linux ELF binaries (GeneralsX, GeneralsXZH)
- **Size**: ~2-3 GB (the baked vcpkg and its prewarmed cache dominate)

> **glibc baseline.** The image's `FROM` line is the ABI contract for everything it builds:
> glibc symbol versioning is forward-only, so a binary linked against glibc `x.y` runs on
> `x.y` or newer and nowhere else. On `ubuntu:24.04` the measured floor is **`GLIBC_2.38`**
> (`objdump -T build/linux64-deploy/Generals/GeneralsX | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1`).
> The deleted `Dockerfile.linux` was `ubuntu:26.04`, whose floor was `GLIBC_2.43` - binaries
> that started on nothing older than Ubuntu 26.04. Do not bump the base without deciding, on
> purpose, to drop every distribution below the new one.

### MinGW Cross-Compiler (`generalsx/mingw-builder:latest`)
- **Base**: Ubuntu 22.04 (linux/amd64)
- **Toolchain**: MinGW-w64 (i686 and x86_64)
- **CMake**: 3.25.0
- **Wine64**: For Windows .exe testing (optional)
- **vcpkg**: NOT needed (MinGW uses system libraries)
- **Purpose**: Windows .exe binaries (cross-compiled on Linux)
- **Size**: ~660MB

## Initial Setup

### 1. Build Docker Images (One-Time)

```bash
# Build all images (recommended first time)
./scripts/docker-build-images.sh all

# Or build individually
./scripts/docker-build-images.sh linux
./scripts/docker-build-images.sh mingw
```

**Time**: ~2-3 minutes (images are lightweight now!)

**Result**: Two local Docker images ready for instant use

### 2. Initialize vcpkg (One-Time)

vcpkg is stored locally at `~/.generalsx/vcpkg` and mounted into containers:

```bash
# Option 1: Automatic (recommended)
# Build scripts auto-initialize on first run
./scripts/docker-build-linux-zh.sh  # Will run init if needed

# Option 2: Manual (if you want to set up ahead of time)
./scripts/docker-vcpkg-init.sh  # ~2-5 minutes
```

**What happens**:
- vcpkg cloned to `~/.generalsx/vcpkg` (full clone for baseline commits)
- Bootstrap script compiles vcpkg binary
- Scripts mount this directory as volume at `/opt/vcpkg` in container

**Benefits**:
- ✅ Image stays small (~90MB vs ~328MB)
- ✅ vcpkg shared across all builds (no duplication)
- ✅ Easy to update: `cd ~/.generalsx/vcpkg && git pull && ./bootstrap-vcpkg.sh`
- ✅ Persists vcpkg package cache between builds

### 3. Verify Setup

```bash
# Check images
docker images | grep generalsx
# Expected:
# generalsx/linux-builder:latest   ~2-3GB (baked vcpkg + prewarmed binary cache)
# generalsx/mingw-builder:latest   ~660MB

# Check vcpkg
ls -la ~/.generalsx/vcpkg
# Expected: vcpkg binary, .git/, ports/, scripts/, etc.
```

## Usage

### Automatic Detection

All build scripts **automatically check** for images and vcpkg:

- If image missing → Build it automatically
- If vcpkg missing → Initialize it automatically

**Example**:
```bash
# First run: Builds image + initializes vcpkg + compiles
./scripts/docker-build-linux-zh.sh

# Subsequent runs: Just compiles (fast!)
./scripts/docker-build-linux-zh.sh
```

### Build Commands

#### Linux Native Builds

```bash
# Configure (optional, build scripts auto-configure)
./scripts/docker-configure-linux.sh linux64-deploy

# Build Zero Hour (linux64-deploy preset)
./scripts/docker-build-linux-zh.sh

# Build Generals base game
./scripts/docker-build-linux-generals.sh
```

**Output**: Native Linux ELF binaries
- `build/linux64-deploy/GeneralsMD/GeneralsXZH`
- `build/linux64-deploy/Generals/GeneralsX`

#### Windows MinGW Cross-Compile

```bash
# Build Zero Hour (mingw-w64-i686 preset)
./scripts/docker-build-mingw-zh.sh

# Or specify preset explicitly
./scripts/docker-build-mingw-zh.sh mingw-w64-i686
```

**Output**: Windows .exe binaries
- `build/mingw-w64-i686/GeneralsMD/GeneralsXZH.exe`

### Testing

```bash
# Smoke test Linux binary (checks initialization)
./scripts/docker-smoke-test-zh.sh
```

## Performance Comparison

### Before (No Pre-built Images)
```
Time per build:
- Package installation: ~2-3 minutes
- vcpkg bootstrap:      ~2-5 minutes
- CMake configure:      ~1-2 minutes
- Actual compilation:   ~5-10 minutes
──────────────────────────────────────
Total:                  ~10-20 minutes
```

### After (With Pre-built Images + vcpkg Volume)
```
Time per build (first run):
- Image check:          <1 second
- vcpkg init:           ~2-5 minutes (one-time)
- CMake configure:      ~1-2 minutes
- Actual compilation:   ~5-10 minutes
──────────────────────────────────────
Total:                  ~8-18 minutes (first run)

Time per build (subsequent):
- Image check:          <1 second
- vcpkg check:          <1 second
- CMake configure:      ~1-2 minutes (uses vcpkg cache)
- Actual compilation:   ~5-10 minutes
──────────────────────────────────────
Total:                  ~6-12 minutes

Savings: 40-50% faster! 🚀
Bonus: Image size reduced ~70% (328MB → 90MB)
```

## Rebuilding Images

### When to Rebuild

Rebuild images when:
- Upgrading CMake version
- Adding new build tools
- Changing Ubuntu base version

**Note**: vcpkg updates don't require image rebuild (it's a volume!)

### How to Rebuild

```bash
# Force rebuild all images
./scripts/docker-build-images.sh all

# Rebuild specific image
./scripts/docker-build-images.sh linux
```

**Time**: ~2-3 minutes (Docker uses layer caching)

### Updating vcpkg

```bash
# Update vcpkg (no image rebuild needed!)
cd ~/.generalsx/vcpkg
git pull
./bootstrap-vcpkg.sh -disableMetrics
```

Or re-initialize:
```bash
rm -rf ~/.generalsx/vcpkg
./scripts/docker-vcpkg-init.sh
```

## Troubleshooting

### vcpkg Not Found

If you see:
```
Error: vcpkg directory not found: ~/.generalsx/vcpkg
```

**Solution**: Initialize vcpkg:
```bash
./scripts/docker-vcpkg-init.sh
```

Or check if it exists:
```bash
ls -la ~/.generalsx/vcpkg
```

### vcpkg Baseline Errors

If vcpkg fails with git baseline errors:
```bash
# Re-initialize (fixes corruption)
rm -rf ~/.generalsx/vcpkg
./scripts/docker-vcpkg-init.sh
```

### Image Not Found

If you see:
```
Error: Cannot find image 'generalsx/linux-builder:latest'
```

**Solution**: Build the image:
```bash
./scripts/docker-build-images.sh linux
```

Or let the build script do it automatically.

### Old Images Taking Space

```bash
# Remove old images
docker rmi generalsx/linux-builder:latest
docker rmi generalsx/mingw-builder:latest

# Rebuild
./scripts/docker-build-images.sh all
```

### vcpkg Taking Too Much Space

```bash
# Check size
du -sh ~/.generalsx/vcpkg
# Expected: ~200-500MB (includes git history + package cache)

# Clean package cache (keeps git repo)
rm -rf ~/.generalsx/vcpkg/buildtrees
rm -rf ~/.generalsx/vcpkg/packages
rm -rf ~/.generalsx/vcpkg/downloads

# Full cleanup and reinstall
rm -rf ~/.generalsx/vcpkg
./scripts/docker-vcpkg-init.sh
```

### Full Docker Cleanup

If Docker is taking too much space:
```bash
# WARNING: Removes ALL unused images/containers/volumes
docker system prune --all --volumes

# Then rebuild GeneralsX images and reinit vcpkg
./scripts/docker-build-images.sh all
./scripts/docker-vcpkg-init.sh
```

## Advanced Usage

### Using Images Directly

You can use the images for custom workflows:

```bash
# Interactive shell in Linux builder
docker run --rm -it \
    -v "$PWD:/work" \
    -v "$HOME/.generalsx/vcpkg:/opt/vcpkg" \
    -w /work \
    generalsx/linux-builder:latest bash

# Run custom CMake command
docker run --rm \
    -v "$PWD:/work" \
    -v "$HOME/.generalsx/vcpkg:/opt/vcpkg" \
    -w /work \
    generalsx/linux-builder:latest \
    cmake --build build/linux64-deploy --target some_custom_target
```

### Customizing Dockerfiles

Edit the Dockerfiles in `resources/dockerbuild/`:
- `Dockerfile.dev` - Linux native builder **and** godmode's agent base. One image; changing
  its `FROM` line changes the glibc floor of every locally built binary.
- `Dockerfile.mingw` - MinGW cross-compiler

Then rebuild:
```bash
./scripts/env/docker/docker-build-images.sh all
```

### Using Different vcpkg Location

```bash
# Set custom location in scripts
export VCPKG_DIR="/custom/path/to/vcpkg"
./scripts/docker-build-linux-zh.sh
```

## Integration with VS Code Tasks

VS Code tasks automatically use these Docker workflows:

- **Docker: Build Images (All)** → Build both images
- **Docker: Build Linux Builder Image** → Build Linux only
- **Docker: Build MinGW Builder Image** → Build MinGW only
- **Configure (Linux Docker)** → Uses `generalsx/linux-builder`
- **Build GeneralsXZH (Linux Docker)** → Uses `generalsx/linux-builder`
- **Build Windows (MinGW Docker)** → Uses `generalsx/mingw-builder`

Just run tasks from VS Code; image and vcpkg management is automatic.

## References

- Dockerfiles: `resources/dockerbuild/`
- Build scripts: `scripts/docker-*.sh`
- vcpkg init: `scripts/docker-vcpkg-init.sh`
- Image build logs: `logs/` (created during build)

---

**Tip**: After initial setup, you never need to think about Docker images or vcpkg—scripts handle everything automatically!

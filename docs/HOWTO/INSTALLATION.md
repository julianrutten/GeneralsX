# How to Install GeneralsX

## Prerequisites

1. You must own a legitimate copy of the game. We build and test against the [Steam version](https://store.steampowered.com/app/2732960/Command__Conquer_Generals_Zero_Hour/). Other retail releases may work, but are not officially supported.

   > **On macOS or Linux?** This title is Windows-only on Steam. On macOS, Steam usually does not show an install option. On Linux, installation may be available via Steam Play/Proton depending on your configuration. See [GETTING_THE_GAME_FILES.md](GETTING_THE_GAME_FILES.md) for all supported ways to obtain game files.

2. Copy the game data from your existing installation (for example, from `C:\Program Files (x86)\Steam\steamapps\common\Command & Conquer Generals - Zero Hour`) to a local folder on your Linux or macOS system. A recommended layout is:
   - `$HOME/GeneralsX/Generals` for Command & Conquer: Generals
   - `$HOME/GeneralsX/GeneralsZH` for Command & Conquer: Generals - Zero Hour

## Linux

1. Install Flatpak for your distribution by following the official setup guide:

   https://flatpak.org/setup/

   Each Linux distribution packages Flatpak differently, so rely on the upstream instructions for installing the Flatpak tool itself.

2. Download the Linux Flatpak release asset (`GeneralsXZH-linux-flatpak.zip` for Zero Hour, `GeneralsX-linux-flatpak.zip` for the base game) and extract it. Each zip contains a single `.flatpak` bundle at the root of the archive.

3. Install the Flatpak bundle:

   ```bash
   unzip GeneralsXZH-linux-flatpak.zip
   flatpak --user install -y ./GeneralsXZH-linux64-deploy.flatpak
   ```

   For the base game, use `GeneralsX-linux-flatpak.zip` and install `./GeneralsX-linux64-deploy.flatpak`.

4. Launch the game with Flatpak:

   ```bash
   flatpak run com.fbraz3.GeneralsXZH
   ```

   For the base game:

   ```bash
   flatpak run com.fbraz3.GeneralsX
   ```

5. The Flatpak package auto-detects game data in the following default locations:
   - `$HOME/GeneralsX/GeneralsZH`
   - `$HOME/GeneralsX/Generals`

   If your assets are stored elsewhere, pass the path explicitly when launching:

   ```bash
   flatpak run --env=CNC_GENERALS_INSTALLPATH=/path/to/your/game-data com.fbraz3.GeneralsXZH -win
   ```

   The same `CNC_GENERALS_INSTALLPATH` override works for the base game package.

6. The Flatpak bundle ships the required userspace runtime libraries (DXVK, SDL3, SDL3_image, OpenAL, FFmpeg, and related dependencies). You do not need to install those libraries manually on the host.

> **GPU Driver note**: Vulkan support must be provided by your host GPU driver. For NVIDIA use the proprietary driver, for AMD/Intel use Mesa 21+. The Flatpak bundle does not ship GPU drivers.

## macOS

1. Download the macOS `.zip` file from this release.
2. Extract the `.zip` and copy the app bundle into your `Applications` folder.
3. Make sure your game assets are placed in the following locations:
   - `$HOME/GeneralsX/Generals` for Generals
   - `$HOME/GeneralsX/GeneralsZH` for Zero Hour
4. Because the app is not code-signed, macOS Gatekeeper will initially block it. After the first launch attempt, go to **System Settings -> Privacy & Security** and allow the application to run.

## Requirements

GeneralsX has been developed and tested primarily on the following environments:

- Ubuntu 26.04 LTS (x86_64)
- macOS 26 "Tahoe" on Apple Silicon (M1 / ARM64)

Other Linux distributions or macOS versions may work, but you may need to install additional dependencies manually. On some systems, certain libraries might need to be built from source.

### Linux: what the Flatpak actually requires

The released Linux packages are Flatpak bundles, and a Flatpak carries its own runtime -
`org.freedesktop.Platform` 25.08, including its own glibc 2.42. **Your distribution's glibc
does not matter.** What you need is:

- x86_64
- Flatpak itself (any reasonably current version - see https://flatpak.org/setup/)
- a working Vulkan driver from your host GPU stack (see the GPU driver note above)

Ubuntu 26.04 is where the project is developed and where the numbers below were measured; it
is not a minimum for the Flatpak.

### Linux: what a *self-built* binary requires

Building from source is a different story, because a native build links against the build
machine's glibc and glibc symbol versioning is forward-only - a binary that references
`GLIBC_x.y` starts on glibc >= x.y and nowhere else.

| How it was built | Minimum glibc | Roughly |
|---|---|---|
| `scripts/build/linux/docker-build-linux-*.sh` (Docker image, `ubuntu:24.04`) | **2.38** | Ubuntu 24.04 LTS+, Debian 13+, Fedora 39+, RHEL 10+, current rolling distros |
| Directly on your own machine | your machine's glibc | anything at least as new as your machine |
| `.github/workflows/build-linux.yml` artifacts (`ubuntu-24.04` runner) | **2.38** | as above; the workflow fails if a build exceeds it |

`scripts/build/linux/deploy-linux.sh` prints the floor of what it just deployed and warns if
this machine is older, and you can check any binary directly:

```bash
objdump -T ~/GeneralsX/Generals/GeneralsX | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
```

If your distribution is older than that - Ubuntu 22.04 LTS (glibc 2.35), Debian 12 (2.36),
RHEL 9 (2.34) - install the Flatpak instead of building natively.

## Tested Platforms

Current development and test matrix:

| Platform           | Architecture           | Status  |
|--------------------|------------------------|---------|
| Ubuntu 26.04 LTS   | x86_64                 | Working |
| macOS 26 "Tahoe"   | ARM64 (Apple Silicon)  | Working |

These are the platforms the project is developed on, not minimum versions; for Linux, see
"what the Flatpak actually requires" above.

Support for other platforms and configurations is possible but not yet officially tested.

## Multiplayes features

- LAN play - it's broken for now and we have a [issue](https://github.com/fbraz3/GeneralsX/issues/86) to work on that.
- Online features - not implemented and planned for the future.

## Known Issues & Limitations

For documented limitations and known bugs, check the [issues page](https://github.com/fbraz3/GeneralsX/issues).

If you encounter problems while running the game, please [open an issue](https://github.com/fbraz3/GeneralsX/issues/new/choose) and include as much detail as possible, such as:

- Operating system and version
- CPU architecture (x86_64 / ARM64)
- Steps to reproduce the issue
- Logs or terminal output (if available)

This information greatly helps us reproduce and fix issues.

## Contributing

Contributions of all sizes are welcome.

If you are interested in helping with development, bug fixes, testing on additional platforms, or improving compatibility, feel free to open a pull request or start a discussion in the repository.

Even small contributions, such as testing, documentation improvements, or well-documented bug reports, are very valuable.

## Credits

This project exists thanks to the Command & Conquer community and the many tools created around the game over the years.

Special thanks to everyone who has contributed time to testing, debugging, and development.

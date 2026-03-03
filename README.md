<div id="top"></div>

<img src="https://i.imgur.com/4FlvRAt.png" width="200">

# DeroGold

<summary>Table of Contents</summary>
<ol>
  <li><a href="#development-resources">Development Resources</a></li>
  <li><a href="#introduction">Introduction</a></li>
  <li><a href="#installing">Installing</a></li>
  <li><a href="#docker-images">Docker Images</a></li>
  <li><a href="#build-instructions">Build Instructions</a></li>
  <ol>
    <li><a href="#windows-x64-only">Windows (x64 only)</a></li>
    <ol>
      <li><a href="#msvc-visual-studio-2022">MSVC (Visual Studio 2022)</a></li>
      <li><a href="#mingw64-msys2">MINGW64 (msys2)</a></li>
      <li><a href="#clang64-msys2">CLANG64 (msys2)</a></li>
    </ol>
    <li><a href="#linux-x64aarch64">Linux (x64/aarch64)</a></li>
    <ol>
      <li><a href="#gcc">GCC</a></li>
      <li><a href="#clang">CLANG</a></li>
    </ol>
    <li><a href="#macos-x64-only">MacOS (x64 only)</a></li>
    <ol>
      <li><a href="#clang-1">CLANG</a></li>
    </ol>
  </ol>
  <li><a href="#fast-sync---sync-from-height">Fast Sync (--sync-from-height)</a></li>
  <li><a href="#license">License</a></li>
  <li><a href="#thanks">Thanks</a></li>
</ol>

## Development Resources

* Web: https://derogold.com/
* GitHub: https://github.com/jianmingyong/derogold
* Discord: https://discordapp.com/invite/j2aSNFn

<p align="right">(<a href="#top">back to top</a>)</p>

## Introduction

DeroGold is a digital assets project focused on preserving our life environment here on Earth.

DeroGold aspires to solve problems such as circular economy in recycling, re-use of waste materials and how we can drive positive behaviour by rewarding people with digital assets for recycling. And build habitable floating islands.

For simplicity, we say we are the digital "Nectar Card for Recycling".

However, we are much more than that. We run our own privacy digital asset that allows people and organisations to send and receive our native digital coins called DEGO.

<p align="right">(<a href="#top">back to top</a>)</p>

## Installing

We offer binary images of the latest releases here: https://github.com/jianmingyong/derogold/releases

If you would like to compile yourself, read on.

<p align="right">(<a href="#top">back to top</a>)</p>

## Docker Images

We offer docker images of the latest releases here: https://hub.docker.com/r/jianmingyong/derogold/tags

By default, the images use Ubuntu 20.04 LTS as a base image when build.

Supported tags:
- latest, 0.7.2.3

<p align="right">(<a href="#top">back to top</a>)</p>

## Build Instructions

The CMake build system will, by default, create optimized *native* builds for your particular system type when you build the software. Using this method, the binaries created provide a better experience and all together faster performance.

However, if you wish to create *portable* binaries that can be shared between systems, specify `-DARCH=default` in your CMake arguments during the build process. Note that *portable* binaries will have a noticeable difference in performance than *native* binaries. For this reason, it is always best to build for your particular system if possible.

Note that the instructions below create *native* binaries.

### Windows (x64 only)

#### MSVC (Visual Studio 2022)

Prerequisites:
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/downloads/#visual-studio-community-2022)
  - Note: This installs the full IDE which may take up more space. If you want a more lightweight installation, install [Build Tools for Visual Studio 2022](https://aka.ms/vs/17/release/vs_BuildTools.exe) instead.
  - In the installer, under `Workloads` tab, select `Desktop development with C++`
    - Ensure that under the optional component, the following is checked:
    - `MSVC v143 - VS 2022 C++ x64/x86 build tools (latest)`
    - `Windows 11 SDK (10.0.22621.0)` - Pick the latest one if possible.
    - `C++ CMake tools for Windows`
- [Build Tools for Visual Studio 2022](https://aka.ms/vs/17/release/vs_BuildTools.exe)
  - Note: You are not required to install this if you have [Visual Studio Community/Professional/Enterprise 2022](https://visualstudio.microsoft.com/downloads/#visual-studio-community-2022) installed.
  - In the installer, under `Workloads` tab, select `Desktop development with C++`
    - Ensure that under the optional component, the following is checked:
      - `MSVC v143 - VS 2022 C++ x64/x86 build tools (latest)`
      - `Windows 11 SDK (10.0.22621.0)` - Pick the latest one if possible.
      - `C++ CMake tools for Windows`
- [Git](https://git-scm.com/downloads)
  - Skip this if you have installed `Git For Windows` in the Visual Studio installer
  - If the development console could not find git, you can always install this to resolve the issue.
- [CMake](https://cmake.org/download/)
  - Skip this if you have installed `C++ CMake tools for Windows` in the Visual Studio installer
  - If the development console could not find cmake, you can always install this to resolve the issue.

Building:
- From the start menu, open 'x64 Native Tools Command Prompt for VS 2022'
- If you need to change the default drive C: to D: for example, just type `D:` and hit enter
- Use `cd` to change to your desired directory to store DeroGold code
- `git clone -b development --recursive https://github.com/derogold/derogold.git`
- `cd derogold`
- `cmake --preset windows-x64-msvc-install`
- `cmake --build --preset windows-x64-msvc-install`
- Enjoy your build at `build/bin`

Alternatively:
- `cd derogold`
- `cmake -D VCPKG_TARGET_TRIPLET=x64-windows-static-release -D CMAKE_INSTALL_PREFIX=build -G Visual Studio 17 2022 -T host=x64 -A x64 -S . -B build`
- `cmake --build build -t INSTALL --config Release`

<p align="right">(<a href="#top">back to top</a>)</p>

#### MINGW64 (msys2)

Prerequisites:
- [MSYS2](https://www.msys2.org/)
  - Open `MSYS2 MINGW64`
  - Run `pacman -Syu` twice (to ensure all packages are updated) The window will close the first time
  - Run `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-ccache git`

Building:
- `git clone -b development --recursive https://github.com/derogold/derogold.git`
- `cd derogold`
- `cmake --preset windows-x64-mingw-gcc-install`
- `cmake --build --preset windows-x64-mingw-gcc-install`
- Enjoy your build at `build/bin`

Alternatively:
- `cd derogold`
- `CC=gcc CXX=g++ cmake -D VCPKG_TARGET_TRIPLET=x64-mingw-static-release -D CMAKE_INSTALL_PREFIX=build -G Ninja -S . -B build`
- `cmake --build build -t install`

<p align="right">(<a href="#top">back to top</a>)</p>

#### CLANG64 (msys2)

Prerequisites:
- [MSYS2](https://www.msys2.org/)
  - Open `MSYS2 CLANG64`
  - Run `pacman -Syu` twice (to ensure all packages are updated) The window will close the first time
  - Run `pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-ccache git`

Building:
- `git clone -b development --recursive https://github.com/derogold/derogold.git`
- `cd derogold`
- `cmake --preset windows-x64-mingw-clang-install`
- `cmake --build --preset windows-x64-mingw-clang-install`
- Enjoy your build at `build/bin`

Alternatively:
- `cd derogold`
- `CC=clang CXX=clang++ cmake -D VCPKG_TARGET_TRIPLET=x64-mingw-static-release -D CMAKE_INSTALL_PREFIX=build -G Ninja -S . -B build`
- `cmake --build build -t install`

<p align="right">(<a href="#top">back to top</a>)</p>

### Linux (x64/aarch64)

#### GCC

Prerequisites:
- For Ubuntu: `sudo apt-get install git cmake ninja-build build-essential curl zip unzip tar pkg-config`
- (Optional) `sudo apt-get install crossbuild-essential-arm64` for cross building aarch64 target

**NOTE: Ubuntu 20.04 LTS provides an outdated version of CMake hence it does not support presets.**

Building via CLI:
```bash
#########################
# For native x64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold
CC=gcc CXX=g++ cmake -D VCPKG_TARGET_TRIPLET=x64-linux-release -G Ninja -S . -B build
cmake --build build
sudo cmake --install build

#########################
# For native arm64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold
CC=gcc CXX=g++ cmake -D VCPKG_TARGET_TRIPLET=arm64-linux-release -G Ninja -S . -B build
cmake --build build
sudo cmake --install build
```

Building via Presets:
```bash
#########################
# For native x64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold

# For build only.
cmake --preset linux-x64-gcc-all
cmake --build --preset linux-x64-gcc-all

# For build and install.
cmake --preset linux-x64-gcc-install
sudo cmake --build --preset linux-x64-gcc-install

#########################
# For native arm64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold

# For build only.
cmake --preset linux-arm64-gcc-all
cmake --build --preset linux-arm64-gcc-all

# For build and install.
cmake --preset linux-arm64-gcc-install
sudo cmake --build --preset linux-arm64-gcc-install
```

Enjoy your build at `/usr/local/bin`

You can use `--preset linux-arm64-gcc-cross-package` to cross compile for arm64/aarch64 raspberry pi. Output binaries would be stored at `build/Packaging`.

<p align="right">(<a href="#top">back to top</a>)</p>

#### CLANG 

**NOTE: This build is not officially supported due to potential errors during runtime. Prefer GCC instead.**

Prerequisites:
- For Ubuntu: `sudo apt-get install git cmake ninja-build clang curl zip unzip tar pkg-config`

**NOTE: Ubuntu 20.04 LTS provides an outdated version of CMake hence it does not support presets.**

Building via CLI:
```bash
#########################
# For native x64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold
CC=clang CXX=clang++ cmake -D VCPKG_TARGET_TRIPLET=x64-linux-release-clang -D CMAKE_BUILD_TYPE=Release -D ARCH=native -G Ninja -S . -B build
cmake --build build
sudo cmake --install build

#########################
# For native arm64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold
CC=gcc CXX=g++ cmake -D VCPKG_TARGET_TRIPLET=arm64-linux-release-clang -D CMAKE_BUILD_TYPE=Release -D ARCH=native -G Ninja -S . -B build
cmake --build build
sudo cmake --install build
```

Building via Presets:
```bash
#########################
# For native x64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold

# For build only.
cmake --preset linux-x64-clang-all
cmake --build --preset linux-x64-clang-all

# For build and install.
cmake --preset linux-x64-clang-install
sudo cmake --build --preset linux-x64-clang-install

#########################
# For native arm64 builds
#########################
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold

# For build only.
cmake --preset linux-arm64-clang-all
cmake --build --preset linux-arm64-clang-all

# For build and install.
cmake --preset linux-arm64-clang-install
sudo cmake --build --preset linux-arm64-clang-install
```

Enjoy your build at `/usr/local/bin`

You can use `--preset linux-arm64-clang-cross-package` to cross compile for arm64/aarch64 raspberry pi. Output binaries would be stored at `build/Packaging`.

<p align="right">(<a href="#top">back to top</a>)</p>

### MacOS (x64 only)

#### CLANG

**NOTE: This build is not officially supported due to MacOS transitioning to arm64 and the code do not support arm64 architecture.**

Prerequisites:
- [HomeBrew](https://brew.sh/)
  - Install by running `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
  - `brew install git cmake ninja llvm pkg-config`

Building via CLI:
```bash
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold
CC=clang CXX=clang++ LDFLAGS="-L/usr/local/opt/llvm/lib/c++ -L/usr/local/opt/llvm/lib -lunwind" CPPFLAGS="-I/usr/local/opt/llvm/include" cmake -D VCPKG_TARGET_TRIPLET=x64-osx-release -G Ninja -S . -B build
cmake --build build
sudo cmake --install build
```

Building via Presets:
```bash
git clone -b development --recursive https://github.com/derogold/derogold.git
cd derogold

# For build only.
cmake --preset osx-x64-clang-all
cmake --build --preset osx-x64-clang-all

# For build and install.
cmake --preset osx-x64-clang-install
sudo cmake --build --preset osx-x64-clang-install
```

<p align="right">(<a href="#top">back to top</a>)</p>

## Fast Sync (--sync-from-height)

The DeroGold blockchain is 350 GB+ from genesis. The `--sync-from-height` flag lets a fresh node skip the historical chain and start syncing from a recent checkpoint instead, reducing initial sync time from days to hours.

### How it works

On first launch with an empty database, the node injects a trusted anchor block at the specified height (verified against the hard-coded checkpoints) and begins downloading only blocks from that point onward. Blocks below the sync floor are trusted via the checkpoint — ring signature verification is bypassed for historical inputs that predate the floor, identically to how the standard checkpoint zone works.

> **Note:** Do not use `--sync-from-height` on mining nodes. Miners must have the full chain to correctly validate all outputs.

### Available sync floors

| Height    | Approx. date  |
|-----------|---------------|
| 1,000,000 | 2021          |
| 1,500,000 | 2022          |
| 2,000,000 | 2022          |
| 2,500,000 | 2023          |
| 2,700,000 | 2024 *(recommended)* |

The highest available floor (2,700,000) gives the fastest sync. Lower floors retain more history.

### Usage

```bash
./DeroGoldd --sync-from-height=2700000
```

The argument must match one of the supported heights listed above. If the database already contains blocks the flag is silently ignored — use `--resync` first if you need a clean start.

### Exporting a new bootstrap state (for node operators)

If you run a fully-synced node and want to add a new floor height, run this command from the daemon console:

```
export_bootstrap_state <height>
```

The height must already exist in `CryptoNoteCheckpoints.h`. Copy the printed values into a new entry in `src/config/SyncBootstrapCheckpoints.h`, rebuild, and distribute.

<p align="right">(<a href="#top">back to top</a>)</p>

## License

Read the [LICENSE](https://github.com/derogold/derogold/blob/master/LICENSE) file for more details.

<p align="right">(<a href="#top">back to top</a>)</p>

## Thanks

Cryptonote Developers, Bytecoin Developers, Monero Developers, Forknote Project, TurtleCoin Developers

<p align="right">(<a href="#top">back to top</a>)</p>

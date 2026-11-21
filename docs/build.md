# Build Cadence from source

You’ll need a C++20 compiler, CMake, pkg-config, GTK 4.12+, libadwaita 1.5+,
and libwebsockets development files.

| Distribution | Dependencies |
| --- | --- |
| Arch Linux | `sudo pacman -S base-devel cmake git pkgconf gtk4 libadwaita libwebsockets` |
| Fedora | `sudo dnf install gcc-c++ cmake git pkgconf-pkg-config gtk4-devel libadwaita-devel libwebsockets-devel` |
| Debian / Ubuntu | `sudo apt install build-essential cmake git pkg-config libgtk-4-dev libadwaita-1-dev libwebsockets-dev` |

For the Debian/Ubuntu command, use a release with the minimum versions above,
such as Debian 13 or Ubuntu 24.04. Debian 12 and Ubuntu 22.04 ship older GTK
and libadwaita versions.

```sh
git clone https://github.com/MORT1SXD/cadence-linux.git
cd cadence-linux
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/cadence
```

The default build fetches Discord RPC and its RapidJSON dependency. Pass
`-DENABLE_DISCORD=OFF` on the CMake configure line to skip both dependencies
and remove Discord sharing. Discord builds still need an Application ID,
which you can configure at runtime; see the [user guide](README.md#connectivity).

For a desktop launcher and icon, install to `~/.local`:

```sh
cmake --install build --prefix "$HOME/.local"
```

## Build the AppImage

The [AppImage workflow](../.github/workflows/appimage.yml) builds on Ubuntu
24.04 x86-64, tests startup and a local OSC message, and saves the AppImage
and `SHA256SUMS` as its run artifact.

To build it locally on that same baseline, install the build dependencies
listed in the workflow, then run `bash packaging/build-appimage.sh`. The
outputs are in `build/dist/`. The packaging script verifies the downloaded
packaging tools against pinned checksums.

After downloading a release, run `sha256sum -c SHA256SUMS` beside the
AppImage to verify it. See [bundled software](../packaging/THIRD-PARTY.md)
for component notices and source information.

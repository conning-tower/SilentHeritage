# Silent Heritage

An open-source reimplementation of *Silent Hunter* (MicroProse, 1995), the
World War II US submarine simulation, for Linux, Windows and Android. It
includes the game and its scenario editor from the Commander's Edition.

Silent Heritage is only an engine: you need your own copy of the original
game. It reads the original files (graphics, sounds, videos, maps,
scenarios, and the tables and texts stored in `SH.EXE` and `SE.EXE`) directly
from your install.

## Disclaimer

This project is not affiliated with or endorsed by MicroProse, Ubisoft, or
any owner of the Silent Hunter trademark or of the original game. This
repository contains no data from the original game. "Silent Hunter" is a
trademark of its owner and is used here only to say what this software is
compatible with.

## Building

You need CMake 3.25 or later, Ninja, clang and lld, and git (CMake downloads
SDL3 and Catch2 when configuring). On Linux, SDL3 also needs the usual X11 or
Wayland, audio and GL development packages.

    cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD
    ninja -C build/release silent_heritage

Windows builds are cross-compiled from Linux with clang and the mingw-w64
runtime: add `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-clang.cmake`. Debug builds
(the default) enable ASan and UBSan; under WSLg, set
`ASAN_OPTIONS=detect_leaks=0`.

## Running

    build/release/src/app/silent_heritage [--data <install dir>] [--nointro]

Without `--data`, the game asks once for the folder of your Silent Hunter
install (the one holding `SH.EXE` and `DATA`) and remembers it; `--setup`
asks again. Saves go to your per-user application folder, never into the
install.

Other options: `--editor` starts the scenario editor, `--viewer` opens an
asset viewer, `--save DIR` changes where saves go. Alt+Enter toggles
fullscreen.

## Android

`android/build_apk.sh` builds a debug-signed arm64 package (Android 7.0 or
later) without Gradle:

    ANDROID_HOME=<sdk> android/build_apk.sh

It needs the Android SDK (`platforms/android-36`, `build-tools/36.1.0`) and
NDK 29, a JDK 17 or later, ninja, rsync and zip, and python3 with librsvg and
cairo plus the Liberation Sans Narrow font for the launcher icon. The other
settings are described at the top of the script. The package is
`build/android/SilentHeritage-debug.apk`; the debug key is `debug.keystore`
at the repository root (created on first use, not committed; keep it, since
an installed package only accepts updates signed with the same key).

On first start, the app asks for the folder holding your Silent Hunter
install, then either reads it in place or copies it into the app's storage.
A finger acts as the left mouse button and Back as Esc. During a mission, a
floating ORDERS button opens a panel with the orders that are keyboard-only
in the original (depth orders, firing tubes), and its + and − buttons change
the time compression.

## Tests

    ninja -C <build> sh_tests sh_editor_tests
    SH_DATA=<install dir> <build>/tests/sh_tests
    SH_DATA=<install dir> <build>/tests/editor/sh_editor_tests

Most tests need the original game files and are skipped unless `SH_DATA`
points at an install (the Commander's Edition, with `SE.EXE`, for the editor
tests).

## Source layout

`src/` holds one library per module: `core` (basic types, file access, the
original's fixed-point arithmetic), `assets` (the original file formats),
`platform` (SDL3: window, input, audio), `gfx` and `view` (sprites, fonts,
the 3D view), `audio`, `ui` (the original's dialog toolkit), `sim` (the
simulation), `game` (stations, screens, missions, career), `editor` (the
scenario editor) and `app` (the programs). `tests/` holds the Catch2 tests,
`tools/` a build-time tool for the Android icon, and `android/` the Android
packaging.

## Documentation

`docs/` describes the original game's file formats (`formats.md`), its data
tables, simulation objects and behaviour (`structures.md`), and the scenario
editor (`editor-*.md`). Code comments often cite the address and name of the
original function being reimplemented, e.g. `draw_sprite_clipped (0xaaa9c)`:
addresses in the relocated `SH.EXE` (or `SE.EXE` when noted).

## License

MIT, see `LICENSE`. SDL3 (zlib license) and Catch2 (Boost Software License)
are downloaded at build time and are not part of this repository. The
original game's files are not included and not covered by this license.

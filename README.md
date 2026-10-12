# Silent Heritage

An open-source reimplementation of *Silent Hunter* (MicroProse, 1995), the
World War II US submarine simulation, for Linux, Windows and Android. It
includes the game and its scenario editor from the Commander's Edition.

Silent Heritage is only an engine: you need your own copy of the original
game. It reads the original files (graphics, sounds, videos, maps,
scenarios, and the tables and texts stored in `SH.EXE` and `SE.EXE`) directly
from your install.

It comes in two builds. The default one is faithful to the original; it
only differs in what [FAITHFUL.md](FAITHFUL.md) lists. The extended build
adds the improvements and new features of [EXTENDED.md](EXTENDED.md).

## Disclaimer

This project is not affiliated with or endorsed by MicroProse, Ubisoft, or
any owner of the Silent Hunter trademark or of the original game. This
repository contains no data from the original game. "Silent Hunter" is a
trademark of its owner and is used here only to say what this software is
compatible with.

## Downloading

The [releases](https://github.com/conning-tower/SilentHeritage/releases)
hold the packages of both builds, the faithful one (`SilentHeritage-…`) and
the extended one (`SilentHeritageExtended-…`), which install side by side:

- Android 7.0 or later: `…-<version>.apk` (phones and tablets, Chromebooks).
- Linux, x86_64 or aarch64, glibc 2.35 or later: `…-<arch>.AppImage`
  (`chmod +x` and run it), or `…-linux-<arch>.tar.gz` (the program, its
  menu entry and its icon).
- Windows 10 or later, x64: `…-windows-x64.zip`. The program is not signed
  yet: Windows SmartScreen asks for confirmation ("More info", "Run
  anyway") on its first start.

`SHA256SUMS` lists the files' checksums. Each file's build provenance is
attested by GitHub: `gh attestation verify <file> -R conning-tower/SilentHeritage`
shows that it was built by the release workflow from the release's tag.
[RELEASING.md](RELEASING.md) describes how releases are made.

## Building

You need CMake 3.25 or later, Ninja, clang and lld, and git (CMake downloads
SDL3 and Catch2 when configuring). On Linux, SDL3 also needs the usual X11 or
Wayland, audio and GL development packages.

    cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD
    ninja -C build/release silent_heritage

Windows builds are cross-compiled from Linux with clang and the mingw-w64
runtime: add `-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-clang.cmake`. `-DSH_VERSION=`
sets the version (`MAJOR.MINOR.PATCH[-suffix]`, `0.0.0-dev` by default).
`packaging/build_desktop.sh linux|windows` builds the release packages
(described at its top). Debug builds
(the default) enable ASan and UBSan; under WSLg, set
`ASAN_OPTIONS=detect_leaks=0`.

Two variants can be built. The default, `-DSH_VARIANT=faithful`, is the
original game. `-DSH_VARIANT=extended` builds `silent_heritage_extended`,
which adds improvements beyond the original (listed in `EXTENDED.md`).
It keeps its own saves, so the two can be installed side by side.
`--faithful` turns all the improvements off.

## Running

    build/release/src/app/silent_heritage [--data <install dir>] [--nointro]

Without `--data`, the game asks once for the folder of your Silent Hunter
install (the one holding `SH.EXE` and `DATA`) and remembers it; `--setup`
asks again. Saves go to your per-user application folder, never into the
install.

Other options: `--editor` starts the scenario editor, `--viewer` opens an
asset viewer, `--save DIR` changes where saves go, `--no-orders-button`
hides the floating ORDERS button of touch screens. Alt+Enter toggles
fullscreen.

## Android

`android/build_apk.sh` builds a debug-signed package for arm64-v8a and
x86_64 (Android 7.0 or later) without Gradle:

    ANDROID_HOME=<sdk> android/build_apk.sh

It needs the Android SDK (`platforms/android-36`, `build-tools/36.1.0`) and
NDK 29, a JDK 17 or later, ninja and zip (and rsync for the local Silent
Hunter edition), and python3 with librsvg and
cairo plus the Liberation Sans Narrow font for the launcher icon. The other
settings are described at the top of the script. The package is
`build/android/SilentHeritage-debug.apk`; the debug key is `debug.keystore`
at the repository root (created on first use, not committed; keep it, since
an installed package only accepts updates signed with the same key, and
the released packages are signed with another, see RELEASING.md).

The app's saves (the `SAVE` folder) are kept by Android's backup: they come
back when the app is installed again or moved to a new device.

On first start, the app asks for the folder holding your Silent Hunter
install, then either reads it in place or copies it into the app's storage.
A finger acts as the left mouse button. Back goes back one screen in the
menus (the screen's CANCEL, MAIN MENU or OK button) and quits from the main
menu; during a mission and in the scenario editor it acts as Esc. During a
mission, a floating ORDERS button opens a panel with the orders that are
keyboard-only in the original (depth orders, firing tubes), and its + and −
buttons change the time compression.

Leaving the app (Home, the recent apps, the system closing it) keeps the
game: the next start resumes where you were. If it does not, the file
`SAVE/LIFECYCLE.LOG` in the app's storage tells what the system sent and
what became of the resume snapshot; get it with
`adb shell run-as com.conningtower.silentheritage cat files/SAVE/LIFECYCLE.LOG`
(the desktop game keeps the same log in the `SAVE` folder of your per-user
application folder).

## Tests

    ninja -C <build> sh_tests sh_editor_tests
    SH_DATA=<install dir> <build>/tests/sh_tests
    SH_DATA=<install dir> <build>/tests/editor/sh_editor_tests

With `SH_DATA` pointing at an install (the Commander's Edition, with
`SE.EXE`, for the editor tests), the tests run on the original game files.
Without it, they run on a synthetic install generated by code
(`tests/synthetic`), and the tests of the original's content (tagged
`[content]`) are skipped. `make_synthetic_install <dir>` writes that install
to a directory, for running the game without the original files.

The golden traces (`tests/golden`) record the faithful game's behaviour on
scripted runs: those in `real/` on the original files, those in `synthetic/`
on the synthetic install. A test fails with the checkpoints that changed.
Re-record them only for a deliberate change of the faithful behaviour, with
`SH_GOLDEN_RECORD=1` (or part of a trace's name).

The synthetic install takes the shapes of the original's files and
executables from the manifests in `tests/synthetic/manifest` (sizes, frame
boxes, the addresses the code reads; no pictures, sounds or text). When code
starts reading other addresses of `SH.EXE` or `SE.EXE`, refresh the
executables' manifests with a trace build:

    cmake -B build/trace -DSH_TRACE_EXE_READS=ON -DSH_SANITIZE=OFF -DCMAKE_BUILD_TYPE=Release ...
    ninja -C build/trace sh_tests sh_editor_tests
    rm -f trace.log
    SH_DATA=<install dir> SH_TRACE_EXE=$PWD/trace.log build/trace/tests/sh_tests
    SH_DATA=<install dir> SH_TRACE_EXE=$PWD/trace.log build/trace/tests/editor/sh_editor_tests
    SH_DATA=<install dir> SH_SYNTHETIC=1 SH_TRACE_EXE=$PWD/trace.log build/trace/tests/sh_tests
    SH_DATA=<install dir> SH_TRACE_EXE=$PWD/trace.log build/trace/tests/sh_tests "[.synthetic_exe_manifest]"

(`SH_SYNTHETIC=1` runs the synthetic install against the original `SH.EXE`,
for the code paths only the synthetic data takes.) The file and table
manifests are refreshed by `[.synthetic_manifest]` with `SH_DATA` set.

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
tables, simulation objects and behaviour (`structures.md`), the scenario
editor (`editor-*.md`), and the dates of the boats' equipment
(`equipment.md`). Code comments often cite the address and name of the
original function being reimplemented, e.g. `draw_sprite_clipped (0xaaa9c)`:
addresses in the relocated `SH.EXE` (or `SE.EXE` when noted).

## License

MIT, see `LICENSE`. SDL3 (zlib license) and Catch2 (Boost Software License)
are downloaded at build time and are not part of this repository. The
original game's files are not included and not covered by this license.

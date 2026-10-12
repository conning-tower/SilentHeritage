# Silent Heritage

An open-source reimplementation of *Silent Hunter* (MicroProse, 1995), the
World War II US submarine simulation, for Windows, Linux and Android. It
includes the game and the scenario editor of the Commander's Edition.

![The bridge of USS Skate, on a wide screen](docs/screenshots/bridge.png)

## Playing

1. **Get the original game.** Silent Heritage is only an engine: it plays
   your own copy of Silent Hunter and includes none of its files. The
   scenario editor needs the Commander's Edition.
2. **Download** the package for your system from the
   [latest release](https://github.com/conning-tower/SilentHeritage/releases/latest):
   - **Windows** 10 or later (x64): the `.zip`. Unzip it anywhere and start
     the program. It is not signed yet, so Windows SmartScreen asks for
     confirmation on its first start: click "More info", then "Run anyway".
   - **Linux** (x86_64 or aarch64, glibc 2.35 or later, as in Ubuntu 22.04
     and Debian 12): the `.AppImage`; make it executable and run it. The
     `.tar.gz` holds the same program with a menu entry and an icon.
   - **Android** 7.0 or later (phones, tablets and Chromebooks): the `.apk`.
3. **Start it.** On the first start, the game asks for the folder of your
   Silent Hunter install (the one holding `SH.EXE` and `DATA`). On Android,
   it can read that folder in place or copy it into the app. The game never
   writes into your install: saves, options and your own scenarios go to
   your user folder.

Each release comes in two versions, which install side by side and keep
their own saves:

- **Silent Heritage** (`SilentHeritage-…`), the classic version, plays the
  original game: same simulation, rules and screens, original bugs
  included. [CLASSIC.md](CLASSIC.md) lists what it changes to run on modern
  systems and touch screens, and the crashes it avoids.
- **Silent Heritage Extended** (`SilentHeritageExtended-…`) adds
  improvements and new features: wide screens, a crew log, tools to draw on
  the chart, smoother controls, new equipment (the Mark 27 homing torpedo,
  the ST radar, the APR-1 radar detector) and fixes of the original's bugs.
  [EXTENDED.md](EXTENDED.md) lists them all. Its OPTIONS page, in the main
  menu, sets its display and its command bar.

On Android, a finger acts as the mouse, and a small ORDERS button gives the
orders that are keyboard-only in the original. Leaving the app keeps your
game: it resumes where you were.

Each release lists the checksums of its files, and GitHub attests that each
file was built by the project's release workflow from the release's sources
(`gh attestation verify <file> -R conning-tower/SilentHeritage`).

## Screenshots

![The chart, with an intercept course, the target's track, range circles, marks and notes drawn on it](docs/screenshots/chart.png)

![The OPTIONS page of the extended version](docs/screenshots/options.png)

## Disclaimer

This project is not affiliated with or endorsed by MicroProse, Ubisoft, or
any owner of the Silent Hunter trademark or of the original game. Its
source code contains no data from the original game; the screenshots show
the original game's art, as played with Silent Heritage. "Silent Hunter" is
a trademark of its owner and is used here only to say what this software is
compatible with.

## Building and contributing

[BUILD.md](BUILD.md) describes how to build, run and test Silent Heritage,
and how its sources are organised.

## License

MIT, see [LICENSE](LICENSE). SDL3 (zlib license) and Catch2 (Boost Software
License) are downloaded at build time and are not part of this repository.
The original game's files are not included and not covered by this license.

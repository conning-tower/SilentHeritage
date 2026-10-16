# Changelog

## [0.2.0] - 2026-10-16

### Added

- Extended scenario editor: the wide screen. The chart's map fills the
  window's width, the menu bar runs across it and the dialogs are centred;
  the editor follows the WIDE SCREEN option.
- Extended chart tools: marks show the mission time they were put; circles
  and measures can also be clicked or tapped at both ends; new toolbox
  icons.
- Extended SJ radar: the held beam's bearing is set by a drag or the mouse
  wheel, like the other dials.
- Extended deck gun: the sea is grabbed anywhere level with the sight,
  beside it on a wide window too.
- Extended chart: at the edge of the screen the pointer stays put and the
  chart scrolls on at a steady pace.
- Extended SINGLE MISSION menu: Tab goes through the buttons row by row.
- Desktop: the command bar options are ORIGINAL BAR and EXTENDED BAR, the
  layouts with an ORDERS button being for touch screens.

### Fixed

- The mouse pointer no longer disappears for good after a GAME OVER.
- A manned deck gun fires by itself again.
- Touch screens: the first tap on the chart after a tap on a panel is no
  longer lost (the eraser only framed the drawing), and the eraser frames
  nothing under a finger.
- Extended chart: Enter no longer ends the plotting or asks to clear the
  chart.
- Extended bridge: the deck slides out of the wide view as in the original,
  and the whole width of a wide bridge turns the view.
- Extended torpedo page: a Mark 27 on an empty slot has no dark outline
  around it.

## [0.1.1] - 2026-10-13

### Added

- macOS packages: a disk image per version, with one app for Apple Silicon
  and Intel Macs, macOS 13.3 or later. Not notarized: the first start needs
  "Open Anyway" in System Settings.
- Extended: the Esc menu has Game Options and Save; WIDE SCREEN can be
  changed during a mission.
- Extended chart tools: two-line tips worded for the mouse or a finger, and
  a line's next point dragged from its end.
- Android: Back in a mission goes back to the conn, where it opens the END
  MISSION menu.

### Fixed

- Extended: the videos have black bars beside them instead of the brass.
- The window keeps the screen's shape, and the mouse pointer moves over the
  frame beside the screens; in fullscreen it stays off the black bars.
- With the wide screen off, fullscreen shows the 4:3 picture between black
  bars.
- The chart scrolls only at the very edges of the screen.
- Extended chart: the tool's cross shows only over the map, the zoom hint
  no longer covers the command bar, and the clock box shows when it opens.
- The SH icon's letters are centred, on every system: the icons built on
  newer Linux distributions (the Android and Windows ones) drew them off
  centre.
- The ARM builds (Linux aarch64, Android on ARM) compute the sun's position,
  and so the daylight colours, exactly as the others.

## [0.1.0] - 2026-10-12

The first release: the whole game, in the classic version
([CLASSIC.md](CLASSIC.md)) and the extended one ([EXTENDED.md](EXTENDED.md)).

### Added

- Release packages for Android (arm64-v8a and x86_64), Linux (tarball and
  AppImage, x86_64 and aarch64) and Windows (zip, x64), for both versions.
- Android: the saves are kept by Android's backup.
- Windows: the program's icon and version information; no console window.

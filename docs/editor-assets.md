# SE.EXE: assets and palettes

The files the scenario editor reads, what it uses them for, and the palette
in effect when it draws them (the formats are in `formats.md`). Addresses are
linear addresses in the relocated `SE.EXE` image; SE's screen palette is
`0xa4af0` (SH: `0x11a274`).

## Files

Graphics (`ART\`):

- **`SBINTRO.SHP`** — title box ("SCENARIO EDITOR", 484x364, one frame), the
  owner-draw of dialog `0xaf384` (`0x461e0`) at (0x4e, 0x3a), shown while
  `0x44e0b` loads the data and until the first chart frame; the version string
  `0x98308` is written on it in `SBDIGITS.FNT` at (0x169, 0xf2).
- **`SBCHART.SHP`** (45 frames) — the editor's art:
  - 0 scale ruler, 1 memory gauge ("E MEM F"), 2-17 the eight toolbox icon
    buttons (released / pressed pairs, table `0x9e780`), 18-28 small chart
    marks (selection brackets, indicator icons, arrows);
  - 29 SELECT SHIP TYPE picture background, 30 its palette carrier (1-pixel
    frame);
  - 31 SELECT SUB TYPE palette carrier;
  - 32 SELECT AIRCRAFT TYPE picture background, 33 its palette carrier;
  - 34-37 ENVIRONMENT weather pictures (Clear .. Overcast), 38-43 moon phases;
  - 44 the About logo (AEON Electronic Entertainment).
- **`SBCURSOR.SHP`** — the editor's mouse cursors (`load_cursors`; SH's
  `CURSORS.SHP` is not used).
- **`SHIPICON.SHP`**, **`mapicon.shp`** — chart unit and base icons (as SH).
- **`SUBPICS.SHP`**, **`AIRPICS.SHP`**, **`idbook.cat`** — class pictures of
  SELECT SUB TYPE, SELECT AIRCRAFT TYPE and SELECT SHIP TYPE.
- **`SPAC_A..S.SHP`** — PATROL AREA background (dated Pacific map, as SH's
  home port).
- **`GUISPR.SHP`** and the GUI fonts `GUI_S`, `GUI_L`, `GUI_M`, `GUI_1.FNT`
  (`gui_init` config `0x981f4`; SE has `GUI_M` where SH has `FONT1`), plus
  `FONT1.FNT`, `MSG.FNT`, `MENU_S.FNT` (loaded by `0x44e0b`) and
  `SBDIGITS.FNT` (title box).
- **`M8.PCX`** — loaded with its palette by `init_video` only when SE is
  started by SH (`/GFXADR=`); started from DOS, the screen starts black and
  `init_video` sets all 256 DAC entries to the colour at `0xa4520`, which
  nothing writes (black).
- **`3DSHIPS.CAT`**, **`3DAIR.CAT`** — only their directories are read
  (`0x4629f`): the member sizes feed the scenario size gauge.

Unused although present in the binary: `MENU.SHP`, `MENUBGND.PCX` and
`MENU_B/H/L/M.FNT` are only referenced by the menu-skin code linked from
SH's `menu.cpp` (`0x2daa2`, `menu_skin_install` / `menu_load_fonts`), which
nothing calls: the editor's dialogs use the plain GUI look. `ENV-05.ENV` and
`WORLD-1.OBS` are unreferenced strings (initial contents of name buffers).

Data and other files (all in `formats.md`):
`SHPATH.INI`, `DIG.INI`/`MDI.INI`, `DATA\` `AIRCRAFT/SUBS/SHIPS/WEAPONS/
CARSUB/PZ.DAT`, `MAPnBASE.DAT`, `sun.dat`/`earth.dat`, `WORLD-n.WLD`,
`SCEN\HIST???.SCN`, `CUSTOM\*.SCN`, `SAVE\SBOPTION.SAV`, Miles `.MEL`/`.BNK`
(no editor-specific sound).

## Palettes

**Chart screen** (`sbchart_open` `0x3b668` after the title box;
`editor::se_chart_palette`), from first to last write:

1. `M8.PCX` (or black when started from DOS);
2. `SBINTRO` frame 0: 0-1, 48-85, 209-254;
3. `SBCHART` frame 0: 0-15, 49-60, 62-63, 71, 81-83;
4. the map's chart colours (WLD `chart_dac`, range 8) into 128-239;
5. `SBINTRO` frame 0 again (title box redrawn by `0x461b3`), then `SBCHART`
   frame 0 again;
6. `SHIPICON` frame 0 colours 16-63 (they equal SBCHART's 49-63).

So: 0-15 SBCHART, 16-63 SHIPICON, 64-85 SBINTRO (71, 81-83 SBCHART), 86-127
and 255 M8 (black from DOS), 128-208 chart, 209-254 SBINTRO. The chart
pixels only use 128-208, so the title colours never clash with the map.
Nothing the editor draws uses 86-127 or 255 (the GUI is 0-8, the cursors
0, 1 and 8, the art 0-85 and 128-254), so the screens are the same started
from DOS, where those entries stay black: DOSBox captures of both cases
show identical menu bar, dialogs and toolbox.
SBCHART frames 0-28, SBCURSOR (0, 1, 8), GUISPR and the fonts are drawn on
it. No CONN.PCX is involved (unlike SH's chart).

**Dialogs**: each one greys the screen with `palette_grey_out(50, last)`
(colours 9..last; 0-8, the GUI colours of `gui_init`, stay), then the picture
dialogs copy one SBCHART frame's colours into the top of the palette:

- SELECT SUB TYPE (`0x258ba`): frame 0x1f, 225-254; SUBPICS is drawn through
  the remap `0xa6c14` (0x50-0x5f -> 0xfd, 0x60-0x6f -> 0xfe, 177 -> 0xf9,
  192-213 -> 0xf0-0xf8), so it looks different from SH's (M8) version (a
  dark green-grey silhouette on a dark teal sea, as DOSBox captures show).
- SELECT SHIP TYPE (`0x26747`): frame 0x1e, 225-248; background frame 29 at
  (0x10, 0x3b) and (0x10, 0x8a); the `idbook.cat` pictures therefore get
  SBCHART's sky (234 = (190,222,246), bluish) instead of ID-2.PCX's (pinkish)
  as in SH's ID book.
- SELECT AIRCRAFT TYPE (`0x27350`): frame 0x21, 225-255; background frame 32
  at (0x10, 0x3a), AIRPICS frame (AIRCRAFT.DAT `+0x74`) at (0x11, 0x3b).
  AIRPICS' own palette equals frame 33's.
- ENVIRONMENT preview (`0x280a7`, `0x28283`): weather frame 0x22+weather
  with colours 209-245, moon frame 0x26+phase with 246-250 (the moon
  frames' 176 and 205 stay the greyed chart's).
- About (`0x2b5e5`): frame 0x2c drawn, its colours 224-254 set directly in
  the DAC.
- PATROL AREA: SPAC frame colours 128-253 over the chart palette (not
  greyed), as SH.

Each SBCHART frame's own palette matches the colours it is drawn with,
except for the few indices noted above.

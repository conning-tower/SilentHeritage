# Game data file formats

The formats of the original game's files, as the reimplementation reads them
(`src/assets/`). Addresses and function names are those of the original
`SH.EXE` (see the README's "Provenance").

Status legend: **[verified]** decoded and checked (every shipped file parses,
or the arithmetic cross-checks); **[decoded]** read completely by the port;
**[partial]** container/header understood, inner encoding not fully decoded.

Several first-party formats share a 4-byte ASCII magic beginning with `A`
(`AWLD`, `AOBJ`, `ASCN`, `AENV`), a NUL and a `u8` version (the loaders compare
the first 5 bytes as a C string, then read byte 5). Standard third-party formats
(PCX, Smacker) are used as-is.

## Compressed blocks (RLE90 + LZW) **[verified]**

Large payloads (map grids and charts, `.OBS` objects) go through one stream
reader in the game (`read_compressed`, 0x39355; `assets/compressed.h`).

    u32            size            decompressed size (0: empty block)
    u8             max_bits        LZW code-width ceiling (capped at 11)
    ...            code stream     LSB-first bits, read in 16-bit words

LZW with literal codes 0-255, starting at 9 bits, no clear/stop codes: every
code (the first included) adds a dictionary entry, the width grows when the next
free code exceeds the mask, and the dictionary silently resets to 9 bits when
the width would pass `max_bits`. The LZW output is an ARC-style RLE90 stream
(`c 0x90 n` = `n` copies of `c`; `0x90 0x00` = literal 0x90). The stream has no
length: decoding stops after `size` bytes.

## `.CAT` — archive container **[verified]**

Flat archive bundling many `.SHP` members (`3DSHIPS.CAT` = 81 ship models,
`3DAIR.CAT` = 5 aircraft, `IDBOOK.CAT`).

    u16            entry_count
    entry[count]   (26 bytes):
        char[12]   name            space/NUL padded
        u16        flags           observed 0x2000
        u32        timestamp       packed, opaque
        u32        size
        u32        offset          absolute file offset

Members follow the directory contiguously; offsets are self-checking
(`offset[0] == 2 + 26*count`, `offset[i] == offset[i-1] + size[i-1]`).

## `.SHP` — sprite / 3D-view container **[decoded]**

    char[4]        version         ASCII "1.10" or "1.00"
    u32            frame_count
    struct[count]  (8 bytes):  u32 frame offset, u32 palette offset (0: none)
    frame[count]   at the listed offsets
    palette        u32 n, then n x {u8 index, u8 r, g, b} (6-bit VGA)

    frame header (0x18 bytes, then the pixel rows):
    +0x00  u32     1.10: u16 height-1, u16 width-1 (`sprite_frame_size`)
    +0x04  u32     ?
    +0x08  i32 x4  x0, y0, x1, y1: inclusive box relative to the hotspot
    +0x18  rows    one RLE row per y from y0 to y1

Each row is a sequence of control bytes `b`, with `n = b >> 1`, `f = b & 1`:
`n > 0, f = 0` a run (the next byte repeated n times); `n > 0, f = 1` a
literal (the next n bytes); `n = 0, f = 1` a skip (the next byte is a count of
transparent pixels); `n = 0, f = 0` end of row. Read from the blitter
`draw_sprite_clipped` (0xaaa9c; general entry `sprite_draw_transformed`,
0xab591, adds rotation/scale and an optional 256-byte colour remap at
0x11301c). Every frame of 92 of the 93 files in `ART/` decodes within its box
(`SBCHART.SHP` #42 is a bare header with a box near 2**31 and no rows: an
empty frame the blitter clips away). The 3D ship/aircraft archives
(`3DSHIPS.CAT`, `3DAIR.CAT`, `IDBOOK.CAT`), the `.OBS` map objects and the
`.WLD` chart tiles are the same format.

Pixels index the palette of the screen the sprite is drawn on, overlaid
with the frame's own partial palette if it has one (30 files: the
single-mission pictures, the patrol-area maps, the logos...), copied into the
screen palette before drawing (0xaf0a6). Each set's palette comes from the
code that draws it; the asset viewer applies them (`app/palettes.cpp`):

- **Screens**: the background PCX loaded by the screen (e.g. the `P_*` boat
  profiles are only drawn on the port-arrival screen, over `PORTBACK.PCX`,
  whose indices 144-148 are the sea under the hull). The `ART/*.PAL` files
  (CONN, GAUGES, RADAR, SCOPE, TDC, CAPTCAB, CALENDR) are the red-light
  variants of those stations' palettes, for "rig for red" at night.
- **3D view** (ships, aircraft, map objects, effects, the bridge/deck-gun/TBT
  overlays): the station PCX, with 16-63 and 80-129 from the `.ENV` header and
  130-159 from its per-sea-state table (header `0xbeb + 90 * sea state`), as
  the `.ENV` loader and `world_shade_screen_palette` build it.
- **Chart**: CONN.PCX, CHART.SHP frame 0x29's colours, SHIPICON frame 0's
  colours 16-63 and the map's chart colours 128-239.

**Night** is a palette effect, not separate sprites
(`world_apply_daylight_palette`): the shaded ranges are darkened to the `.ENV`
ambient minimum (header byte `0x1153`, e.g. 24 % for ENV-01), except index 128,
39-43 and the fire range 64-74, and indices 44-47 switch to the ship-light
colour (63, 63, 50) (45 blinks); 48 ship sprites use them, mostly merchants and
troopships (up to ~290 lit pixels per view); warships have almost none (blacked
out). No other light mechanism exists: the six per-distance remap tables of the
`.ENV` header (offset `0x3ea`, chosen by range in `veh_draw_ship_sprite`) are
mild distance fades and never map to 44-47. Moonlight and the dawn/dusk tint
make the night lighter than the ambient minimum.

The sprites identify the unnamed `.WLD` location types (6 moored submarine,
7 palm islet, 8 shark); the `SPAC_A`..`SPAC_S` maps are the Pacific with
Japanese-held territory in red at successive war dates. Files come in pairs
`NAME.SHP` (full 3D rotation views) and `NAMES.SHP` (smaller
silhouette/recognition views).

## `.FNT` — bitmap font **[decoded]**

    char[4]        "1.\0\0"
    u32            glyph count (indexed by character code)
    u32            height
    u32            unused_0c (0xff; 0 in BEARING, 0x100 in MSG; never read)
    u32[count]     glyph offsets
    glyph          u32 width, then width x height palette indices, row by row

0xff is transparent; the blitter (0xae5d3, via `gui_draw_text`) maps the
other indices through the GUI colour table. All 28 files tile exactly.
GUI_M and SBDIGITS are only used by SE.EXE, SUBTOUR.FNT by neither EXE.

## `.WLD` — patrol map **[verified layout]**

One `.WLD` per patrol map (`WORLD-1`..`WORLD-15`, Southern Japan .. Aleutians).
Read by `world_load_awld` (0x9102d); every shipped file parses to its last
byte. Version 7 shipped (the game accepts >= 4).

    char[4]+NUL+u8 "AWLD", version
    char[31]       region_name     (version >= 6)
    u32            scale_shift     world units per grid cell = 0x10000 >> shift (0)
    u16            n_locations     <= 400
    loc[n]         24 bytes: i32 type, x, z, y, heading, unused_14
    u32, u32       grid_w, grid_h  601 x 401 typical
    block          grid            terrain/bathymetry colour index per cell
    u32            unused_sea_state read into 0x170f28, overwritten by the sea picture
    u8[0xc0]       grid_dac        64-colour 6-bit VGA palette (indices 0-63)
    u32, u32       chart_w, chart_h 1200 x 800 typical
    u32            chart_size      decompressed size of the next block
    block          chart           chart_h rows: u32 row_size (incl. itself) +
                                   RLE90 row of chart_w+1 pixels
    u8[0x150]      chart_dac       112-colour VGA palette (indices 128-239)
    u32 + bytes    coast tiles     embedded 16-frame `.SHP` (version >= 5;
                                   identical in all but one map)
    i32[4]         bounds          lat north, lat south, lon west, lon east in
                                   16.16 fixed-point degrees (version >= 7)

The grid is the terrain/depth map the simulation samples; the chart is the
painted navigation chart shown to the player (place names, soundings, compass
rose). **World coordinates** (here and in `.SCN`/`.CR`) are 16.16 grid cells
with `x` east (grid column) and the second horizontal axis (`y` in `.WLD`/
`.SCN`, `z` in `.CR`) counting *north from the bottom row*: grid row =
`grid_h - 1 - y`; the chart is the grid scaled by `chart_w / grid_w`.
Locations are map landmarks, drawn as 3D objects
(0x91a91 adds one render-list object per location):

- `type` indexes the `.OBS` objects (`WORLD-1.OBS` has 9; the maps use 0-8) and
  names the landmark for the radio reports (table 0x10eb54): 0 Buoy, 1 City,
  2 Port Facilities, 3 Industrial Facilities, 4 Town, 5 Light House. 6-8
  have no name in the game; their sprites show 6 a Japanese submarine (23
  views, the only oriented object; two of them at Kure, facing 180 deg), 7 a
  palm islet (common in the tropical maps) and 8 a shark (a single one,
  Mariana Islands map, cell 323/128).
- `z` is the height above the sea (render y).
- `heading` is in tenths of a degree, read as a short (1800 on type 6, else 0).
- `unused_14` (always 0) is never read, by SH.EXE nor SE.EXE.

Bomber groups aim at the location nearest their base (`airgroup_init`); photo
recon picks type 2/3 locations within 4 cells of the base
(`intel_report_init`).

## `.OBS` — map objects **[verified layout]**

`DATA/WORLD-1.OBS` only. Read by `world_load_aobj` (0x90f2e).

    char[4]+NUL+u8 "AOBJ", version 2
    u16            count           <= 51
    entry[count]   20 bytes: u32 render_type, u32 size, u32 runtime_ptr,
                   u32 scale (render size x 65536), u32 unused_10 (0, never read)
    block[count]   one compressed block per entry: an embedded `.SHP` ("1.00")

## `.SCN` — scenario / mission **[verified layout]**

Read by `scen_load_ascn` (0x676b5) and one reader per record tag (`scen_read_*`,
0x67c9b-0x68318); all 61 shipped files parse to their last byte (versions 3, 4,
7; the game accepts >= 2). Produced by `SE.EXE`; `SCEN/HIST*.SCN` are the
historical missions, `CUSTOM/` holds custom scenarios. The single-mission
generator fills the same header in memory (`scengen_build_single_mission`,
0x68453); the career builds its scenarios separately (`campaign/career`).

    char[4]+NUL+u8 "ASCN", version
    char[1000]     briefing        briefing + "Results:" text, NUL-terminated
    i32            special_mission -1 when the scenario has a tag 5 or 6 record
                                   (SE recomputes it at every save; the generator
                                   sets it for lifeguard and photo-recon
                                   missions): start placement, end text; reused
                                   in the career as an "enemy contact present"
                                   flag
    char[13]       world           .WLD map, e.g. "WORLD-8.WLD"
    i32 x 9        weather (0-3: Clear, Partly/Mostly Cloudy, Overcast),
                   sea_state (0-2: Calm, Moderate, Heavy), fog, gradient,
                   month (0-based), day, year (since 1930), hour, minute
    i32 x 3        end_on_lost_contact (non-zero: a non-career mission ends
                   60 s after contact is lost; 0 only in SITDUCK.SCN),
                   randomize (0: srand(saved_time), a repeatable run),
                   saved_time (a random seed, not the save time: time() + 1
                   when SE creates the scenario, kept by later saves)
                                                              (version >= 4)
    char[15]       password        empty = none (zeros); else all 15 bytes
                                   (NUL and stale bytes included) + key 00 02
                                   01 0e 0c 03 04 06 05 0a 0d 0b 09 08 07; only
                                   SE checks it, to reopen the file (version >= 4)
    i32            required_memory SE's memory estimate: per unit 1300 (ship),
                                   3000 (sub), 900 (aircraft), 500 / 300 (tags
                                   5 / 6), plus each distinct 3D model's size in
                                   3DSHIPS/3DAIR.CAT; compared to free memory
                                   before "SCENARIO IS TOO LARGE" (version >= 7)
    i32[10]        tube_torpedoes  torpedo type in each tube
    i32[5][3]      reloads         (type, aft, forward) counts; type -1 ends (SE
                                   writes one entry per US torpedo of its list,
                                   MARK10 / MARK14 / MARK18-1, then two -1
                                   entries with stale counts)
    i32            equipment       bits by fit date (table 0x10e408): 0 SD air
                                   radar (1/42), 1 SJa (8/42), 2 SJ-1 (8/43,
                                   replaces SJa); 3 PPI radar display (7/43,
                                   default SJ display); 4 (5/44) fitted but read
                                   by no code (probably the cut ST periscope
                                   radar or APR-1); 5 bathythermograph (3/43,
                                   switches the gauge panel)
    record*        u8 tag + body, until tag 0

Dates agree with the briefing text in 51 of 58 dated files (the others are a
day off, e.g. a 00:05 start). Positions and waypoints are 16.16 fixed-point
cells of the world grid; headings are degrees; string fields keep stale editor
memory after their NUL. Record bodies (file order, `i32` unless noted):

- **1 player sub** (one): `x, y, depth, heading, engine_order, unused_14`
  (never read by SH.EXE; SE.EXE writes back what it loaded; garbage in three
  files), `char[10]` class (`BALAO`), `char[31]` name
  (`(SS-305) Skate`), `crew` (Green, Veteran, Elite; version >= 3).
- **2 ship**: `x, y, heading, speed` (knots), `orders` (None, Escort, Patrol,
  Convoy Leader, Convoy Member, Intercept, Follow Path, Fishing), `crew` (Green,
  Veteran, Elite), `damage_pct` (initial damage), `char[25]` class, `char[31]`
  name, `i32[25]` waypoint x, `i32[25]` waypoint y, `n_waypoints`,
  `convoy_group` (id shared by a convoy's leader, members and escorts),
  `zigzag` (used by convoy leaders with crew > 0), `alert` (initial alert
  state; Patrol orders force -1).
- **3 aircraft**: `mission` (1 Escort of `escort_group`, 2 Patrol: both a
  search orbit around `x, y`; 6 follow path),
  `crew`, `char[25]` type (`BETTY`, `VAL`), `x, y, orbit_radius, strafes`
  (gun-attacks once its bombs are gone, else shadows), `bomb_load` (as
  AIRCRAFT.DAT `bombs`, 1-4), `i32[25]` x 2 (path), `n_waypoints` (path
  points), `escort_group` (id of the group it covers), `relaunch` (version
  >= 6; 0 = one sortie, else unlimited).
- **4 enemy sub**: `x, y, depth` (ft, 64 when submerged), `heading` (x10),
  `speed` (knots), `orders` (5 Intercept, 6 Follow Path), `crew`,
  `damage_pct`, `char[25]` class (`KAIDAI TYPE`), waypoints as for ships,
  `n_waypoints`, `target_x, target_y` (intercept point, -1 = the player),
  `alert`, `side` (friend/foe; version >= 5).
- **5 bomber group** (`i32[4]`): base index (records at 0x116240), size (0-2:
  10/30/50 planes), launch delay and hold time (minutes; SE's "Time Delay" and
  "Flight Delay").
- **6 photo recon** (`i32[3]`): base index, delay and duration (minutes).
  Tags 5 and 6 are not used by the shipped files: they are the lifeguard and
  photo-recon mission icons of the scenario editor (Commander's Edition), and
  the campaign creates the same objects directly. In play, a photo is taken
  with TAB when the TBT/periscope light is red, by day only (500 points); a
  downed pilot is rescued within 200 yd at 5 kn or less with E or by running
  over him (200 points).

Field meanings come from the record readers (0x53d81, 0x54033, 0x5425b), the
scengen builders, the AI that consumes them and SE's writer
(`editor-formats.md`).

## `.ENV` — environment / sky **[partial]**

Read by `world_load_aenv` (0x90d89); the game requires version >= 8 (9 shipped).

    char[4]+NUL+u8 "AENV", version
    u8[0x1158]     header          u8[0x300] palette, u32 pcx_size at 0x300,
                                   rest (mostly zero) unknown
    u8[pcx_size]   sky             PCX image, 1280 x 65 sky/cloud panorama
    u8[0x3c1]      table           unknown
    u8[4][0x441]   tables          unknown (version >= 9)

## `.DAT` — game tables **[partial]**

Per-subsystem balance/reference tables, generally a count or a sequence of
fixed-stride records that begin with a NUL-padded name. Examples:

- `AIRCRAFT.DAT` — `u8 count`, then 32-byte index records (short name + a u32
  stepping by 168), then 168-byte detail records (`MITSUBISHI A6M`, ...).
- `CARSUB.DAT` — submarine classes/boats (`Balao`, `Bowfin`, `Billfish`, ...):
  name + several `u32` stats (displacement, speeds, ...).
- `CMDLIST.DAT` — `u32 count` (308) then commander names (`Fluckey, E.`, ...).

All 8 tables are framed and parsed (`assets/dat_tables.h`); see
`structures.md` for the per-file layouts. Field-level semantics of the record
bodies are still partly tentative.

## `.CR` — convoy routes **[verified layout]**

One file per patrol region (`ALEUTIAN.CR`, `CAROLINE.CR`, ...), named by the
patrol-area record and loaded by 0x51ff2 (all 15 files parse to the last
byte). Little-endian `i32` unless noted; positions are 16.16 grid cells of
the region's `.WLD` map.

    i32               n             routes
    i32[n]            n_waypoints
    i32[6] x n        dates         first/last month (1-based), day, year
    i32[45] x n       frequency     per campaign month, index 0 = Dec 1941
    i32[45] x n       convoy_pct    per campaign month
    u8 x n            flags
    i32[6] x sum(n_waypoints)       waypoint boxes (x0, y0, z0, x1, y1, z1)

- **dates** — the route is used only inside this window (0x30f53, against the
  current campaign date). The months with non-zero `frequency` match it in
  every file.
- **frequency** — spawn rate for the month: when a route's timer runs out the
  daily update (0x2dd2c) spawns a group and re-arms the timer to
  `0x34bc0 / frequency` ticks plus jitter; 0 means no traffic.
- **convoy_pct** — chance (%) that a spawned group is a merchant convoy
  (kind 0) rather than a warship task force (kind 1, BB/CV/CA/CL from the
  monthly mix); a 2% roll gives enemy submarines (kind 3), and a route whose
  alert level reached 3 spawns patrol / ASW groups (kind 2) instead
  (`convoy_build_on_route` 0x2e4c9; confirmed by the per-kind builders and the
  "Convoy / Warships / Patrol / Submarine" contact reports). Some files set it
  in months without traffic.
- **flags** — bit 0 = *coastal route* (13 routes in 7 regions): the merchant
  picker may choose coastal tankers (`CTANKER`), and the spawned group (its
  +0x154) materializes into real ships only within sighting range, not radar
  range (once materialized they show on radar normally). The geometry agrees:
  flagged routes run on average 7.6 grid cells from land, the others 37.6.
- **waypoints** — boxes, not points: for each spawned group, every waypoint is
  a random point inside its box (`y` is always 0), so groups on the same route
  take slightly different tracks. The group runs the route forward or backward
  at random. End boxes often lie on or beyond the map edge (traffic entering or
  leaving the region) or at a port. Many boxes have zero size (a fixed
  point).

Routes are not the only place ships appear (see the contact groups in
`structures.md`): each base also spawns a patrol near the port and anchored
ships with guards in its harbour (base records at 0x116240), sampans appear
daily at a random water cell anywhere on the map, aircraft come from the air
bases, and escorts sail a screening box around their convoy rather than the
line. Historical `.SCN` missions place every unit by hand.

## Third-party / standard formats

- **`.PCX`** — ZSoft PCX, 8bpp RLE, 256-color palette in the trailer. Standard;
  readable by ImageMagick/Pillow.
- **`.PAL`** — also a ZSoft PCX file, used to carry a palette: the red-light
  ("rig for red") palette of a station (CONN, GAUGES, RADAR, SCOPE, TDC,
  CAPTCAB, CALENDR).
- **`.SMK`** — RAD **Smacker** video (`CINES/`, `BUD/`). Decodable with ffmpeg.
- **`.RAW`** — headerless unsigned 8-bit mono PCM speech (`SPEECH/` crew
  voices, `ALT/` radio messages). The rate is not stored; 22050 Hz is what the
  speech driver is configured with (pitch confirmed by ear).
- **`.MEL`** (`SFX/`, 54) — not music: one sound effect each, a 0x18-byte
  header (u32 0 never read, i32 rate Hz or -1 = random in [max, min) given
  next, i32 loop count 1/2/-1, i32 volume offset) then u8 mono PCM. Played by
  `sound_play_sfx` through the Miles AIL 3 digital driver.
- **`.BNK`** (`SFX/1.BNK`, `2.BNK`) — the sample table `init_sound` preloads:
  0x24-byte records {i32 id, size, pointer, copy of the `.MEL` header}, ended
  by id -1 (`2.BNK` is never opened).
- **`.AD` / `.OPL`** — the stock Miles global timbre library (identical
  copies, 128 GM + 34 percussion 2-operator FM timbres); unused.
- **`.DIG`** — Miles AIL 3 sound-card driver binaries (no audio).
- **There is no in-game music**: the AIL music path (`%d.xmi`) is never
  used and no XMIDI ships; the only music is the Smacker videos'
  soundtracks.

## Other files

- **`DATA/PZ.DAT`** — the 15 patrol zones (`load_pz_dat` 0x51d03): name,
  map/route/base files, date window, per-year weights, command, map boxes,
  forbidden start edges, sampan density, monthly fog/weather and the monthly
  convoy, escort, crew, air-cover and air-raid tables
  (`assets/patrol_zones.h`).
- **`DATA/RMSG.DAT`** — the 3 radio-message templates (placeholders filled by
  0x33dc7).
- **`SUN.DAT`, `EARTH.DAT`, `PLANETS.NDX`** — abridged VSOP87 series (Meeus,
  *Astronomical Algorithms*) for the sun position (`load_ephemeris`);
  `STARS` holds the star positions.
- **`MAP10BAS.DAT`..`MAP15BAS.DAT`** — the base tables of maps 10-15 (8.3
  names), same layout as `MAP<n>BASE.DAT`.
- **`SAVE/*.SAV`** — options (GOPTION v3, GDETAIL/REALISM/SMISSION v1;
  SBOPTION v2 is SE.EXE's), career roster (ROSTER0 v7: the 0x451-byte career
  record, summary, 0x53-byte log events), best missions (v2, 10 x 0x116f) and
  best careers (v3, 10 x 0x8609); the layouts are in the reading and
  writing code (`game/saves_options.h`, `game/career.h`,
  `game/frontend_best.h`). `SAVE/DATA.001` belongs to NEWBOAT.EXE,
  a third-party 1999 tool, and is not decoded.
- **Text** — `SHPATH.INI` (data directories), `DIG.INI`/`MDI.INI` (Miles
  driver settings), `BREAK.LOG` (MemCheck breakpoints), `AILDRVR.LST`, the
  developers' `.RPT` build reports and the READMEs.

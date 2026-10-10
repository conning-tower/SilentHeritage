# SE.EXE: files written by the editor

What the scenario editor writes, read from its code. The game-side view of
the same formats is in `formats.md` (`.SCN`, as read by `SH.EXE`); this page
only adds what the writer shows. Addresses are SE.EXE linear addresses unless
noted; the in-memory side is in `editor-structures.md`.

## Reading and writing paths

- **Reading** a `.SCN` (File / Load Scenario, Load Historical) goes through
  the game's own reader, linked unchanged: `scen_load_ascn` (0x479a9) and the
  `scen_read_*` record readers fill the same header globals and unit arrays
  as in SH (SE address = SH address - 0x766bc, e.g. tubes 0xaf8cc = SH
  0x125f88). `scen_instantiate_units` then calls the editor's `sbfile_spawn_*`
  callbacks, which turn each record into an editor object (the downed-pilot
  callback is empty). `sb_finish_scenario_load` loads the scenario's world and
  bases and copies the reload counts back.
- **Writing** is `scen_write_ascn` (0x464c8): the header from the same
  globals, then one record per object through the object's vtable slot `+0x40`
  (`sbplayer_write_scn`, `sbship_write_scn`, `sbsub_write_scn`,
  `sbair_write_scn`, `sbmission_write_scn`), then tag 0. The file is opened by
  `dfile_open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY)` and written by
  `dfile_write` (0x13e03).

## `.SCN` as written **[verified against the shipped v7 files]**

The editor always writes **version 7**; there is no other writer path. Every
field below is a raw copy of memory (`i32` unless noted, little endian).

    char[4]+NUL+u8 "ASCN", 7                     (constant 0x9e9c4)
    char[1000]     briefing        0xaf4a8, whole buffer
    i32            special_mission -1 when the scenario holds an AIRSTRIKE or
                                   PHOTO RECON object (their vtable slot +0x20),
                                   else 0; recomputed at every write
    char[13]       world           0xaf894, "WORLD-n.WLD"
    i32 x 9        weather, sea_state, fog, gradient, month (0-based), day,
                   year (since 1930), hour, minute      (0xaf8a4..0xaf8c4)
    i32            end_on_lost_contact  0xaf93c (OPTIONS "Exit On Break Contact")
    i32            randomize       0xaf940 (OPTIONS "Randomize")
    i32            random_seed     0xaf944: time() + 1 when the scenario was
                                   created (File / New, start-up); kept by
                                   load and save
    char[15]       password        0xaf948, obfuscated (see below)
    i32            required_memory 0xaf958 (see below)
    i32[10]        tube_torpedoes  0xaf8cc: WEAPONS.DAT torpedo id per tube
    i32[5][3]      reloads         (type id, aft, forward), rebuilt at write
    i32            equipment       0xaf930
    record*        player, then the other objects in list order
    u8             0

Header notes:

- **Defaults** (`sb_reset_scenario`): Clear, Calm, no fog, gradient 0,
  7 December 1941 12:00, empty briefing and password, end-on-lost-contact and
  randomize from `SBOPTION.SAV`, equipment bits 0-5 cleared (bits 6-7 are
  kept, never set).
- **`random_seed`** is what `formats.md` calls `saved_time`. It
  is not the save time: it is set once per new scenario and only used by the
  game when `randomize` is 0 (`srand(seed)`, so the run is repeatable).
- **`password`**: the 15 bytes of the buffer (NUL and the bytes after it
  included) have `key[i]` added, key = `00 02 01 0e 0c 03 04 06 05 0a 0d 0b
  09 08 07` (0x9bcb4; the first character stays in clear). An empty password
  is stored as zeros, unencoded. The editor decodes it (subtracting the key)
  to compare with what the player types, case-insensitively (`stricmp`), 3
  attempts (CANCEL refuses at once), when a protected scenario is loaded
  (`ask_password` 0x2b47b); a refusal leaves a new scenario. The entry
  takes 14 characters (`max_length` 15 with the NUL). SH reads the field
  into 0x126004 and never uses it, and Delete Scenario does not ask it: the
  password only protects editing.
  `cmd_set_password` (0x2b327) stores the entry with `strcpy`, so clearing a
  password zeroes its first byte only (the stale encoded bytes stay in the
  file; `sb_reset_scenario` does the same on New), and a new one is copied
  up to the first zero byte of the *encoded* 15-byte entry buffer 0xa6d1c,
  which the key makes non-zero after the text: the copy runs past the 15
  bytes, its NUL landing in the padding byte 0xaf957 (the byte 0xa6d2b
  after the buffer only receives that padding byte's 0 from the decoding
  copy), harmless. The reimplementation zeroes all 15 bytes on a clear and
  on New.
- **`required_memory`** is the editor's estimate of the mission's memory:
  per object 1300 bytes (ship), 3000 (submarine), 900 (aircraft), 500
  (airstrike), 300 (photo recon), 0 (player), plus once per distinct 3D model
  the size of its file in `ART\3DSHIPS.CAT` or `ART\3DAIR.CAT` (SHIPS.DAT
  `sprite1` and `sprite2` for ships and subs, the AIRCRAFT.DAT sprite for
  aircraft; reference-counted cache 0xae754). The editor warns ("Playing
  scenario may cause abnormal program operation!") when it exceeds its budget
  0xa5108 = BASMEM - (world sizes + SFXBUF + 0x4b6f90), on Save and on Run;
  SH makes the same comparison with its free memory. When a pre-v7 file is
  loaded the value is recomputed (starting from -1, so it is one byte low).
  Shipped v7 files: 235,860 to 1,261,278.
- **`tube_torpedoes`**: `sb_reset_torpedo_load` fills all 10 slots, whatever
  the class's tube count, with the default torpedo id: 2 (MARK14) when the
  class's SUBS.DAT `modern_torpedoes` (+0x84) is set, else 1 (MARK10). The
  TORPEDOES dialog edits them; SITDUCK.SCN shows 0 for empty tubes.
- **`reloads`**: the editor keeps one (aft, forward) pair per torpedo of its
  list, which holds the leading US torpedoes of WEAPONS.DAT (nation 0, id != 4:
  MARK10, MARK14, MARK18-1; MARK18-2 is excluded). Entry `i` < 3 is
  `(id of torpedo i, aft[i], forward[i])`, entries 3 and 4 get type -1, so the
  table always ends with two terminators (SH stops at the first). Their count
  fields are left as they were (0 in every shipped file). Default counts: the
  class's stern reloads (+0x80) aft and bow reloads (+0x7c) forward for the
  default torpedo, 0 for the others. On load the counts are copied back by
  position, not by type.
- **`special_mission`**: SH uses it for the start placement and the "Mission
  over." end text; it is the editor's flag for "this scenario has a lifeguard
  (airstrike) or photo-recon task".

### Records

Records are written by walking the object list (0xa5088). Object 0 is always
the player, so tag 1 comes first; the others follow in creation order. A
loaded scenario is rebuilt in `scen_instantiate_units` order (ships, enemy
subs, aircraft, tag 5, tag 6), so loading and saving a file regroups its
records by kind. Each record is the object's in-memory record copied field by
field; char fields are whole buffers, so the bytes after the NUL are whatever
the heap held (the "stale editor memory" of the shipped files).

- **1 player** (`sbplayer_write_scn`, object +8): `x, y, depth, heading,
  engine_order` (0 Stop, 1-5 Ahead 1/3 .. Flank, 6 Back Emergency, 7 Back
  Full, 8 Back 2/3, 9 Back 1/3), `unk5`, `char[10] class`, `char[31] name`,
  `crew`. The editor never sets `unk5`: 0 for a player it creates, otherwise
  the loaded value (SH does not read it). Default player: the last SUBS.DAT
  class in service at the scenario date, named "(SS-<hull>) <boat>" from the
  first CARSUB.DAT boat of that class, depth = class +0x9c, at the chart
  centre moved to the nearest open water.
- **2 ship** (`sbship_write_scn`): exactly the 300-byte SH record (`x, y,
  heading, speed, orders, crew, damage_pct, char[25] class, char[31] name,
  i32[25] wp_x, i32[25] wp_y, n_waypoints, convoy_group, zigzag, alert`).
  Orders 0 None, 1 Escort, 2 Patrol, 3 Convoy Leader, 4 Convoy Member,
  5 Intercept, 6 Follow Path, 7 Fishing.
- **4 enemy sub** (`sbsub_write_scn`): SH's file order (`x, y, depth, heading,
  speed, orders, crew, damage_pct, char[25] class, wp_x[25], wp_y[25],
  n_waypoints, target_x, target_y, alert, side`); in memory `side` sits after
  `speed`, as in SH. Ships and subs are one editor object (`sbvessel`) holding
  both records; its kind decides the tag, so switching kind in the editor
  keeps position, heading, speed, crew, damage and waypoints.
- **3 aircraft** (`sbair_write_scn`): SH's record (`mission, crew, char[25]
  type, x, y, orbit_radius, strafes, bomb_load, wp_x[25], wp_y[25],
  n_waypoints, escort_group, relaunch`). The editor's aircraft orders are
  1 Escort (with `escort_group`), 2 Patrol, 6 Follow Path.
- **5 airstrike** (`sbmission_write_scn`): `base, size, delay, hold` (size
  0-2 = Small / Medium / Large, 10 / 30 / 50 planes).
- **6 photo recon**: `base, delay, duration`.

### Differences with what SH reads

- Nothing in the written file is unknown to SH's reader and the record
  layouts match `scen_read_*` field for field (the reader is the same code).
- Written but not used by SH: the player's `unk5`; the password (read into
  0x126004, never used); the count fields of the -1 reload entries.
- Read by SH but never produced by the editor: nothing in v7. Older versions
  only lack fields (SH defaults: end-on-lost-contact and randomize -1,
  special_mission 0 below v4).

### Re-saving older versions

A version 2-6 file loaded and saved comes out as version 7 with the values
`scen_load_ascn` (SE 0x479a9) and the record readers leave in the fields the
file lacks, then the editor's rebuild:

- **`special_mission`**: read, then cleared below version 4; recomputed at
  the save anyway.
- **`end_on_lost_contact`, `randomize`**: -1 below version 4 (set before the
  header is read).
- **`random_seed`**: not touched below version 4, so the seed drawn by the
  full `sb_reset_scenario` that precedes every load (`time() + 1` at the
  load) is written.
- **`password`**: below version 4 only its first byte is cleared (no
  password); the other 14 bytes keep the previous scenario's and are written
  (the reimplementation's editor zeroes them).
- **`required_memory`**: -1 below version 7, then accumulated from that -1 by
  the object spawners, one byte low (original bug).
- **Records**: player `crew` 0 below version 3, aircraft `relaunch` 1 below
  version 6; an enemy sub's `side` below version 5 is whatever
  `scen_read_enemy_sub`'s stack held. Records are regrouped and the reload
  table rebuilt as for any load.

The reimplementation (`src/assets/scn_file`) reads every version into
a lossless model and writes version 7 byte for byte (all 31 shipped version-7
files round-trip). Its deterministic choices for the stale values: seed 0,
password bytes zero, `side` 0 (the values the game's reader gives). The
rebuild (record order, `special_mission`, reloads, `required_memory`) belongs
to the editor's object model (`src/editor/scenario`), whose load also
keeps the reset's seed as above (and zeroes the password). A
version-7 file keeps its own `required_memory` (the spawners only count
below version 7); the estimate computed from the tables equals the shipped
value, or is one more for the files re-saved from older versions (HIST06,
HIST32-45).

## `CUSTOM\AEONSCEN.TMP` **[verified: same writer]**

File / Run (`cmd_run_scenario`) writes the current scenario with
`scen_write_ascn` to `<CUSTOM>\AEONSCEN.TMP` (CUSTOM path from `SHPATH.INI`),
byte for byte the same format as a saved `.SCN`, then sets the exit code 0xa50c8
= 2 so the editor returns 1 to SH, which plays the file and deletes it
(`editor-design.md`, "One program, the editor as a library"). Before writing it
runs the same checks as Save: the "No ships are within detection range"
confirmation (only when end-on-lost-contact is set and no vessel is in visual or
radar range of the player and there is no airstrike / photo recon), the memory
warning, and the "Save changes to scenario?" prompt when the scenario is
modified. Write results (`scen_write_ascn` returns 0, `dfile_open`'s 4-6, or
`dfile_write`'s 1 = write() failed / 2 = short write): 0 runs; 2 "DISK FULL" and
4-6 "CANNOT CREATE FILE" stay in the editor; 1 shows "UNABLE TO WRITE TO FILE"
and still sets the Run exit code (an original bug: SH then plays a truncated
file). Save maps the codes the same way ("CANNOT OPEN FILE" for 4-6) and clears
the modified flag even after "UNABLE TO WRITE TO FILE".

## `SAVE\SBOPTION.SAV` **[verified: `sboption_load` 0x473ef, `sboption_save` 0x475b1]**

37 bytes:

    u8             version         2
    i32 x 9        0xa50e4..0xa5104, 0 / -1: limit ships, subs, aircraft,
                   patrol areas, equipment by date; display range circles;
                   display convoy icons; exit on break contact; randomize

`sboption_load` sets the 9 values to -1, then reads the file with plain
`open`/`read`. A missing file or another version is rewritten with the current
values (`sboption_save`) and read again; only failed opens are counted (two
attempts), so a wrong-version file the rewrite cannot replace makes SE retry
forever (original bug; the reimplementation gives up after one rewrite). A short
read keeps the values read so far, and an empty file (no version byte) is kept,
with all options on, without a rewrite. The last two options are the defaults
copied into a new scenario's end-on-lost-contact and randomize. The shipped file
holds 0 except range circles and convoy icons (-1). `load_shpath_ini` creates
the SAVE and CUSTOM directories at start-up if they are missing.

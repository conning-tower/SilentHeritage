# SE.EXE: in-memory structures

The editor's object classes, globals and data tables, and the bugs found in
the original. The file formats are in `editor-formats.md`. Addresses are
linear addresses in the relocated `SE.EXE` image.

## Shared state at shifted SH addresses

SE links SH's `scen.cpp` and `chart0.cpp` unchanged, so their globals keep
the SH layout at a constant offset:

- **Scenario header and unit arrays**: SE = SH - 0x766bc. Version 0xaf4a4,
  briefing 0xaf4a8[1000], special mission 0xaf890, world 0xaf894[13],
  weather, sea, fog, gradient, month (0-based), day, year (- 1930), hour,
  minute 0xaf8a4..0xaf8c4, tube torpedoes 0xaf8cc[10], reloads 0xaf8f4[15],
  equipment 0xaf930, rating 0xaf934, end on lost contact 0xaf93c, randomize
  0xaf940, random seed 0xaf944, password 0xaf948[15] (encoded), required
  memory 0xaf958 (the live scenario size), player record 0xaf95c, unit arrays
  0xaf9a8..0xaf9bc and counts 0xaf9c0..0xaf9d4 (only between load and
  `scen_instantiate_units`).
- **Chart view state**: SE = SH - 0x74344 (view moved 0xa4728, zoom box
  0xa4734, requested panel 0xa4748, active panel 0xa474c, ...); the view
  object is 0xa4e30 (zoom at +0x24).

## Editor objects

The scenario is the intrusive list `slist` 0xa5088 (count 0xa5094, last
inserted 0xa5090) of C++ objects `{+0 next, +4 vtable, +8 .SCN record...}`;
the record is the file's record body unchanged, so `+0x40` writes it field by
field and the loader's `sbfile_spawn_*` copy it in. Object 0 is always the
player; 0xa5098 is the selected object.

- **`sbplayer`** (0x54 bytes, vtable 0x97f48): the 0x4c-byte player record.
- **`sbvessel`** (0x2e4, 0x97e1c): every ship or enemy sub. +8 kind (0 ship,
  1 sub), +0xc / +0x10 / +0x14 seen by the player / inside its radar range /
  sighting range in yards (set by `update_detection`), +0x18 an `sbship`,
  +0x188 an `sbsub`. Both parts are kept in sync, so a class change can switch
  the kind and keep position, heading, speed, crew, damage and waypoints.
- **`sbship`** (0x170, 0x97d8c): the 300-byte ship record, then the SHIPS.DAT
  `sprite1` / `sprite2` model names (+0x134 / +0x152).
- **`sbsub`** (0x97dd4): the 0x118-byte enemy-sub record, then the model
  names (+0x120 / +0x13e).
- **`sbair`** (0x134, 0x97e64): the 0x10c-byte aircraft record, then the
  AIRCRAFT.DAT sprite name (+0x114).
- **`sbmission`** (0x1c, 0x97eac): u8 tag +8 (5 airstrike, 6 photo recon),
  then base +0xc and the tag's fields (airstrike size, delay, hold; photo
  recon delay, duration).

Vtable (18 slots, methods named `<class>_<slot>`; `sbobj_*` are the shared
defaults, `<class>_construct` / `<class>_deleting_dtor` the constructors):

- **+0x00 `get_position`**(&x, &y), **+0x04 `move_to`**(screen x, y; no-op
  for the parts and missions), **+0x08 `draw`**(zoomed out, pass, selected)
  -> rect, **+0x0c `hit_rect`**.
- **+0x10 `type_id`** (0 player, 1 ship, 2 sub, 3 aircraft, 4 airstrike,
  5 photo recon), **+0x14 `type_name`** ("PLAYER" .. "PHOTO RECON").
- **+0x18 `convoy_group`**(leader?): group id of a Convoy Leader / Member
  ship; **+0x1c `escorted_group`**: ship with Escort orders, aircraft with
  mission 1 (Escort); convoys are ids 1-8, shown as letters A-H.
- **+0x20 `is_special_mission`**: -1 only for `sbmission` (the `.SCN`
  `special_mission`).
- **+0x24 `describe`**: the hover-popup text (up to 6 lines of 75 chars).
- **+0x28 `update_detection`**, **+0x2c `visual_range`** (yards),
  **+0x30 `in_detection_range`**: vessels only; drive the range circles,
  the "NO SHIPS IN RANGE?" check and the TOOLBOX indicators (the eye at
  (0xd5, 0x133) lit when the player could see the vessel, the radar at
  (0xfc, 0x133) when it is within the player's surface radar range, never
  without a radar; confirmed in DOSBox).
- **+0x34 `memory_cost`**: player 0, ship 1300, sub 3000, aircraft 900,
  airstrike 500, photo recon 300.
- **+0x38 / +0x3c `model_name1` / `model_name2`**: 3D model file names (0 for
  none), reference-counted in the model cache 0xae754 (130 entries of 0x18
  bytes: name[16], count, catalogue size; `model_cache_add` / `_release`).
- **+0x40 `write_scn`**, **+0x44 `deleting_dtor`**.

## Editor globals

- **Program**: exit code 0xa50c8 (1 Exit, 2 Run, 3 Quit to DOS; the process
  returns it - 1 to SH), modified flag 0xa50c4 ("Save changes to scenario?"),
  started by SH 0xa5060 (`/GFXADR=` given: File / Run offered), scenario name
  0xa6bb8[9], menu selection 0xa6d38.
- **Options** (`SBOPTION.SAV`) 0xa50e4..0xa5104: limit ships, subs,
  aircraft, patrol areas, equipment by date; range circles; convoy icons;
  defaults for end on lost contact and randomize.
- **Memory budget** 0xa5108 = BASMEM - (world sizes + SFXBUF + 0x4b6f90),
  compared with the scenario size 0xaf958 (toolbox gauge, "SCENARIO IS TOO
  LARGE").
- **Data tables**: SUBS.DAT 0xa5064 (count 0xa506c, 0x6a8 bytes each),
  SHIPS.DAT 0xa5070 (0xa5078, 0xf4), AIRCRAFT.DAT 0xa507c (0xa5084, 0xa8),
  WEAPONS torpedoes 0xa50a0 (0xa50a4), torpedo choices 0xa50a8 / 0xa50ac
  (count 0xa50b0, with "EMPTY"), per-torpedo reload counts aft 0xa50b4 /
  forward 0xa50b8, CARSUB.DAT 0xa50bc (0xa50c0), PZ.DAT 0xa50d0 (0xa50cc) with
  enabled-zone flags 0xa50d4, bases 0xa4524 (count 0xa4528, owners today
  0xa452c), base list 0xa50d8 (`sb_build_base_list`) with the numbers of
  valid airstrike / photo-recon targets 0xa50dc / 0xa50e0.
- **Chart editing state** (click mode 0xaacd4, prompts, pending additions,
  toolbox record pointers, change detection, popup): cited in
  `editor/chart_screen.h`.

## Bugs in the original

Some of these were checked by running the original under DOSBox. What the
reimplementation does with each (fixed or kept, and why) is in
`editor-design.md` "Original bugs".

- **Sub class by list position**: `select_sub_type` stores the index in the
  filtered list as the SUBS.DAT class index (0x25b2f: `mov eax, [0xa8c5c]`
  then `mov [edx], eax`, also on CANCEL); with "Limit by date: Subs" on,
  the player's sub gets the wrong class (and SELECT SUB NAME the wrong
  boats). Real, but with the shipped SUBS.DAT only from April 1945: until
  then the filter only drops the classes at the end of the list (not yet
  in service), so the indexes match.
- **Run after a write error**: a failed `dfile_write` (code 1) shows "UNABLE
  TO WRITE TO FILE" but still exits with the Run code, so SH plays a
  truncated `AEONSCEN.TMP`, which SH's `scen_load_ascn` rejects with a
  fatal error (`scen_report_load_error` 0x53c2a: 0x37 read error or 0x20,
  back to DOS); Save clears the modified flag in the same case. A
  read-only file fails to open (CANNOT CREATE FILE), which aborts Run.
- **SELECT A CITY cancel** (`sbchart_city_button_cancel`, also Esc) unlinks
  the selected object: for an existing airstrike opened from its Location
  button the airstrike disappears, also from the saved file (confirmed in
  DOSBox; intended, consistent with the UI). The bugs around it: the
  object is not freed, the modified flag is cleared (unsaved changes can
  be lost without a prompt), and the object remembered by the last ADD
  AIRSTRIKE / PHOTO RECON (0xaad24, possibly removed since) is selected
  again.
- **`sbfile_spawn_enemy_sub`** copies both model names to +0x2a8, so the
  first model's cache reference is never released (the memory estimate keeps
  it after the sub is removed).
- **`required_memory`** of a loaded pre-v7 file is accumulated from
  `scen_load_ascn`'s initial -1 (one byte low).
- **Reload table**: entries 3-4 are written with type -1 and stale counts; on
  load the counts are copied back by position, not by torpedo type (SE's
  own files always list the types in the same order).
- **No player record**: a file without tag 1 still gets a player object,
  built from the stale player record 0xaf95c.
- **Convoy member join** (`sbchart_apply_convoy_membership`): after copying
  the leader's heading and speed, the sub part's speed is zeroed and stored
  into the ship's damage (+0x38): a new Convoy Member loses its damage.
- **Escort speed of aircraft** (`sb_convoy_update_escort_speed`): every
  object whose slot +0x1c names the convoy is written as a ship record, so
  an escorting aircraft's x (+0x2c) gets twice the leader's speed, then the
  top speed `sb_ship_max_speed` reads for it (its bomb load taken as a class
  name: past the end of SHIPS.DAT): the aircraft jumps to the chart's west
  edge whenever the leader's speed digits change.
- **Escort refresh flag** (`sbchart_toolbox_update`): 0xaad10 is set to -1
  instead of 0 after the escort update, so once a leader's speed changed
  its escorts are recomputed on every update until `sbchart_toolbox_open`
  (0x3bc38) clears it. Harmless (same result).
- **Scenario lists** (`scenario_list_build`): reading each listed file's
  header with `scen_load_ascn(path, 0, 0)` overwrites the briefing 0xaf4a8
  and version 0xaf4a4 globals of the current scenario (Load Historical,
  Load Scenario, Delete Scenario): after a cancel, Edit / Briefing Text
  shows the last listed file's briefing.
- **Copy-paste slip**: `sbchart_city_panel_redraw` and
  `sbchart_remove_panel_redraw` invalidate dialog 0xad954 instead of their
  own (not visible).
- **ENVIRONMENT preview light** (`environment_update_lighting`): the
  preview writes the sighting light 0xf6224 whenever it is redrawn, so
  after CANCEL the visibility checks use the light of the last date and
  time shown (until the next OK, load or New).
- **Password storage** (`cmd_set_password`): clearing the password copies the
  empty entry with `strcpy`, which zeroes the first stored byte only (the other
  14 keep the old password's encoded bytes, saved in the file;
  `sb_reset_scenario` and pre-v4 loads do the same). Setting one copies the
  encoded buffer with `strcpy`, which has no NUL within its 15 bytes (the key
  makes the bytes after the text non-zero): the copy ends in the padding byte
  0xaf957, harmless. The entry holds 14 characters at most (`max_length` 15 with
  the NUL), and a 14-character password works.
- **PATROL AREA map click** (`patrol_area_idle`): the zone hotspots come
  after the dialog's own, so a press on a zone fades the screen to black
  and sets 0xa6d74, which nothing clears (its only other reference is the
  test at 0x2518b): the dialog stays open, black, until OK, CANCEL or Esc,
  and every later PATROL AREA of the session fades to black after its
  first frames.
- **Reload digits** (`reload_*_cycle`): a digit keeps cycling until the
  racks' total fits the capacity, forever when the other counts alone
  exceed it (a loaded file).
- Editor limits rather than bugs: enemy subs only get None or Follow Path
  ("Patrol"; Intercept and its target are never set), Follow Path ships only
  come from loaded files, SELECT SUB NAME is not filtered by date (the manual
  says it is), the EQUIPMENT dates differ from the manual's table, and the
  password protects editing only (the manual says playing: SH's
  `scen_load_ascn` reads it into 0x126004, which nothing else reads;
  Delete Scenario does not ask it either).

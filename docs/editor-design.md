# SE.EXE: design of the editor's port

How the scenario editor `SE.EXE` is reimplemented, with the game's principles
(`README.md`, "Principles"): the original 640x480 8-bit look, the original
behaviour, the original data read at runtime. Its formats are in
`editor-formats.md`, its objects and globals in `editor-structures.md`, its
files and palettes in `editor-assets.md`.

## Hand-off with the game

The original game's `run_scenario_editor` (SH `0xa22d2`, BUILD CUSTOM
MISSION) frees its GUI and sound, checks the free memory and spawns
`SE.EXE /GFXADR=<n> /SFXBUF=<n> /BASMEM=<n>`. The editor's exit code
(`0xa50c8` - 1) tells the game what to do:

- **0**: File / Exit (`0xa50c8` = 1), back to the game's menu.
- **1**: File / Run (`cmd_run_scenario`, `0xa50c8` = 2): the editor saved the
  scenario to `CUSTOM\AEONSCEN.TMP`; the game shows the realism menu, plays
  it with `run_mission`, records the score, then deletes the file.
- **2**: Quit to DOS (`quit_menu_select`, `0xa50c8` = 3): the game exits too.

Started from DOS (no `/GFXADR=`), the editor offers no File / Run.

## One program, the editor as a library

The editor is the library `sh_editor` (`src/editor/`, namespace
`sh::editor`), entered from the front end's BUILD CUSTOM MISSION and with
`--editor` (SE started from DOS: no File / Run).

Run needs no second process. The editor returns `EditorExit { kExit, kRun,
kQuitToDos }` (the exit codes 0 / 1 / 2) to the single-mission menu, which
then does what `run_scenario_editor` does after `spawnl`: "ONE MOMENT
PLEASE...", quit on 2, and on 1 the realism menu, the mission of
`CUSTOM\AEONSCEN.TMP` (its briefing first), the score, the file deleted,
then the single-mission menu again. The file stays the hand-off, so the game
plays exactly the bytes the editor wrote.

Sharing the process is simple because the port keeps its state in objects
(`ui::Gui`, `FrontEnd`, service structs) rather than globals: the editor
builds its own `Editor` object, which holds SE's globals, its skin, cursors
and palette on the same screen, and nothing has to be reset between editor
and game. A second executable would only add process spawning, passing the
install and save directories, closing and reopening the window (the original
kept the VESA mode) and a second binary to ship.

`sh_editor` depends on `sh_assets`, `sh_ui`, `sh_gfx`, `sh_sim` and
`sh_game`; only `app/` depends on it. The front end calls it through an
`EditorRunner` interface in `game/frontend_host.h` (like `MissionRunner`), so
`sh_game` does not link the editor. Its tests are a separate executable,
`sh_editor_tests` (`tests/editor/`).

The CUSTOM directory is a `SaveDir`: the player's `CUSTOM` beside `SAVE` (or
`<--save>/CUSTOM`) over the install's `CUSTOM\`, deletions masked by empty
files; the install is never written. It is read by the game's scenario lists
and `Mission::load` for `CUSTOM\` paths.

## Modules

Outside `editor/` (shared with the game):

- `assets/scn_file`: the lossless `.SCN` model, header and records as the
  original keeps them in memory (whole char buffers, all 25 waypoint slots,
  the five reload triples with their counts, the encoded password, records in
  file order); `read_scn_file` (versions 2..7, the reader's defaults for
  missing fields), `write_scn_file` (always version 7, `scen_write_ascn`
  layout), password encode / decode. The game's `parse_scenario` is a view of
  this model.
- `game/stations/chart_panels`: the chart parts SE shares with the game (SE
  links SH's `chart0.cpp`): panel host and drag, zoom box and magnifier,
  rubber band, hover popup and its placement, edge scroll.
- `ui/menu_bar` (`gui_menu_bar_run`, SE only) and `ui/dial_control` (the
  heading dial, `gui_dial_handle` 0x97f04).
- `game/frontend` (BUILD CUSTOM MISSION through `EditorRunner`), `app/`
  (`--editor`).

In `editor/`:

- `se_strings`: the `SE.EXE` image and its strings by SE address (menus,
  dialog texts, prompts, popups, errors).
- `options`: `SBOPTION.SAV` (`sboption_load` / `_save`, through `SaveDir`).
- `data`: `sb_load_data`: the tables, CARSUB, the PZ zones, the patrol map
  with its bases and their owners at the scenario date, the base lists for
  airstrikes / photo recon, the 3D-model catalogue sizes, the memory budget.
- `objects`: `EditorObject` with the 18 vtable slots as virtual methods
  (`editor-structures.md`), `PlayerSub`, `Vessel` (ship and sub parts kept in
  sync), `Aircraft`, `SpecialMission` (airstrike / photo recon), the model
  cache.
- `scenario`: the header state and the object list (player first, selection,
  insertion order): `sb_reset_scenario`, the default player,
  `sb_reset_torpedo_load`, New / load / save (header rebuilt:
  `special_mission`, reloads, `required_memory`), add / remove / move / class
  changes, convoy helpers, courses, visibility, the modified flag.
- `chart_screen`: `sbchart_run`: the 640x459 view and status bar, the SE chart
  palette, cursors, panels, click modes 0-11 and their chains, drawing (bases,
  courses, icons, range circles, convoy icons, selection), hit testing, hover
  popup, SELECT A CITY.
- `toolbox`: TOOLBOX and its six object panels with every field callback,
  PLOT COURSE, the pick panel, REMOVE OBJECT, the add tools.
- `editor_ui`, `environment`, `loadout`, `class_sheets`: the editor's screen,
  fonts and skin; the ENVIRONMENT working copy and its sky preview; the
  EQUIPMENT fit dates and the TORPEDOES working copy; the SELECT lists by date
  and the class sheets' texts.
- `dialogs_settings` (ENVIRONMENT, EQUIPMENT, TORPEDOES, BRIEFING TEXT,
  OPTIONS, PASSWORD), `dialogs_select` (SELECT SUB TYPE / SUB NAME / SHIP TYPE
  / AIRCRAFT TYPE), `dialogs_patrol` (PATROL AREA, the ORDERS popup),
  `dialogs_boxes` (message, yes / no, save changes, QUIT TO DOS?, About),
  `dialogs_file` (SAVE / LOAD / LOAD HISTORICAL / DELETE).
- `file_commands`: Save with the range and size checks, overwrite, the
  writer's error codes and boxes, New, the loads with their read errors and
  the password, Delete, Run.
- `editor`: `Editor`: start-up (title box), the menu bar read from SE.EXE's
  tables and its hotkeys (`editor_menu_handle_event`), every File / Edit
  command.

Dialog geometry comes from SE's two static initializers
(`guiuser_init_dialogs`, `sbchart_init_dialogs`), transcribed into the code;
the texts are read from `SE.EXE`, nothing is embedded.

## Tests

`sh_editor_tests` (built and run like `sh_tests`, the game files from
`SH_DATA`):

- the `.SCN` model: the 31 shipped version-7 files read and written back
  byte-identical, also through the editor's objects; the 30 older ones
  re-saved as SE does (records regrouped, reload table, `required_memory` one
  byte low); the memory estimate of every version-7 file; truncated and
  damaged files refused;
- `SBOPTION.SAV`, New, add / remove, class changes, convoys, courses, moves,
  hover texts, visibility, the budget;
- the chart, toolbox and every dialog driven through the GUI by scripted
  clicks and keys, with screenshots (`SH_SCREENS`, `SH_SCREENSHOT_DIR`);
- whole sessions: a scenario with every object type and setting built with
  the real dialogs, saved, loaded back, run and played headless; the 60
  historical scenarios through Load Historical and Save Scenario;
- the screens of DOSBox captures of the original editor rebuilt in the same
  state (`dosbox_screens_test.cpp`) and compared with them (dialogs, panels,
  range circles, indicators, courses).

Deviations: Alt-M (the MemCheck report) does nothing; during the title box's
wait the chart is not drawn yet around it (black; the original shows the
map).

## Original bugs

The port is faithful by default, but obvious editor bugs (crashes, hangs,
data loss, memory corruption, a wrong record field, clearly unintended
behaviour) are fixed; debatable or harmless quirks stay. Each site keeps an
`// Original bug:` comment naming the entry below and saying what SE did. The
game's own bugs stay faithful.

Fixed in the reimplementation (SE's behaviour in each entry):

- **Sub class by list position** (`select_sub_type`): SE stores the row of
  the date-limited list as the SUBS.DAT class (also on CANCEL) and chains
  to SELECT SUB NAME on a changed row, so with "Limit by date: Subs" the
  sub gets another class and SELECT SUB NAME that class's boats. Real, but
  with the shipped SUBS.DAT only from April 1945 (BARRACUDA leaves after
  March 1945, NARWHAL after April; until then the date filter only drops
  classes at the end of the list). Fixed: the row's class; CANCEL keeps the
  class; the chain follows a changed class.
- **Run after a write error** (`cmd_run_scenario`, `cmd_save_scenario`):
  "UNABLE TO WRITE TO FILE" (write code 1) still ends with Run, and SH
  stops with a fatal error on the truncated `AEONSCEN.TMP`
  (`scen_report_load_error` 0x53c2a, 0x37 / 0x20, back to DOS); Save
  clears the modified flag. Fixed: every write error aborts Run, Save
  keeps the flag (and writes are whole or nothing anyway). A read-only
  `AEONSCEN.TMP` is a CANNOT CREATE FILE, which aborts in SE too.
- **SELECT A CITY cancel** (`sbchart_city_button_cancel`): removing the
  mission, also when opened from an existing airstrike's Location button,
  is intended (confirmed in DOSBox; consistent with the UI) and kept. Fixed:
  SE unlinks it without freeing it, clears the modified flag (unsaved
  changes could be lost without a prompt) and selects the object
  remembered by the last ADD AIRSTRIKE / PHOTO RECON, possibly removed
  since (a dangling pointer). Here the mission is removed (modified set),
  then the object selected before ADD, or the player.
- **Enemy-sub model names** (`sbfile_spawn_enemy_sub`): both names go to
  +0x2a8, so the first model's cache reference is never released and the
  estimate keeps it after the sub is removed. Fixed.
- **Convoy member join** (`sbchart_apply_convoy_membership`): after
  copying the leader's speed, the sub part's speed is zeroed and copied
  into the ship's damage, so a ship joining as a Convoy Member loses its
  damage. Fixed (both parts keep the leader's speed, the damage stays).
- **Escort speed of aircraft** (`sb_convoy_update_escort_speed`): an
  aircraft escorting the convoy is written as a ship record when the
  leader's speed changes: its x gets twice the leader's speed, capped by
  the top speed of a "class" read from its bomb load past SHIPS.DAT, so
  the aircraft jumps to the chart's west edge. Fixed: aircraft are left
  alone.
- **Scenario lists overwrite the briefing** (`scenario_list_build`): each
  listed header is read with `scen_load_ascn` into the current scenario's
  briefing and version globals, so the scenario keeps the last listed
  file's briefing after Load Historical / Load Scenario / Delete Scenario
  is cancelled (Delete does not even ask to save first). Fixed: the
  headers are read into locals.
- **ENVIRONMENT preview light** (`environment_update_lighting`): the
  preview writes the sighting light 0xf6224, so after CANCEL the
  visibility uses the light of the last date and time shown. Fixed: CANCEL
  puts the light back.
- **PATROL AREA map click** (`patrol_area_idle`): a press on a zone fades
  the screen out and sets 0xa6d74, which fades it out on every later idle
  call and is never cleared; the dialog ignores the zone hotspots and
  stays open, black, until OK / CANCEL / Esc, and every later PATROL AREA
  of the session is black too. Fixed: the press accepts the zone (as OK).
- **Password storage** (`cmd_set_password`, `sb_reset_scenario`, pre-v4
  loads): clearing the password, New, and loading a file older than
  version 4 zero its first byte only, so the old password's encoded bytes
  1-14 are saved in the next file. Fixed: all 15 bytes zeroed. Setting one
  copies the encoded buffer with `strcpy` past its 15 bytes (its NUL lands
  in the padding byte 0xaf957): harmless, nothing to fix (no overrun here).
- **Reload digits** (`reload_*_cycle`): a digit cycles forever when the
  other counts alone exceed the racks' capacity (a loaded file). Fixed: one
  full turn, then it stops.
- **Digit cycles hang** (`toolbox_sub_depth_digit*` and the other digit
  cycles): forever when no value fits (a player sub with less water under
  it than its draft + 1). Fixed: the value stays after ten tries.
- **`SBOPTION.SAV` retry loop** (`sboption_load`): a wrong-version file
  that cannot be rewritten hangs SE. Fixed: one rewrite, then the defaults.
- **Unknown classes**: an object whose class name is in no table reads
  past SHIPS.DAT / SUBS.DAT. Fixed: no class icon, a draft of 0.
- **Damaged files** (`Scenario::check_file`): SE indexes its tables with
  any value read. Fixed: CANNOT READ FILE for a weather, sea state, date or
  time outside ENVIRONMENT's choices, a waypoint count past 24, a crew,
  orders, heading or convoy out of its lists; route legs whose clipping
  does not end are dropped.
- **Wrong dialog invalidated** by the SELECT A CITY and REMOVE OBJECT
  redraws: their redraw slots invalidate the closed pick panel (not
  visible). The merged slot invalidates the open panel.
- **Stale heap bytes** after the NUL of char buffers: loaded bytes are
  kept; new buffers are zero-filled, as are the fields the load leaves
  unset in the other part of a vessel.

Kept (with the reason):

- **Pre-v7 `required_memory`** one byte low (accumulated from the reader's
  -1): a 1-byte error in an estimate; kept so re-saved old files match
  SE's.
- **Pre-v4 seed**: the seed the reset before the load drew (time() + 1);
  a new seed for a file without one is reasonable. (`read_scn_file` gives
  0, as the game's reader.)
- **Reload table**: terminator entries keep stale counts and counts are
  copied back by position, not by type: SH stops at the -1 type, and SE's
  files always list the types in SE's order; needed for byte-identical
  files.
- **No player record**: the player object is built from the stale player
  record; the reimplementation uses the record of the previous load (or
  the default player), the nearest deterministic equivalent. Damaged
  files only.
- **Range circles off** (`sbplayer_draw`): the courses pass draws the
  player's icon too, drawn again by the icon pass. Harmless.
- **Escorts follow the leader every frame** (`sbchart_toolbox_update`):
  the escort-speed flag 0xaad10 is set again instead of cleared, so after a
  leader's speed change its escorts' speed is recomputed on every toolbox
  update until the toolbox reopens. Harmless (same result each time) now
  that aircraft are left alone.
- **Ship <-> submarine class change** (`sbchart_change_ship_class`): the
  vessel keeps its position, heading, speed, crew and damage (both parts
  are written), loses its orders (subs have their own role, 4: always a
  role change, a leader's convoy released) and shows the other part's
  course (none for a vessel placed in the editor, the course read from the
  file for a loaded one); the course plotted before stays in the hidden
  part and comes back on a switch back. Consistent with what the toolbox
  then shows.
- **Started from DOS** (`--editor`): palette entries 86-127 stay black (no
  `M8.PCX`), as in the original.
- Editor limits, kept as is: enemy subs only get None / Follow Path,
  Follow Path ships only from loaded files, SELECT SUB NAME not filtered by
  date, the EQUIPMENT dates differ from the manual, the password protects
  editing only (SH reads and ignores it), Delete Scenario asks no
  password, the airstrike hold labelled "Flight Delay", ADD SHIP placing
  one vessel per use (the vessel stays selected and further clicks move
  it, confirmed in DOSBox), an escort's "Alerted:" shown as Off / On, the
  toolbox's highlight of the orders field never drawn (its flag is only
  cleared).

## Decisions

- **Memory budget**: the gauge and SCENARIO IS TOO LARGE compare with
  BASMEM - (world sizes + SFXBUF + 0x4b6f90), BASMEM and SFXBUF coming from
  SH's command line. The reimplementation uses SE's own defaults, those it
  computes when started without `/BASMEM=` and `/SFXBUF=` (0x2d7bb with
  `se_main`'s argument 1: BASMEM = free memory at start-up + 0x183000,
  0x230000 being SH's; SFXBUF = 530000 plus a share of the memory above
  0x6d4f70, capped at 0x1ab3f0), with a fixed free memory of 15 MB
  (`editor::kAssumedFreeMemory`, a DOSBox 16 MB machine; SFXBUF is then at
  its cap). An optional mode without the budget, or with one matching the
  running game's, could lift this limitation.
- **Chart panel host**: SE runs SH's chart0 panel code; the reimplementation
  has it in a module shared by the chart station and the editor
  (`game/stations/chart_panels`), not copied into `editor/`.

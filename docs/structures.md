# Game tables, simulation objects and behaviour

The record layouts of the `DATA/*.DAT` game tables (each scheme below
reproduces the exact file length; parsed by `assets/dat_tables.h`), the
original's simulation objects and globals, the behaviour models the
simulation reproduces and the bugs of the original. Addresses are linear
addresses in the relocated `SH.EXE`; function names are those cited in the
code comments (see the README's "Provenance").

**Confidence key.** *Framing* = record boundaries and field offsets are verified
against the byte strides. *Semantics* = what a field means; some are still
tentative (marked `?`). See the open questions at the bottom.

## Framing schemes

Four schemes cover the eight table files:

- **indexed** — `u8 count`, then `count` 32-byte index records
  (`char[28] name` + `u32 file_offset`), then fixed-size detail records the
  index points at. The detail-record size is the gap between consecutive index
  offsets. Used by `AIRCRAFT.DAT`, `SHIPS.DAT`, `SUBS.DAT`.
- **counted** — `u32 count`, then `count` fixed-size records back to back.
  Used by `CMDLIST.DAT`.
- **flat** — a bare array of fixed-size records, no header; `count =
  len/stride`. Used by `CARSUB.DAT`, `IJNSHIPS.DAT`, `HOMEPORT.DAT`.
- **weapons** — two `counted` sections in one file (torpedoes, then guns).
  Used by `WEAPONS.DAT`.

All multi-byte integers are little-endian. Name fields are NUL-terminated and
NUL/space padded to the field width.

## Per-file layouts

### `AIRCRAFT.DAT` — indexed, 5 records x 168-byte detail **[mostly decoded]**

Japanese aircraft (`ZEKE`, `KATE`, `JILL`, `VAL`, `BETTY`). Layout: `+0
name[25]`, `+25 long_name[25]` (`MITSUBISHI A6M`), `+50 name2[25]`, `+75 u8
role` (0 fighter, 1 torpedo bomber, 2 dive bomber, 3 level bomber), `+76
sprite[16]` (`ZEKE.SHP`), then: `+92 intro_year`, `+96 end_year`, `+100/+104`
months, `+116 aircraft_id` (0..4), `+120 cruise_speed_mph` (Zeke 180, Kate 140),
`+124 max_speed_mph`, `+128 ceiling_ft`, `+132/+136/+140` length / span / height
in ft (Betty 67 / 82 / 19), `+144 range_mi`. Verified vs history: Zeke 1940 /
32800 ft ceiling; Kate 235 mph; Jill 299 mph; Betty 3765 mi range; Val 840 mi.
From the aircraft AI: `+148 guns` (> 0: can strafe) and `+152 bombs` (bomb load:
Zeke 2, Kate/Jill 3, Val 1, Betty 4). From the `SE.EXE` class sheet: `+156
torpedoes` (1 on Kate, Jill, Betty). `+108` is the 16.16 render size
(`aircraft_add_render` 0x84871), `+160` the flight model's roll rate (tenths),
and `+164` holds at run time the 3DAIR.CAT sprite of `+76` (`load_aircraft_dat`
0x5158e; 0 in the file). `+112` (24..28) has no reader found.

### `SHIPS.DAT` — indexed, 55 records x 244-byte detail **[mostly decoded]**

Ship *classes* (IJN warships `FUSO`/`ISE`/`YAMATO`/… plus generic merchant
types `MERCHANT`/`OILTANK`/`SAMPAN`/…); `IJNSHIPS.DAT` instances key in by
`class_name`. Layout (offsets within the record; full record in
`assets/dat_tables.h`):

- `+0  char name[25]`, `+25 u8 ship_type` (0 CV, 1 CVE, 2 CVS, 3 BB, 4 BBV,
  5 CA, 6 CL, 7 DD, 8 DE, 9 kaibokan, 11 SS, 12-20 merchant types, 21 sampan),
  `+26 u8 role` (0 merchant, 1 escort, 2 warship, 3 small craft, 4 submarine;
  picks the contact/sighting class), `+27/+40/+53` three 13-byte sprite
  filenames (`FUSO.SHP`/`FUSOS.SHP`/… — profile/silhouette variants; `+53`
  is the `ART\IDBOOK.CAT` picture the `SE.EXE` class sheet shows).
- `+68 u32 start_year`, `+72 u32 end_year`, `+76/+80` start/end month.
- `+84 i32 selection_weight` — weight of the class when the scenario
  generator picks one of a role (-1 on merchants, which are picked
  otherwise); `+88/+92` the 16.16 render sizes of the hull sprite
  (`ship_add_render` 0x8197e, `enemy_sub_add_render` 0x7fb89) and of the bow
  wave (`ship_render_cb` 0x81a84); `+96 category`; `+100 screw_sound`
  (hydrophone report: 0 high-speed, 1 low-speed, 2 none); `+104 fire_order`
  (bit 0/1: the fire overlay goes behind the hull in the bow/stern views,
  `veh_draw_ship_sprite`).
- `+108 u32 hull_strength` — durability (Yamato 600 > battleships 400 >
  merchant 160 > sampan 4).
- `+136 u32 main_gun_count` (Fuso 12, Yamato 9, Nagato 8 — all exact),
  `+140 char main_gun_caliber_in[8]` (ASCII inches, "14" / "18.1").
  What the AI actually fires (`ai_ship_fire_guns` 0x12f7a): `+148/+152` the
  fore and aft mounts (non-zero: it fires), `+156 gun_id` the `WEAPONS.DAT`
  gun (battleships and cruisers 6, carriers and destroyers 2, merchants 1).
- `+112 i32 max_power` — engine power cap of the motion model (see
  "Behaviour models"); it, not `top_speed_kts`, sets the simulated top speed.
  `+116 submerged_power`: the submarine classes' cap below the surface
  (`ai_sub_think` 0x13471; 0 on surface ships).
  `+120/+124/+128` depth charges (torpedoes on submarines), depth-charge
  racks and throwers (the AI's charge runs and the recognition manual).
  `+132 has_sonar` (-1 on DD/DE/kaibokan).
- `+160 char top_speed_kts[8]` (ASCII knots, "22.5" / "27"); `+168 char
  submerged_speed_kts[8]` (submarine classes only, "8" / "6.2", printed by the
  recognition manual).
- `+176 length_ft`, `+180 beam_ft`, `+184 draft_ft`, `+188 visual_size`
  (target size passed to the sighting-range function: BB 250-280, DD 100,
  SS 90), `+192 deck_height_ft` (the lookouts', guns', fire and bow wave's
  height above the waterline; a submarine's awash depth, `enemy_sub_init`
  0x7f12f), `+196 displacement_tons` (full load; the submarine's surfaced
  displacement), `+200/+204` the submarine classes' neutral and maximum
  ballast (as SUBS.DAT `+212/+216`; 0 on surface ships).
- Motion coefficients, all 16.16: `+208 drag_cubic` (BB 408, DD 164), `+216
  drag_linear` (0.032 everywhere), `+224 lateral_drag` (sideslip damping),
  `+228 astern_drag_factor`, `+232 turn_factor` (yaw-rate divisor, BB 59.2, DD
  29.2).

Verified against history: Yamato 862 ft / 121 ft beam / 71659 t / 9×18.1";
Nagato 42850 t / 8×16"; Fuso 12×14" / 22.5 kts. `+212/+220` are the submarine
classes' submerged drag pair (0 on surface ships; KAIDAI's match SUBS.DAT's
pairs exactly); `+236/+240` are filled at load time with the 3DSHIPS.CAT sprite
pointers; `+66` is two bytes of padding. The fields from `ship_type` on that are
not dimensions were read from the vehicle, sensor and generator code, not from
history. Check of `max_power`: solving the drag model for it gives
`top_speed_kts` within 0.63 kn (mean 0.22) on all 51 surface classes; the
submarine classes miss by 1-3 kn.

### `SUBS.DAT` — indexed, 9 records x 1704-byte detail **[mostly decoded]**

US submarine **classes** (`S CLASS`, `BARRACUDA`, `NARWHAL`, `P CLASS`,
`SALMON`, `T CLASS`, `GATO`, `BALAO`, `TENCH`). This is the per-class
performance model that `CARSUB.DAT.class_index` points at. Layout (offsets
within the record; see `assets/dat_tables.h` for the full record):

- `+0  char name[10]`, `+10 char display_name[26]`, `+36 class_index`, `+40
  u8 picture_frame` (the `ART\SUBPICS.SHP` picture, `subscrn_draw_sub_info`
  0x718b7).
- `+41 sprite_silhouette` (`SUB-7.SHP`), `+55 sprite_profile` (`P_GATO.SHP`).
- `+72 u32 intro_month`, `+76 u32 intro_year` — class introduction (monotonic
  by class: S=2/1921 … Gato=4/1942, Balao=7/1943, Tench=2/1945); `+80 u32
  end_month`, `+84 u32 end_year` = 10/1945 (constant). Months are 1-based
  (`SE.EXE` class sheet and default player).
- `+100 u32 range_nm`, `+104 u32 range_at_kts` — endurance (Gato 11800 nm @ 10).
- `+116 u32 bow_tubes`, `+120 u32 stern_tubes` (Gato 6/4), `+124/+128`
  bow/stern rack reloads, `+132` modern torpedoes (0: Mark 10 only, else
  Mark 14/18), `+136` deck gun count (Narwhal 2), `+140` gun caliber (in),
  `+144` mount of a one-gun boat (0 forward, 1 aft).
- `+148 length_ft`, `+152 beam_ft`, `+156 draft_ft` (also `SE.EXE`'s default
  player depth), `+160 test_depth_ft`
  (**Gato=300, Balao/Tench=400** — the historical differentiator), `+164`
  periscope depth (65), `+168` radar depth (48; SD/SJ usable above it),
  `+172` periscope eye height, `+176` bridge eye height (also the depth where
  `sub_wave_effect_pct` starts fading the waves), `+180` awash depth, `+184`
  sonar noise factor % (Tench 50).
- `+188 char surface_speed_kts[10]`, `+198 char submerged_speed_kts[10]` —
  ASCII decimal knots ("20.75", "8.75").
- `+208/+212/+216` displacements in tons: surfaced (the `SE.EXE` sheet's
  "Displace"), submerged (historical: Tambor 2370, Balao 2391) and a maximum
  (`+212` + 10 on every class); the dive model's ballast runs from `+208` on
  the surface, rests at `+212` (neutral) and floods up to `+216`.
  `+220/+224` diesel/motor power caps of the motion model.
- Motion hull coefficients that `sub_dive_update` passes to `motion_step`
  (16.16): `+228 drag_cubic[2]`, `+236 drag_linear[2]` (each [surfaced, below
  the draft]), `+244` lateral drag, `+248` astern factor, `+252` turn factor;
  `+256` pitch factor (38.2), `+260/+264` diesel and motor counts, `+268`
  fuel capacity, `+272` fuel use, `+276` deck gun rounds, `+280/+324` power
  per telegraph setting (surfaced/submerged).
- `+368 char description[1336]` — recognition-manual text (zero-padded tail).

The naval characteristics of the 9 classes (dimensions, depth, range,
displacement, speeds) match the historical figures. The armament
fields above `+148` were read from the dive model, the player init and the
sensors; `+88..+96` (a small value, then a
month and a year) and `+108/+112` remain unknown.

### `IJNSHIPS.DAT` — flat, 210 records x 66 bytes **[decoded]**

Individual IJN ship **instances** grouped by class. Per-ship data is pure
identity + a historical service window; gameplay stats come from the class
(`class_name` → `SHIPS.DAT`). Layout:

- `char[25] ship_name` — the individual ship (`Agano`, `Noshiro`, `Yahagi`…).
- `char[25] class_name` — its class (`Agano`), which keys into `SHIPS.DAT`.
- `u32 commission_year` (two-digit, 19xx), `u32 commission_month`.
- `u32 end_year`, `u32 end_month` — sinking/decommission (or war end).

Confirmed against history: Agano commissioned 1942, sunk 2/1944; Akagi sunk
6/1942 (Midway); Yahagi sunk 4/1945 (with Yamato); Teruzuki sunk 12/1942.

### `CARSUB.DAT` — flat, 249 records x 41 bytes **[decoded]**

Roster of individual US submarine boats (`Balao`, `Billfish`, `Bowfin`…). As
with `IJNSHIPS`, identity + service window only; performance is per-class
(`class_index` → `SUBS.DAT`). Layout:

- `char[20] name`.
- `u32 hull_number` — US Navy SS-xxx (`Balao` = SS-285, `Billfish` = SS-286).
- `u32 avail_year` (19xx), `u32 avail_month` — available-from date.
- `u32 end_year`, `u32 end_month` — availability end (loss/decom; defaults to
  45/10 = end of war for boats that survived).
- `u8 class_index` — index into `SUBS.DAT` (0..8; e.g. 7 = BALAO, 6 = GATO).

Confirmed against history: hull numbers match the real SS-series; loss dates
match (USS Wahoo lost 10/1943, Tang 10/1944, Harder 8/1944, Cisco 9/1943).

### `CMDLIST.DAT` — counted, 308 records x 230 bytes **[decoded]**

`u32 count` (308) then 308 records: `char name[50]` (`Fluckey, E. B.`,
`O'Kane, R. H.`…) and `i32 tonnage_by_month[45]` at byte 50 (campaign month 0 =
Dec 1941), unaligned. The TOP COMMANDERS list sums each commander's months
before the current date and ranks the player's career tonnage among them;
Fluckey and O'Kane have the two largest totals, as in history.

### `HOMEPORT.DAT` — flat, 10 records x 3928 bytes **[decoded]**

The ten submarine bases. Fields (4-byte, see `assets/dat_tables.h`): name[20],
flags (bit 0: transfer destination), open and close dates (month 0-based,
day, year), a per-month weight as a transfer destination, patrol-zone weights
`[20 zones][45 months]`, the distance in nm to each zone (transit time and
fuel), the command (0 ComSubsAsiatic, 1 ComSubPac, 2 ComSubSoWesPac, 3 Task
Force 42) and the map point / label offset. The dates form the historical
chain of forward bases: Pearl Harbor (to Jul 1942) -> Midway (to Aug 1944) ->
Majuro / Saipan -> Guam; Manila -> Surabaya -> Fremantle (Mar 1942 - Feb 1944)
-> Darwin; Brisbane (Task Force 42, Apr 1942 - Jun 1943). Mostly zero because
most ports serve only a few zones.

### `MAP<n>BASE.DAT` — flat, 0x7f-byte records **[decoded]**

The ports and bases of patrol map n (the file is named by the `PZ.DAT` zone
record; loaded by `bases_load` to 0x116240). Each: name[15], three world points
(station = chart icon and single-mission start, harbour = anchored ships and
career start, patrol point), three dated owner periods (1 Japanese, 2 Allied:
no enemy ships spawn), flags (active, small guard, aircraft, spawns patrols,
excluded from recon), anchored-ship / air-base size and cap, patrol
parameters and daily spawn %, and a pick weight for air raids and photo
recon (`assets/bases.h`). `PZ.DAT` (the 15 patrol zones: files, dates,
weights and monthly tables) is in `formats.md` ("Other files").

### `WEAPONS.DAT` — two counted sections **[mostly decoded]**

- **Torpedoes**: `u32 count` (6) then 6 x 156-byte records (MARK 10/14/18-1/18-2
  US, TYPE 95 MOD 1/2 IJN). Fields (offsets within the record): `+0
  short_name[10]`, `+10 long_name[26]`, `+36 id` (2 = Mark 14: the dud rules),
  `+40 nation`, `+44` 1-based service month, `+48 year`, `+52 range_long` / `+56
  range_short` (yd), `+60 speed_low_kts` / `+64 speed_high_kts` (the two in-game
  settings), `+68 diameter_in` (21), `+72 warhead_lb`. Verified vs history (Mark
  14 31/46 kts & 668 lb; Type 95 49 kts & 900/1210 lb).
- **Guns**: at offset `4 + 6*156 = 940`, a second `u32 count` (6) then 6 x
  64-byte records: `+0 long_name[32]` (`5 INCH TYPE 89`), `+32 id` (1..6),
  `+36 caliber[12]`
  (ASCII, `5.5`), `+48 shell_caliber_in` (5/6: the caliber, hence the damage,
  of the shells the AI fires), `+52 max_range_yd` (8" = 31600), `+56
  muzzle_velocity_fps`, `+60 reload_time_s` (4..15: the AI reloads in (3 -
  crew) x it, x 3 on merchants, plus up to 3/4 more; `ai_ship_fire_guns`).
  Deck/AA guns for the IJN ships (SHIPS.DAT `+156`).

## Simulation objects (in memory)

Read from the original's `veh`, `ai` and `campaign/contacts` modules. Offsets
are byte offsets in the object.

### Vehicles

Every simulated object is a C++ object in the vehicle list `0x129cec` (count
`0x129cf8`), with its vtable pointer at `+0xcc`; vtable slot 0 returns the class
id. `career_read_state_lists` re-creates saved vehicles with a switch on that
id, which ties each id to its constructor, size and vtable:

- **1 ship** — vtable `0x101b2c`, 0x4e0 bytes, SHIPS.DAT class at `+0x224`.
- **2 enemy submarine** — `0x101c18`, 0xb40 bytes, SHIPS.DAT class at `+0x1e0`.
- **3 aircraft** — `0x101d04`, 0x368 bytes, AIRCRAFT.DAT record at `+0x20c`.
- **4 bomber group** — `0x101df0`, 0x1d8 bytes (air strike on a map location).
- **5 torpedo** — `0x1018b4`, 0x250 bytes, WEAPONS.DAT torpedo at `+0xe8`.
- **6 depth charge**, **7 shell / AA round**, **8 aerial bomb** — `0x101edc`,
  `0x101fc8`, `0x1020b4`.
- **9 water splash**, **10 gun flash / explosion**, **11 underwater blast
  noise** (no sprite, masks sonar), **12 jettisoned debris**, **15 smoke puff**
  — effect objects.
- **13 downed pilot** (raft), **14 air strike / photo recon report** — the radio
  report objects behind the chart reports.

The player boat is the static object `0x11a8f0` (SUBS.DAT record at `+0xd0`,
position at `0x11a9c4/c8/cc`, heading at `0x11aa3c`).

**Vtable slots** (byte offsets, 59 slots): `+0x04` init, `+0x08` update pass
(0..10, `veh_simulate_pass` runs them in order), `+0x0c` maximum time
compression the object allows, `+0x10` update(dt) with dt in 16.16 seconds,
`+0x14` add to the render list, `+0x18` get position, `+0x30` rebuild the
collision box, `+0x48` chart contact text, `+0x50` take damage (target,
attacker, kind, distance, amount), `+0x68` radar cross-section / detection
range, `+0x6c` noise level for sonar, `+0x80` spotted flag (query/set), `+0x84`
tracking time, `+0x88` per-frame spotted refresh, `+0x90` exposure (1 surfaced,
2 below the waterline, 5 deep), `+0xdc`/`+0xe0` write/read save state, `+0xe4`
turn saved ids back into pointers, `+0xe8` drop pointers to removed objects.

**Common header:** `+0x04` bounding radius; `+0x08` 3x3 orientation; `+0x2c..`
box centre, half-extents and the 8 corners (`+0x50` x[8], `+0x70` y[8], `+0x90`
z[8]); `+0xb0`/`+0xb4` collided flag and partner index; `+0xb8` sinking;
`+0xbc` removed (deleted by `veh_simulate_frame`); `+0xc0` side; `+0xc4` id
saved in place of pointers; `+0xc8` owning contact group (see below); `+0xd0`
hit this frame and `+0xd4` attacker (class data starts at `+0xd0` for the
small classes).

**Ship:** position `+0x228/+0x22c/+0x230` (y up; 16 units = 1 ft, 0x30 = 1 yd),
heading `+0x2a0`, engine power `+0x2b4` (x550) and ordered power `+0x2b8`
(capped by damage), speed `+0x2bc` (knots 16.16) and `+0x2c0` (tenths of a
knot), hit points `+0x3a4` (from `hull_strength`), orders `+0x3a8` (the `.SCN`
codes: 0 none, 1 escort, 2 patrol, 3 convoy leader, 4 convoy member, 5
intercept, 6 follow path, 7 fishing), crew `+0x3b0`, waypoints `+0x3b4` x[25] /
`+0x418` z[25] / count `+0x480`, convoy group id `+0x484`, zigzag `+0x488`,
damage `+0x4dc`. **Enemy sub:** position `+0x1e4..` (same motion block layout as
the ship's), heading `+0x25c`, engine power `+0x270/+0x274`, speed
`+0x278/+0x27c`, dive state `+0x328`, AI block `+0x34c`, hit points `+0xa2c`,
orders `+0xa30`, crew `+0xa38`, waypoints `+0xa64`/`+0xac8`, intercept target
`+0xb34/+0xb38`. **Aircraft:** position `+0x158..`, heading/pitch/roll
`+0x1d0..+0x1d8`, speed `+0x204`, hit points `+0x250`, mission `+0x344`,
waypoints `+0x254`/`+0x2b8`. The other classes' fields are cited in the code of
their classes (`sim/`).

Damage dealt by the player is scaled by `3 - ((0x11a85c & 0xf) >> 2)`
(realism). Ships and subs sink at 1/20 of their hit points and burn at 1/2
(`ship_update_damage_state`, `enemy_sub_update_damage_state`).

### Render list

`render_list_add` (`0x91b08`) appends to a 400-entry list of 0x48-byte records
at `0x170ff0` (type, owner, sprite, x/y/z, heading, size, distance, screen
x/y); `0x91ae6(type, cb)` registers the draw callback of each render type.
Render types are sprite kinds, not vehicle classes: 0 smoke / static billboard,
1 oriented map object, 3 ship, 4 enemy sub, 5 splash, 6 torpedo wake (skipped
for electric torpedoes), 7 ship wake, 8 depth charge, 9 shell, 10 explosion,
11 aircraft, 12 bomb, 13 raft, 14 bomber-group dots.

### Strategic contact groups

Off-screen traffic lives in the list `0x119248` as contact groups, each with a
12-slot vtable at `+0x134` (kind, read/write, update, chart draw,
try_materialize, contact report, ...). Kinds: 1 route group (spawned on a `.CR`
convoy route), 2 patrol and 4 anchored ships (spawned per base from the base
records at `0x116240`, 0x7f bytes each), 8 sampans. A group *materializes* —
the scengen builders fill the `.SCN` unit arrays and `0x67400` instantiates
them, the same path as a loaded mission — when the player could see it
(sighting range from `visual_size`) or pick it up on radar; groups the player no
longer detects are removed again. Route groups also carry a kind (0 merchant
convoy, 1 warship task force, 2 patrol / ASW, 3 enemy submarine), size class,
escort level, crew quality and an HQ contact report.

Per-route state at `0x1191e8` (0x1c bytes per route): active today, month's
frequency and `convoy_pct`, spawn timer, flags, and an alert level raised each
time a route group engages the player (decays daily); at alert 3 or more the
route spawns patrols instead.

### Behaviour models

Formulas read from the original functions; 16 units = 1 ft, 0x30 = 1 yd, 1 kn =
27.008 world units/s, angles in tenths of a degree.

- **Motion** (`motion_step`, `motion_power_for_speed`; SHIPS.DAT
  coefficients). Drag `D(v) = (c v)^3 + b v` with `c = drag_cubic`, `b =
  drag_linear` (x `astern_drag_factor` going astern; x `1 + s/100` in a sea
  state `s` of 0, 1 or 3, faded with depth for submarines). Power = D / 550,
  capped at `max_power`; thrust slews toward the ordered power. Acceleration =
  (thrust - drag - lateral) / (displacement x 2200 lb) x 16. Rudder = 250 x
  clamp(heading error, +-60 deg) / 60 deg; yaw rate is divided by (length/2 x
  `turn_factor`). A waypoint is reached within `turn_factor` x 34 x angle_off
  / 180 deg + 100 yd (aircraft: 1000 yd).
- **Visual sighting** (`sighting_check`, `sighting_compute_range`). Range
  `(2027 sqrt(h_obs) + 2027 sqrt(h_tgt)) / 2` yd (heights in ft: the horizon
  formula), then reduced by `(100 - light) x 0.8 %`, `weather x 15 %` and
  `fog %` (light % `0x170f84` from the sun/moon, fog % `0x170f88`); land on the
  line (terrain grid) blocks it. The player's eye is SUBS.DAT `+172`
  (periscope, 68 ft) or `+176` (bridge); the range is divided by the target's
  exposure (vtable `+0x90`) and cut by crew quality. AI observers lose a
  further 20 % (aircraft 30 %), `2 x sea state %` and crew; the player boat is
  much harder to see below radar depth or awash.
- **Radar** (`sj_radar_sweep`, `sd_radar_sweep`). Option bits 16-17: SJ
  15000 yd, improved SJ 35000 yd, or ST periscope radar; bit 18 SD (aircraft,
  17600 yd); bit 19 PPI. The SJ beam turns 40 deg/s, +-7.6 deg wide; signal =
  RCS / max(1, 4 range / Rmax), detected above 0.1; RCS (vtable `+0x68`) is
  ship length x 100. Blips last 8 sweeps; the SJ also shows land. Both are off
  below radar depth.
- **Escort sonar** (`ai_ship_sonar_search`, ships with `has_sonar`). Passive
  `5000 + 1000 (crew + 1)` yd, active `2000 + 400 (crew + 1)` yd, reduced by
  sea state and own speed; rear baffles 170-190 deg; only subs above the
  thermal layer `0x11b7c8` (`init_thermal_layer`); noisy objects (explosions)
  mask contacts; position error grows as signal and crew drop. The player's
  hydrophones (vtable `+0x58..+0x60`) hear screws by target power minus own
  noise (`player_noise_level`: /4 silent running, x4 below the layer, x5 in
  the baffles) and report the SHIPS.DAT `screw_sound` type.
- **TDC solution** (`tdc_estimate_solution`). Quality q from range against
  the sighting range, a realism floor `100 - 35 x realism` and tracking time
  against crew; error `(100 - q) / 5 %` skews course by up to 4.5 deg per
  point and scales speed and range.
- **Own boat** (`player_update`, `player_update_helm_and_dive`). Telegraph
  0..10 (1-5 ahead, 6-9 astern, 10 silent running) maps to a power % per
  engine from SUBS.DAT `power_surfaced`/`power_submerged`; diesels on the
  surface, motors submerged. Fuel burns (power / `diesel_hp`) x `fuel_use` per
  second; the battery (0..600) drains (power / `motor_hp`)^2 x 14.67 per
  second and charges only on the surface at standard speed or slower (manual).
  Crew air lasts about a day submerged; fires and chlorine (flooded battery
  room) eat it. Damage: 24 systems (severity > 85 destroys; repair time from
  severity and crew) and 20 compartments (flooding grows with depth, pumps
  need battery or surfacing) (`sim/player_sub.h`, `sim/player_damage.h`).
- **Torpedoes** (`fire_tube`, `tdc_*`). Gyro angle limit 130 deg; the
  torpedo leaves the tube turned at most 10 deg, then turns to the gyro
  course; spreads offset +-20 deg. Duds (Dud Torpedoes on): a Mark 14 before its
  fix date fails with probability (impact-angle factor / 135), about 67 % on a
  square hit; other torpedoes 3 %; none arm within 200 yd. Reloads take 60 s +
  rand(60), or 420 s + rand(120) with Realistic Reloads, plus a crew term,
  one at a time per torpedo room.
- **Deck gun** (`deckgun_*`). Two mounts at +-length/4, unable to train
  within 7 deg of the conning tower; 2100 fps shells with the class caliber
  (not WEAPONS.DAT). The crew fires by itself at quality >= 80 inside
  7000 yd; reload 2 s, or (3 - crew) x 4 s with Realistic Reloads.
- **Chart** (`chart_*`). Up to 24 player waypoints (x `0x11ab2c[]`, z
  `0x11ab90[]`), each in deep water with no land on the leg; click modes
  0 info/target, 1 plot, 2 move the sub (cheat, `/SS305`). Limited
  Visibility shows only spotted contacts at their last-known position;
  without Realistic Charts it also shows weapons; contact-group symbols fade
  over 8 h.
- **Career** (`career_*`, `game/career*.h`, `game/patrol.h`).
  START WAR PATROL picks a patrol zone weighted by the home port's
  `zone_weight` for the month (re-picked until twice the transit fuel fits in
  90 % of the tank); transit time = zone distance / (2/3 of the boat's
  speed). After a patrol: score/tonnage added, repair (from damage) + refit
  (5-11) + 8 days in port, then letters (poor fuel management, poor patrol
  performance, loss of command: relieved after 3 / 2 / 1 too many), medals
  (Bronze Star per (n+1) x 4 ships, Navy Cross per 50000 score, Medal of
  Honor at 100000, DSM after 4 high-realism patrols, unit citations for
  streaks), promotion after 2 and 5 patrols, new-command offers and
  transfers between the open home ports.
- **Day and night** (`world_apply_daylight_palette`). Ambient light % (sun and
  moon elevation, `.ENV` dawn/dusk tint and night minimum) darkens the 3D
  palette, which also drives sighting ranges (`0x170f84`); self-lit colours
  and the fire range stay bright and ship lights switch on at night. Details
  in `formats.md` (`.SHP` palettes).
- **Scenario groups** (`scengen_*`). Convoys of 1-4 / 5-10 / 11-15 ships in up
  to 4 columns 1000 yd apart, the slowest ship's speed, the leader zigzags;
  escorts on a ring (1-3 / 4-6 / 7-9, picked by `selection_weight` and speed);
  task forces of 1-4 real IJN ships (IJNSHIPS.DAT, in commission, no repeats)
  stepping down BB/CV -> CA -> CL; patrols of 1-6 escorts at 12 kn already
  alerted; anchored ships on rings in port with guard ships; sampans fishing;
  enemy subs at 64 ft; aircraft orbit a covered group and strafe with a
  realism-dependent chance.

### Globals

- **`0x129c04` game clock** — `+0x08` sec, `+0x0c` min, `+0x10` hour, `+0x14`
  day, `+0x18` month, `+0x1c` year (since `0x10e3a4`), `+0x20` weekday, `+0x24`
  moon phase, `+0x28..+0x44` sun/moon rise and set, and three time-compression
  values (all 1 at mission start):
  - `+0x48` (`0x129c4c`) the *effective* compression the simulation runs at
    and the main screen shows; lowered to at most 512 near materialized
    contacts or when deep. The manual gives the player-visible limits: 1x to
    256x, at most 16x during encounters and 8x while torpedoes are running
    (the internal scale of these values is not pinned down yet).
  - `+0x4c` (`0x129c50`) the *requested* compression, set by the keys
    (`time_compression_double`/`halve`/`reset`) and shown in the
    time-compression panel.
  - `+0x50` (`0x129c54`) the current cap: recomputed every frame as the
    minimum allowed by the vehicles (vtable `+0x0c`). Doubling only works below
    it, so a requested value capped earlier does not grow back by itself when
    the cap rises (confirmed in the game).
- **`0x11a85c` options** — the realism settings plus the radar fit. The
  realism checkboxes (`wscreens_init_dialogs`, callbacks 0xa298e..0xa2b94)
  each toggle one bit; the NOVICE / INTERMEDIATE / EXPERT presets set the low
  16 bits to 0x0000.. / 0x11d4 / 0x61a8 (keeping bits 0-1). Effects as
  described in the game manual ("Realism"):
  - bits 2-3 combat level (damage scaling `3 - level` for the player's hits);
    bits 4-5 probably the sailing model (0x10 intermediate, 0x20 expert).
  - Set = realistic option **on**: `0x80` Limited Visibility (the chart shows
    only contacts the crew sees, at their last-known position), `0x100` Run
    Aground (grounding damages the boat), `0x2000` Realistic Reloads, `0x4000`
    Dud Torpedoes (Mark 14 deep running and premature/dud detonation until
    their historical fix dates).
  - Set = realistic option **off**: `0x40` not Realistic Charts (the chart
    also shows torpedoes, shells, splashes, debris), `0x200` / `0x400` /
    `0x800` unlimited fuel / battery / ammo, `0x1000` not Limited Depth Data
    (the "Depth Under Keel" gauge appears), `0x8000` not Vulnerable Boat (no
    damage).
  - Bits 16-17 SJ radar level, 18 SD, 19 PPI, `0x200000` bathythermograph
    (see "Behaviour models"); bit 0 tentatively gates the strategic contact
    system (career).
- **`0x125f60/64/68`** weather, sea state, fog (from the `.SCN` header).
- **`0x11a860`** current station object; the station objects at
  `0x11a7d8..0x11a850` are chart, conn, bridge, deck gun, gauges, scope, radar,
  status, TBT, TDC and captain's cabin.
- **`0x129a20`, `0x11c1c4`** selected target (TDC, deck gun).
- **`0x11ab0c`** player depth (ft); radar depth `0x11b1c8`, periscope depth
  `0x11b1c4`, awash depth `0x11b1d4`; crew quality `0x11ac2b`.
- **`0x11a890`** mission end code; results record `0x187254` (score
  `0x187338`).
- **`0x11b118`** radio-message queue (0xa0-byte records: kind 0 air strike,
  1 downed pilot, 2 photo recon).

## Bugs in the original

Found while reading the original's code (how the reimplementation handles
each is noted where it is ported):

- **Escort sonar masking never triggers**: it needs vtable `+0xd4` != 0 and
  noise > 0, but only class 10 (explosion, noise -1) returns 1 at `+0xd4`;
  class 11 (underwater blast, noise 200) returns 0. Depth-charge noise never
  masks the escorts' sonar.
- **Contact "comms range" (vtable `+0x34`) is never initialised** for ships,
  enemy subs, bomber groups and pilots (only save/load write it; aircraft get
  it from `air_sorties_relaunch`).
- **`enemy_sub_update_depth`** writes world units into the ordered-depth
  field `+0x330`, which holds feet.
- **`sim_read_state`** reuses a stale pointer for a contact-group kind it has
  no constructor for (3, 5, 6, 7).
- **`event_queue_status`** is called without its two pointer arguments.
- **`scen_fill_torpedo_load`** lists "MARK18-1" twice (the 18-2 mix is dead;
  harmless, `tdc_upgrade_mark18_by_date` swaps by date anyway).
- Library quirks: `font_draw_string` draws glyph 0 for an empty string;
  `path_dirname` breaks on a path without a backslash; `point_rotate_scale`'s
  rounding constant has no effect (it floors); speech with format flags 9 plays
  mono although stereo is asked.

## Open questions (need loader or running game)

- `SUBS.DAT` remaining fields: `+88..+96`, `+108/+112`; `AIRCRAFT.DAT`
  `+112`.

// The simulation state lists of the original's saves (`sim_write_state`
// 0x19061 / `sim_read_state` 0x1873b), shared by MISSn.SAV and the ROSTER
// of a career saved on patrol (sim/state_io.h for the principle):
//
//   player_write_state (0x61b35): the boat's block 0x11a9c4..0x11b117 as
//     ranges (motion, dive, route, name, crew, helm, engines, fuel and
//     battery, tubes and racks, systems, compartments, hull, air, deck
//     guns), the radio messages (a 24000-byte buffer of 0xa0-byte records:
//     the waiting queue 0x11b118, then the log 0x11b11c, and both counts),
//     then (version 7) the UI globals: pages, the station, the radar
//     controls, the chart's panels and view, the swell, rig for red;
//   i32 count + 0x4b-byte patrol-log records (0x11b110);
//   (version 7) `tdc_write_state` and `deckgun_write_state`;
//   i32 count + 0x1c-byte route states (0x1191e8);
//   i32 count + per contact group: i32 kind (1, 2, 4, 8) + its fields;
//   (version 4+) i32 count + per vehicle but the player: i32 class id +
//     its `*_write_state` fields (game/vehicle_state.h).
//
// The reimplementation appends its extension after them: "SHRX", u32
// version, then sections (u32 tag, u32 size, bytes): "SIM " the mission
// state the original does not save, "UI  " the session's, "RULE" the
// extended variant's rules of the mission (game/mission_rules.h), only when
// they are not the original's, and "CLOG" the extended variant's crew log
// (`Features::crew_log`, game/crew_log.h). Readers skip the sections they
// do not know.
//
// Deviations from the original's bytes: the unused stack and heap bytes it
// writes (free radio buffer records, unknown fields) are zeros; the TDC's
// tracked-torpedo pointers (0x129b08) and a vehicle's collision partner
// (+0xb4) are written as vehicle ids where the original writes raw
// pointers (its reader keeps them as stale pointers).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_reader.h"
#include "game/patrol_contacts.h"
#include "sim/state_io.h"
#include "sim/vehicle.h"

namespace sh {

class Mission;
class MissionScreen;
class AttackServices;
class Patrol;
struct CabinLogs;
struct PatrolLogEntry;
struct RadioMessage;

// The version of the state lists (and of MISSn.SAV and ROSTER files).
inline constexpr std::uint8_t kSimStateVersion = 7;

// The player block 0x11a9c4..0x11b117 as an image (offsets from 0x11a9c4).
inline constexpr std::uint32_t kPlayerBlockBase = 0x11a9c4;
inline constexpr std::uint32_t kPlayerBlockSize = 0x754;
// The radio buffer `player_write_state` writes whole: 150 records.
inline constexpr std::size_t kRadioRecordSize = 0xa0;
inline constexpr std::size_t kRadioBufferSize = 24000;
inline constexpr std::size_t kPatrolLogRecordSize = 0x4b;
inline constexpr std::size_t kRouteStateSize = 0x1c;

// The UI globals at the end of `player_write_state` (version 7), in its
// order.
struct SavedStationUi {
  std::int32_t compartment = 7;        // 0x11bcdc
  std::int32_t status_page = 0;        // 0x11bcd4
  std::int32_t gauges_page = 0;        // 0x11bce0
  std::uint8_t lower_panel = 0;        // 0x11bcd8 (one byte)
  std::int32_t tdc_panel = 0;          // 0x1282b8
  std::int32_t station = 1;            // index of 0x11a860 in the table 0x105154 (`StationId`)
  std::int32_t id_book_page = -1;      // 0x1094dc
  std::int32_t log_page = 1;           // 0x119280
  std::int32_t sj_mode = 0;            // 0x125558
  std::int32_t sj_beam = 0;            // 0x12555c
  std::int32_t sj_scale = 4;           // 0x125560
  std::int32_t sj_hold = 0;            // 0x125564
  std::int32_t sj_nearest = 0;         // 0x125568
  std::int32_t sd_on = 0;              // 0x12559c
  std::int32_t sd_scale = 4;           // 0x1255a0
  std::int32_t sd_nearest = 0;         // 0x1255a4
  std::int32_t aa_manned = 0;          // 0x124e94
  std::int32_t cabin_page = 0;         // 0x11bd00
  std::int32_t chart_panel = 0;        // 0x118a8c
  std::array<std::int32_t, 6> chart_panel_xy{0x16, 300, 0x16, 0x59, 0x16, 300};  // 0x117b68.., 0x117fe4.., 0x118278..
  std::int32_t chart_x = 0;            // chart_view_get_center
  std::int32_t chart_z = 0;
  std::int32_t chart_zoom = 0;         // 0x11a888 (16.16)
  std::array<std::int32_t, 5> swell{30, 1, 0x1e0000, 30, 1000};  // 0x124ef4..0x124f04
  std::int32_t rig_for_red = 0;        // 0x11a8c0

  bool operator==(const SavedStationUi&) const = default;
};

// The state lists of a file, as images of the original's layouts.
struct SimStateFile {
  struct Group {
    ContactGroupKind kind = ContactGroupKind::kRoute;
    StateImage image;
  };
  struct Unit {
    VehicleClass cls = VehicleClass::kShip;
    StateImage image;
  };

  StateImage player{kPlayerBlockSize};
  std::vector<StateImage> radio_waiting;  // 0xa0 each (0x11b118)
  std::vector<StateImage> radio_log;      // 0xa0 each (0x11b11c)
  std::optional<SavedStationUi> ui;       // version 7
  std::vector<StateImage> patrol_log;     // 0x4b each (0x11b110)
  std::optional<StateImage> tdc;          // version 7, 0x129a20..0x129b9f
  std::optional<StateImage> deck_gun;     // version 7, 0x11c188..0x11c1df
  std::vector<StateImage> routes;         // 0x1c each (0x1191e8)
  std::vector<Group> groups;              // 0x119248
  std::vector<Unit> vehicles;             // 0x129cec but the player (version 4+)
  // The extension sections ("SIM ", "UI  ", "RULE"), empty when the file
  // has none.
  std::vector<std::uint8_t> sim_extension;
  std::vector<std::uint8_t> ui_extension;
  std::vector<std::uint8_t> rules_extension;
  // The extended variant's crew log (section "CLOG", `CrewLog::serialize`;
  // not in the original), empty without `Features::crew_log`.
  std::vector<std::uint8_t> crew_log_extension;

  // Parses the lists at `in` of a file of `version` (`sim_read_state`),
  // then the extension when it follows; with `log_only`, stops after the
  // patrol log (the career roster's view). Throws `DataError` on a short or
  // malformed file.
  static SimStateFile parse(ByteReader& in, std::int32_t version, bool log_only = false);
  // Returns the lists in version 7 (`sim_write_state`) and the extension.
  std::vector<std::uint8_t> serialize() const;
};

// Returns the size of the contact-group object of `kind` (0x198, 0x188,
// 0x184, 0x174); throws `DataError` for an unknown kind.
std::uint32_t contact_group_size(ContactGroupKind kind);

// What the state lists cover besides the mission; null parts are absent
// (written empty, not restored).
struct SimStateParts {
  // The career patrol: routes, contact groups, weather, air raids.
  Patrol* patrol = nullptr;
};

// The session's parts of the state lists.
struct SessionStateParts {
  MissionScreen* screen = nullptr;     // UI state, radio, logs, bar
  AttackServices* attack = nullptr;    // zoom, lower panels
};

// `sim_write_state`: captures `mission` (with `parts` and the session's
// `session` parts, when given). Like the original, every vehicle's id
// (+0xc4: the ships', submarines' and aircraft's AI id) becomes its list
// index.
SimStateFile capture_sim_state(Mission& mission, const SimStateParts& parts, const SessionStateParts& session);

// `sim_read_state`'s simulation part over `mission`, built for the save
// with its boat and no other unit (game/mission_save.h): the boat's state,
// the TDC and deck gun, the routes and contact groups (into `parts`), the
// vehicles with their links, then the "SIM " extension when present. The
// mission's rules become those of the "RULE" section when there is one.
// Throws `DataError` when a vehicle's class is missing.
void restore_sim_state(Mission& mission, const SimStateFile& file, const SimStateParts& parts);

// The session's part of `sim_read_state` once the mission screen exists:
// the patrol and radio logs, the waiting radio messages, the UI globals
// (current station, pages, radar controls, rig for red, zoom...), then the
// "UI  " extension when present.
void restore_session_state(const SimStateFile& file, const SessionStateParts& session);

// The patrol log entries of a file's records (`career_load_roster_lists`
// with `log_only` reads them for VIEW CAREER).
std::vector<PatrolLogEntry> saved_patrol_log(const SimStateFile& file);

}  // namespace sh

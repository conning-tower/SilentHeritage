// `.SCN` scenarios (`SCEN/HIST*.SCN`, `CUSTOM/*.SCN`, magic `ASCN`), written
// by the scenario editor `SE.EXE`. Layout from `scen_load_ascn` (0x676b5) and
// its per-tag readers (0x67c9b..0x68318); field meanings from the spawners
// (`scen_spawn_*`) and the generators. docs/formats.md ".SCN".
//
// The game's view of the lossless model of assets/scn_file.h (strings cut at
// their NUL, units grouped by kind). Positions are world units of the
// `world` map (see assets/world.h); fields absent from older versions take
// the values the loader leaves in them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/environment.h"

namespace sh {

inline constexpr std::size_t kMaxScenarioWaypoints = 25;
inline constexpr std::int32_t kScenarioYearBase = 1930;

enum class Weather : std::int32_t {
  kClear = 0,
  kPartlyCloudy = 1,
  kMostlyCloudy = 2,
  kOvercast = 3,
};

// Crew quality of the player's and the enemy's crews.
enum class CrewQuality : std::int32_t { kGreen = 0, kVeteran = 1, kElite = 2 };

// Ship and enemy-submarine orders (the editor's list).
enum class ShipOrders : std::int32_t {
  kNone = 0,
  kEscort = 1,
  kPatrol = 2,
  kConvoyLeader = 3,
  kConvoyMember = 4,
  kIntercept = 5,
  kFollowPath = 6,
  kFishing = 7,
};

// Aircraft missions: the two search orbits around the aircraft's (x, y) and
// the path; no other values are known.
enum class AircraftMission : std::int32_t {
  kSearchOrbit = 1,
  kSearchOrbit2 = 2,
  kFollowPath = 6,
};

// The bits of `ScenarioHeader::equipment`, by fit date (table 0x10e408).
enum class Equipment : std::uint32_t {
  kSdAirRadar = 1u << 0,        // 1/42
  kSjaRadar = 1u << 1,          // 8/42
  kSj1Radar = 1u << 2,          // 8/43, replaces the SJa
  kPpiDisplay = 1u << 3,        // 7/43, the SJ radar's default display
  // 5/44, the APR-1 radar detector (the manual's date, SE.EXE's "radar
  // detector"); the original reads it nowhere, the extended variant's APR-1
  // rule does (sim/radar_emissions.h).
  kApr1Detector = 1u << 4,
  kBathythermograph = 1u << 5,  // 3/43, switches the gauge panel
  // Not in the original: the ST periscope radar (11/44, kept with the SJ),
  // fitted by the extended variant's ST rule.
  kStRadar = 1u << 8,
};

// Not in the original: the dates the extended variant fits the APR-1 (the
// original's 5/44) and the ST (11/44): month 0-based, year since 1930.
inline constexpr std::int32_t kApr1Month = 4;
inline constexpr std::int32_t kApr1Year = 14;
inline constexpr std::int32_t kStRadarMonth = 10;
inline constexpr std::int32_t kStRadarYear = 14;

// Bomber-group sizes: 10, 30 or 50 planes (table 0x1030fc).
enum class BomberGroupSize : std::int32_t { k10Planes = 0, k30Planes = 1, k50Planes = 2 };

// A path of up to 25 points; entries past `count` hold stale editor memory.
struct Waypoints {
  std::array<std::int32_t, kMaxScenarioWaypoints> x{};
  std::array<std::int32_t, kMaxScenarioWaypoints> y{};
  std::int32_t count = 0;

  // Returns the number of valid points (`count` clamped to 0..25).
  std::size_t size() const;

  bool operator==(const Waypoints&) const = default;
};

// One reload entry: a torpedo type and how many go to each room.
struct TorpedoReload {
  std::int32_t type = -1;  // WEAPONS.DAT torpedo index; -1 ends the list
  std::int32_t aft = 0;
  std::int32_t forward = 0;

  bool operator==(const TorpedoReload&) const = default;
};

// The scenario header (fixed fields before the unit records).
struct ScenarioHeader {
  // Set by the generator for lifeguard and photo-recon missions (start
  // placement, end text); forced to 0 below version 4.
  std::int32_t special_mission = 0;
  std::string world;  // `.WLD` file name, e.g. "WORLD-8.WLD"
  Weather weather = Weather::kClear;
  SeaState sea_state = SeaState::kCalm;
  std::int32_t fog = 0;
  std::int32_t gradient = 0;
  std::int32_t month = 0;  // 0-based
  std::int32_t day = 0;
  std::int32_t year = 0;   // since 1930
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  // Version >= 4 (else -1, -1, 0, ""): non-zero ends a non-career mission
  // 60 s after contact is lost; 0 seeds rand() with `saved_time`; Unix time
  // of the save; empty for no password.
  std::int32_t end_on_lost_contact = -1;
  std::int32_t randomize = -1;
  std::int32_t saved_time = 0;
  std::string password;
  // Version >= 7 (else -1): checked against free memory.
  std::int32_t required_memory = -1;
  std::array<std::int32_t, 10> tube_torpedoes{};  // torpedo type per tube
  std::array<TorpedoReload, 5> reloads{};
  std::uint32_t equipment = 0;  // `Equipment` bits

  std::int32_t full_year() const { return kScenarioYearBase + year; }
  bool has(Equipment e) const { return (equipment & static_cast<std::uint32_t>(e)) != 0; }

  bool operator==(const ScenarioHeader&) const = default;
};

// Tag 1: the player's submarine.
struct PlayerSubRecord {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t depth = 0;        // ft
  std::int32_t heading = 0;      // degrees
  std::int32_t engine_order = 0;
  // +0x14: never read by SH (`scen_spawn_player_sub` 0x53c62) or SE; kept
  // only so a rewritten file is byte-identical (shipped files hold stale
  // values; SE writes back what it loaded, 0 for a new player).
  std::int32_t preserved_14 = 0;
  std::string sub_class;         // SUBS.DAT class, e.g. "BALAO"
  std::string name;              // e.g. "(SS-305) Skate"
  CrewQuality crew = CrewQuality::kGreen;  // version >= 3

  bool operator==(const PlayerSubRecord&) const = default;
};

// Tag 2: a surface ship.
struct ShipRecord {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t heading = 0;  // degrees
  std::int32_t speed = 0;    // knots
  ShipOrders orders = ShipOrders::kNone;
  CrewQuality crew = CrewQuality::kGreen;
  std::int32_t damage_pct = 0;  // initial damage
  std::string ship_class;       // SHIPS.DAT class, e.g. "MUTSUKI"
  std::string name;
  Waypoints waypoints;
  std::int32_t convoy_group = 0;  // shared by a convoy's leader, members and escorts
  std::int32_t zigzag = 0;        // used by convoy leaders with crew > 0
  std::int32_t alert = 0;         // initial alert state

  bool operator==(const ShipRecord&) const = default;
};

// Tag 3: an aircraft.
struct AircraftRecord {
  AircraftMission mission = AircraftMission::kSearchOrbit;
  CrewQuality crew = CrewQuality::kGreen;
  std::string aircraft_type;  // AIRCRAFT.DAT name, e.g. "BETTY"
  std::int32_t x = 0;         // orbit centre
  std::int32_t y = 0;
  std::int32_t orbit_radius = 0;
  std::int32_t strafes = 0;   // non-zero: gun-attacks once its bombs are gone
  std::int32_t bomb_load = 0; // as AIRCRAFT.DAT `bombs`
  Waypoints waypoints;
  std::int32_t escort_group = 0;  // `convoy_group` of the ships it covers
  std::int32_t relaunch = 1;      // version >= 6; 0: one sortie, else unlimited

  bool operator==(const AircraftRecord&) const = default;
};

// Tag 4: an enemy submarine.
struct EnemySubRecord {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t depth = 0;    // ft
  std::int32_t heading = 0;  // tenths of a degree
  std::int32_t speed = 0;    // knots
  ShipOrders orders = ShipOrders::kNone;
  CrewQuality crew = CrewQuality::kGreen;
  std::int32_t damage_pct = 0;
  std::string sub_class;  // SHIPS.DAT class, e.g. "KAIDAI TYPE"
  Waypoints waypoints;
  std::int32_t target_x = -1;  // intercept point; -1 = the player
  std::int32_t target_y = -1;
  std::int32_t alert = 0;
  // Version >= 5: friend or foe. Older files leave the game's stack garbage;
  // 0 here.
  std::int32_t side = 0;

  bool operator==(const EnemySubRecord&) const = default;
};

// Tag 5: a bomber group (unused by the shipped files).
struct BomberGroupRecord {
  std::int32_t base = 0;  // index into the map's base records
  BomberGroupSize size = BomberGroupSize::k10Planes;
  std::int32_t launch_delay_min = 0;
  std::int32_t hold_min = 0;

  bool operator==(const BomberGroupRecord&) const = default;
};

// Tag 6: a photo-recon mission (unused by the shipped files).
struct PhotoReconRecord {
  std::int32_t base = 0;  // index into the map's base records
  std::int32_t delay_min = 0;
  std::int32_t duration_min = 0;

  bool operator==(const PhotoReconRecord&) const = default;
};

// A parsed `.SCN` file. Units are grouped by kind in file order, as the game
// keeps them.
struct Scenario {
  std::uint8_t version = 0;  // 3, 4 and 7 shipped; >= 2 accepted
  std::string briefing;      // briefing and "Results:" text
  ScenarioHeader header;
  // The last tag-1 record (each one overwrites the previous); every shipped
  // file has exactly one.
  std::optional<PlayerSubRecord> player_sub;
  std::vector<ShipRecord> ships;
  std::vector<AircraftRecord> aircraft;
  std::vector<EnemySubRecord> enemy_subs;
  std::vector<BomberGroupRecord> bomber_groups;
  std::vector<PhotoReconRecord> photo_recons;

  bool operator==(const Scenario&) const = default;
};

// Parses a whole `.SCN` file up to its end tag; `context` names it in
// errors. Throws `DataError` if it is truncated, has an unknown tag or an
// unsupported version.
Scenario parse_scenario(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh

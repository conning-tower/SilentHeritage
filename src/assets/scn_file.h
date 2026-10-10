// `.SCN` scenarios as the scenario editor `SE.EXE` keeps them: a lossless
// model of the file (whole char buffers with the stale bytes after their NUL,
// all 25 waypoint slots, the five reload entries with their counts, the
// encoded password, the records in file order), its reader (versions 2..7,
// `scen_load_ascn` SE 0x479a9 = SH 0x676b5) and its writer (always version 7,
// `scen_write_ascn` SE 0x464c8). `parse_scenario` (assets/scenario.h) is the
// game's view of the same model. Layouts: docs/formats.md ".SCN"
// and docs/editor-formats.md.
//
// A version-7 file read and written back is byte-identical. Fields absent
// from older versions get the values the original reader leaves in them
// (docs/editor-formats.md, "Re-saving older versions"), so writing an
// older file gives what SE writes, but for what the editor's object model
// rebuilds at each save (record order, `special_mission`, reloads,
// `required_memory`).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "assets/environment.h"
#include "assets/exe_image.h"
#include "assets/scenario.h"

namespace sh {

// The version `write_scn_file` writes (SE has no other writer path).
inline constexpr std::uint8_t kScnWriteVersion = 7;
inline constexpr std::size_t kScnBriefingSize = 1000;
inline constexpr std::size_t kScnPasswordSize = 15;

// A fixed-size char field, NUL-terminated (or full), with whatever bytes
// follow the NUL kept.
template <std::size_t N>
using ScnChars = std::array<char, N>;

// Returns the text of a char field: its bytes up to the first NUL.
std::string scn_text(std::span<const char> chars);

// Tag 1: the player's submarine (`scen_read_player_sub`).
struct ScnPlayerSub {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t depth = 0;
  std::int32_t heading = 0;
  std::int32_t engine_order = 0;
  // Never read by SH or SE; kept only so a rewritten file is byte-identical
  // (shipped files hold stale values).
  std::int32_t preserved_14 = 0;
  ScnChars<10> sub_class{};
  ScnChars<31> name{};
  CrewQuality crew = CrewQuality::kGreen;  // version >= 3

  bool operator==(const ScnPlayerSub&) const = default;
};

// Tag 2: a surface ship (the 300-byte record).
struct ScnShip {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t heading = 0;
  std::int32_t speed = 0;
  ShipOrders orders = ShipOrders::kNone;
  CrewQuality crew = CrewQuality::kGreen;
  std::int32_t damage_pct = 0;
  ScnChars<25> ship_class{};
  ScnChars<31> name{};
  Waypoints waypoints;
  std::int32_t convoy_group = 0;
  std::int32_t zigzag = 0;
  std::int32_t alert = 0;

  bool operator==(const ScnShip&) const = default;
};

// Tag 3: an aircraft.
struct ScnAircraft {
  AircraftMission mission = AircraftMission::kSearchOrbit;
  CrewQuality crew = CrewQuality::kGreen;
  ScnChars<25> aircraft_type{};
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t orbit_radius = 0;
  std::int32_t strafes = 0;
  std::int32_t bomb_load = 0;
  Waypoints waypoints;
  std::int32_t escort_group = 0;
  std::int32_t relaunch = 1;  // version >= 6

  bool operator==(const ScnAircraft&) const = default;
};

// Tag 4: an enemy submarine (fields in file order).
struct ScnEnemySub {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t depth = 0;
  std::int32_t heading = 0;
  std::int32_t speed = 0;
  ShipOrders orders = ShipOrders::kNone;
  CrewQuality crew = CrewQuality::kGreen;
  std::int32_t damage_pct = 0;
  ScnChars<25> sub_class{};
  Waypoints waypoints;
  std::int32_t target_x = -1;
  std::int32_t target_y = -1;
  std::int32_t alert = 0;
  // Version >= 5. The original reader leaves stack garbage in older files;
  // 0 here, as in `parse_scenario`.
  std::int32_t side = 0;

  bool operator==(const ScnEnemySub&) const = default;
};

// One record; tags 5 and 6 hold only integers, so the game's records are
// already lossless.
using ScnRecord = std::variant<ScnPlayerSub, ScnShip, ScnAircraft, ScnEnemySub, BomberGroupRecord, PhotoReconRecord>;

// The header of a `.SCN` file: the scenario globals 0xaf4a4..0xaf958 of the
// editor (SH's at +0x766bc), which the editor keeps between files. The
// defaults are those of the fields absent from the oldest version
// (`scen_load_ascn`'s initial values).
struct ScnHeader {
  std::uint8_t version = kScnWriteVersion;  // as read; the writer ignores it
  ScnChars<kScnBriefingSize> briefing{};
  // 0 below version 4 (the reader clears it after reading the records).
  std::int32_t special_mission = 0;
  ScnChars<13> world{};
  Weather weather = Weather::kClear;
  SeaState sea_state = SeaState::kCalm;
  std::int32_t fog = 0;
  std::int32_t gradient = 0;
  std::int32_t month = 0;  // 0-based
  std::int32_t day = 0;
  std::int32_t year = 0;  // since 1930
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  // Version >= 4.
  std::int32_t end_on_lost_contact = -1;
  std::int32_t randomize = -1;
  // The original keeps the previous scenario's seed below version 4; 0 here
  // (deterministic, and the value `parse_scenario` gives).
  std::int32_t random_seed = 0;
  // Encoded (`encode_scn_password`). Below version 4 the original clears the
  // first byte only and keeps the previous scenario's other 14; all zeros
  // here.
  std::array<std::uint8_t, kScnPasswordSize> password{};
  // Version >= 7.
  std::int32_t required_memory = -1;
  std::array<std::int32_t, 10> tube_torpedoes{};
  std::array<TorpedoReload, 5> reloads{};
  std::uint32_t equipment = 0;

  bool operator==(const ScnHeader&) const = default;
};

// A whole `.SCN` file.
struct ScnFile : ScnHeader {
  std::vector<ScnRecord> records;  // file order, without the end tag

  bool operator==(const ScnFile&) const = default;
};

// Reads a whole `.SCN` file up to its end tag (bytes after it are ignored,
// as by the original); `context` names it in errors. Throws `DataError` if
// it is truncated, has an unknown tag or a version below 2.
ScnFile read_scn_file(std::span<const std::uint8_t> data, std::string_view context);

// Returns the version-7 file image of `file`, field by field as
// `scen_write_ascn` and the objects' `write_scn` slots write them.
std::vector<std::uint8_t> write_scn_file(const ScnFile& file);

// The key `password_crypt` adds to each password byte (the first one is 0:
// the first character stays in clear).
using ScnPasswordKey = std::array<std::uint8_t, kScnPasswordSize>;

// Returns the password key of SE.EXE (table 0x9bcb4) from its image
// `se_exe`. Throws `DataError` unless it is a permutation of 0..14 starting
// with 0.
ScnPasswordKey read_scn_password_key(const ExeImage& se_exe);

// Returns the stored form of the password `text` (`password_crypt`
// 0x2b51e): the text zero-padded to 15 bytes, with `key` added to every
// byte; an empty text gives zeros. Throws `std::invalid_argument` when
// `text` does not fit (14 characters at most).
std::array<std::uint8_t, kScnPasswordSize> encode_scn_password(std::string_view text, const ScnPasswordKey& key);

// Returns the clear text of a stored password (`key` subtracted, up to the
// first NUL); empty when there is none.
std::string decode_scn_password(std::span<const std::uint8_t, kScnPasswordSize> stored, const ScnPasswordKey& key);

}  // namespace sh

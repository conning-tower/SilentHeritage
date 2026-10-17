// The career state of the campaign (modules campaign/career and carldsv):
// the career record 0x116810 (one roster entry), the summary block
// 0x116c68 and the career log 0x116c64, as `career_save` (0x18e52) writes
// them to `SAVE\ROSTER<n>.SAV` (docs/formats.md "Other files").
//
// Every record keeps the bytes it was read from: the fields not decoded and
// the stale bytes after the strings' NULs are written back unchanged, so a
// file read and written again is byte-identical.
//
// Dates follow the game clock: month 0-based, year since 1930.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/boat_fit.h"
#include "game/frontend_results.h"
#include "game/torpedo_load.h"

namespace sh {

// `+0x39`: the career's status (strings 0x10eac0).
enum class CareerStatus : std::uint8_t {
  kNone = 0,
  kInPort = 1,
  kOnPatrol = 2,
  kDeceased = 3,
  kRetired = 4,
  kRelieved = 5,  // relieved of duty
  kCaptured = 6,
};

// The award counters `+0xe8..+0x104`, in record order.
enum class Award : int {
  kBronzeStar = 0,
  kSilverStar = 1,
  kNavyCross = 2,
  kDsm = 3,  // Distinguished Service Medal
  kMedalOfHonor = 4,
  kAsiaticPacific = 5,  // Asiatic-Pacific Campaign Medal
  kNuc = 6,  // Navy Unit Commendation
  kPuc = 7,  // Presidential Unit Citation
};
inline constexpr std::size_t kAwardCount = 8;

// The realism values of the career (`+0x12c`, the order of
// `patrol_orders_realism` 0x62fdc, not that of REALISM.SAV).
enum class CareerRealism : int {
  kCombatLevel = 0,
  kSailingModel = 1,
  kLimitedVisibility = 2,
  kUnlimitedFuel = 3,
  kUnlimitedBattery = 4,
  kUnlimitedAmmo = 5,
  kRealisticReloads = 6,
  kDudTorpedoes = 7,
  kLimitedDepthDataOff = 8,
  kInvulnerable = 9,
  kRealisticChartsOff = 10,
  kRunAground = 11,
};
inline constexpr std::size_t kCareerRealismCount = 12;

// The career record (0x451 bytes at 0x116810).
struct CareerRecord {
  static constexpr std::size_t kSize = 0x451;
  // The world-files block 0x104e80 copied at `+0x160` (on-patrol saves):
  // five char[0x90] names and one byte.
  static constexpr std::size_t kWorldFilesSize = 0x2d1;

  std::string name;          // +0x00 char[21]
  // The career's start date (`career_init_new`).
  std::int32_t start_month = 0;  // +0x15, 0-based
  std::int32_t start_day = 1;    // +0x19
  std::int32_t start_year = 0;   // +0x1d, since 1930
  // The current date and time.
  std::int32_t month = 0;    // +0x21, 0-based
  std::int32_t day = 1;      // +0x25
  std::int32_t year = 0;     // +0x29, since 1930
  std::int32_t hour = 0;     // +0x2d
  std::int32_t minute = 0;   // +0x31
  std::int32_t second = 0;   // +0x35
  CareerStatus status = CareerStatus::kNone;  // +0x39 u8
  std::int32_t roster_slot = 0;  // +0x3a: n of ROSTER<n>.SAV
  std::string boat;              // +0x3e char[30], e.g. "(SS-337) Carbonero"
  std::uint8_t class_index = 0;  // +0x5c: SUBS.DAT record
  std::string class_name;        // +0x5d char[10]: SUBS.DAT name, e.g. "BALAO"
  std::string class_title;       // +0x67 char[25]: SUBS.DAT display name, e.g. "SALMON/SARGO"
  std::int32_t fuel_capacity = 0;  // +0x80
  // +0x84: the boat's fuel use (CARSUB table +0x48 = SUBS.DAT +0x110, set by
  // `carsub_assign_new_boat` 0x1e124); `patrol_compute_transit` (0x638d0)
  // turns it into the transit fuel.
  std::int32_t fuel_use = 0;
  std::int32_t transit_speed = 0;  // +0x88: knots
  std::int32_t last_patrol_zone = 0;  // +0x8c: PZ.DAT index
  std::uint32_t equipment = 0;     // +0x90: `.SCN` equipment bits
  std::int32_t rank = 0;           // +0x94: 0..2
  std::string home_port;           // +0x98 char[20]: a HOMEPORT.DAT name
  std::string location;            // +0xac char[20]: where the boat is
  std::int32_t patrols = 0;              // +0xc0
  std::int32_t promotion_patrols = 0;    // +0xc4
  std::int32_t high_realism_patrols = 0;  // +0xc8: reset by each DSM
  std::int32_t poor_patrol_streak = 0;   // +0xcc
  std::int32_t fuel_failures = 0;        // +0xd0
  std::int32_t patrols_from_port = 0;    // +0xd4
  std::int32_t tonnage = 0;              // +0xd8
  std::int32_t ships_sunk = 0;           // +0xdc
  std::int32_t score = 0;                // +0xe0
  // +0xe4: a second score total, added to with `score` by
  // `career_apply_patrol_results` (0x1bc18) and zeroed by `career_init_new`
  // (0x5ae56); nothing reads it (the summaries show `score`).
  std::int32_t unread_score = 0;
  std::array<std::int32_t, kAwardCount> awards{};  // +0xe8, by `Award`
  // +0x108: the patrol's transit hours, outbound then plus the return
  // (`career_base_loop` 0x1b556); the patrol results' "Total Patrol Time".
  std::int32_t transit_hours = 0;
  std::int32_t sunk_since_bronze_star = 0;  // +0x10c
  std::int32_t nuc_streak = 0;              // +0x110
  std::int32_t puc_streak = 0;              // +0x114
  std::int32_t patrols_since_new_boat = 0;  // +0x118
  std::uint32_t seed = 0;                   // +0x11c: srand() seed of the next patrol
  // +0x120 / +0x124: the career totals of pilots rescued and photo recons
  // (`career_apply_patrol_results`, `career_end_summary` 0x1ce8d); 0 before
  // version 2.
  std::int32_t pilots_rescued = 0;
  std::int32_t photo_recons = 0;
  std::uint16_t raids_this_patrol = 0;      // +0x128: air raids and recons, at most 2
  std::uint16_t boats_lost = 0;             // +0x12a
  std::array<std::int32_t, kCareerRealismCount> realism{};  // +0x12c, by `CareerRealism`
  std::int32_t on_patrol = 0;  // +0x15c: saved on patrol (the state lists follow)
  // +0x160; its last byte (+0x430) is the block's sea picture 0x105150.
  std::array<std::uint8_t, kWorldFilesSize> world_files{};
  std::int32_t weather_days_left = 0;  // +0x431 (0x187218)
  std::int32_t weather = 0;            // +0x435 (0x125f60)
  std::int32_t sea_state = 0;          // +0x439 (0x125f64)
  std::int32_t daily_fog = 0;          // +0x43d (0x129c68)
  std::int32_t thermal_layer_ft = 0;   // +0x441 (0x11b7c8)
  std::int32_t sea_picture = 0;        // +0x445 (0x105150)
  std::int32_t log_count = 0;          // +0x449: career-log entries in the file
  std::int32_t patrol_log_count = 0;   // +0x44d

  // The bytes the record was parsed from (zeros for a new one).
  std::array<std::uint8_t, kSize> raw{};

  std::int32_t award(Award a) const { return awards[static_cast<std::size_t>(a)]; }
  std::int32_t& award(Award a) { return awards[static_cast<std::size_t>(a)]; }
  std::int32_t realism_value(CareerRealism r) const { return realism[static_cast<std::size_t>(r)]; }

  // Parses `data` (at least `kSize` bytes). Throws `DataError` when short.
  static CareerRecord parse(std::span<const std::uint8_t> data);
  // Returns the record's bytes: `raw` with every field written over it.
  std::array<std::uint8_t, kSize> serialize() const;
};

// The summary block (0x74 bytes at 0x116c68, files of version 5 and up).
struct CareerSummary {
  static constexpr std::size_t kSize = 0x74;

  std::int32_t preferred_command = -1;  // +0x00: -1 any, else a `SubCommand`
  std::int32_t current_command = 0;     // +0x04
  std::int32_t patrols_since_transfer = 0;  // +0x08
  // +0x0c: -1 when `career_check_transfer` (0x4dd7e) offers a transfer (the
  // BASE MENU then shows REQUEST TRANSFER, 0x1b94a); cleared by the transfer.
  std::int32_t transfer_offered = 0;
  std::uint16_t same_zone_repeats = 0;      // +0x10
  std::int32_t fog_pct = 0;                 // +0x12: 0x170f88 (version 6 and up)
  // +0x16..+0x73 are never read nor written by SH.EXE (zero, kept in `raw`).

  std::array<std::uint8_t, kSize> raw{};

  static CareerSummary parse(std::span<const std::uint8_t> data);
  std::array<std::uint8_t, kSize> serialize() const;
};

// One career-log event (0x53 bytes, the sunk-list record).
struct CareerLogEntry {
  static constexpr std::size_t kSize = 0x53;

  SunkListEntry event;
  std::array<std::uint8_t, kSize> raw{};

  static CareerLogEntry parse(std::span<const std::uint8_t> data);
  static CareerLogEntry from(const SunkListEntry& event);
  std::array<std::uint8_t, kSize> serialize() const;
};

// What the career's commander asked for at the base and was granted (the
// extended variant's REQUESTS, game/career_requests.h; not in the
// original), for the boat named here: requests made on another boat do not
// apply. Kept in ROSTER<n>.EXT.
struct CareerRequests {
  // The boat (`CareerRecord::boat`) and its class (`class_name`).
  std::string boat;
  std::string class_name;
  // The deck gun fit, kept until the boat changes.
  BoatFit fit{};
  // The torpedo load chosen for the next patrol (WEAPONS.DAT ids, 0 for an
  // empty place), nothing for the generated one; cleared once the patrol
  // starts.
  std::optional<TorpedoLoad> torpedo_load{};
  // The patrol zone (PZ.DAT index) asked for the next patrol, nothing for
  // the one assigned; cleared once the patrol starts.
  std::optional<std::int32_t> patrol_zone{};

  friend bool operator==(const CareerRequests&, const CareerRequests&) = default;
};

// A whole career: what a ROSTER<n>.SAV holds.
struct Career {
  CareerRecord record;
  CareerSummary summary;
  // The career log (0x116c64): the ships sunk, in order.
  std::vector<CareerLogEntry> log;
  // The `sim_write_state` block of a career saved on patrol, kept as read
  // (empty in port).
  std::vector<std::uint8_t> patrol_state;
  // The version `patrol_state` is written in (the roster file's version,
  // `sim_read_state`'s argument; 7 for a state captured in game).
  std::uint8_t patrol_state_version = 7;
  // The extended variant's rules of the career (not in the original), kept
  // in ROSTER<n>.EXT beside its roster; none for the original's rules (the
  // classic variant writes no such file).
  std::optional<MissionRules> rules;
  // The requests granted (ROSTER<n>.EXT; not in the original), empty
  // without the extended rules.
  CareerRequests requests;
};

}  // namespace sh

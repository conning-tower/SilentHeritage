// The option files of the SAVE directory and where they are kept:
// `GOPTION.SAV` (game options), `GDETAIL.SAV` (detail level), `REALISM.SAV`
// (realism settings) and `SMISSION.SAV` (the single-mission encounter
// settings), in the original binary formats of their loaders and savers in
// SH.EXE (`goption_load` 0x524cb .. `smission_save` 0x52ff9).
//
// Each loader follows the original: the defaults first, then the fields of
// a file of the right version, in order, keeping those read before a short
// read; a missing file or a wrong version makes it write the current values
// (and use them).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sim/context.h"

namespace sh {

class Install;

// Writes `data` to `path` through a temporary file renamed over it, so
// `path` keeps its previous content when anything fails. Returns false on
// any failure (including a failed flush), leaving no temporary file. An
// existing `path` that is not a regular file (a device) is written in place.
bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> data);

// The save directory. Files are written to a host directory of their own;
// reading falls back to the install's SAVE directory for files not written
// yet, so the player's original saves are used without ever modifying the
// install.
class SaveDir {
 public:
  // `dir` is created on the first write. Reads fall back to `install_dir`
  // (a DOS path such as "SAVE\") of `install` when `install` is given.
  explicit SaveDir(std::filesystem::path dir, const Install* install = nullptr, std::string install_dir = {});

  const std::filesystem::path& dir() const { return dir_; }

  // Returns the content of `name` (e.g. "GOPTION.SAV"), or nothing when it
  // exists in neither place.
  std::optional<std::vector<std::uint8_t>> read(std::string_view name) const;
  // Writes `data` as `name` atomically (see `write_file_atomic`); returns
  // false when the file cannot be written (the original's savers then
  // silently write nothing). Names are matched case-insensitively against
  // the files already in `dir`, by all members.
  bool write(std::string_view name, std::span<const std::uint8_t> data) const;
  // Returns the names (upper case) of the files of both places whose name
  // ends with `extension` (e.g. ".SAV"), sorted, each once.
  std::vector<std::string> list(std::string_view extension) const;
  // Deletes `name` (the original's `remove`). A file of the install's
  // directory cannot be deleted: an empty file of that name is left in its
  // place, which reads as an empty file.
  void remove(std::string_view name) const;

 private:
  // Returns the path of the file of `dir` named `name` ignoring case, or
  // `dir / name` when there is none.
  std::filesystem::path own_path(std::string_view name) const;

  std::filesystem::path dir_;
  const Install* install_;
  std::string install_dir_;
};

// The original booleans of the option files: -1 true, 0 false.
inline constexpr std::int32_t kOptionOn = -1;
inline constexpr std::int32_t kOptionOff = 0;

// `GOPTION.SAV` version 3, the game options 0x11a8c4..0x11a8e8 and 0x11bd04
// (Alt-O dialog). Booleans are -1 / 0, volumes 0..127.
struct GameOptions {
  static constexpr std::uint8_t kVersion = 3;
  static constexpr std::string_view kFileName = "GOPTION.SAV";

  std::int32_t auto_torp_load = kOptionOn;   // 0x11a8c4
  std::int32_t auto_sj_radar = kOptionOn;    // 0x11a8c8
  std::int32_t auto_sd_radar = kOptionOn;    // 0x11a8cc
  std::int32_t auto_red_lights = kOptionOn;  // 0x11a8d0 (file version >= 2, stored last)
  std::int32_t sound_effects = kOptionOn;    // 0x11a8d4
  std::int32_t engine_sounds = kOptionOn;    // 0x11a8d8
  std::int32_t speech = kOptionOn;           // 0x11a8dc
  std::int32_t collision_alarm = kOptionOn;  // 0x11a8e0
  std::int32_t effects_volume = 100;         // 0x11a8e4
  std::int32_t speech_volume = 100;          // 0x11a8e8
  std::int32_t chart_follow_sub = 0;         // 0x11bd04 (file version >= 3)

  // Returns the file image (`goption_save`, 0x526ea).
  std::vector<std::uint8_t> to_bytes() const;
  // Reads the fields of `data` over `*this` (the read part of
  // `goption_load`). Returns false, changing nothing, when the version byte
  // is missing or is not 3.
  bool read_from(std::span<const std::uint8_t> data);
  // `goption_load` (0x524cb): the defaults, then the file.
  static GameOptions load(const SaveDir& dir);
  bool save(const SaveDir& dir) const { return dir.write(kFileName, to_bytes()); }

  bool operator==(const GameOptions&) const = default;
};

// `GDETAIL.SAV` version 1, the detail level (Alt-D dialog).
struct DetailOptions {
  static constexpr std::uint8_t kVersion = 1;
  static constexpr std::string_view kFileName = "GDETAIL.SAV";

  // Bits of `detail_bits`.
  static constexpr std::uint32_t kClouds = 0x1;
  static constexpr std::uint32_t kWaves = 0x2;
  static constexpr std::uint32_t kLandTexture = 0x4;
  static constexpr int kVisibilityShift = 3;  // bits 3-7
  static constexpr std::uint32_t kVisibilityMask = 0xf8;

  // 0x170b24: `gdetail_load` sets only its low byte to 0x7f.
  std::uint32_t detail_bits = 0x7f;
  std::int32_t torpedo_wakes = kOptionOn;  // 0x129cfc
  std::int32_t ship_wakes = kOptionOn;     // 0x129d00

  std::vector<std::uint8_t> to_bytes() const;
  bool read_from(std::span<const std::uint8_t> data);
  // `gdetail_load` (0x5231a).
  static DetailOptions load(const SaveDir& dir);
  bool save(const SaveDir& dir) const { return dir.write(kFileName, to_bytes()); }

  bool operator==(const DetailOptions&) const = default;
};

// The realism bits of the options word `0x11a85c` (low 16 bits; bits 0-1
// are the game mode), completing those of sim/context.h.
namespace option {
inline constexpr std::uint32_t kCombatLevelMask = 0xc;       // Bits 2-3: 0 novice .. 3 expert.
inline constexpr std::uint32_t kRealisticChartsOff = 0x40;
inline constexpr std::uint32_t kLimitedVisibility = 0x80;
inline constexpr std::uint32_t kUnlimitedAmmo = 0x800;
inline constexpr std::uint32_t kLimitedDepthDataOff = 0x1000;
inline constexpr std::uint32_t kRealisticReloads = 0x2000;
inline constexpr std::uint32_t kDudTorpedoes = 0x4000;
inline constexpr std::uint32_t kInvulnerable = 0x8000;
inline constexpr std::uint32_t kModeMask = 0x3;
inline constexpr std::uint32_t kRealismMask = 0xfffc;
}  // namespace option

// `REALISM.SAV` version 1: twelve values packed into the realism bits of
// the options word.
struct RealismFile {
  static constexpr std::uint8_t kVersion = 1;
  static constexpr std::string_view kFileName = "REALISM.SAV";
  // The realism bits `realism_load` starts from: the EXPERT preset.
  static constexpr std::uint32_t kDefaultBits = 25000;

  // Returns the file image of the realism bits of `options`
  // (`realism_save`, 0x52b54).
  static std::vector<std::uint8_t> to_bytes(std::uint32_t options);
  // Unpacks the values of `data` into `options` (the read part of
  // `realism_load`). Returns false, changing nothing, when the version byte
  // is missing or is not 1.
  static bool read_into(std::span<const std::uint8_t> data, std::uint32_t& options);
  // `realism_load` (0x5286f): replaces the realism bits of `options` by the
  // defaults, then by the file; the mode bits and the bits above 16 stay.
  static void load(const SaveDir& dir, std::uint32_t& options);
  static bool save(const SaveDir& dir, std::uint32_t options) { return dir.write(kFileName, to_bytes(options)); }
};

// The war window of the encounter dates (0x10e3a4..0x10e3bc): month 0-based,
// year since 1930.
struct WarWindow {
  static constexpr std::int32_t kYearBase = 1930;      // 0x10e3a4
  static constexpr std::int32_t kStartMonth = 11;      // 0x10e3a8: 7 December 1941
  static constexpr std::int32_t kStartDay = 7;         // 0x10e3ac
  static constexpr std::int32_t kStartYear = 11;       // 0x10e3b0
  static constexpr std::int32_t kEndMonth = 7;         // 0x10e3b4: 15 August 1945
  static constexpr std::int32_t kEndDay = 15;          // 0x10e3b8
  static constexpr std::int32_t kEndYear = 15;         // 0x10e3bc
};

// `SMISSION.SAV` version 1, the single-mission encounter settings
// 0x11bd08..0x11bd3c (indexes into the strings of the encounter dialogs).
struct EncounterSettings {
  static constexpr std::uint8_t kVersion = 1;
  static constexpr std::string_view kFileName = "SMISSION.SAV";

  std::int32_t year = WarWindow::kStartYear;    // 0x11bd08, since 1930
  std::int32_t month = WarWindow::kStartMonth;  // 0x11bd0c, 0-based
  std::int32_t time = 0;           // 0x11bd10: dawn, day, sunset, night
  std::int32_t weather = 0;        // 0x11bd14: clear .. overcast (4)
  std::int32_t sea = 0;            // 0x11bd18: calm, moderate, heavy
  std::int32_t convoy_size = 0;    // 0x11bd1c: small, medium, large
  std::int32_t escort = 0;         // 0x11bd20: none .. large (4)
  std::int32_t enemy = 0;          // 0x11bd24: green, veteran, elite
  std::int32_t crew = 0;           // 0x11bd28: green, veteran, elite
  std::int32_t air_cover = 0;      // 0x11bd2c: no, yes
  std::int32_t fog = 0;            // 0x11bd30: no, yes
  std::int32_t warship_type = 0;   // 0x11bd34: battleship, carrier, heavy / light cruiser
  std::int32_t patrol_size = 0;    // 0x11bd38: small (light) .. large (heavy)
  std::int32_t special_mission = 0;  // 0x11bd3c: lifeguard, photo recon, enemy sub

  std::vector<std::uint8_t> to_bytes() const;
  bool read_from(std::span<const std::uint8_t> data);
  // `smission_load` (0x52d93).
  static EncounterSettings load(const SaveDir& dir);
  bool save(const SaveDir& dir) const { return dir.write(kFileName, to_bytes()); }

  // Returns true when month/year lie in the war window
  // (`encounter_date_in_war`, 0xa384f).
  bool date_in_war() const;

  bool operator==(const EncounterSettings&) const = default;
};

}  // namespace sh

// The scenario editor's own executable `SE.EXE` as data: its relocated image
// (a DOS/4GW LE based at 0x10000 like SH.EXE) and the texts and constant
// tables the editor reads from it, by SE linear address (the addresses
// cited in the editor's comments).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "assets/exe_image.h"
#include "core/install.h"

namespace sh::editor {

// SE addresses of the strings and tables read by the editor library.
namespace se_addr {

// Object type names (vtable slot +0x14).
inline constexpr std::uint32_t kPlayer = 0x92f30;      // "PLAYER"
inline constexpr std::uint32_t kShip = 0x91ff1;        // "SHIP"
inline constexpr std::uint32_t kSubmarine = 0x91ff6;   // "SUBMARINE"
inline constexpr std::uint32_t kAircraft = 0x92000;    // "AIRCRAFT"
inline constexpr std::uint32_t kAirstrike = 0x92009;   // "AIRSTRIKE"
inline constexpr std::uint32_t kPhotoRecon = 0x92013;  // "PHOTO-RECON"

// `describe` labels (sbplayer_describe .. sbmission_describe).
inline constexpr std::uint32_t kClassLabel = 0x9262f;        // "Class: "
inline constexpr std::uint32_t kLocLabel = 0x9264f;          // "Loc: "
inline constexpr std::uint32_t kLatLonSeparator = 0x9266d;   // ", "
inline constexpr std::uint32_t kDepthLabel = 0x92688;        // "Depth: "
inline constexpr std::uint32_t kFeet = 0x926a8;              // " feet"
inline constexpr std::uint32_t kHeadingLabel = 0x926ba;      // "Heading: "
inline constexpr std::uint32_t kSpeedLabel = 0x926dc;        // "Speed: "
inline constexpr std::uint32_t kOpenParen = 0x92751;         // " ("
inline constexpr std::uint32_t kCloseParen = 0x9276c;        // ")"
inline constexpr std::uint32_t kCloseParenSpace = 0x928a8;   // ") "
inline constexpr std::uint32_t kKnots = 0x927f5;             // " kts"
inline constexpr std::uint32_t kOrdersLabel = 0x92806;       // "Orders: "
inline constexpr std::uint32_t kOpenBracket = 0x92827;       // " ["
inline constexpr std::uint32_t kCloseBracket = 0x92842;      // "]"
inline constexpr std::uint32_t kOpenQuote = 0x929af;         // " '"
inline constexpr std::uint32_t kCloseQuote = 0x929ca;        // "'"
inline constexpr std::uint32_t kGunsLabel = 0x929e4;         // "Guns: "
inline constexpr std::uint32_t kBombsLabel = 0x92a03;        // "Bombs: "
inline constexpr std::uint32_t kFlightDelayLabel = 0x92a7a;  // "Flight delay: "
inline constexpr std::uint32_t kTimeLimitLabel = 0x92a89;    // "Time limit: "
inline constexpr std::uint32_t kTargetLabel = 0x92aa2;       // "Target: "
inline constexpr std::uint32_t kTimeDelayLabel = 0x92b08;    // "Time delay: "
inline constexpr std::uint32_t kTimeSeparator = 0x92b2d;     // ":"
inline constexpr std::uint32_t kDegreeMark = 0x92b9d;        // "\x1a" (degree glyph)
inline constexpr std::uint32_t kMinuteMark = 0x92bb7;        // "\x1b" (minute glyph)

// `char*` tables, 4 bytes per entry unless noted.
inline constexpr std::uint32_t kEngineOrders = 0x9e6e0;  // "Stop" .. "Back 1/3" (10)
inline constexpr std::uint32_t kShipOrders = 0x9e640;    // "None" .. "Fishing" (8)
inline constexpr std::uint32_t kConvoyLetters = 0x9e730;  // "NA", "A" .. "H" (9)
inline constexpr std::uint32_t kYesNo = 0x9e75c;          // "No", "Yes"
inline constexpr std::uint32_t kStrikeSizes = 0x9e774;    // "Strike Size: SMALL" .. "LARGE" (3)
inline constexpr std::uint32_t kAircraftRoles = 0x9f0d8;  // "Fighter" .. "Medium Bomber" (4)
// SHIPS.DAT ship types: {abbreviation, long name} pairs (8 bytes per type).
inline constexpr std::uint32_t kShipTypeAbbrevs = 0x9f028;
inline constexpr std::uint32_t kShipTypeNames = 0x9f02c;

// Default player (sb_create_default_player).
inline constexpr std::uint32_t kHullPrefix = 0x93057;  // "(SS-"
inline constexpr std::uint32_t kHullSuffix = 0x93072;  // ") "
// The world of a new scenario (sb_load_data, 0x44e0b).
inline constexpr std::uint32_t kDefaultWorld = 0x92f42;  // "WORLD-1.WLD"
// Default torpedoes (sb_reset_torpedo_load).
inline constexpr std::uint32_t kMark10 = 0x93087;  // "MARK10"
inline constexpr std::uint32_t kMark14 = 0x93080;  // "MARK14"

}  // namespace se_addr

// The relocated image of SE.EXE.
class SeExe {
 public:
  // Builds the image from the bytes of SE.EXE; throws `DataError` if it is
  // not a DOS/4GW LE program based at 0x10000.
  explicit SeExe(std::span<const std::uint8_t> exe) : image_(exe) {}
  // Loads SE.EXE from `install`.
  static SeExe load(const Install& install);

  const ExeImage& image() const { return image_; }
  // Returns the NUL-terminated string at `address`.
  std::string_view str(std::uint32_t address) const { return image_.c_string(address); }
  // Returns entry `index` of the `char*` table at `table`, whose entries are
  // `stride` bytes apart.
  std::string_view table_str(std::uint32_t table, std::size_t index, std::size_t stride = 4) const;
  std::int32_t i32(std::uint32_t address) const { return image_.i32(address); }

 private:
  ExeImage image_;
};

}  // namespace sh::editor

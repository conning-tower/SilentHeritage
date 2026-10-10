// `SAVE\SBOPTION.SAV`, the scenario editor's options (globals 0xa50e4 ..
// 0xa5104), in the binary format of `sboption_load` (SE 0x473ef) and
// `sboption_save` (SE 0x475b1). docs/editor-formats.md "SBOPTION.SAV".
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "game/saves_options.h"

namespace sh::editor {

// The nine options, -1 on / 0 off. The last two are the defaults of a new
// scenario's `end_on_lost_contact` and `randomize`.
struct EditorOptions {
  static constexpr std::uint8_t kVersion = 2;
  static constexpr std::string_view kFileName = "SBOPTION.SAV";

  // OPTIONS "Limit by date".
  std::int32_t limit_ships = kOptionOn;         // 0xa50e4
  std::int32_t limit_subs = kOptionOn;          // 0xa50e8
  std::int32_t limit_aircraft = kOptionOn;      // 0xa50ec
  std::int32_t limit_patrol_areas = kOptionOn;  // 0xa50f0
  std::int32_t limit_equipment = kOptionOn;     // 0xa50f4
  // OPTIONS "Display".
  std::int32_t range_circles = kOptionOn;  // 0xa50f8
  std::int32_t convoy_icons = kOptionOn;   // 0xa50fc
  // OPTIONS "Exit On Break Contact" and "Randomize".
  std::int32_t end_on_lost_contact = kOptionOn;  // 0xa5100
  std::int32_t randomize = kOptionOn;            // 0xa5104

  // Returns the 37-byte file image (`sboption_save`).
  std::vector<std::uint8_t> to_bytes() const;
  // `sboption_load`: all options on, then the file. A missing file or one
  // of another version is rewritten with the current values and read again;
  // an empty file or a short read keeps the values read so far without
  // rewriting.
  static EditorOptions load(const SaveDir& dir);
  // Returns false when the file cannot be written (the original then writes
  // nothing and says nothing).
  bool save(const SaveDir& dir) const { return dir.write(kFileName, to_bytes()); }

  bool operator==(const EditorOptions&) const = default;
};

}  // namespace sh::editor

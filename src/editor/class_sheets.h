// The lists and class sheets of SELECT SUB TYPE, SELECT SUB NAME, SELECT
// SHIP TYPE and SELECT AIRCRAFT TYPE without their dialogs: the rows of
// each list ("Limit by date" applied to the scenario date) with the table
// index of each row, and the texts of the sheets with their positions in
// the dialog (`sub_type_list_build` / `sub_type_sheet_draw`, SE 0x25bce /
// 0x25cef, and their ship, aircraft and boat-name counterparts).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/fnt.h"
#include "editor/data.h"
#include "editor/options.h"
#include "editor/scenario.h"

namespace sh::editor {

// A filtered list: the row texts, the table index of each row (the list
// index maps 0xa6760 / 0xa6768 / 0xa6770) and the row of the current
// entry (0 when the current entry is not listed).
struct ClassList {
  std::vector<std::string> rows;
  std::vector<std::size_t> entries;
  std::int32_t current = 0;
};

// Whether a table entry serving from (`start_month` 1-based, `start_year`)
// to (`end_month`, `end_year`), both included and years in full, is in
// service on (`month` 0-based, `year` since 1930), as the list builders
// compare.
bool in_service(std::int32_t start_month, std::int32_t start_year, std::int32_t end_month, std::int32_t end_year,
                std::int32_t month, std::int32_t year);

// `sub_type_list_build`: SUBS.DAT names (+0), the classes in service on
// (`month`, `year`) with "Limit by date: Subs"; `current` is the SUBS.DAT
// index of the player's class.
ClassList sub_type_list(const EditorData& data, const EditorOptions& options, std::int32_t month, std::int32_t year,
                        std::size_t current);

// `ship_type_list_build`: "<type abbreviation padded to 40 px in
// `small`><class name>" upper-cased (the merchant types show their long
// type name instead of the class), filtered with "Limit by date: Ships";
// `current` is the SHIPS.DAT index of the current class (-1 none).
ClassList ship_type_list(const EditorData& data, const EditorOptions& options, const Font& small, std::int32_t month,
                         std::int32_t year, std::int32_t current);

// `aircraft_type_list_build`: AIRCRAFT.DAT long names (+0x19), filtered
// with "Limit by date: Aircraft"; `current` as for the ships.
ClassList aircraft_type_list(const EditorData& data, const EditorOptions& options, std::int32_t month,
                             std::int32_t year, std::int32_t current);

// `sub_name_list_build`: the CARSUB.DAT boats of SUBS.DAT class `sub_class`
// as "(SS-<hull>) <name>" (not filtered by date), `entries` their CARSUB
// indexes; the current row is the one equal to `current_name`. With
// `commissioned_by` (not in the original), only the boats available from
// that date's month or earlier (CARSUB +24 / +28), or all of the class's
// boats when none is.
ClassList sub_name_list(const EditorData& data, std::size_t sub_class, std::string_view current_name,
                        const std::optional<ScenarioDate>& commissioned_by = std::nullopt);

// One text of a class sheet at (x, y) of the dialog, drawn in FONT1.
struct SheetText {
  int x = 0;
  int y = 0;
  std::string text;

  bool operator==(const SheetText&) const = default;
};

// The texts of the SELECT SUB TYPE sheet of SUBS.DAT class `sub_class`:
// "<DISPLAY NAME> CLASS" centred with `font1`, Speed, Fwd Tubes,
// Submerged, Aft Tubes, Test Depth, Torpedoes (tubes + reloads), Displace,
// Range, Available m/yy thru m/yy (clamped to the war), Guns n - cal".
std::vector<SheetText> sub_sheet(const EditorData& data, std::size_t sub_class, const Font& font1);

// The texts of the SELECT SHIP TYPE sheet of SHIPS.DAT class `ship_class`:
// the long type name, Tonnage, Guns, Length, DC's (Torpedoes for a
// submarine), Beam, Racks, Draft, Throwers (not for a submarine), Speed
// (surfaced (submerged) for a submarine), Available.
std::vector<SheetText> ship_sheet(const EditorData& data, std::size_t ship_class, const Font& font1);

// The texts of the SELECT AIRCRAFT TYPE sheet of AIRCRAFT.DAT type
// `aircraft`: "<long name> '<name>'", Type, Span, Cruise Spd, Length, Max
// Speed, Height, Range, Guns, Ceiling, Bombs, Available, Torpedoes.
std::vector<SheetText> aircraft_sheet(const EditorData& data, std::size_t aircraft, const Font& font1);

}  // namespace sh::editor

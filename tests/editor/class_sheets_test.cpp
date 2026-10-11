// The lists and class sheets of the SELECT dialogs: "Limit by date"
// against the tables' service dates, the ship rows' type column, the boat
// names of SELECT SUB NAME, and the sheet texts of known classes.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include "editor/class_sheets.h"
#include "editor/scenario.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

constexpr std::int32_t year_of(std::int32_t full) { return full - kScenarioYearBase; }

// Returns the text of `texts` at (x, y), or nothing.
std::optional<std::string> text_at(const std::vector<SheetText>& texts, int x, int y) {
  const auto it = std::ranges::find_if(texts, [&](const SheetText& t) { return t.x == x && t.y == y; });
  if (it == texts.end()) {
    return std::nullopt;
  }
  return it->text;
}

}  // namespace

TEST_CASE("Service periods include both ends by month", "[editor][select]") {
  // From March 1943 to June 1944.
  CHECK_FALSE(in_service(3, 1943, 6, 1944, 1, year_of(1943)));
  CHECK(in_service(3, 1943, 6, 1944, 2, year_of(1943)));
  CHECK(in_service(3, 1943, 6, 1944, 5, year_of(1944)));
  CHECK_FALSE(in_service(3, 1943, 6, 1944, 6, year_of(1944)));
}

TEST_CASE("The SELECT lists keep the classes in service with Limit by date", "[editor][select]") {
  SH_REQUIRE_INSTALL(install);
  const EditorData& data = test::editor_data(install);
  const Font small = Font::load(install, "ART/GUI_S.FNT");
  EditorOptions limited;
  EditorOptions all;
  all.limit_subs = all.limit_ships = all.limit_aircraft = 0;
  // May 1945: the BARRACUDA and NARWHAL left service in March and April.
  const std::int32_t month = 4;
  const std::int32_t year = year_of(1945);

  SECTION("Subs") {
    const ClassList list = sub_type_list(data, limited, month, year, 0);
    std::vector<std::size_t> expected;
    for (std::size_t i = 0; i < data.tables.subs.size(); ++i) {
      if (sub_in_service(data.tables.subs[i], month, year)) {
        expected.push_back(i);
      }
    }
    CHECK(list.entries == expected);
    REQUIRE_FALSE(list.entries.empty());
    CHECK(list.entries[1] > 1);  // classes out of service before later ones
    CHECK(list.rows.front() == data.tables.subs[list.entries.front()].name);
    CHECK(sub_type_list(data, all, month, year, 0).rows.size() == data.tables.subs.size());
    // The current class's row; an unlisted one gives row 0.
    CHECK(sub_type_list(data, limited, month, year, expected.back()).current ==
          static_cast<std::int32_t>(expected.size()) - 1);
    CHECK(sub_type_list(data, limited, month, year, 0).current == 0);
  }
  SECTION("Ships") {
    const ClassList list = ship_type_list(data, limited, small, month, year, -1);
    for (std::size_t row = 0; row < list.entries.size(); ++row) {
      const ShipClass& s = data.tables.ships[list.entries[row]];
      const std::int32_t start = static_cast<std::int32_t>(s.start_year * 12 + s.start_month);
      const std::int32_t end = static_cast<std::int32_t>(s.end_year * 12 + s.end_month);
      CAPTURE(s.name);
      CHECK(start <= 1945 * 12 + 5);
      CHECK(end >= 1945 * 12 + 5);
    }
    const ClassList every = ship_type_list(data, all, small, month, year, -1);
    CHECK(every.rows.size() == data.tables.ships.size());
    CHECK(list.rows.size() < every.rows.size());
    // "<type> <class>" upper case, the type padded to 40 pixels; merchants
    // show their type's long name.
    for (std::size_t row = 0; row < every.rows.size(); ++row) {
      const ShipClass& s = data.tables.ships[every.entries[row]];
      const std::string& text = every.rows[row];
      const bool merchant = static_cast<int>(s.ship_type) > static_cast<int>(ShipType::kSubmarine);
      std::string tail(merchant ? data.se.table_str(se_addr::kShipTypeNames, static_cast<std::size_t>(s.ship_type), 8)
                                : s.name);
      std::ranges::transform(tail, tail.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
      CAPTURE(text);
      REQUIRE(text.ends_with(tail));
      CHECK(small.text_width(text.substr(0, text.size() - tail.size())) >= 0x28);
    }
  }
  SECTION("Aircraft") {
    const ClassList list = aircraft_type_list(data, limited, month, year, -1);
    for (const std::size_t i : list.entries) {
      const AircraftType& a = data.tables.aircraft[i];
      CHECK(in_service(static_cast<std::int32_t>(a.start_month), static_cast<std::int32_t>(a.intro_year),
                       static_cast<std::int32_t>(a.end_month), static_cast<std::int32_t>(a.end_year), month, year));
    }
    CHECK(aircraft_type_list(data, all, month, year, -1).rows.size() == data.tables.aircraft.size());
    CHECK(list.rows.front() == data.tables.aircraft[list.entries.front()].long_name);
  }
}

TEST_CASE("SELECT SUB NAME lists the boats of a class", "[editor][select][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const EditorData& data = test::editor_data(install);
  const std::size_t gato = *data.find_sub("GATO");
  const ClassList list = sub_name_list(data, gato, "");
  REQUIRE_FALSE(list.rows.empty());
  for (std::size_t row = 0; row < list.rows.size(); ++row) {
    const CarsubBoat& boat = data.tables.carsub[list.entries[row]];
    CHECK(boat.class_index == data.tables.subs[gato].class_index);
    CHECK(list.rows[row] == "(SS-" + std::to_string(boat.hull_number) + ") " + boat.name);
  }
  CHECK(list.current == 0);
  CHECK(sub_name_list(data, gato, list.rows.back()).current == static_cast<std::int32_t>(list.rows.size()) - 1);
}

TEST_CASE("The class sheets show the tables' figures", "[editor][select][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const EditorData& data = test::editor_data(install);
  const Font font1 = Font::load(install, "ART/FONT1.FNT");

  SECTION("GATO") {
    const SubClass& g = data.tables.subs[*data.find_sub("GATO")];
    const std::vector<SheetText> t = sub_sheet(data, *data.find_sub("GATO"), font1);
    const std::string title = g.display_name + " CLASS";
    CHECK(t.front() == SheetText{(0x14f - font1.text_width(title)) / 2 + 0x10, 0x2f, title});
    CHECK(text_at(t, 0x14, 0x8e) == "Speed:");
    CHECK(text_at(t, 100, 0x8e) == g.surface_speed_kts + " kts");
    CHECK(text_at(t, 100, 0x99) == g.submerged_speed_kts + " kts");
    CHECK(text_at(t, 0x118, 0x8e) == std::to_string(g.bow_tubes));
    CHECK(text_at(t, 0x118, 0x99) == std::to_string(g.stern_tubes));
    CHECK(text_at(t, 100, 0xa4) == "300 ft");
    CHECK(text_at(t, 0x118, 0xa4) == "24");
    CHECK(text_at(t, 100, 0xaf) == std::to_string(g.displacement_surfaced) + " tons");
    CHECK(text_at(t, 0x118, 0xaf) == std::to_string(g.range_nm) + " nm");
    CHECK(text_at(t, 0x118, 0xba) ==
          std::to_string(g.deck_gun_count) + " - " + std::to_string(g.deck_gun_caliber_in) + "\"");
    // Available: clamped to 12/41 .. 8/45.
    const std::optional<std::string> available = text_at(t, 0x64, 0xba);
    REQUIRE(available);
    CHECK(available->ends_with(" thru 8/45"));
  }
  SECTION("A submarine of SHIPS.DAT shows torpedoes and both speeds") {
    const auto it = std::ranges::find_if(data.tables.ships,
                                         [](const ShipClass& s) { return s.ship_type == ShipType::kSubmarine; });
    REQUIRE(it != data.tables.ships.end());
    const auto cls = static_cast<std::size_t>(it - data.tables.ships.begin());
    const std::vector<SheetText> t = ship_sheet(data, cls, font1);
    CHECK(t.front().text == "SUBMARINE");
    CHECK(text_at(t, 0xa4, 0xe8) == "Torpedoes:");
    CHECK_FALSE(text_at(t, 0xa4, 0xf4) == "Racks:");
    CHECK(text_at(t, 0xa4, 0xf4) == "Available:");  // in the Racks row
    const std::optional<std::string> speed = text_at(t, 0x55, 0x10c);
    REQUIRE(speed);
    CHECK(speed->starts_with(it->top_speed_kts + " ("));
    CHECK(speed->ends_with(") kts"));
  }
  SECTION("A destroyer shows depth charges, racks and throwers") {
    const auto it = std::ranges::find_if(data.tables.ships,
                                         [](const ShipClass& s) { return s.ship_type == ShipType::kDestroyer; });
    REQUIRE(it != data.tables.ships.end());
    const std::vector<SheetText> t = ship_sheet(data, static_cast<std::size_t>(it - data.tables.ships.begin()), font1);
    CHECK(t.front().text == "DESTROYER");
    CHECK(text_at(t, 0x55, 0xdc) == std::to_string(it->displacement_tons) + " tons");
    CHECK(text_at(t, 0xa4, 0xe8) == "DC's:");
    CHECK(text_at(t, 0xa4, 0xf4) == "Racks:");
    CHECK(text_at(t, 0xa4, 0x100) == "Throwers:");
    CHECK(text_at(t, 0xa4, 0x10c) == "Available:");
    CHECK(text_at(t, 0x55, 0x10c) == it->top_speed_kts + " kts");
  }
  SECTION("An aircraft") {
    const AircraftType& a = data.tables.aircraft.front();
    const std::vector<SheetText> t = aircraft_sheet(data, 0, font1);
    CHECK(t.front().text == a.long_name + " '" + a.name + "'");
    CHECK(text_at(t, 100, 0xb2) == std::to_string(a.max_speed_mph) + " kts");
    CHECK(text_at(t, 100, 0xbd) == std::to_string(a.range_mi) + " miles");
    CHECK(text_at(t, 300, 0xbd) == std::to_string(a.guns));
    CHECK(text_at(t, 300, 200) == std::to_string(a.bombs));
    CHECK(text_at(t, 0xdc, 0xd3) == "Torpedoes:");
  }
}

TEST_CASE("Extended: SELECT SUB NAME keeps the boats commissioned by the date", "[editor][select][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  const EditorData& data = test::editor_data(install);
  const std::size_t gato = *data.find_sub("GATO");
  const ClassList all = sub_name_list(data, gato, "");
  // January 1943 (month 0-based, years since 1930).
  const ScenarioDate jan43{1, 0, 13};
  const ClassList dated = sub_name_list(data, gato, "", jan43);
  REQUIRE_FALSE(dated.rows.empty());
  CHECK(dated.rows.size() < all.rows.size());
  for (const std::size_t e : dated.entries) {
    const CarsubBoat& b = data.tables.carsub[e];
    CHECK((b.avail_year < 43 || (b.avail_year == 43 && b.avail_month <= 1)));
  }
  std::size_t commissioned = 0;
  for (const std::size_t e : all.entries) {
    const CarsubBoat& b = data.tables.carsub[e];
    commissioned += b.avail_year < 43 || (b.avail_year == 43 && b.avail_month <= 1) ? 1 : 0;
  }
  CHECK(dated.rows.size() == commissioned);
  // The war's end lists them all; no date is the original's list.
  CHECK(sub_name_list(data, gato, "", ScenarioDate{15, 7, 15}).rows == all.rows);
  CHECK(sub_name_list(data, gato, "", std::nullopt).rows == all.rows);
  // Before any boat of the class: all of them rather than none.
  CHECK(sub_name_list(data, gato, "", ScenarioDate{1, 0, 5}).rows == all.rows);
}

}  // namespace sh::editor

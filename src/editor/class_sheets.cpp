#include "editor/class_sheets.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "editor/environment.h"

namespace sh::editor {

namespace {

std::string upper(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

std::string str(const EditorData& data, std::uint32_t address) { return std::string(data.se.str(address)); }

// "m/yy thru m/yy" of a service period, each end clamped to the war;
// `strings` are the call site's "/", " thru ", "/".
std::string available(const EditorData& data, std::int32_t start_month, std::int32_t start_year,
                      std::int32_t end_month, std::int32_t end_year, const std::array<std::uint32_t, 3>& strings) {
  return format_month_year(clamp_to_war(start_month, start_year - kScenarioYearBase), data.se.str(strings[0])) +
         str(data, strings[1]) +
         format_month_year(clamp_to_war(end_month, end_year - kScenarioYearBase), data.se.str(strings[2]));
}

// The x of a title centred over the sheet (`(0x14f - width) / 2 + 0x10`).
int centred(const Font& font, std::string_view text) { return (0x14f - font.text_width(text)) / 2 + 0x10; }

// `pad_to_pixel_width` (SE 0x2a891): spaces, then char 2 for the last
// pixels, until `text` is `width` pixels wide in `font`.
void pad_to_pixel_width(std::string& text, const Font& font, int width) {
  int w = font.text_width(text);
  const int space = font.glyph_width(' ');
  while (w < width) {
    const char c = width - w < space ? '\x02' : ' ';
    const int step = font.glyph_width(static_cast<std::uint8_t>(c));
    text += c;
    if (step <= 0) {
      break;  // Deviation: the original loops forever on an empty glyph.
    }
    w += step;
  }
}

}  // namespace

bool in_service(std::int32_t start_month, std::int32_t start_year, std::int32_t end_month, std::int32_t end_year,
                std::int32_t month, std::int32_t year) {
  const std::int32_t y = year + kScenarioYearBase;
  const std::int32_t m = month + 1;
  const bool started = start_year < y || (start_year == y && start_month <= m);
  const bool not_ended = y < end_year || (y == end_year && m <= end_month);
  return started && not_ended;
}

ClassList sub_type_list(const EditorData& data, const EditorOptions& options, std::int32_t month, std::int32_t year,
                        std::size_t current) {
  ClassList list;
  const std::vector<SubClass>& subs = data.tables.subs;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    const SubClass& s = subs[i];
    if (options.limit_subs != 0 &&
        !in_service(static_cast<std::int32_t>(s.intro_month), static_cast<std::int32_t>(s.intro_year),
                    static_cast<std::int32_t>(s.end_month),
                    static_cast<std::int32_t>(s.end_year), month, year)) {
      continue;
    }
    if (i == current) {
      list.current = static_cast<std::int32_t>(list.rows.size());
    }
    list.rows.push_back(s.name);
    list.entries.push_back(i);
  }
  return list;
}

ClassList ship_type_list(const EditorData& data, const EditorOptions& options, const Font& small, std::int32_t month,
                         std::int32_t year, std::int32_t current) {
  ClassList list;
  const std::vector<ShipClass>& ships = data.tables.ships;
  for (std::size_t i = 0; i < ships.size(); ++i) {
    const ShipClass& s = ships[i];
    if (options.limit_ships != 0 &&
        !in_service(static_cast<std::int32_t>(s.start_month), static_cast<std::int32_t>(s.start_year),
                    static_cast<std::int32_t>(s.end_month), static_cast<std::int32_t>(s.end_year), month, year)) {
      continue;
    }
    if (static_cast<std::int32_t>(i) == current) {
      list.current = static_cast<std::int32_t>(list.rows.size());
    }
    const auto type = static_cast<std::size_t>(s.ship_type);
    std::string row(data.se.table_str(se_addr::kShipTypeAbbrevs, type, 8));
    pad_to_pixel_width(row, small, 0x28);
    row += type > static_cast<std::size_t>(ShipType::kSubmarine) ? data.se.table_str(se_addr::kShipTypeNames, type, 8)
                                                                  : s.name;
    list.rows.push_back(upper(row));
    list.entries.push_back(i);
  }
  return list;
}

ClassList aircraft_type_list(const EditorData& data, const EditorOptions& options, std::int32_t month,
                             std::int32_t year, std::int32_t current) {
  ClassList list;
  const std::vector<AircraftType>& aircraft = data.tables.aircraft;
  for (std::size_t i = 0; i < aircraft.size(); ++i) {
    const AircraftType& a = aircraft[i];
    if (options.limit_aircraft != 0 &&
        !in_service(static_cast<std::int32_t>(a.start_month), static_cast<std::int32_t>(a.intro_year),
                    static_cast<std::int32_t>(a.end_month), static_cast<std::int32_t>(a.end_year), month, year)) {
      continue;
    }
    list.rows.push_back(a.long_name);
    list.entries.push_back(i);
    if (static_cast<std::int32_t>(i) == current) {
      list.current = static_cast<std::int32_t>(list.rows.size()) - 1;
    }
  }
  return list;
}

ClassList sub_name_list(const EditorData& data, std::size_t sub_class, std::string_view current_name,
                        const std::optional<ScenarioDate>& commissioned_by) {
  ClassList list;
  // The boats whose CARSUB class byte equals the class's SUBS.DAT +0x24.
  const std::uint32_t class_index = data.tables.subs[sub_class].class_index;
  const std::vector<CarsubBoat>& boats = data.tables.carsub;
  // CARSUB's years are two-digit (19xx), its months 1-12.
  const auto commissioned = [&](const CarsubBoat& b) {
    const std::int32_t y = commissioned_by->year + kScenarioYearBase - 1900;
    const auto year = static_cast<std::int32_t>(b.avail_year < 100 ? b.avail_year : b.avail_year - 1900);
    return year < y || (year == y && static_cast<std::int32_t>(b.avail_month) <= commissioned_by->month + 1);
  };
  const bool filtered = commissioned_by && std::ranges::any_of(boats, [&](const CarsubBoat& b) {
                          return b.class_index == class_index && commissioned(b);
                        });
  for (std::size_t i = 0; i < boats.size(); ++i) {
    if (boats[i].class_index != class_index || (filtered && !commissioned(boats[i]))) {
      continue;
    }
    std::string row = str(data, 0x90cb9) + std::to_string(boats[i].hull_number) + str(data, 0x90cd6) + boats[i].name;
    if (row == current_name) {
      list.current = static_cast<std::int32_t>(list.rows.size());
    }
    list.rows.push_back(std::move(row));
    list.entries.push_back(i);
  }
  return list;
}

std::vector<SheetText> sub_sheet(const EditorData& data, std::size_t sub_class, const Font& font1) {
  // sub_type_sheet_draw (0x25cef)
  const SubClass& s = data.tables.subs[sub_class];
  const auto n = [](std::uint32_t v) { return std::to_string(v); };
  const std::string title = s.display_name + str(data, 0x90b08);
  return {
      {centred(font1, title), 0x2f, title},
      {0x14, 0x8e, str(data, 0x90b0f)},
      {100, 0x8e, s.surface_speed_kts + str(data, 0x90b2e)},
      {200, 0x8e, str(data, 0x90b33)},
      {0x118, 0x8e, n(s.bow_tubes)},
      {0x14, 0x99, str(data, 0x90b3e)},
      {100, 0x99, s.submerged_speed_kts + str(data, 0x90b61)},
      {200, 0x99, str(data, 0x90b66)},
      {0x118, 0x99, n(s.stern_tubes)},
      {0x14, 0xa4, str(data, 0x90b71)},
      {100, 0xa4, n(s.test_depth_ft) + str(data, 0x90b95)},
      {200, 0xa4, str(data, 0x90b99)},
      {0x118, 0xa4, n(s.bow_reloads + s.stern_reloads + s.bow_tubes + s.stern_tubes)},
      {0x14, 0xaf, str(data, 0x90ba4)},
      {100, 0xaf, n(s.displacement_surfaced) + str(data, 0x90bc6)},
      {200, 0xaf, str(data, 0x90bcc)},
      {0x118, 0xaf, n(s.range_nm) + str(data, 0x90beb)},
      {0x14, 0xba, str(data, 0x90bef)},
      {0x64, 0xba,
       available(data, static_cast<std::int32_t>(s.intro_month), static_cast<std::int32_t>(s.intro_year),
                 static_cast<std::int32_t>(s.end_month),
                 static_cast<std::int32_t>(s.end_year), {0x90c12, 0x90c2c, 0x90c4b})},
      {0xc8, 0xba, str(data, 0x90c59)},
      {0x118, 0xba, n(s.deck_gun_count) + str(data, 0x90c6b) + n(s.deck_gun_caliber_in) + str(data, 0x90c87)},
  };
}

std::vector<SheetText> ship_sheet(const EditorData& data, std::size_t ship_class, const Font& font1) {
  // ship_type_sheet_draw (0x26a1a): rows 12 pixels apart from y 0xdc.
  const ShipClass& s = data.tables.ships[ship_class];
  const bool sub = s.ship_type == ShipType::kSubmarine;
  const auto count = [&](std::int32_t v, std::uint32_t none) { return v == 0 ? str(data, none) : std::to_string(v); };
  const std::string title = upper(std::string(data.se.table_str(se_addr::kShipTypeNames,
                                                                static_cast<std::size_t>(s.ship_type), 8)));
  std::vector<SheetText> t{{centred(font1, title), 0x2f, title}};
  constexpr int kX = 0x14;
  int y = 0xdc;
  t.push_back({kX, y, str(data, 0x90d85)});
  t.push_back({kX + 0x41, y, std::to_string(s.displacement_tons) + str(data, 0x90d9a)});
  t.push_back({kX + 0x90, y, str(data, 0x90da0)});
  t.push_back({kX + 0xde, y,
               s.main_gun_count == 0 ? str(data, 0x90ddc)
                                     : std::to_string(s.main_gun_count) + str(data, 0x90db2) + s.main_gun_caliber_in +
                                           str(data, 0x90dce)});
  y += 0xc;
  t.push_back({kX, y, str(data, 0x90de1)});
  t.push_back({kX + 0x41, y, std::to_string(s.length_ft) + str(data, 0x90df5)});
  t.push_back({kX + 0x90, y, str(data, sub ? 0x90df9 : 0x90e04)});
  t.push_back({kX + 0xde, y, count(static_cast<std::int32_t>(s.depth_charges), 0x90e16)});
  y += 0xc;
  t.push_back({kX, y, str(data, 0x90e1b)});
  t.push_back({kX + 0x41, y, std::to_string(s.beam_ft) + str(data, 0x90e2d)});
  const int beam_y = y;
  if (!sub) {
    t.push_back({kX + 0x90, y, str(data, 0x90e31)});
    t.push_back({kX + 0xde, y, count(static_cast<std::int32_t>(s.dc_racks), 0x90e44)});
  }
  y += 0xc;
  t.push_back({kX, y, str(data, 0x90e49)});
  t.push_back({kX + 0x41, y, std::to_string(s.draft_ft) + str(data, 0x90e5c)});
  if (!sub) {
    t.push_back({kX + 0x90, y, str(data, 0x90e60)});
    t.push_back({kX + 0xde, y, count(static_cast<std::int32_t>(s.dc_throwers), 0x90e76)});
  }
  y += 0xc;
  t.push_back({kX, y, str(data, 0x90e7b)});
  std::string speed = s.top_speed_kts;
  if (sub) {
    speed += str(data, 0x90e9a) + s.submerged_speed_kts + str(data, 0x90eb5);
  }
  t.push_back({kX + 0x41, y, speed + str(data, 0x90ec3)});
  // A submarine's Available takes the empty Racks row.
  if (sub) {
    y = beam_y;
  }
  t.push_back({kX + 0x90, y, str(data, 0x90ec8)});
  t.push_back({kX + 0xde, y,
               available(data, static_cast<std::int32_t>(s.start_month), static_cast<std::int32_t>(s.start_year),
                         static_cast<std::int32_t>(s.end_month), static_cast<std::int32_t>(s.end_year),
                         {0x90eeb, 0x90f05, 0x90f24})});
  return t;
}

std::vector<SheetText> aircraft_sheet(const EditorData& data, std::size_t aircraft, const Font& font1) {
  // aircraft_type_sheet_draw (0x275db)
  const AircraftType& a = data.tables.aircraft[aircraft];
  const auto u = [](std::uint32_t v) { return std::to_string(v); };
  const std::string title = a.long_name + str(data, 0x90f92) + a.name + str(data, 0x90fad);
  return {
      {centred(font1, title), 0x2f, title},
      {0x14, 0x9c, str(data, 0x90faf)},
      {100, 0x9c, std::string(data.se.table_str(se_addr::kAircraftRoles, static_cast<std::size_t>(a.role)))},
      {0xdc, 0x9c, str(data, 0x90fc1)},
      {300, 0x9c, u(a.span_ft) + str(data, 0x90fdf)},
      {0x14, 0xa7, str(data, 0x90fe3)},
      {100, 0xa7, u(a.cruise_speed_mph) + str(data, 0x91007)},
      {0xdc, 0xa7, str(data, 0x9100c)},
      {300, 0xa7, u(a.length_ft) + str(data, 0x9102c)},
      {0x14, 0xb2, str(data, 0x91030)},
      {100, 0xb2, u(a.max_speed_mph) + str(data, 0x91053)},
      {0xdc, 0xb2, str(data, 0x91058)},
      {300, 0xb2, u(a.height_ft) + str(data, 0x91078)},
      {0x14, 0xbd, str(data, 0x9107c)},
      {100, 0xbd, u(a.range_mi) + str(data, 0x9109b)},
      {0xdc, 0xbd, str(data, 0x910a2)},
      {300, 0xbd, u(a.guns)},
      {0x14, 200, str(data, 0x910a8)},
      {100, 200, u(a.ceiling_ft) + str(data, 0x910c9)},
      {0xdc, 200, str(data, 0x910cd)},
      {300, 200, u(a.bombs)},
      {0x14, 0xd3, str(data, 0x910d4)},
      {0x64, 0xd3,
       available(data, static_cast<std::int32_t>(a.start_month), static_cast<std::int32_t>(a.intro_year),
                 static_cast<std::int32_t>(a.end_month), static_cast<std::int32_t>(a.end_year),
                 {0x910f7, 0x91111, 0x91130})},
      {0xdc, 0xd3, str(data, 0x9113e)},
      {300, 0xd3, u(a.torpedoes)},
  };
}

}  // namespace sh::editor

#include "game/stations/chart_reports.h"

#include <algorithm>
#include <format>

#include "assets/dat_tables.h"
#include "core/fixed_math.h"
#include "game/stations/chart.h"
#include "sim/aircraft_strike.h"
#include "sim/enemy_sub.h"
#include "sim/ship.h"

namespace sh {

namespace {

// SH.EXE tables.
constexpr std::uint32_t kMonths = 0x10e910;        // char*[12] "January"..
constexpr std::uint32_t kSizeNames = 0x1033e8;     // char*[3] "Small", "Medium", "Large"
constexpr std::uint32_t kWarshipNames = 0x1033d8;  // char*[4] "BB", "CV", "CA", "CL"
constexpr std::uint32_t kWarshipFrames = 0x1033f4; // int[4] CHART.SHP frames of the task forces
constexpr std::uint32_t kSideNames = 0x1030dc;     // char*[3] "", "Japanese", "American"
constexpr std::uint32_t kPortSizes = 0x1030e8;     // char*[4] "Fishing", "Small", "Medium", "Large"
constexpr std::uint32_t kShipTypes = 0x10ea00;     // {abbreviation, name}[] by SHIPS.DAT +25
constexpr std::uint32_t kSubmarineType = 0x10ea5c; // "Submarine"
constexpr std::uint32_t kLocationTypes = 0x10eb54; // char*[] .WLD location type names

// The group symbols (CHART.SHP frames and colours).
constexpr std::size_t kSymbolSmall = 0x1f;
constexpr std::size_t kSymbolUnknownSize = 0x20;
constexpr std::uint8_t kMerchantColour = 0x7b;
constexpr std::uint8_t kWarshipColour = 0xfa;
// A contact lasts 8 h (0x7080 s): 0x707fffff in 16.16.
constexpr std::int32_t kContactSeconds = 0x7080;
constexpr std::int32_t kContactTimer = 0x707fffff;
// SHIPS.DAT +25 types below this are warships and merchants; from 12 on
// merchants.
constexpr std::int32_t kFirstOtherType = 0x15;
constexpr std::int32_t kFirstMerchantType = 0xc;

std::string str(const ExeImage& exe, std::uint32_t address) { return std::string(exe.c_string(address)); }

std::string table_string(const ExeImage& exe, std::uint32_t table, std::int32_t index) {
  return std::string(exe.string_pointer(table + static_cast<std::uint32_t>(index) * 4));
}

// `int_to_padded_str` (0x5908d).
std::string padded(std::int32_t value, int width) { return std::format("{:0{}}", value, width); }

// "HHMM<hrs>Mon <day>": the time line of the reports, the month cut to
// three letters.
std::string time_line(const ExeImage& exe, std::int32_t hour, std::int32_t minute, std::int32_t month,
                      std::int32_t day, std::uint32_t hrs, std::uint32_t space) {
  return padded(hour, 2) + padded(minute, 2) + str(exe, hrs) + table_string(exe, kMonths, month).substr(0, 3) +
         str(exe, space) + std::to_string(day);
}

std::string title(const ExeImage& exe, bool materialized, std::uint32_t sighted, std::uint32_t report) {
  return str(exe, materialized ? sighted : report);
}

}  // namespace

std::vector<std::string> chart_popup_lines(const std::vector<std::string>& lines) {
  std::vector<std::string> shown;
  for (const std::string& l : lines) {
    if (l.empty()) {
      break;
    }
    shown.push_back(l);
  }
  return shown;
}

std::string chart_location_line(const ExeImage& exe, const World& world, std::int32_t x, std::int32_t z,
                                std::uint32_t loc, std::uint32_t comma) {
  std::string line = str(exe, loc);
  if (world.bounds) {
    line += chart_format_latlon(world.latitude_at(z)) + str(exe, comma) + chart_format_latlon(world.longitude_at(x));
  }
  return line;
}

std::optional<ChartFix> vehicle_chart_fix(const Vehicle& v, bool limited) {
  ChartFix f;
  const ShipSightingRecord* record = nullptr;
  bool true_position = !limited;
  if (const auto* ship = dynamic_cast<const Ship*>(&v)) {
    // ship_chart_position (0x27862)
    record = &ship->ai().sighting;
  } else if (const auto* sub = dynamic_cast<const EnemySub*>(&v)) {
    // enemy_sub_chart_position (0x278de): the true position only while
    // the lookouts have it in range (+0xa3c).
    record = &sub->sighting();
    true_position = !limited && sub->player_range_yd() > 0;
  } else {
    return std::nullopt;
  }
  if (true_position) {
    f.x = v.position().x;
    f.z = v.position().z;
  } else {
    if (record->last_x < 0) {
      return std::nullopt;
    }
    f.x = record->last_x;
    f.z = record->last_z;
  }
  if (v.sighted() || true_position) {
    f.heading = v.heading().tenths();
  }
  f.age = limited ? record->age : 0;
  return f;
}

void refresh_group_contact(ContactGroup& g, const std::vector<GroupMember>& members, std::int32_t player_x,
                           std::int32_t player_z, const ChartStamp& now, bool limited) {
  if (g.finished || !g.materialized) {
    return;
  }
  const Vehicle* best = nullptr;
  std::int32_t heading = -1;
  std::int32_t nearest = 1000000;
  for (const GroupMember& m : members) {
    const std::optional<ChartFix> f = vehicle_chart_fix(*m.vehicle, limited);
    if (!f) {
      continue;
    }
    if (g.kind == ContactGroupKind::kRoute && m.leader) {
      // convoy_group_chart_draw: the convoy leader settles it.
      best = m.vehicle;
      heading = f->heading;
      break;
    }
    const auto yd = static_cast<std::int32_t>(fx::approx_distance(f->x - player_x, f->z - player_z) / 0x30);
    if (yd < nearest) {
      best = m.vehicle;
      heading = -1;
      nearest = yd;
    }
  }
  if (best == nullptr) {
    return;
  }
  const ChartFix f = *vehicle_chart_fix(*best, limited);
  HqReport& r = g.report;
  r.valid = true;
  r.hour = now.hour;
  r.minute = now.minute;
  r.day = now.day;
  r.month = now.month;
  r.timer = std::max(kContactTimer - f.age, 0);
  r.x = f.x;
  r.z = f.z;
  r.heading = heading;
  switch (g.kind) {
    case ContactGroupKind::kRoute:
      g.heading = heading;
      r.size = g.size;
      // The member's slot +0x20: its speed.
      g.speed = best->speed().raw();
      break;
    case ContactGroupKind::kPatrol:
      r.size = g.patrol_size;
      break;
    case ContactGroupKind::kAnchored:
      r.size = std::max(g.anchored_size - 1, 0);
      break;
    case ContactGroupKind::kSampans:
      r.size = g.sampan_size;
      break;
  }
}

std::optional<GroupSymbol> group_symbol(ContactGroup& g, const ExeImage& exe) {
  HqReport& r = g.report;
  if (g.finished || !r.valid) {
    return std::nullopt;
  }
  GroupSymbol s;
  s.x = r.x;
  s.z = r.z;
  std::uint8_t colour = kWarshipColour;
  switch (g.kind) {
    case ContactGroupKind::kRoute:
      // convoy_group_chart_draw (0x27a08)
      colour = kMerchantColour;
      s.frame = kSymbolUnknownSize;
      switch (g.route_kind) {
        case RouteGroupKind::kTaskForce:
          colour = kWarshipColour;
          if (g.warship_type != -1) {
            s.frame = exe.u32(kWarshipFrames + static_cast<std::uint32_t>(g.warship_type) * 4);
          }
          break;
        case RouteGroupKind::kPatrol:
          colour = kWarshipColour;
          [[fallthrough]];
        case RouteGroupKind::kConvoy:
          if (r.size != -1) {
            s.frame = static_cast<std::size_t>(r.size) + kSymbolSmall;
          }
          break;
        case RouteGroupKind::kSubmarines:
          colour = kWarshipColour;
          s.frame = kSymbolSmall;
          break;
      }
      if (r.heading != -1) {
        s.heading = fx::angle_add(Angle10(r.heading), Angle10(1800));
      }
      break;
    case ContactGroupKind::kPatrol:     // contacts_patrol_chart_draw (0x288c6)
    case ContactGroupKind::kAnchored:   // contacts_anchored_chart_draw (0x29019)
      s.frame = static_cast<std::size_t>(r.size) + kSymbolSmall;
      break;
    case ContactGroupKind::kSampans:    // contacts_sampans_chart_draw (0x29635)
      colour = kMerchantColour;
      s.frame = kSymbolSmall;
      break;
  }
  r.symbol_frame = static_cast<std::int32_t>(s.frame);
  const std::int32_t fade = std::min((kContactSeconds - fx::trunc(r.timer)) * 5 / kContactSeconds, 4);
  s.colour = static_cast<std::uint8_t>(colour + fade);
  return s;
}

std::vector<std::string> group_report_lines(const ContactGroup& g, const ExeImage& exe, const World& world) {
  std::vector<std::string> lines(6);
  const HqReport& r = g.report;
  std::size_t i = 2;
  switch (g.kind) {
    case ContactGroupKind::kRoute: {
      // chart_contact_report (0x28338)
      lines[0] = title(exe, g.materialized, 0xf24d2, 0xf24eb);
      std::string& kind = lines[1];
      switch (g.route_kind) {
        case RouteGroupKind::kConvoy:
          if (r.size != -1) {
            kind += table_string(exe, kSizeNames, r.size) + str(exe, 0xf2548);
          }
          kind += str(exe, 0xf2554);
          break;
        case RouteGroupKind::kTaskForce:
          kind = g.warship_type == -1 ? str(exe, 0xf2505)
                                      : table_string(exe, kWarshipNames, g.warship_type) + str(exe, 0xf2522);
          break;
        case RouteGroupKind::kPatrol:
          if (r.size != -1) {
            kind += table_string(exe, kSizeNames, r.size) + str(exe, 0xf257a);
          }
          kind += str(exe, 0xf2586);
          break;
        case RouteGroupKind::kSubmarines:
          kind += str(exe, 0xf2597);
          if (g.size > 1) {
            kind += str(exe, 0xf25ab);
          }
          break;
      }
      lines[i] = time_line(exe, r.hour, r.minute, r.month, r.day, 0xf25cb, 0xf25f0);
      lines[i + 1] = chart_location_line(exe, world, r.x, r.z, 0xf2606, 0xf2620);
      lines[i + 2] = str(exe, 0xf2637) + (r.heading == -1 ? str(exe, 0xf264b) : padded(r.heading / 10, 3));
      lines[i + 3] = str(exe, 0xf2667) +
                     (r.heading == -1 ? str(exe, 0xf2679) : padded(fx::trunc(g.speed), 2) + str(exe, 0xf2695));
      break;
    }
    case ContactGroupKind::kPatrol:
      // chart_patrol_contact_report (0x28d7b)
      lines[0] = title(exe, g.materialized, 0xf26ed, 0xf2706);
      if (r.size != -1) {
        lines[1] = table_string(exe, kSizeNames, r.size) + str(exe, 0xf2735);
      }
      lines[1] += str(exe, 0xf2741);
      lines[2] = time_line(exe, r.hour, r.minute, r.month, r.day, 0xf2766, 0xf278b);
      lines[3] = chart_location_line(exe, world, r.x, r.z, 0xf27a1, 0xf27bb);
      break;
    case ContactGroupKind::kAnchored:
      // chart_anchored_ships_report (0x29437)
      lines[0] = str(exe, 0xf2812);
      lines[1] = str(exe, 0xf282b);
      lines[2] = time_line(exe, r.hour, r.minute, r.month, r.day, 0xf2858, 0xf287d);
      lines[3] = chart_location_line(exe, world, r.x, r.z, 0xf2893, 0xf28ad);
      break;
    case ContactGroupKind::kSampans:
      // chart_sampans_sighted_report (0x29a62)
      lines[0] = str(exe, 0xf28ec);
      lines[1] = str(exe, 0xf2905);
      lines[2] = time_line(exe, r.hour, r.minute, r.month, r.day, 0xf292b, 0xf2950);
      lines[3] = chart_location_line(exe, world, r.x, r.z, 0xf2966, 0xf2980);
      break;
  }
  return lines;
}

std::vector<std::string> port_info_lines(const BaseRecord& base, BaseSide owner, const ExeImage& exe,
                                         const World& world) {
  // chart_port_info (0x22171)
  std::vector<std::string> lines(6);
  lines[0] = base.name;
  lines[1] = chart_location_line(exe, world, base.station.x, base.station.z, 0xf1bc4, 0xf1bde);
  lines[2] = table_string(exe, kSideNames, static_cast<std::int32_t>(owner));
  lines[3] = table_string(exe, kPortSizes, base.anchored_size) + str(exe, 0xf1c09);
  if (base.has(BaseFlag::kActive)) {
    lines[3] += str(exe, base.has(BaseFlag::kSmallGuard) ? 0xf1c15 : 0xf1c2a);
  }
  return lines;
}

std::vector<std::string> hull_contact_lines(const HullContact& c, const ExeImage& exe) {
  // chart_ship_contact_info (0x22f02) / chart_contact_info (0x23ce6): the
  // same text at their own string addresses.
  struct Strings {
    std::uint32_t last_sighted, hrs, range, yards, bearing, sinking, speed, dot, kts, solution, percent;
  };
  constexpr Strings kShip{0xf1cf8, 0xf1d25, 0xf1d34, 0xf1d50, 0xf1d62, 0xf1d80,
                          0xf1d9a, 0xf1db6, 0xf1dcc, 0xf1ddc, 0xf1dfb};
  constexpr Strings kSub{0xf1e73, 0xf1ea0, 0xf1eaf, 0xf1ecb, 0xf1edd, 0xf1efb,
                         0xf1f15, 0xf1f31, 0xf1f47, 0xf1f57, 0xf1f76};
  const Strings& s = c.submarine ? kSub : kShip;
  std::vector<std::string> lines(6);
  const SolutionEstimate& e = c.estimate;
  // Realism levels 2 and 3 never name the class.
  const bool unnamed = e.quality < 0x55 || (c.options & 0xc) > 7;
  const std::string class_name = c.cls != nullptr ? c.cls->name : std::string();
  if (c.submarine) {
    lines[0] = (unnamed ? str(exe, 0xf1e46) : class_name) + str(exe, 0xf1e5d) +
               std::string(exe.string_pointer(kSubmarineType));
  } else {
    const std::int32_t type = c.cls != nullptr ? static_cast<std::int8_t>(c.cls->ship_type) : 0;
    const auto type_name = [&](std::int32_t t) { return table_string(exe, kShipTypes + 4, t * 2); };
    if (e.quality < 0x32 && type < kFirstOtherType) {
      lines[0] = str(exe, 0xf1c6c);
    } else if (type < kFirstOtherType) {
      lines[0] = str(exe, 0xf1c92) + table_string(exe, kShipTypes, type * 2) + str(exe, 0xf1ca8);
      if (unnamed) {
        lines[0] += type < kFirstMerchantType ? type_name(type) : str(exe, 0xf1ce5);
      } else {
        lines[0] += type < kFirstMerchantType ? class_name + str(exe, 0xf1cbf) : type_name(type);
      }
    } else {
      lines[0] = type_name(type);
    }
  }
  std::size_t i = 1;
  if (!c.seen) {
    lines[i++] = str(exe, s.last_sighted) + padded(c.sighted_hour, 2) + padded(c.sighted_minute, 2) + str(exe, s.hrs);
  }
  lines[i++] = str(exe, s.range) + std::to_string(e.range / 0x30) + str(exe, s.yards);
  lines[i++] = str(exe, s.bearing) + padded(e.bearing.tenths() / 10, 3);
  if (c.sinking) {
    lines[i] = str(exe, s.sinking);
  } else {
    lines[i] = str(exe, s.speed) + padded(e.speed_tenths / 10, 2) + str(exe, s.dot) +
               padded(e.speed_tenths - e.speed_tenths / 10 * 10, 1) + str(exe, s.kts);
    lines[i + 1] = str(exe, s.solution) + std::to_string(e.quality) + str(exe, s.percent);
  }
  return lines;
}

std::vector<std::string> air_group_report_lines(const AirGroup& g, const ExeImage& exe, const World& world) {
  // chart_aircraft_sighted_report (0x26619)
  std::vector<std::string> lines(6);
  if (!g.sighted()) {
    return lines;
  }
  lines[0] = str(exe, 0xf22a1);
  const std::int32_t planes = g.plane_count();
  lines[1] = str(exe, planes >= 0x32 ? 0xf22bd : planes >= 0x1e ? 0xf22cd : 0xf22de) + str(exe, 0xf22ee);
  lines[2] = chart_location_line(exe, world, g.position().x, g.position().z, 0xf2306, 0xf2320);
  lines[3] = str(exe, 0xf2337) + padded(fx::angle_add(g.heading(), Angle10(0)).tenths() / 10, 3);
  return lines;
}

std::vector<std::string> intel_report_lines(const IntelReport& r, const std::string& base_name, const ExeImage& exe,
                                            const World& world) {
  // chart_air_strike_recon_report (0x2603d)
  std::vector<std::string> lines(6);
  const bool strike = r.kind() == IntelReport::Kind::kAirStrike;
  std::size_t i = 1;
  if (strike) {
    lines[0] = str(exe, 0xf21bf);
  } else {
    lines[0] = str(exe, 0xf2197);
    lines[1] = r.location_type() < 0 ? str(exe, 0xf21ae) : table_string(exe, kLocationTypes, r.location_type());
    i = 2;
  }
  lines[i] = base_name;
  lines[i + 1] = chart_location_line(exe, world, r.position().x, r.position().z, 0xf21df, 0xf21f9);
  const std::array<std::int32_t, 5> eta = r.eta();
  lines[i + 2] = str(exe, strike ? 0xf2219 : 0xf2210) + time_line(exe, eta[0], eta[1], eta[3], eta[2], 0xf2243, 0xf2268);
  return lines;
}

std::vector<std::string> pilot_report_lines(const DownedPilot& pilot, const ExeImage& exe, const World& world) {
  // chart_downed_pilot_report (0x25b88), zoomed out.
  std::vector<std::string> lines(6);
  lines[0] = str(exe, pilot.sighted() ? 0xf20c4 : 0xf20e4);
  lines[1] = time_line(exe, pilot.report_hour(), pilot.report_minute(), pilot.report_month(), pilot.report_day(),
                       0xf2120, 0xf2145);
  lines[2] = chart_location_line(exe, world, pilot.reported_x(), pilot.reported_z(), 0xf215b, 0xf2175);
  return lines;
}

std::vector<std::string> pilot_contact_lines(const SolutionEstimate& estimate, const ExeImage& exe) {
  // chart_downed_pilot_report (0x25b88), zoomed in.
  std::vector<std::string> lines(6);
  lines[0] = str(exe, 0xf2034);
  lines[1] = str(exe, 0xf204b) + std::to_string(estimate.range / 0x30) + str(exe, 0xf2067);
  lines[2] = str(exe, 0xf2079) + padded(estimate.bearing.tenths() / 10, 3);
  lines[3] = str(exe, 0xf2097) + std::to_string(estimate.quality) + str(exe, 0xf20b6);
  return lines;
}

}  // namespace sh

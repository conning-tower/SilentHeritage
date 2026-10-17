#include "game/scengen.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>

#include "core/error.h"
#include "core/fixed_math.h"
#include "game/class_params.h"
#include "game/torpedo_load.h"
#include "assets/exe_tables.h"
#include "sim/clock.h"
#include "sim/geometry.h"

namespace sh {

namespace {

// The war start (0x10e3a8..0x10e3b0): month 0-based, year since 1930.
constexpr std::int32_t kStartDay = 7;
constexpr std::int32_t kStartMonth = 11;
constexpr std::int32_t kStartYear = 11;

// 1 statute mile in world units (0x14a00), the generator's distance unit.
constexpr std::uint32_t kMile = 0x14a00;
constexpr std::int32_t kYard = kUnitsPerYard;

// Group sizes [lo, hi] by size class: lo + 1 + rand(hi - lo).
const ExeIntTable kConvoySizes{0x10ce50, 4};
const ExeIntTable kEscortSizes{0x10ce60, 5};
const ExeIntTable kPatrolSizes{0x10ce74, 4};
const ExeIntTable kAnchoredSizes{0x10ce84, 4};
// Escort and anchorage ring bearings, degrees.
const ExeIntTable kRingBearings{0x10cea4, 8};
// Patrol sectors, degrees.
const ExeIntTable kPatrolSectors{0x10cec4, 6};
// Anchorage guard bearings, degrees, with the two words that follow in
// memory (a padding 0 and the first task-force tier's 3), which a fourth
// and fifth guard read.
const ExeIntTable kGuardBearings{0x10cedc, 5};

// Task-force tiers (0x10ceec): ship types [lo, hi] and the next tier. The
// builder can step past tier 3 into the following memory (tiers 4, 5).
struct Tier {
  std::int32_t lo;
  std::int32_t hi;
  std::int32_t next;
};
constexpr std::size_t kTierCount = 6;
const ExeIntTable kTierTable{0x10ceec, 3 * kTierCount};

// Returns tier `t`; throws `std::out_of_range` past the table.
Tier tier_at(std::size_t t) { return {kTierTable.at(3 * t), kTierTable.at(3 * t + 1), kTierTable.at(3 * t + 2)}; }

// The equipment fit dates (0x10e408: {month 0-based, day, year since
// 1930}[6]) and the bit each one fits.
const ExeIntTable kEquipmentDates{0x10e408, 3 * 6};
struct EquipmentDate {
  Equipment bit;
  std::size_t date;  // index into kEquipmentDates
};

bool date_reached(std::int32_t month, std::int32_t year, std::int32_t fit_month, std::int32_t fit_year) {
  return year > fit_year || (year == fit_year && month >= fit_month);
}

// `angle_add` (0x5447c) on raw tenths.
std::int32_t angle_add(std::int32_t a, std::int32_t b) { return fx::angle_add(Angle10(a), Angle10(b)).tenths(); }

std::int32_t angle_sub(std::int32_t a, std::int32_t b) { return fx::angle_sub(Angle10(a), Angle10(b)).tenths(); }

fx::SinCos sincos(std::int32_t tenths) { return fx::angle_sincos(Angle10(tenths)); }

std::int32_t heading_from_delta(std::int32_t dx, std::int32_t dz) { return fx::heading_from_delta(dx, dz).tenths(); }

// Returns lo + 1 + rand(hi - lo): a size from a [lo, hi] table pair.
std::int32_t draw_size(WatcomRandom& random, std::int32_t lo, std::int32_t hi) {
  return lo + 1 + random.random_int(hi - (lo + 1) + 1);
}

// The C library's atoi.
std::int32_t atoi_c(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  bool negative = false;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
    negative = s[i] == '-';
    ++i;
  }
  std::int32_t v = 0;
  for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
    v = v * 10 + (s[i] - '0');
  }
  return negative ? -v : v;
}

// SHIPS.DAT +160 / +168: the surface / submerged speed strings.
std::int32_t ship_speed(const ShipClass& c, bool submerged) {
  return atoi_c(submerged ? c.submerged_speed_kts : c.top_speed_kts);
}

void set_waypoint(Waypoints& w, std::size_t i, std::int32_t x, std::int32_t y) {
  w.x.at(i) = x;
  w.y.at(i) = y;
}

// Rotates (x, y) by the 16.16 cosine and sine (`scengen_rotate_point`,
// 0x6bbb5).
void rotate_point(const fx::SinCos& r, std::int32_t& x, std::int32_t& y) {
  const std::int32_t x0 = x;
  const std::int32_t y0 = y;
  x = fx::mul(x0, r.cos) - fx::mul(y0, r.sin);
  y = fx::mul(y0, r.cos) + fx::mul(x0, r.sin);
}

}  // namespace

// ------------------------------------------------------------- calendar

std::int32_t campaign_day_number(std::int32_t day, std::int32_t month, std::int32_t year) {
  // campaign_day_number (0x173a4)
  if (year < 0) {
    return 100000;
  }
  std::int32_t d = kStartDay;
  std::int32_t m = kStartMonth;
  std::int32_t y = kStartYear;
  std::int32_t n = 0;
  while (d != day || m != month || y != year) {
    ++d;
    ++n;
    if (d > days_in_month(y, m)) {
      d = 1;
      if (++m == 12) {
        m = 0;
        ++y;
      }
    }
    if (y > year + 1 && y > kStartYear + 100) {
      throw DataError(std::format("campaign day of {}/{}/{}: not after the war start", day, month + 1,
                                  year + kScenarioYearBase));
    }
  }
  return n;
}

std::uint32_t equipment_on_date(std::int32_t month, std::int32_t year) {
  // scengen_build_single_mission (0x68453), the table 0x10e408.
  static constexpr std::array<EquipmentDate, 6> kDates = {{{Equipment::kSdAirRadar, 0},
                                                           {Equipment::kSjaRadar, 1},
                                                           {Equipment::kSj1Radar, 2},
                                                           {Equipment::kPpiDisplay, 3},
                                                           {Equipment::kApr1Detector, 4},
                                                           {Equipment::kBathythermograph, 5}}};
  std::uint32_t bits = 0;
  for (const EquipmentDate& e : kDates) {
    if (date_reached(month, year, kEquipmentDates.at(3 * e.date), kEquipmentDates.at(3 * e.date + 2))) {
      bits |= static_cast<std::uint32_t>(e.bit);
    }
  }
  // The SJ-1 replaces the SJa.
  if ((bits & static_cast<std::uint32_t>(Equipment::kSj1Radar)) != 0) {
    bits &= ~static_cast<std::uint32_t>(Equipment::kSjaRadar);
  }
  return bits;
}

// ------------------------------------------------------------ zone data

ZoneData ZoneData::load(const Install& install, const PatrolZone& zone) {
  ZoneData z;
  z.zone = zone;
  const std::string routes = "DATA/" + zone.routes;
  z.routes = parse_routes(install.read(routes), routes);
  const std::string bases = "DATA/" + zone.bases;
  z.bases = parse_bases(install.read(bases), bases);
  // bases_init_owner_table (0x167e7)
  z.owners.resize(z.bases.size());
  for (std::size_t i = 0; i < z.bases.size(); ++i) {
    const BaseRecord& b = z.bases[i];
    if (b.has(BaseFlag::kActive)) {
      z.owners[i].harbor_heading = fx::heading_from_delta(b.harbor.x - b.station.x, b.harbor.z - b.station.z);
    }
  }
  return z;
}

void ZoneData::update_owners(std::int32_t day, std::int32_t month, std::int32_t year) {
  // bases_update_owners (0x16852) / bases_owner_on_date (0x17315)
  const std::int32_t today = campaign_day_number(day, month, year);
  for (std::size_t i = 0; i < bases.size(); ++i) {
    const auto& o = bases[i].owners;
    const auto day_of = [](const BaseOwnerPeriod& p) {
      return campaign_day_number(p.day, p.month - 1, p.year - kScenarioYearBase);
    };
    BaseSide side = o[0].side;
    if (day_of(o[1]) <= today) {
      side = o[1].side;
      if (day_of(o[2]) <= today) {
        side = o[2].side;
      }
    }
    owners[i].side = side;
  }
}

BasePick pick_random_japanese_base(const ZoneData& zone, bool no_recon, WatcomRandom& random) {
  // bases_pick_random_japanese_base (0x1720b)
  const auto eligible = [&](std::size_t i) {
    const BaseRecord& b = zone.bases[i];
    return b.has(BaseFlag::kActive) && b.anchored_size != 0 && (!no_recon || !b.has(BaseFlag::kNoRecon)) &&
           zone.owners[i].side == BaseSide::kJapanese;
  };
  std::int32_t total = 0;
  for (std::size_t i = 0; i < zone.bases.size(); ++i) {
    if (eligible(i)) {
      total += zone.bases[i].pick_weight;
    }
  }
  const std::int32_t r = random.random_int(total);
  std::int32_t sum = 0;
  for (std::size_t i = 0; i < zone.bases.size(); ++i) {
    if (eligible(i)) {
      sum += zone.bases[i].pick_weight;
      if (r < sum) {
        return {static_cast<std::int32_t>(i), zone.bases[i].anchored_size - 1};
      }
    }
  }
  return {};
}

// ---------------------------------------------------------- the generator

ScenarioGenerator::ScenarioGenerator(const GameTables& tables, GeneratorParams& params, Scenario& scenario,
                                     WatcomRandom& random, std::uint32_t options, bool limit_classes)
    : tables_(tables),
      params_(params),
      scenario_(scenario),
      random_(random),
      options_(options),
      limit_classes_(limit_classes) {}

bool ScenarioGenerator::in_service(std::uint32_t start_year, std::uint32_t start_month, std::uint32_t end_year,
                                   std::uint32_t end_month) const {
  const auto y = static_cast<std::int64_t>(full_year());
  const std::int64_t m = params_.month + 1;
  return static_cast<std::int64_t>(start_year) <= y && (y != start_year || start_month <= m) && y <= end_year &&
         (y != end_year || m <= end_month);
}

const ShipClass& ScenarioGenerator::pick_merchant_class(bool allow_coastal_tanker) {
  // scengen_pick_merchant_class (0x6bc36)
  const std::size_t n = tables_.ships.size();
  if (!ship_used_) {
    ship_used_ = UsedClasses{std::vector<bool>(n, false), 0};
  }
  std::vector<bool> candidate(n, false);
  std::int32_t count = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const ShipClass& c = tables_.ships[i];
    if (c.role != ShipRole::kMerchant || (static_cast<int>(c.ship_type) == 18 && !allow_coastal_tanker)) {
      continue;
    }
    // Armed merchants from March 1942.
    if (c.main_gun_count > 0 && (full_year() < 1942 || (full_year() == 1942 && params_.month + 1 < 3))) {
      continue;
    }
    if (ship_used_->used[i] || ship_used_->distinct < 3) {
      candidate[i] = true;
      ++count;
    }
  }
  std::int32_t r = random_.random_int(count);
  std::size_t i = 0;
  while (i < n && (!candidate[i] || --r != -1)) {
    ++i;
  }
  if (i == n) {
    throw DataError("SHIPS.DAT: no merchant class to pick");
  }
  if (limit_classes_ && !ship_used_->used[i]) {
    ++ship_used_->distinct;
  }
  ship_used_->used[i] = true;
  return tables_.ships[i];
}

const ShipClass& ScenarioGenerator::pick_escort_class() {
  // scengen_pick_escort_class (0x6bdda)
  const std::size_t n = tables_.ships.size();
  if (!ship_used_) {
    ship_used_ = UsedClasses{std::vector<bool>(n, false), 0};
  }
  std::vector<bool> candidate(n, false);
  std::int32_t total = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const ShipClass& c = tables_.ships[i];
    if (c.role == ShipRole::kEscort && in_service(c.start_year, c.start_month, c.end_year, c.end_month) &&
        (ship_used_->used[i] || ship_used_->distinct < 3)) {
      candidate[i] = true;
      total += c.selection_weight;
    }
  }
  std::int32_t r = random_.random_int(total);
  std::size_t i = 0;
  while (i < n && (!candidate[i] || (r -= tables_.ships[i].selection_weight) > 0)) {
    ++i;
  }
  if (i == n) {
    throw DataError("SHIPS.DAT: no escort class in service");
  }
  if (limit_classes_ && !ship_used_->used[i]) {
    ++ship_used_->distinct;
  }
  ship_used_->used[i] = true;
  return tables_.ships[i];
}

const ShipClass* ScenarioGenerator::pick_ijn_warship(std::string& name, std::int32_t type_lo, std::int32_t type_hi,
                                                     std::int32_t& candidates) {
  // scengen_pick_ijn_warship (0x6bf85)
  const std::size_t n = tables_.ijn_ships.size();
  if (!ijn_used_) {
    ijn_used_ = std::vector<bool>(n, false);
  }
  // The warship classes of the type range, in file order.
  std::vector<const ShipClass*> classes;
  for (const ShipClass& c : tables_.ships) {
    const auto type = static_cast<std::int8_t>(c.ship_type);
    if (c.role == ShipRole::kWarship && type_lo <= type && type <= type_hi) {
      classes.push_back(&c);
    }
  }
  const auto class_of = [&](const IjnShip& s) -> const ShipClass* {
    for (const ShipClass* c : classes) {
      if (equals_ignore_case(c->index_name, s.class_name)) {
        return c;
      }
    }
    return nullptr;
  };
  std::vector<bool> candidate(n, false);
  std::int32_t count = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const IjnShip& s = tables_.ijn_ships[i];
    if (!(*ijn_used_)[i] && class_of(s) != nullptr &&
        in_service(s.commission_year + 1900, s.commission_month, s.end_year + 1900, s.end_month)) {
      candidate[i] = true;
      ++count;
    }
  }
  std::int32_t r = random_.random_int(count);
  candidates = count;
  for (std::size_t i = 0; i < n; ++i) {
    if (candidate[i] && --r == -1) {
      (*ijn_used_)[i] = true;
      name = tables_.ijn_ships[i].ship_name;
      return class_of(tables_.ijn_ships[i]);
    }
  }
  return nullptr;
}

const ShipClass& ScenarioGenerator::pick_sub_class() {
  // scengen_pick_sub_class (0x6c245)
  const std::size_t n = tables_.ships.size();
  std::vector<bool> candidate(n, false);
  std::int32_t total = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const ShipClass& c = tables_.ships[i];
    if (c.role == ShipRole::kSubmarine && in_service(c.start_year, c.start_month, c.end_year, c.end_month)) {
      candidate[i] = true;
      total += c.selection_weight;
    }
  }
  std::int32_t r = random_.random_int(total);
  std::size_t i = 0;
  while (i < n && (!candidate[i] || (r -= tables_.ships[i].selection_weight) > 0)) {
    ++i;
  }
  if (i == n) {
    throw DataError("SHIPS.DAT: no submarine class in service");
  }
  return tables_.ships[i];
}

const ShipClass& ScenarioGenerator::pick_small_craft_class() {
  // scengen_pick_small_craft_class (0x6c353): the first candidate for 0
  // and 1, then candidate r - 1.
  const std::size_t n = tables_.ships.size();
  std::int32_t count = 0;
  for (const ShipClass& c : tables_.ships) {
    count += c.role == ShipRole::kSmallCraft ? 1 : 0;
  }
  std::int32_t r = random_.random_int(count);
  std::size_t i = 0;
  while (i < n && (tables_.ships[i].role != ShipRole::kSmallCraft || --r > 0)) {
    ++i;
  }
  if (i == n) {
    throw DataError("SHIPS.DAT: no small craft class");
  }
  return tables_.ships[i];
}

void ScenarioGenerator::place_ships(std::size_t first, WorldPoint at, Angle10 heading, ShipOrders orders) {
  const fx::SinCos r = sincos(-heading.tenths());
  for (std::size_t i = first; i < scenario_.ships.size(); ++i) {
    ShipRecord& s = scenario_.ships[i];
    rotate_point(r, s.x, s.y);
    s.x += at.x;
    s.y += at.z;
    if (s.orders == orders) {
      for (std::size_t k = 0; k < s.waypoints.size(); ++k) {
        rotate_point(r, s.waypoints.x[k], s.waypoints.y[k]);
        s.waypoints.x[k] += at.x;
        s.waypoints.y[k] += at.z;
      }
    }
  }
}

void ScenarioGenerator::build_convoy(WorldPoint at, Angle10 heading, const WorldPath& path, bool alerted, bool fast,
                                     bool allow_coastal_tankers, std::int32_t group) {
  // scengen_build_convoy (0x69f08)
  const auto size = static_cast<std::size_t>(params_.convoy_size);
  const std::int32_t merchants = draw_size(random_, kConvoySizes.at(size), kConvoySizes.at(size + 1));
  std::int32_t escorts = 0;
  if (params_.escort != 0) {
    const auto level = static_cast<std::size_t>(params_.escort);
    escorts = draw_size(random_, kEscortSizes.at(level), kEscortSizes.at(level + 1));
  }
  scenario_.ships.assign(static_cast<std::size_t>(merchants + escorts), ShipRecord{});
  const std::int32_t columns = std::min(random_.random_int(merchants / 2) + 1, 4);
  std::int32_t column = 0;
  bool first_row = true;
  std::int32_t row_y = 0;
  std::int32_t x0 = 16000000, x1 = -16000000, y0 = 16000000, y1 = -16000000;
  std::int32_t slowest = 10000;
  ship_used_.reset();
  const std::int32_t min_speed = fast ? 15 : 0;
  for (std::int32_t i = 0; i < merchants; ++i) {
    ShipRecord& s = scenario_.ships[static_cast<std::size_t>(i)];
    s.convoy_group = group;
    if (first_row && i == columns / 2) {
      s.orders = ShipOrders::kConvoyLeader;
      s.zigzag = params_.enemy > 0 ? 1 : 0;
      for (std::size_t k = 0; k < path.size(); ++k) {
        set_waypoint(s.waypoints, k, path[k].x, path[k].z);
      }
      s.waypoints.count = static_cast<std::int32_t>(path.size());
    } else {
      s.orders = ShipOrders::kConvoyMember;
    }
    if (column >= columns) {
      column = 0;
      first_row = false;
      row_y -= random_.random_int(4800) + 24000;
    }
    s.x = random_.random_int(9600) + column * 48000;
    s.y = row_y;
    x0 = std::min(x0, s.x);
    x1 = std::max(x1, s.x);
    y0 = std::min(y0, s.y);
    y1 = std::max(y1, s.y);
    const std::int32_t crew = random_.random_int(params_.enemy);
    s.heading = heading.tenths() / 10;
    s.crew = static_cast<CrewQuality>(random_.random_int(crew));
    s.damage_pct = 0;
    ++column;
    const ShipClass* c = nullptr;
    std::int32_t speed = 0;
    do {
      c = &pick_merchant_class(allow_coastal_tankers);
      speed = atoi_c(c->top_speed_kts);
    } while (speed < min_speed);
    slowest = std::min(slowest, speed);
    s.ship_class = c->index_name;
    s.alert = 0;
  }
  ship_used_.reset();
  for (std::int32_t i = 0; i < merchants; ++i) {
    ShipRecord& s = scenario_.ships[static_cast<std::size_t>(i)];
    if (s.orders == ShipOrders::kConvoyLeader) {
      s.speed = slowest;
    }
    s.y += (y1 - y0) / 2;
    s.x -= (x1 - x0) / 2;
  }
  add_convoy_escorts(heading, static_cast<std::size_t>(merchants), x0, y0, x1, y1, slowest, alerted, group,
                     static_cast<std::size_t>(escorts));
  place_ships(0, at, heading, ShipOrders::kEscort);
}

void ScenarioGenerator::add_convoy_escorts(Angle10 heading, std::size_t first, std::int32_t x0, std::int32_t y0,
                                           std::int32_t x1, std::int32_t y1, std::int32_t speed, bool alerted,
                                           std::int32_t group, std::size_t count) {
  // scengen_add_convoy_escorts (0x6b845)
  std::int32_t radius = std::abs(x1 - x0) > std::abs(y1 - y0) ? x1 - x0 : y1 - y0;
  radius = std::max(std::abs(radius), 48000);
  const std::int32_t width = x1 - x0;
  std::int32_t ring_turn = 0;
  std::int32_t in_ring = 0;
  std::int32_t bearing_index = 0;
  ship_used_.reset();
  for (std::size_t e = first; e < first + count; ++e) {
    ShipRecord& s = scenario_.ships[e];
    if (in_ring > 3) {
      in_ring = 0;
      radius += radius / 2;
    }
    if (bearing_index > 7) {
      bearing_index = 0;
      ring_turn += 220;
    }
    s.convoy_group = group;
    s.orders = ShipOrders::kEscort;
    const std::int32_t jitter = random_.random_int(radius / 4);
    const std::int32_t b = kRingBearings.at(static_cast<std::size_t>(bearing_index)) + 5;
    const std::int32_t angle = angle_add((b - random_.random_int(10)) * 10, ring_turn);
    const fx::SinCos r = sincos(angle);
    s.x = fx::mul(radius + jitter, r.sin);
    s.y = fx::mul(radius + jitter, r.cos);
    s.heading = heading.tenths() / 10;
    s.damage_pct = 0;
    s.crew = static_cast<CrewQuality>(params_.enemy);
    const ShipClass* c = nullptr;
    std::int32_t top = 0;
    do {
      c = &pick_escort_class();
      top = atoi_c(c->top_speed_kts);
    } while (top < speed);
    s.speed = std::min(top, speed * 3 / 2);
    s.ship_class = c->index_name;
    s.alert = alerted ? 1 : 0;
    const std::int32_t depth = std::max(std::abs(y1 - y0), 48000);
    std::int32_t side = std::max(std::abs(width), 48000);
    set_waypoint(s.waypoints, 0, s.x, s.y);
    if (angle > 1800) {
      side = -side;
    }
    if (angle > 900 && angle < 2700) {
      s.waypoints.y[0] -= depth;
    }
    const std::int32_t wx = s.waypoints.x[0];
    const std::int32_t wy = s.waypoints.y[0];
    if (count == 1) {
      // A lone escort sweeps ahead and astern.
      const std::int32_t ahead = radius * 3 / 2;
      set_waypoint(s.waypoints, 1, wx, wy + ahead);
      set_waypoint(s.waypoints, 2, -wx, wy + ahead);
      set_waypoint(s.waypoints, 3, -wx, wy - radius * 2);
      set_waypoint(s.waypoints, 4, wx, wy - radius * 2);
      set_waypoint(s.waypoints, 5, -1, -1);
      s.waypoints.count = 5;
    } else {
      set_waypoint(s.waypoints, 1, wx, wy + depth);
      set_waypoint(s.waypoints, 2, wx + side, wy + depth);
      set_waypoint(s.waypoints, 3, wx + side, wy);
      set_waypoint(s.waypoints, 4, -1, -1);
      s.waypoints.count = 4;
    }
    ++in_ring;
    ++bearing_index;
  }
  ship_used_.reset();
}

void ScenarioGenerator::build_task_force(WorldPoint at, Angle10 heading, const WorldPath& path, bool alerted,
                                         std::int32_t group) {
  // scengen_build_task_force (0x6a31f)
  std::int32_t tier = params_.warship_type;
  ijn_used_.reset();
  std::string name;
  std::int32_t main_count = 0;
  const Tier main = tier_at(static_cast<std::size_t>(tier));
  pick_ijn_warship(name, main.lo, main.hi, main_count);
  std::int32_t lesser_count = 0;
  for (std::int32_t t = main.next; t < 4; ++t) {
    std::int32_t n = 0;
    const Tier lesser = tier_at(static_cast<std::size_t>(t));
    pick_ijn_warship(name, lesser.lo, lesser.hi, n);
    lesser_count += n;
  }
  ijn_used_->assign(ijn_used_->size(), false);
  const std::int32_t main_extra = random_.random_int(std::min(main_count, 2));
  const std::int32_t warships = main_extra + 1 + random_.random_int(std::min(lesser_count, 2) + 1);
  std::int32_t escorts = 0;
  if (params_.escort != 0) {
    const auto level = static_cast<std::size_t>(params_.escort);
    escorts = draw_size(random_, kEscortSizes.at(level), kEscortSizes.at(level + 1));
  }
  scenario_.ships.assign(static_cast<std::size_t>(warships + escorts), ShipRecord{});
  const std::int32_t columns = std::min(random_.random_int(warships / 2) + 1, 4);
  std::int32_t column = 0;
  bool first_row = true;
  std::int32_t row_y = 0;
  std::int32_t x0 = 16000000, x1 = -16000000, y0 = 16000000, y1 = -16000000;
  std::int32_t slowest = 10000;
  for (std::int32_t i = 0; i < warships; ++i) {
    ShipRecord& s = scenario_.ships[static_cast<std::size_t>(i)];
    s.convoy_group = group;
    if (first_row && i == columns / 2) {
      s.orders = ShipOrders::kConvoyLeader;
      s.zigzag = params_.enemy > 0 ? 1 : 0;
      for (std::size_t k = 0; k < path.size(); ++k) {
        set_waypoint(s.waypoints, k, path[k].x, path[k].z);
      }
      s.waypoints.count = static_cast<std::int32_t>(path.size());
    } else {
      s.orders = ShipOrders::kConvoyMember;
    }
    if (column >= columns) {
      column = 0;
      first_row = false;
      row_y -= random_.random_int(4800) + 24000;
    }
    s.x = random_.random_int(9600) + column * 48000;
    s.y = row_y;
    x0 = std::min(x0, s.x);
    x1 = std::max(x1, s.x);
    y0 = std::min(y0, s.y);
    y1 = std::max(y1, s.y);
    s.heading = heading.tenths() / 10;
    ++column;
    s.crew = static_cast<CrewQuality>(params_.enemy);
    s.damage_pct = 0;
    if (tier < 0 || static_cast<std::size_t>(tier) >= kTierCount) {
      throw DataError(std::format("task force: warship tier {} out of the table", tier));
    }
    const Tier t = tier_at(static_cast<std::size_t>(tier));
    std::int32_t left = 0;
    // The ship name goes to the record's name (the IJN ship).
    const ShipClass* c = pick_ijn_warship(s.name, t.lo, t.hi, left);
    if (c == nullptr) {
      throw DataError(std::format("task force: no IJN ship of types {}..{} in commission", t.lo, t.hi));
    }
    s.ship_class = c->index_name;
    if (i == main_extra) {
      tier = t.next;
    }
    if (left == 1) {
      ++tier;
    }
    slowest = std::min(slowest, atoi_c(c->top_speed_kts));
    s.alert = 0;
  }
  slowest = std::min(slowest, 27);
  for (std::int32_t i = 0; i < warships; ++i) {
    ShipRecord& s = scenario_.ships[static_cast<std::size_t>(i)];
    if (s.orders == ShipOrders::kConvoyLeader) {
      s.speed = slowest;
    }
    s.x -= (x1 - x0) / 2;
    s.y += (y1 - y0) / 2;
  }
  add_convoy_escorts(heading, static_cast<std::size_t>(warships), x0, y0, x1, y1, slowest, alerted, group,
                     static_cast<std::size_t>(escorts));
  place_ships(0, at, heading, ShipOrders::kEscort);
  ijn_used_.reset();
}

PatrolBox ScenarioGenerator::build_patrol(WorldPoint at, Angle10 heading, bool tight, bool append) {
  // scengen_build_patrol (0x6a81a)
  ijn_used_.reset();
  if (!append) {
    scenario_.ships.clear();
  }
  const auto size = static_cast<std::size_t>(params_.patrol_size);
  std::int32_t count = 0;
  if (size + 1 >= kPatrolSizes.size()) {
    // Deviation from the original (a hang): an alert-route patrol of size 3
    // (game/patrol_contacts.cpp, the original's fall-through) reads past
    // the table into the next one, for 7..12 ships, and the 7th never finds
    // a free sector of 6. Here its draw is made and the count capped at 6.
    random_.rand();
    count = 6;
  } else {
    count = draw_size(random_, kPatrolSizes.at(size), kPatrolSizes.at(size + 1));
  }
  if (tight && count == 1) {
    count = 2;
  }
  const std::size_t first = scenario_.ships.size();
  scenario_.ships.resize(first + static_cast<std::size_t>(count));
  const std::int32_t spacing = tight ? 24000 : (random_.random_int(1500) + 1500) * kYard;
  PatrolBox box;
  box.width = spacing * (random_.random_int(6) + 1);
  box.height = spacing * (random_.random_int(6) + 1);
  ship_used_.reset();
  std::array<bool, 6> taken{};
  for (std::size_t i = first; i < scenario_.ships.size(); ++i) {
    ShipRecord& s = scenario_.ships[i];
    s.orders = ShipOrders::kPatrol;
    s.convoy_group = 0;
    const std::int32_t distance = random_.random_int(spacing) + spacing;
    std::int32_t sector = 0;
    do {
      sector = random_.random_int(6);
    } while (taken[static_cast<std::size_t>(sector)]);
    taken[static_cast<std::size_t>(sector)] = true;
    const std::int32_t angle =
        (kPatrolSectors.at(static_cast<std::size_t>(sector)) + 10 - random_.random_int(20)) * 10;
    const fx::SinCos r = sincos(angle);
    const std::int32_t yards = distance / kYard;
    s.x = fx::mul(yards, r.sin) * kYard;
    s.y = fx::mul(yards, r.cos) * kYard;
    s.heading = heading.tenths() / 10;
    s.damage_pct = 0;
    s.crew = static_cast<CrewQuality>(params_.enemy);
    s.ship_class = pick_escort_class().index_name;
    s.speed = 12;
    s.alert = -1;
    s.waypoints.count = 3;
    const std::int32_t x0 = s.x;
    const std::int32_t y0 = s.y;
    const std::int32_t w = box.width;
    const std::int32_t h = box.height;
    set_waypoint(s.waypoints, 0, x0, y0);
    // A leg across the box, by sector.
    switch (sector) {
      case 0:
        set_waypoint(s.waypoints, 1, x0 + w, y0 + h);
        set_waypoint(s.waypoints, 2, x0 - w, y0 + h);
        break;
      case 1:
        set_waypoint(s.waypoints, 1, x0 + w, y0 + h);
        set_waypoint(s.waypoints, 2, x0 + w, y0);
        break;
      case 2:
        set_waypoint(s.waypoints, 1, x0 + w, y0);
        set_waypoint(s.waypoints, 2, x0 + w, y0 - h);
        break;
      case 3:
        set_waypoint(s.waypoints, 1, x0 - w, y0 - h);
        set_waypoint(s.waypoints, 2, x0 + w, y0 - h);
        break;
      case 4:
        set_waypoint(s.waypoints, 1, x0 - w, y0);
        set_waypoint(s.waypoints, 2, x0 - w, y0 - h);
        break;
      default:
        set_waypoint(s.waypoints, 1, x0 - w, y0 + h);
        set_waypoint(s.waypoints, 2, x0 - w, y0);
        break;
    }
    set_waypoint(s.waypoints, 3, -1, -1);
  }
  ship_used_.reset();
  place_ships(first, at, heading, ShipOrders::kPatrol);
  ijn_used_.reset();
  return box;
}

void ScenarioGenerator::build_anchored_ships(WorldPoint at, std::int32_t merchant_pct, Angle10 heading,
                                             bool no_ships, bool append) {
  // scengen_build_anchored_ships (0x6ad25)
  ijn_used_.reset();
  if (!append) {
    scenario_.ships.clear();
  }
  std::int32_t anchored = 0;
  bool small_craft = false;
  if (!no_ships) {
    // The size parameter itself is decremented; a negative size gives
    // small craft.
    params_.convoy_size -= 1;
    if (params_.convoy_size < 0) {
      params_.convoy_size = 0;
      small_craft = true;
    }
    const auto size = static_cast<std::size_t>(params_.convoy_size);
    anchored = draw_size(random_, kAnchoredSizes.at(size), kAnchoredSizes.at(size + 1));
  }
  const std::int32_t guards = params_.escort;
  const std::size_t first = scenario_.ships.size();
  scenario_.ships.resize(first + static_cast<std::size_t>(anchored + guards));
  std::int32_t ring = 14400;
  std::int32_t in_ring = 0;
  std::int32_t bearing_index = 0;
  std::int32_t ring_turn = 0;
  std::int32_t extent = 0;
  ship_used_.reset();
  for (std::size_t i = first; i < first + static_cast<std::size_t>(anchored); ++i) {
    ShipRecord& s = scenario_.ships[i];
    s.orders = ShipOrders::kNone;
    s.convoy_group = 0;
    s.x = 0;
    s.y = 0;
    // The first ship of an empty list lies at the centre.
    if (i > 0) {
      if (in_ring > 3) {
        in_ring = 0;
        ring += ring / 2;
      }
      if (bearing_index > 7) {
        bearing_index = 0;
        ring_turn += 220;
      }
      const std::int32_t distance = ring + random_.random_int(ring / 4);
      const std::int32_t b = kRingBearings.at(static_cast<std::size_t>(bearing_index)) + 5;
      const fx::SinCos r = sincos(angle_add((b - random_.random_int(10)) * 10, ring_turn));
      s.x = fx::mul(distance, r.sin);
      s.y = fx::mul(distance, r.cos);
      extent = std::max(extent, distance);
    }
    s.heading = random_.random_int(360);
    s.crew = CrewQuality::kGreen;
    s.damage_pct = 0;
    const ShipClass* c = nullptr;
    std::string ignored_name;
    while (c == nullptr) {
      if (small_craft) {
        c = &pick_small_craft_class();
        continue;
      }
      const std::int32_t roll = random_.random_int(100);
      if (roll < merchant_pct) {
        c = merchant_pct == 100 && roll < 5 ? &pick_small_craft_class() : &pick_merchant_class(true);
      } else if (random_.random_int(2) == 0) {
        c = &pick_escort_class();
      } else {
        std::int32_t n = 0;
        // The IJN ship's name is overwritten with "" below.
        c = pick_ijn_warship(ignored_name, 0, 6, n);
      }
    }
    s.speed = 0;
    s.ship_class = c->index_name;
    s.name.clear();
    s.alert = 0;
    s.waypoints.count = 0;
    ++in_ring;
    ++bearing_index;
  }
  ship_used_.reset();
  WorldPoint centre = at;
  if (extent != 0) {
    // The anchorage centre moves by the ring extent, x along the heading's
    // sine and z against its cosine.
    const fx::SinCos r = sincos(heading.tenths());
    centre.x += fx::mul(extent, r.sin);
    centre.z -= fx::mul(extent, r.cos);
  }
  extent = std::max(extent, 48000);
  const std::int32_t beat = extent * 3 / 2;
  for (std::int32_t g = 0; g < guards; ++g) {
    ShipRecord& s = scenario_.ships[first + static_cast<std::size_t>(anchored + g)];
    s.orders = ShipOrders::kPatrol;
    s.convoy_group = 0;
    const std::int32_t distance = beat + random_.random_int(extent / 4);
    const std::int32_t bearing = kGuardBearings.at(static_cast<std::size_t>(g)) * 10;
    const fx::SinCos r = sincos(bearing);
    s.x = fx::mul(distance, r.sin);
    s.y = fx::mul(distance, r.cos);
    if (bearing == 0) {
      s.x -= 0x79e0;
    }
    s.heading = heading.tenths() / 10;
    s.damage_pct = 0;
    s.crew = static_cast<CrewQuality>(params_.enemy);
    s.ship_class = pick_escort_class().index_name;
    s.speed = 12;
    s.alert = -1;
    const std::int32_t side = bearing >= 0 ? 62400 : -62400;
    const std::int32_t x0 = s.x;
    const std::int32_t y0 = s.y;
    set_waypoint(s.waypoints, 0, x0, y0);
    set_waypoint(s.waypoints, 1, x0, y0 + 33600);
    set_waypoint(s.waypoints, 2, x0 + side, y0 + 33600);
    set_waypoint(s.waypoints, 3, x0 + side, y0);
    set_waypoint(s.waypoints, 4, -1, -1);
    s.waypoints.count = 4;
  }
  ship_used_.reset();
  place_ships(first, centre, heading, ShipOrders::kPatrol);
  ijn_used_.reset();
}

void ScenarioGenerator::build_enemy_subs(WorldPoint at, Angle10 heading, const WorldPath& path, bool submerged,
                                         std::int32_t count, bool intercept, WorldPoint target) {
  // scengen_build_enemy_subs (0x6b5e3)
  scenario_.enemy_subs.assign(static_cast<std::size_t>(count), EnemySubRecord{});
  std::int32_t distance = 0x4b00;
  for (EnemySubRecord& s : scenario_.enemy_subs) {
    distance = distance + random_.random_int(distance / 2) + distance / 2;
    const fx::SinCos r = sincos(random_.random_int(3600));
    s.x = fx::mul(distance, r.sin);
    s.y = fx::mul(distance, r.cos);
    // The degrees, as the other unit records.
    s.heading = heading.tenths() / 10;
    s.damage_pct = 0;
    s.side = 1;
    s.crew = static_cast<CrewQuality>(params_.enemy);
    const ShipClass& c = pick_sub_class();
    s.speed = ship_speed(c, submerged);
    s.sub_class = c.index_name;
    s.depth = submerged ? 64 : 0;
    s.alert = 0;
    s.orders = intercept ? ShipOrders::kIntercept : ShipOrders::kFollowPath;
    s.waypoints.count = static_cast<std::int32_t>(path.size());
    s.target_x = target.x;
    s.target_y = target.z;
  }
  const fx::SinCos r = sincos(-heading.tenths());
  for (EnemySubRecord& s : scenario_.enemy_subs) {
    rotate_point(r, s.x, s.y);
    // The path is offset by the boat's place in the group.
    for (std::size_t k = 0; k < path.size(); ++k) {
      set_waypoint(s.waypoints, k, path[k].x + s.x, path[k].z + s.y);
    }
    s.x += at.x;
    s.y += at.z;
  }
}

void ScenarioGenerator::build_aircraft(WorldPoint at, std::int32_t count, std::int32_t escort_group) {
  // scengen_build_aircraft (0x6c41f)
  scenario_.aircraft.clear();
  if (count == 0) {
    return;
  }
  const std::size_t n = tables_.aircraft.size();
  std::vector<bool> candidate(n, false);
  std::int32_t candidates = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const AircraftType& a = tables_.aircraft[i];
    if (in_service(a.intro_year, a.start_month, a.end_year, a.end_month)) {
      candidate[i] = true;
      ++candidates;
    }
  }
  // Realism bits 2-3.
  const std::int32_t strafe_pct = static_cast<std::int32_t>(((options_ & 0xf) >> 2) + 1) * 25;
  scenario_.aircraft.assign(static_cast<std::size_t>(count), AircraftRecord{});
  for (AircraftRecord& a : scenario_.aircraft) {
    std::int32_t r = random_.random_int(candidates);
    std::size_t i = 0;
    while (i < n && (!candidate[i] || --r != -1)) {
      ++i;
    }
    if (i == n) {
      throw DataError("AIRCRAFT.DAT: no aircraft type in service");
    }
    const AircraftType& type = tables_.aircraft[i];
    a.mission = escort_group == 0 ? AircraftMission::kSearchOrbit2 : AircraftMission::kSearchOrbit;
    a.escort_group = escort_group;
    a.aircraft_type = type.index_name;
    a.crew = static_cast<CrewQuality>(params_.enemy);
    a.x = at.x;
    a.y = at.z;
    a.orbit_radius = random_.random_int(0x135600) + 0x7bc00;
    a.strafes = type.guns != 0 && random_.random_int(100) < strafe_pct ? 1 : 0;
    a.bomb_load = static_cast<std::int32_t>(type.bombs);
    // A strafing fighter drops its bombs 30 % of the time.
    if (a.strafes != 0 && type.role == AircraftRole::kFighter && random_.random_int(100) < 30) {
      a.bomb_load = 0;
    }
    a.relaunch = -1;
  }
}

// ------------------------------------------------------- torpedo load

void fill_torpedo_load(ScenarioHeader& header, const SubClass& sub, const GameTables& tables, WatcomRandom& random,
                       const MissionRules& rules, SubCommand command) {
  // scen_fill_torpedo_load (0x69a67)
  const auto find = [&](std::string_view name) -> const Torpedo& {
    const Torpedo* t = tables.find_torpedo(name);
    if (t == nullptr) {
      throw DataError(std::format("WEAPONS.DAT: no torpedo {}", name));
    }
    return *t;
  };
  header.tube_torpedoes.fill(-1);
  header.reloads.fill(TorpedoReload{});
  std::array<std::int32_t, 2> types{static_cast<std::int32_t>(find("MARK10").id), 0};
  std::int32_t mixed = 0;
  if (sub.modern_torpedoes) {
    types[0] = static_cast<std::int32_t>(find("MARK14").id);
    const Torpedo& mk18 = find("MARK18-1");
    // WEAPONS.DAT +48 is the service year, +44 its month.
    const auto mk18_year = static_cast<std::int32_t>(mk18.year) - kScenarioYearBase;
    const auto mk18_month = static_cast<std::int32_t>(mk18.month) - 1;
    if (header.year > mk18_year || (header.year == mk18_year && header.month >= mk18_month)) {
      const std::int32_t roll = random.random_int(100);
      const std::int32_t year = header.full_year();
      const std::int32_t pct = year == 1944 ? 40 : year == 1945 ? 60 : 20;
      if (roll < pct) {
        types[0] = static_cast<std::int32_t>(find("MARK18-1").id);
      } else if (roll < pct + 30) {
        // Meant to be the MARK18-2: both table entries read "MARK18-1".
        types[1] = static_cast<std::int32_t>(find("MARK18-1").id);
        if (rules.mixed_mark18) {
          // The extended variant's real 18-2, once in service.
          const Torpedo& mk18_2 = find("MARK18-2");
          const auto mk18_2_year = static_cast<std::int32_t>(mk18_2.year) - kScenarioYearBase;
          const auto mk18_2_month = static_cast<std::int32_t>(mk18_2.month) - 1;
          if (header.year > mk18_2_year || (header.year == mk18_2_year && header.month >= mk18_2_month)) {
            types[1] = static_cast<std::int32_t>(mk18_2.id);
          }
        }
        mixed = 1;
      }
    }
  }
  const auto tubes = static_cast<std::size_t>(sub.bow_tubes + sub.stern_tubes);
  if (tubes > header.tube_torpedoes.size()) {
    throw DataError(std::format("SUBS.DAT {}: {} tubes", sub.name, tubes));
  }
  for (std::size_t i = 0; i < tubes; ++i) {
    header.tube_torpedoes[i] = types[static_cast<std::size_t>(random.random_int(mixed + 1))];
  }
  const auto bow = static_cast<std::int32_t>(sub.bow_reloads);
  const auto stern = static_cast<std::int32_t>(sub.stern_reloads);
  std::size_t entries = 1;
  if (mixed == 0) {
    header.reloads[0] = {types[0], stern, bow};
  } else {
    std::int32_t fwd = std::clamp(bow / 2 + random.random_int(bow / 2) - bow / 4, 0, bow);
    std::int32_t aft = stern / 2 + random.random_int(stern / 2) - stern / 4;
    aft = std::clamp(aft, 0, stern);
    for (std::size_t i = 0; i < 2; ++i) {
      header.reloads[i] = {types[i], aft, fwd};
      fwd = bow - fwd;
      aft = stern - aft;
    }
    entries = 2;
  }
  const Torpedo* mk27 = tables.find_torpedo(kMark27Name);
  if (rules.mark27 && mk27 != nullptr && sub.modern_torpedoes && stern >= kMark27Rack &&
      mark27_in_service(header.year, header.month, command)) {
    // The extended variant's Mark 27 take two stern rack places from the
    // entry with the most.
    TorpedoReload& most = entries == 2 && header.reloads[1].aft > header.reloads[0].aft ? header.reloads[1]
                                                                                       : header.reloads[0];
    most.aft -= kMark27Rack;
    header.reloads[entries++] = {static_cast<std::int32_t>(mk27->id), kMark27Rack, 0};
  }
  header.reloads[entries].type = -1;
}

std::vector<const SubClass*> sub_classes_in_service(const GameTables& tables, std::int32_t month, std::int32_t year) {
  // encounter_sub_class_in_service (0xa1902): SUBS.DAT +0x48..+0x54.
  std::vector<const SubClass*> out;
  const std::int32_t y = year + kScenarioYearBase;
  const std::int32_t m = month + 1;
  for (const SubClass& s : tables.subs) {
    const auto start_month = static_cast<std::int32_t>(s.intro_month);
    const auto start_year = static_cast<std::int32_t>(s.intro_year);
    const auto end_month = static_cast<std::int32_t>(s.end_month);
    const auto end_year = static_cast<std::int32_t>(s.end_year);
    if (start_year <= y && (y != start_year || start_month <= m) && y <= end_year && (y != end_year || m <= end_month)) {
      out.push_back(&s);
    }
  }
  return out;
}

// ------------------------------------------------------ single missions

namespace {

// What a mission kind asks of the zone (`scengen_pick_patrol_zone` mode).
enum class ZoneNeed : std::uint8_t { kActiveRoute = 0, kAirStrikeBase = 1, kReconBase = 2 };

// The single-mission state: the zone and the scenario under construction.
class SingleMission {
 public:
  SingleMission(const Install& install, const GameTables& tables, GeneratorParams& params, std::uint32_t options,
                WatcomRandom& random)
      : install_(install), tables_(tables), params_(params), options_(options), random_(random) {}

  GeneratedMission& result() { return result_; }

  bool pick_patrol_zone(ZoneNeed need);
  bool convoy();
  bool task_force();
  bool patrol();
  bool enemy_subs();
  bool lifeguard_recon();

 private:
  struct Intercept {
    std::vector<WorldPoint> points;  // the jittered route
    WorldPoint at;
    std::size_t leg = 0;
  };
  // The route part from the intercept and the index of its start point.
  struct Course {
    WorldPath path;
    std::size_t start = 0;
    Angle10 heading;
  };

  bool route_is_active(const ConvoyRoute& route) const;
  std::size_t pick_route();
  std::vector<WorldPoint> route_points(std::size_t route);
  Intercept pick_route_intercept(std::size_t route);
  static Course course_of(const Intercept& i);
  // The player at the intercept, `start_bearing` from a fresh jitter of
  // the route, a random heading.
  void place_player_ahead(std::size_t route, const Intercept& i, const Course& c);
  ScenarioGenerator generator() {
    return ScenarioGenerator(tables_, params_, result_.scenario, random_, options_);
  }

  const Install& install_;
  const GameTables& tables_;
  GeneratorParams& params_;
  std::uint32_t options_;
  WatcomRandom& random_;
  GeneratedMission result_;
};

bool SingleMission::route_is_active(const ConvoyRoute& route) const {
  // scengen_route_is_active (0x69cf0)
  const std::int32_t y = params_.year;
  const std::int32_t first = route.first.year - kScenarioYearBase;
  const std::int32_t last = route.last.year - kScenarioYearBase;
  if (!(first <= y && y <= last && (first != y || route.first.month - 1 <= params_.month) &&
        (last != y || params_.month <= route.last.month - 1))) {
    return false;
  }
  std::int64_t length = 0;
  for (std::size_t i = 0; i + 1 < route.waypoints.size(); ++i) {
    const RouteBox& a = route.waypoints[i];
    const RouteBox& b = route.waypoints[i + 1];
    length += static_cast<std::int32_t>(fx::approx_distance(a.x0 - b.x0, a.z0 - b.z0));
  }
  return length > 0x338fff;
}

std::size_t SingleMission::pick_route() {
  // scengen_pick_route (0x69dd7)
  const auto& routes = result_.zone.routes;
  while (true) {
    const auto r = static_cast<std::size_t>(random_.random_int(static_cast<int>(routes.size())));
    if (route_is_active(routes[r])) {
      return r;
    }
  }
}

std::vector<WorldPoint> SingleMission::route_points(std::size_t route) {
  // scengen_route_points (0x69df1): each point at random in its box.
  std::vector<WorldPoint> points;
  for (const RouteBox& b : result_.zone.routes[route].waypoints) {
    const std::int32_t dx = (b.x0 - b.x1) / 0x7fff;
    const std::int32_t dz = (b.z0 - b.z1) / 0x7fff;
    const std::int32_t ox = dx == 0 ? 0 : (random_.rand() % dx) * fx::sign(dx);
    const std::int32_t oz = dz == 0 ? 0 : fx::sign(dz) * (random_.rand() % dz);
    points.push_back({b.x0 - ox * 0x7fff, b.z0 - oz * 0x7fff});
  }
  return points;
}

SingleMission::Intercept SingleMission::pick_route_intercept(std::size_t route) {
  // scengen_pick_route_intercept (0x6971b): a point on a random leg, at
  // least 5 miles from the route's ends.
  Intercept out;
  out.points = route_points(route);
  const auto& p = out.points;
  const auto n = static_cast<std::int32_t>(p.size());
  while (true) {
    const std::int32_t leg = random_.random_int(n - 1);
    const WorldPoint& a = p.at(static_cast<std::size_t>(leg));
    const WorldPoint& b = p.at(static_cast<std::size_t>(leg + 1));
    const std::int32_t dx = (a.x - b.x) / kYard;
    const std::int32_t dz = (a.z - b.z) / kYard;
    std::int32_t major = dx != 0 ? dx : dz;
    std::int32_t minor = dx != 0 ? dz : 0;
    if (major == 0) {
      continue;
    }
    int shift = 0;
    while (std::abs(major) > 0x7fff || std::abs(minor) > 0x7fff) {
      ++shift;
      major >>= 1;
      minor >>= 1;
    }
    std::int32_t along = random_.random_int(std::abs(major));
    if (major < 0) {
      along = -along;
    }
    std::int32_t across = minor * along / major;
    for (; shift > 0; --shift) {
      along *= 2;
      across *= 2;
    }
    const std::int32_t ox = dx != 0 ? along : 0;
    const std::int32_t oz = dx != 0 ? across : along;
    out.at = {b.x + ox * kYard, b.z + oz * kYard};
    out.leg = static_cast<std::size_t>(leg);
    bool too_close = false;
    if (leg == 0) {
      too_close = fx::approx_distance(out.at.x - p[0].x, out.at.z - p[0].z) / kMile < 5;
    } else if (n - 1 == leg + 1) {
      too_close = fx::approx_distance(out.at.x - b.x, out.at.z - b.z) / kMile < 5;
    }
    if (!too_close) {
      return out;
    }
  }
}

SingleMission::Course SingleMission::course_of(const Intercept& i) {
  // The route from the intercept on, run toward its nearer end.
  Course c;
  const std::size_t n = i.points.size();
  if (i.leg < n / 2) {
    c.start = i.leg;
    for (std::size_t k = i.leg; k < n; ++k) {
      c.path.push_back(i.points[k]);
    }
  } else {
    c.start = i.leg + 1;
    for (std::size_t k = i.leg + 2; k-- > 0;) {
      c.path.push_back(i.points[k]);
    }
  }
  c.heading = Angle10(heading_from_delta(i.at.x - c.path[0].x, i.at.z - c.path[0].z));
  return c;
}

void SingleMission::place_player_ahead(std::size_t route, const Intercept& i, const Course& c) {
  const std::vector<WorldPoint> again = route_points(route);
  const WorldPoint& from = again.at(c.start);
  std::int32_t bearing = heading_from_delta(i.at.x - from.x, i.at.z - from.z);
  bearing = angle_add(bearing, angle_add(random_.random_int(600), 3300));
  result_.start_bearing = bearing;
  PlayerSubRecord& p = *result_.scenario.player_sub;
  p.x = i.at.x;
  p.y = i.at.z;
  p.heading = random_.random_int(360);
  p.depth = 0;
  p.engine_order = 0;
}

bool SingleMission::pick_patrol_zone(ZoneNeed need) {
  // scengen_pick_patrol_zone (0x69440)
  const std::vector<PatrolZone> zones = parse_patrol_zones(install_.read("DATA/PZ.DAT"), "DATA/PZ.DAT");
  const std::int32_t year = params_.year + kScenarioYearBase;
  const std::int32_t month = params_.month + 1;
  const std::size_t weight_index = static_cast<std::size_t>(year - 1941);
  std::vector<bool> candidate(zones.size(), false);
  for (int tries = 1;; ++tries) {
    if (tries > 10000) {
      throw DataError("scenario generator: no patrol zone fits the mission");
    }
    std::int32_t total = 0;
    for (std::size_t i = 0; i < zones.size(); ++i) {
      const PatrolZone& z = zones[i];
      // 0x11bcc4: the zone's map is installed.
      candidate[i] = install_.exists("DATA/" + z.world) && z.first_year <= year &&
                     year <= z.last_year && (year != z.first_year || z.first_month <= month) &&
                     (year != z.last_year || month <= z.last_month);
      if (candidate[i]) {
        total += z.year_weight.at(weight_index);
      }
    }
    const std::int32_t r = random_.random_int(total);
    std::int32_t sum = 0;
    std::optional<std::size_t> picked;
    for (std::size_t i = 0; i < zones.size(); ++i) {
      if (candidate[i] && r < (sum += zones[i].year_weight[weight_index])) {
        picked = i;
        break;
      }
    }
    if (!picked) {
      return false;
    }
    result_.zone = ZoneData::load(install_, zones[*picked]);
    ZoneData& zone = result_.zone;
    bool ok = true;
    if (need == ZoneNeed::kActiveRoute) {
      ok = std::any_of(zone.routes.begin(), zone.routes.end(),
                       [&](const ConvoyRoute& route) { return route_is_active(route); });
    } else {
      // The original reads the owner table before any date filled it
      // (fresh heap memory); the sides on the mission date stand in.
      zone.update_owners(params_.day, params_.month, params_.year);
      const BasePick base = pick_random_japanese_base(zone, need == ZoneNeed::kReconBase, random_);
      ok = base.base != -1;
      if (ok && need == ZoneNeed::kAirStrikeBase) {
        const int index = campaign_month_index(year, month);
        ok = zone.zone.air_raid_pct.at(static_cast<std::size_t>(index)) != 0 || tries >= 30;
      }
    }
    if (ok) {
      return true;
    }
  }
}

bool SingleMission::convoy() {
  // scengen_mission_convoy (0x687c1)
  const std::size_t route = pick_route();
  const Intercept i = pick_route_intercept(route);
  const Course c = course_of(i);
  const std::int32_t alert_pct = params_.enemy * 45;
  const bool alerted = random_.random_int(100) <= alert_pct;
  const bool fast = random_.random_int(4) == 0;
  generator().build_convoy(i.at, c.heading, c.path, alerted, fast, true, 1);
  place_player_ahead(route, i, c);
  if (params_.air_cover == 1) {
    generator().build_aircraft(i.at, params_.enemy + 1, 1);
  }
  return true;
}

bool SingleMission::task_force() {
  // scengen_mission_task_force (0x689f9)
  const std::size_t route = pick_route();
  const Intercept i = pick_route_intercept(route);
  const Course c = course_of(i);
  const std::int32_t alert_pct = params_.enemy * 45;
  const bool alerted = random_.random_int(100) <= alert_pct;
  generator().build_task_force(i.at, c.heading, c.path, alerted, 1);
  place_player_ahead(route, i, c);
  if (params_.air_cover == 1) {
    generator().build_aircraft(i.at, params_.enemy + 1, 1);
  }
  return true;
}

bool SingleMission::patrol() {
  // scengen_mission_patrol (0x68bc8)
  const std::size_t route = pick_route();
  const Intercept i = pick_route_intercept(route);
  const Course c = course_of(i);
  const PatrolBox box = generator().build_patrol(i.at, c.heading, false, false);
  // The boat inside the patrol box, ahead along the route.
  const std::int32_t reach = random_.random_int(std::max(box.width, box.height) / 0xc0);
  const fx::SinCos r = sincos(c.heading.tenths());
  PlayerSubRecord& p = *result_.scenario.player_sub;
  p.x = i.at.x + fx::mul(reach, r.sin) * kYard;
  p.y = i.at.z + fx::mul(reach, r.cos) * kYard;
  result_.start_bearing = -1;
  p.heading = angle_sub(c.heading.tenths(), 1800) / 10;
  p.engine_order = 0;
  p.depth = static_cast<std::int32_t>(tables_.find_sub(p.sub_class)->periscope_depth_ft);
  if (params_.air_cover == 1) {
    generator().build_aircraft(i.at, params_.enemy + 1, 0);
  }
  return true;
}

bool SingleMission::enemy_subs() {
  // scengen_mission_enemy_subs (0x68de5)
  const std::size_t route = pick_route();
  const Intercept i = pick_route_intercept(route);
  const Course c = course_of(i);
  const std::int32_t intercept_pct = params_.enemy * 25;
  const bool intercept = random_.random_int(100) <= intercept_pct;
  std::int32_t count = random_.random_int(3) + 1;
  bool submerged = false;
  if (intercept) {
    submerged = random_.random_int(100) < 51;
    count = std::min(count, 2);
  }
  generator().build_enemy_subs(i.at, c.heading, c.path, submerged, count, intercept, {-1, -1});
  place_player_ahead(route, i, c);
  return true;
}

bool SingleMission::lifeguard_recon() {
  // scengen_mission_lifeguard_recon (0x69029)
  ZoneData& zone = result_.zone;
  Scenario& scenario = result_.scenario;
  PlayerSubRecord& p = *scenario.player_sub;
  if (params_.special_mission != 0 && params_.special_mission != 1) {
    return false;
  }
  const bool lifeguard = params_.special_mission == 0;
  scenario.header.special_mission = -1;
  zone.update_owners(params_.day, params_.month, params_.year);
  const BasePick pick = pick_random_japanese_base(zone, !lifeguard, random_);
  if (pick.base == -1) {
    return false;
  }
  const auto b = static_cast<std::size_t>(pick.base);
  const BaseRecord& base = zone.bases[b];
  const Angle10 harbor_heading = zone.owners[b].harbor_heading;
  const WorldPoint harbor{base.harbor.x, base.harbor.z};
  const std::int32_t saved_escort = params_.escort;
  if (lifeguard) {
    scenario.bomber_groups.push_back({pick.base, static_cast<BomberGroupSize>(pick.size), 0, 0});
    result_.start_bearing = harbor_heading.tenths();
  } else {
    const std::int32_t duration = random_.random_int(720) + 1440;
    scenario.photo_recons.push_back({pick.base, 0, duration});
    result_.start_bearing =
        angle_add(angle_sub(harbor_heading.tenths(), 900), random_.random_int(1800));
  }
  p.x = base.station.x;
  p.y = base.station.z;
  // In tenths of a degree, unlike the other missions (the start placement
  // turns the boat toward the station anyway).
  p.heading = angle_sub(result_.start_bearing, 1800);
  p.depth = 0;
  p.engine_order = 0;
  params_.escort = params_.patrol_size + 1;
  if (lifeguard) {
    // Guard patrols only at the target, a patrol at its patrol point, and
    // a guard at the first other base within 40 miles.
    generator().build_anchored_ships(harbor, 0, harbor_heading, true, false);
    if (params_.patrol_size > 0 && base.patrol_spawn_pct > 0) {
      params_.patrol_size -= 1;
      generator().build_patrol({base.patrol.x, base.patrol.z}, harbor_heading, base.patrol_param_b != 0, true);
      p.x = base.patrol.x;
      p.y = base.patrol.z;
      params_.patrol_size += 1;
    }
    if (params_.patrol_size > 1) {
      params_.escort = 1;
      std::int32_t left = 1;
      for (std::size_t o = 0; o < zone.bases.size(); ++o) {
        const BaseRecord& other = zone.bases[o];
        if (o != b && other.has(BaseFlag::kActive) &&
            fx::approx_distance(harbor.x - other.harbor.x, harbor.z - other.harbor.z) / kMile < 0x29 && left-- > 0) {
          generator().build_anchored_ships({other.harbor.x, other.harbor.z}, 0, zone.owners[o].harbor_heading, true,
                                           true);
        }
      }
    }
  } else {
    const std::int32_t saved_size = params_.convoy_size;
    params_.convoy_size = base.anchored_size;
    generator().build_anchored_ships(harbor, 0, harbor_heading, false, false);
    params_.convoy_size = saved_size;
  }
  params_.escort = saved_escort;
  if (params_.air_cover == 1) {
    generator().build_aircraft(harbor, params_.enemy + 1, 0);
  }
  return true;
}

// `scengen_pick_sub_name` (0x698c7): "(SS-<n>) <name>" of a random boat of
// the class in service, else of its first boat, else "".
std::string pick_sub_name(const GameTables& tables, const SubClass& sub, std::int32_t month, std::int32_t year,
                          WatcomRandom& random) {
  const std::int32_t y = year + kScenarioYearBase - 1900;
  const std::int32_t m = month + 1;
  const auto in_service = [&](const CarsubBoat& b) {
    const auto ay = static_cast<std::int32_t>(b.avail_year);
    const auto ey = static_cast<std::int32_t>(b.end_year);
    return ay <= y && y <= ey && (y != ay || static_cast<std::int32_t>(b.avail_month) <= m) &&
           (y != ey || m <= static_cast<std::int32_t>(b.end_month));
  };
  std::int32_t count = 0;
  for (const CarsubBoat& b : tables.carsub) {
    if (b.class_index == sub.class_index && in_service(b)) {
      ++count;
    }
  }
  const std::int32_t pick = random.random_int(count) + 1;
  const bool any = count == 0;
  std::int32_t seen = 0;
  for (const CarsubBoat& b : tables.carsub) {
    if (b.class_index == sub.class_index && (any || in_service(b)) && ++seen >= pick) {
      return std::format("(SS-{}) {}", b.hull_number, b.name);
    }
  }
  return "";
}

}  // namespace

GeneratedMission generate_single_mission(const Install& install, const GameTables& tables, MissionType type,
                                         const GeneratorParams& params_in, std::uint32_t options,
                                         WatcomRandom& random, const MissionRules& rules) {
  // scengen_build_single_mission (0x68453)
  if (type != MissionType::kConvoy && type != MissionType::kWarship && type != MissionType::kPatrol &&
      type != MissionType::kEnemySub && type != MissionType::kSpecial) {
    throw DataError("scenario generator: not an encounter mission");
  }
  GeneratorParams params = params_in;
  const SubClass* sub = tables.find_sub(params.sub_class);
  if (sub == nullptr) {
    throw DataError(std::format("SUBS.DAT: no class {}", params.sub_class));
  }
  SingleMission mission(install, tables, params, options, random);
  Scenario& s = mission.result().scenario;
  s.version = 7;
  ScenarioHeader& h = s.header;
  h.year = params.year;
  h.special_mission = 0;
  h.month = params.month;
  h.end_on_lost_contact = -1;
  h.day = params.day;
  h.minute = params.time;  // the time-of-day setting, with hour -1
  h.randomize = -1;
  h.required_memory = -1;
  h.weather = static_cast<Weather>(params.weather);
  h.hour = -1;
  h.sea_state = static_cast<SeaState>(params.sea);
  h.gradient = random.random_int(5);
  h.equipment = equipment_on_date(params.month, params.year);

  PlayerSubRecord player;
  player.sub_class = sub->index_name;
  player.name = pick_sub_name(tables, *sub, params.month, params.year, random);
  player.crew = static_cast<CrewQuality>(params.crew);
  s.player_sub = player;

  ZoneNeed need = ZoneNeed::kActiveRoute;
  if (type == MissionType::kSpecial) {
    need = params.special_mission == 0 ? ZoneNeed::kAirStrikeBase
           : params.special_mission == 1 ? ZoneNeed::kReconBase
                                          : ZoneNeed::kActiveRoute;
  }
  while (true) {
    if (!mission.pick_patrol_zone(need)) {
      throw DataError("scenario generator: no patrol zone on the mission date");
    }
    h.world = mission.result().zone.zone.world;
    h.fog = params.fog;
    bool built = false;
    switch (type) {
      case MissionType::kConvoy:
        built = mission.convoy();
        break;
      case MissionType::kWarship:
        built = mission.task_force();
        break;
      case MissionType::kPatrol:
        built = mission.patrol();
        // Not in the original: at the realistic periscope depth.
        if (built && rules.realistic_depths) {
          mission.result().scenario.player_sub->depth = sub_class_params(*sub, rules).periscope_depth_ft;
        }
        break;
      case MissionType::kEnemySub:
        built = mission.enemy_subs();
        break;
      default:
        built = mission.lifeguard_recon();
        break;
    }
    if (built) {
      fill_torpedo_load(h, *sub, tables, random, rules);
      return std::move(mission.result());
    }
  }
}

}  // namespace sh

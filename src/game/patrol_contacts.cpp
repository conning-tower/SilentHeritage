#include "game/patrol_contacts.h"

#include <algorithm>
#include <string_view>

#include "assets/campaign_month.h"
#include "assets/world.h"
#include "core/error.h"
#include "core/fixed_math.h"
#include "assets/exe_tables.h"
#include "sim/geometry.h"
#include "sim/sighting.h"

namespace sh {

namespace {

constexpr std::int32_t kUnitsPerYd = 0x30;
// Within this range (yd) a group caps the time compression at 0x200.
constexpr std::uint32_t kNearYd = 40000;
constexpr std::uint32_t kSampansNearYd = 20000;
constexpr std::int32_t kNearCompression = 0x200;
// A route group reaches its waypoint within 5000 yd.
constexpr std::uint32_t kWaypointYd = 5000;
// One day in seconds (the route alert decay).
constexpr std::int32_t kDaySeconds = 86400;
// The spawn interval numerator (seconds): 0x34bc0 / frequency.
constexpr std::int32_t kSpawnInterval = 0x34bc0;
// A waypoint box is walked in steps of 0x7fff units.
constexpr std::int32_t kBoxStep = 0x7fff;
// World units per hour at one knot (2025.5 yd/h).
constexpr std::int32_t kUnitsPerKnotHour = 0x17bd0;

// The sighting slots of 0x119228.
constexpr std::size_t kSlotEscort = 0;
constexpr std::size_t kSlotMerchant = 1;
constexpr std::size_t kSlotSub = 2;
constexpr std::size_t kSlotSmallCraft = 3;
constexpr std::size_t kSlotBattleship = 4;

// The SHIPS.DAT type ranges of the task-force mix 0x1037e8: BB, CV, CA, CL.
// {lo, hi}[4].
const ExeIntTable kWarshipTypes{0x1037e8, 2 * 4};
constexpr std::size_t kWarshipKinds = 4;

// The sampan counts by size.
const ExeIntTable kSampanSizes{0x10ce94, 4};

// The water test of a sampan spot 0x1037c8 / 0x1037d8: the cell and its
// east, south and south-east neighbours.
constexpr std::array<std::int32_t, 4> kSpotDx = {0, 0, 1, 1};
constexpr std::array<std::int32_t, 4> kSpotDy = {1, 0, 0, 1};
// Grid levels above this are too shallow for sampans.
constexpr std::uint8_t kSampanMaxLevel = 0x1f;

std::size_t kind_index(ContactGroupKind kind) { return static_cast<std::size_t>(kind); }

// Returns the campaign month of the clock's date (0..44).
std::size_t month_index(const GameClock& clock) {
  const int m = campaign_month_index(clock.year() + kCalendarBaseYear, clock.month() + 1);
  return static_cast<std::size_t>(std::clamp(m, 0, kCampaignMonths - 1));
}

// `random_weighted_index` (0x3124d): the first of `n` percentages whose
// running sum exceeds a roll of 100, else the last.
template <std::size_t N>
std::int32_t random_weighted_index(const std::array<std::uint8_t, N>& pct, WatcomRandom& random) {
  const std::int32_t roll = random.random_int(100);
  std::int32_t sum = 0;
  for (std::size_t i = 0; i < N; ++i) {
    sum += pct[i];
    if (roll < sum) {
      return static_cast<std::int32_t>(i);
    }
  }
  return static_cast<std::int32_t>(N) - 1;
}

// `angle_add` (0x5447c) on raw tenths.
std::int32_t angle_add(std::int32_t a, std::int32_t b) { return fx::angle_add(Angle10(a), Angle10(b)).tenths(); }

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

std::uint32_t distance_yd(const GroupPoint& p, std::int32_t x, std::int32_t z) {
  return fx::approx_distance(p.x - x, p.z - z) / kUnitsPerYd;
}

WorldPoint at_of(const ContactGroup& g) { return {g.position.x, g.position.z}; }

}  // namespace

ContactGroups::ContactGroups(const GameTables& tables, ZoneData& zone, const World& world, ContactSpawner& spawner,
                             bool limit_classes)
    : tables_(tables), zone_(zone), world_(world), spawner_(spawner), limit_classes_(limit_classes) {}

void ContactGroups::init(const GameClock& clock, WatcomRandom& random) {
  // contacts_init (0x2d9fd)
  move_acc_ = 0;
  route_acc_ = 0;
  last_day_ = -1;
  last_month_ = -1;
  routes_.assign(zone_.routes.size(), RouteState{});
  routes_new_day(clock, random);
  visual_sizes_.fill(0);
  for (const ShipClass& c : tables_.ships) {
    std::size_t slot = 0;
    switch (c.role) {
      case ShipRole::kMerchant:
        slot = kSlotMerchant;
        break;
      case ShipRole::kEscort:
        slot = kSlotEscort;
        break;
      case ShipRole::kWarship: {
        const auto type = static_cast<std::int32_t>(c.ship_type);
        // BB for the battleships and the types past CL; CV, CA, CL.
        slot = type <= 2 ? kSlotBattleship + 1 : type == 5 ? kSlotBattleship + 2 : type == 6 ? kSlotBattleship + 3
                                                                                         : kSlotBattleship;
        break;
      }
      case ShipRole::kSmallCraft:
        slot = kSlotSmallCraft;
        break;
      case ShipRole::kSubmarine:
        slot = kSlotSub;
        break;
    }
    visual_sizes_[slot] = std::max(visual_sizes_[slot], static_cast<std::int32_t>(c.visual_size));
  }
}

void ContactGroups::update_bases(const GameClock& clock, WatcomRandom& random, bool force) {
  // bases_update_daily (0x16745) -> bases_update_owners (0x16852)
  if (force) {
    bases_day_ = -1;
  }
  if (bases_day_ == clock.day()) {
    return;
  }
  zone_.update_owners(clock.day(), clock.month(), clock.year());
  for (std::size_t i = 0; i < zone_.bases.size(); ++i) {
    spawn_base_groups(i, random, clock);
  }
  bases_day_ = clock.day();
}

void ContactGroups::spawn_base_groups(std::size_t base, WatcomRandom& random, const GameClock& clock) {
  // bases_spawn_contact_groups (0x168ad)
  const BaseRecord& b = zone_.bases[base];
  if (!b.has(BaseFlag::kActive)) {
    return;
  }
  const auto index = static_cast<std::int32_t>(base);
  const std::int32_t kinds = kinds_at_base(index);
  if (zone_.owners[base].side == BaseSide::kAllied) {
    return;
  }
  const std::int32_t heading = zone_.owners[base].harbor_heading.tenths();
  if ((kinds & static_cast<std::int32_t>(ContactGroupKind::kAnchored)) == 0) {
    // contacts_anchored_init (0x30255)
    auto g = std::make_unique<ContactGroup>();
    g->kind = ContactGroupKind::kAnchored;
    g->base = index;
    g->position = {b.harbor.x, 0, b.harbor.z};
    g->heading = heading;
    g->seed = static_cast<std::uint32_t>(random.rand());
    g->anchored_size = b.anchored_size;
    if (limit_classes_ && g->anchored_size > 2) {
      g->anchored_size = 2;
    }
    g->guard_size = std::min(g->anchored_size, b.anchored_size_cap);
    g->merchant_pct = b.has(BaseFlag::kSmallGuard) ? 20 : 100;
    groups_.push_back(std::move(g));
  }
  if (b.has(BaseFlag::kSpawnsPatrols) && (kinds & static_cast<std::int32_t>(ContactGroupKind::kPatrol)) == 0 &&
      random.random_int(100) < b.patrol_spawn_pct) {
    // contacts_patrol_init (0x2fb54)
    auto g = std::make_unique<ContactGroup>();
    g->kind = ContactGroupKind::kPatrol;
    g->base = index;
    g->seed = static_cast<std::uint32_t>(random.rand());
    g->position = {b.patrol.x, 0, b.patrol.z};
    g->patrol_size = b.patrol_param_a;
    g->patrol_tight = b.patrol_param_b != 0;
    g->crew_quality = random_weighted_index(zone_.zone.crew_pct[month_index(clock)], random);
    g->heading = heading;
    groups_.push_back(std::move(g));
  }
}

std::int32_t ContactGroups::kinds_at_base(std::int32_t base) {
  // contacts_kinds_at_base (0x2e1fd) with slot +0x28
  // (`contacts_patrol_retire_at_base` / `contacts_anchored_retire_at_base`):
  // a group of the base still on the strategic map is retired.
  std::int32_t kinds = 0;
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    if ((g->kind != ContactGroupKind::kPatrol && g->kind != ContactGroupKind::kAnchored) || g->base != base) {
      continue;
    }
    if (g->materialized) {
      kinds |= static_cast<std::int32_t>(g->kind);
    } else {
      g->finished = true;
    }
  }
  return kinds;
}

bool ContactGroups::route_active(std::size_t route, const GameClock& clock) const {
  // convoy_route_active (0x30f53)
  const ConvoyRoute& r = zone_.routes[route];
  const std::int32_t year = clock.year();
  const std::int32_t month = clock.month();
  const std::int32_t day = clock.day();
  const std::int32_t first_year = r.first.year - kCalendarBaseYear;
  const std::int32_t last_year = r.last.year - kCalendarBaseYear;
  if (year < first_year || year > last_year) {
    return false;
  }
  if (year == first_year && month < r.first.month - 1) {
    return false;
  }
  if (year == last_year && month > r.last.month - 1) {
    return false;
  }
  if (year == first_year && month == r.first.month - 1 && day < r.first.day) {
    return false;
  }
  return !(year == last_year && month == r.last.month - 1 && day > r.last.day);
}

void ContactGroups::routes_new_day(const GameClock& clock, WatcomRandom& random) {
  // convoy_routes_new_day (0x2dd2c)
  if (last_day_ == clock.day()) {
    return;
  }
  const std::size_t m = month_index(clock);
  for (std::size_t i = 0; i < routes_.size(); ++i) {
    RouteState& s = routes_[i];
    if (!route_active(i, clock)) {
      s.active = false;
      s.alert = 0;
      continue;
    }
    const ConvoyRoute& r = zone_.routes[i];
    s.frequency = r.frequency[m];
    s.convoy_pct = r.convoy_pct[m];
    s.flags = r.flags;
    if (!s.active) {
      s.spawn_timer = random.random_int(0x2a30) * 2;
    }
    s.active = true;
  }
  // Slot +0x2c: the sampans not materialized go.
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    if (g->kind == ContactGroupKind::kSampans && !g->materialized) {
      g->finished = true;
    }
  }
  const std::int32_t quarter = zone_.zone.sampan_density / 4;
  const std::int32_t count = quarter + random.random_int(quarter >> 1);
  for (std::int32_t i = 0; i < count; ++i) {
    auto g = std::make_unique<ContactGroup>();
    const std::int32_t size = random.random_int(3);
    if (sampans_init(*g, size, random)) {
      groups_.push_back(std::move(g));
    }
  }
  last_day_ = clock.day();
}

bool ContactGroups::sampans_init(ContactGroup& g, std::int32_t size, WatcomRandom& random) {
  // contacts_sampans_init (0x30962): a random grid cell whose block of four
  // is deep enough. Cells past the grid's end count as land (the original
  // reads past its array there).
  const IndexedRaster& grid = world_.grid;
  std::int32_t column = 0;
  std::int32_t row = 0;
  bool water = false;
  while (!water) {
    column = random.random_int(grid.width);
    row = random.random_int(grid.height);
    water = true;
    for (std::size_t k = 0; k < kSpotDx.size() && water; ++k) {
      const std::int64_t index = static_cast<std::int64_t>(column) + kSpotDx[k] +
                                 static_cast<std::int64_t>(grid.width) * (row + kSpotDy[k]);
      water = index < static_cast<std::int64_t>(grid.pixels.size()) &&
              grid.pixels[static_cast<std::size_t>(index)] <= kSampanMaxLevel;
    }
  }
  g.kind = ContactGroupKind::kSampans;
  const std::int32_t cell = world_.cell_size();
  g.position = {column * cell, 0, cell * (grid.height - row)};
  g.seed = static_cast<std::uint32_t>(random.rand());
  g.report.valid = false;
  g.sampan_size = size;
  return true;
}

void ContactGroups::update(const GameClock& clock, const ContactView& view, WatcomRandom& random) {
  // contacts_update (0x2db52)
  // The vehicles a spawner deleted without `forget` leave their groups.
  for (auto it = owner_.begin(); it != owner_.end();) {
    if (spawner_.contains(it->first)) {
      ++it;
      continue;
    }
    std::erase(it->second->vehicles, it->first);
    it = owner_.erase(it);
  }

  if (last_month_ != clock.month()) {
    update_warship_mix(clock);
    last_month_ = clock.month();
  }
  routes_update(clock, view, random);
  move_groups(clock, view, random);
  materialized_.fill(0);
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    if (g->materialized) {
      ++materialized_[kind_index(g->kind)];
    }
  }
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    if (g->near) {
      spawner_.limit_compression(kNearCompression);
    }
    if (!g->materialized) {
      g->materialized = try_materialize(*g, clock, view, random);
      if (g->materialized) {
        ++materialized_[kind_index(g->kind)];
      }
    } else if (!g->seen) {
      dematerialize(*g);
    }
    g->blocked = false;
  }
  force_ = false;
  // slist_remove_at by index while counting up: the group after a removed
  // one is not looked at until the next update.
  for (std::size_t i = 0; i < groups_.size(); ++i) {
    if (groups_[i]->finished) {
      groups_.erase(groups_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  // contacts_clear_detected (0x2e1b0), run by the next frame's
  // player_report_contacts before it marks the groups again.
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    g->seen = false;
  }
}

void ContactGroups::routes_update(const GameClock& clock, const ContactView& view, WatcomRandom& random) {
  // convoy_routes_update (0x2db98)
  route_acc_ += view.frame_hundredths;
  const std::int32_t seconds = route_acc_ / 100;
  if (seconds == 0) {
    return;
  }
  while (route_acc_ > 99) {
    route_acc_ -= 100;
  }
  routes_new_day(clock, random);
  for (std::size_t i = 0; i < routes_.size(); ++i) {
    RouteState& s = routes_[i];
    if (!s.active) {
      continue;
    }
    if (s.alert != 0) {
      s.alert_timer -= seconds;
      if (s.alert_timer < 1) {
        s.alert_timer = 0;
        --s.alert;
        if (s.alert != 0) {
          s.alert_timer = kDaySeconds;
        }
      }
    }
    s.spawn_timer -= seconds;
    if (s.spawn_timer < 1 && s.frequency != 0) {
      auto g = std::make_unique<ContactGroup>();
      const bool backward = random.random_int(2) != 0;
      if (build_on_route(*g, i, backward, clock, random)) {
        groups_.push_back(std::move(g));
      }
      const std::int32_t frequency = s.alert < 3 ? s.frequency : 5;
      const std::int32_t interval = kSpawnInterval / frequency;
      s.spawn_timer = interval + (interval / 4 - random.random_int(interval / 2));
    }
  }
}

bool ContactGroups::build_on_route(ContactGroup& g, std::size_t route, bool backward, const GameClock& clock,
                                   WatcomRandom& random) {
  // convoy_build_on_route (0x2e4c9)
  if (route >= zone_.routes.size()) {
    return false;
  }
  g.kind = ContactGroupKind::kRoute;
  g.finished = false;
  g.materialized = false;
  const std::vector<RouteBox>& boxes = zone_.routes[route].waypoints;
  const auto n = static_cast<std::int32_t>(boxes.size());
  if (n == 0) {
    return false;
  }
  g.seed = static_cast<std::uint32_t>(random.rand());
  g.route = static_cast<std::int32_t>(route);
  std::int32_t k = backward ? n - 1 : 0;
  g.waypoints.clear();
  for (std::int32_t i = 0; i < n; ++i) {
    const RouteBox& box = boxes[static_cast<std::size_t>(k)];
    const std::int32_t steps_x = (box.x0 - box.x1) / kBoxStep;
    const std::int32_t steps_z = (box.z0 - box.z1) / kBoxStep;
    const std::int32_t dx = steps_x == 0 ? 0 : (random.rand() % steps_x) * fx::sign(steps_x);
    const std::int32_t dz = steps_z == 0 ? 0 : (random.rand() % steps_z) * fx::sign(steps_z);
    g.waypoints.push_back({box.x0 - dx * kBoxStep, 0, box.z0 - dz * kBoxStep});
    k += backward ? -1 : 1;
  }
  g.waypoint_index = 1;
  g.position = g.waypoints.front();
  // The original reads waypoint 1 of its fixed array: a stale point for a
  // one-point route (here the start point itself).
  g.target = g.waypoints[std::min<std::size_t>(1, g.waypoints.size() - 1)];

  const std::size_t m = month_index(clock);
  const RouteState& state = routes_[route];
  if (state.alert < 3) {
    g.route_kind = state.convoy_pct < random.random_int(100) ? RouteGroupKind::kTaskForce : RouteGroupKind::kConvoy;
    if (random.random_int(100) < 2) {
      g.route_kind = RouteGroupKind::kSubmarines;
    }
  } else {
    g.route_kind = RouteGroupKind::kPatrol;
  }
  std::int32_t air_pct = zone_.zone.air_cover_pct[m];
  g.fast_or_intercept = random.random_int(4) == 0;
  g.escort = random_weighted_index(zone_.zone.escort_pct[m], random);
  g.crew = random_weighted_index(zone_.zone.crew_pct[m], random);
  g.coastal_or_submerged = (state.flags & ConvoyRoute::kCoastal) != 0;
  switch (g.route_kind) {
    case RouteGroupKind::kConvoy: {
      g.size = random_weighted_index(zone_.zone.convoy_size_pct[m], random);
      const std::int32_t base = g.fast_or_intercept ? 15 : 10;
      g.speed = (base + 1 - random.random_int(3)) << 16;
      break;
    }
    case RouteGroupKind::kTaskForce:
      g.warship_type = random_weighted_index(warship_mix_, random);
      air_pct = std::min(air_pct + (g.warship_type == 1 ? 100 : 20), 100);
      g.speed = (random.random_int(10) + 18) << 16;
      break;
    case RouteGroupKind::kPatrol:
      g.size = random_weighted_index(zone_.zone.convoy_size_pct[m], random);
      g.speed = 12 << 16;
      // The original falls through into the submarines' case, which draws
      // the size and speed again.
      [[fallthrough]];
    case RouteGroupKind::kSubmarines:
      g.coastal_or_submerged = false;
      g.fast_or_intercept = random.random_int(100) <= g.crew * 25;
      g.size = random.random_int(3) + 1;
      if (g.fast_or_intercept) {
        g.coastal_or_submerged = random.random_int(100) < 26;
      }
      if (g.fast_or_intercept && g.size > 2) {
        g.size = 2;
      }
      g.speed = (random.random_int(5) + 10) << 16;
      break;
  }
  g.aircraft = 0;
  if (g.route_kind != RouteGroupKind::kSubmarines && random.random_int(100) < air_pct) {
    g.aircraft = (air_pct + 24) / 25;
  }
  if (g.route_kind == RouteGroupKind::kTaskForce && g.escort < 3 && random.random_int(100) < 90) {
    ++g.escort;
  }
  if ((g.route_kind == RouteGroupKind::kTaskForce || g.route_kind == RouteGroupKind::kSubmarines) && g.crew < 2 &&
      random.random_int(100) < 90) {
    ++g.crew;
  }
  // Heading from the start point to waypoint 1.
  g.heading = fx::heading_from_delta(g.target.x - g.position.x, g.target.z - g.position.z).tenths();
  g.engaged = false;
  g.alert_counted = false;
  g.report.valid = false;
  return true;
}

void ContactGroups::update_warship_mix(const GameClock& clock) {
  // convoy_update_warship_mix (0x3103a): the BB / CV / CA / CL shares of the
  // IJNSHIPS.DAT warships in commission this month.
  struct Node {
    const ShipClass* cls;
    std::size_t type;
  };
  std::vector<Node> warships;
  for (const ShipClass& c : tables_.ships) {
    if (c.role != ShipRole::kWarship) {
      continue;
    }
    const auto type = static_cast<std::int32_t>(c.ship_type);
    std::size_t k = 0;
    while (k < kWarshipKinds && (type < kWarshipTypes.at(2 * k) || type > kWarshipTypes.at(2 * k + 1))) {
      ++k;
    }
    warships.push_back({&c, std::min(k, kWarshipKinds - 1)});
  }
  std::array<std::int32_t, 4> counts{};
  std::int32_t total = 0;
  const std::int32_t year = clock.year() + kCalendarBaseYear;
  const std::int32_t month = clock.month() + 1;
  for (const IjnShip& s : tables_.ijn_ships) {
    const auto node = std::ranges::find_if(warships, [&](const Node& n) {
      return equals_ignore_case(n.cls->name, s.class_name);
    });
    if (node == warships.end()) {
      continue;
    }
    const auto from = static_cast<std::int32_t>(s.commission_year) + 1900;
    const auto to = static_cast<std::int32_t>(s.end_year) + 1900;
    if (from <= year && (year != from || static_cast<std::int32_t>(s.commission_month) <= month) && year <= to &&
        (year != to || month <= static_cast<std::int32_t>(s.end_month))) {
      ++total;
      ++counts[node->type];
    }
  }
  if (total == 0) {
    // The original divides by zero.
    throw DataError("IJNSHIPS.DAT: no warship in commission");
  }
  std::uint8_t sum = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    warship_mix_[i] = static_cast<std::uint8_t>(counts[i] * 100 / total);
    sum = static_cast<std::uint8_t>(sum + warship_mix_[i]);
  }
  warship_mix_[3] = static_cast<std::uint8_t>(100 - sum);
}

void ContactGroups::move_groups(const GameClock& clock, const ContactView& view, WatcomRandom& random) {
  // contacts_move_groups (0x2ded7): every group's slot +0x10 with the time
  // gathered (16.16 s) once over 5 hundredths.
  move_acc_ += view.frame_hundredths;
  if (move_acc_ <= 5) {
    return;
  }
  const std::int32_t dt =
      fx::div(static_cast<std::int32_t>(static_cast<std::uint32_t>(move_acc_) << 16), 100 << 16);
  for (const std::unique_ptr<ContactGroup>& g : groups_) {
    update_group(*g, dt, clock, view, random);
  }
  move_acc_ = 0;
}

void ContactGroups::update_group(ContactGroup& g, std::int32_t dt, const GameClock& clock, const ContactView& view,
                                 WatcomRandom& random) {
  switch (g.kind) {
    case ContactGroupKind::kRoute: {
      // convoy_group_update (0x2e95b)
      if (g.engaged && !g.alert_counted) {
        g.alert_counted = true;
        RouteState& s = routes_[static_cast<std::size_t>(g.route)];
        ++s.alert;
        s.alert_timer = kDaySeconds;
      }
      if (g.materialized) {
        return;
      }
      g.report.timer -= dt;
      if (g.report.timer < 0) {
        g.report.valid = false;
      }
      if (view.radio_available &&
          random.random_int(static_cast<std::int32_t>((view.options & 0xf) >> 2) * 1000 + 1000) == 0) {
        HqReport& r = g.report;
        r.valid = true;
        r.hour = clock.hour();
        r.minute = clock.minute();
        r.day = clock.day();
        r.month = clock.month();
        r.timer = 0x707fffff;
        r.accuracy = random.random_int(100);
        if ((view.options & 0xc) < 8) {
          r.accuracy = 100;
        }
        r.x = g.position.x;
        r.z = g.position.z;
        r.heading = r.accuracy < 25 ? -1 : g.heading;
        r.size = r.accuracy < 50 ? -1 : g.size;
      }
      move_route_group(g, dt);
      if (distance_yd(g.position, g.target.x, g.target.z) < kWaypointYd) {
        ++g.waypoint_index;
        if (g.waypoint_index < static_cast<std::int32_t>(g.waypoints.size())) {
          g.target = g.waypoints[static_cast<std::size_t>(g.waypoint_index)];
          g.heading = fx::heading_from_delta(g.target.x - g.position.x, g.target.z - g.position.z).tenths();
        } else {
          g.speed = 0;
          g.finished = true;
        }
      }
      return;
    }
    case ContactGroupKind::kPatrol:
      // contacts_patrol_update (0x2fc37)
      if (g.engaged && !g.engaged_counted) {
        g.engaged_counted = true;
      }
      [[fallthrough]];
    case ContactGroupKind::kAnchored:  // contacts_anchored_update (0x3034d)
    case ContactGroupKind::kSampans:   // contacts_sampans_update (0x30a33)
      if (!g.materialized) {
        g.report.timer -= dt;
        if (g.report.timer < 0) {
          g.report.valid = false;
        }
      }
      return;
  }
}

void ContactGroups::move_route_group(ContactGroup& g, std::int32_t dt) {
  // The move of convoy_group_update: steps of at most 0x7fff units along
  // the heading (`point_rotate_scale` of (0, -d)), halved and doubled back
  // to stay in 16 bits.
  const std::int32_t knots = fx::trunc(g.speed);
  const std::int32_t velocity = fx::div(fx::mul(g.speed, kUnitsPerKnotHour), 3600);
  const std::int32_t steps = (knots * kUnitsPerKnotHour / 3600 * fx::trunc(dt)) / kBoxStep + 1;
  const std::int32_t step = fx::div(dt, steps << 16);
  std::int32_t left = dt;
  while (left != 0) {
    const std::int32_t t = step < left ? step : left;
    left -= t;
    std::int32_t d = fx::trunc(fx::mul(velocity, t));
    int shift = 0;
    while (fx::abs(d) > 0x7fff) {
      d >>= 1;
      ++shift;
    }
    fx::Point2 p = fx::point_rotate_scale({0, -d}, {0, 0}, Angle10(g.heading));
    for (; shift > 0; --shift) {
      p.x = static_cast<std::int32_t>(static_cast<std::uint32_t>(p.x) << 1);
      p.y = static_cast<std::int32_t>(static_cast<std::uint32_t>(p.y) << 1);
    }
    g.position.x += p.x;
    g.position.z -= p.y;
  }
}

std::int32_t ContactGroups::detection_range(const ContactView& view, std::int32_t size, std::int32_t radar_yd) const {
  // sighting_check (0x2c034) without the land test, for the range only.
  std::int32_t range = 0;
  if (view.observer_height_ft > 0 && size > 0) {
    range = sighting_compute_range(view.observer_height_ft, size, {view.weather, view.fog_pct, view.light_pct})
                .range_yd;
  }
  return std::max(range, radar_yd);
}

bool ContactGroups::try_materialize(ContactGroup& g, const GameClock& clock, const ContactView& view,
                                    WatcomRandom& random) {
  const auto count = [&](ContactGroupKind k) { return materialized_[kind_index(k)]; };
  const std::int32_t routes = count(ContactGroupKind::kRoute);
  const std::int32_t patrols = count(ContactGroupKind::kPatrol);
  const std::int32_t anchored = count(ContactGroupKind::kAnchored);
  std::int32_t size = 0;
  std::int32_t radar = view.sj_range_yd;
  std::uint32_t near_yd = kNearYd;
  switch (g.kind) {
    case ContactGroupKind::kRoute:
      // convoy_group_try_materialize (0x2ed1f)
      if (routes != 0 || patrols != 0 || anchored != 0) {
        return false;
      }
      switch (g.route_kind) {
        case RouteGroupKind::kConvoy:
          size = visual_sizes_[kSlotMerchant];
          break;
        case RouteGroupKind::kTaskForce:
          size = visual_sizes_[kSlotBattleship + static_cast<std::size_t>(g.warship_type)];
          break;
        case RouteGroupKind::kPatrol:
          size = visual_sizes_[kSlotEscort];
          break;
        case RouteGroupKind::kSubmarines:
          size = visual_sizes_[kSlotSub] / (g.coastal_or_submerged ? 3 : 1);
          break;
      }
      // A coastal route (or submerged boats) is found by sight only.
      if (g.coastal_or_submerged) {
        radar = 0;
      }
      break;
    case ContactGroupKind::kPatrol:
      // contacts_patrol_try_materialize (0x2fc76)
      if (routes != 0 || patrols != 0) {
        return false;
      }
      size = visual_sizes_[kSlotEscort];
      break;
    case ContactGroupKind::kAnchored:
      // contacts_anchored_try_materialize (0x30370)
      if (routes != 0 || anchored > (limit_classes_ ? 0 : 2)) {
        return false;
      }
      size = visual_sizes_[kSlotMerchant];
      break;
    case ContactGroupKind::kSampans:
      // contacts_sampans_try_materialize (0x30a56)
      if (count(ContactGroupKind::kSampans) != 0 || routes != 0 || patrols != 0 || anchored != 0) {
        return false;
      }
      size = visual_sizes_[kSlotSmallCraft];
      radar /= 4;
      near_yd = kSampansNearYd;
      break;
  }
  const std::uint32_t dist = distance_yd(g.position, view.player_x, view.player_z);
  g.near = dist < near_yd;
  // Only a group exactly on the boat while near fails this test.
  if (static_cast<std::uint32_t>(g.near ? 1 : 0) > dist) {
    return false;
  }
  const std::int32_t range = detection_range(view, size, radar);
  if (static_cast<std::int32_t>(dist) > range) {
    return false;
  }
  if (g.blocked || (!force_ && range - range / 8 > static_cast<std::int32_t>(dist))) {
    // Found already too close (or just created in range): dropped.
    g.finished = true;
    return false;
  }

  params_.day = clock.day();
  params_.month = clock.month();
  params_.year = clock.year();
  const WorldPoint at = at_of(g);
  const Angle10 heading(g.heading);
  Scenario units;
  switch (g.kind) {
    case ContactGroupKind::kRoute: {
      params_.escort = g.escort;
      params_.enemy = g.crew;
      params_.air_cover = 0;
      WorldPath path;
      for (std::size_t i = static_cast<std::size_t>(g.waypoint_index - 1); i < g.waypoints.size(); ++i) {
        path.push_back({g.waypoints[i].x, g.waypoints[i].z});
      }
      random.srand(g.seed);
      ScenarioGenerator gen(tables_, params_, units, random, view.options, limit_classes_);
      std::int32_t escort_group = 0;
      switch (g.route_kind) {
        case RouteGroupKind::kConvoy:
          escort_group = spawner_.free_convoy_group();
          params_.convoy_size = g.size;
          gen.build_convoy(at, heading, path, false, g.fast_or_intercept, g.coastal_or_submerged, escort_group);
          break;
        case RouteGroupKind::kTaskForce:
          params_.warship_type = g.warship_type;
          gen.build_task_force(at, heading, path, false, 1);
          break;
        case RouteGroupKind::kPatrol:
          params_.patrol_size = g.size;
          gen.build_patrol(at, heading, false, false);
          break;
        case RouteGroupKind::kSubmarines:
          gen.build_enemy_subs(at, heading, path, g.coastal_or_submerged, g.size, g.fast_or_intercept, {-1, -1});
          break;
      }
      gen.build_aircraft(at, g.aircraft, escort_group);
      break;
    }
    case ContactGroupKind::kPatrol: {
      params_.enemy = g.crew_quality;
      random.srand(g.seed);
      params_.patrol_size = g.patrol_size;
      ScenarioGenerator gen(tables_, params_, units, random, view.options, limit_classes_);
      gen.build_patrol(at, heading, g.patrol_tight, false);
      break;
    }
    case ContactGroupKind::kAnchored: {
      // The crew table is indexed by the calendar month (an original bug).
      params_.enemy =
          random_weighted_index(zone_.zone.crew_pct[static_cast<std::size_t>(clock.month())], random);
      random.srand(g.seed);
      params_.escort = g.guard_size;
      params_.convoy_size = g.anchored_size;
      if (anchored > 0) {
        params_.escort = std::min(params_.escort, 1);
        params_.convoy_size = std::min(params_.convoy_size, 1);
      }
      ScenarioGenerator gen(tables_, params_, units, random, view.options, limit_classes_);
      gen.build_anchored_ships(at, g.merchant_pct, heading, false, false);
      const BaseRecord& b = zone_.bases[static_cast<std::size_t>(g.base)];
      gen.build_aircraft(at, b.has(BaseFlag::kAircraft) ? b.anchored_size : 0, 0);
      break;
    }
    case ContactGroupKind::kSampans: {
      random.srand(g.seed);
      params_.convoy_size = g.sampan_size;
      const std::int32_t sampan_heading = random.random_int(3600);
      ScenarioGenerator gen(tables_, params_, units, random, view.options, limit_classes_);
      build_sampans(gen, units, g.position.x, g.position.z, sampan_heading, random);
      break;
    }
  }
  materialize(g, units);
  return true;
}

void ContactGroups::build_sampans(ScenarioGenerator& generator, Scenario& units, std::int32_t x, std::int32_t z,
                                  std::int32_t heading, WatcomRandom& random) const {
  // scengen_build_sampans (0x6b3fa)
  const auto size = static_cast<std::size_t>(std::clamp(params_.convoy_size, 0, 2));
  const std::int32_t lo = kSampanSizes.at(size);
  const std::int32_t count = lo + 1 + random.random_int(kSampanSizes.at(size + 1) - (lo + 1) + 1);
  units.ships.assign(static_cast<std::size_t>(count), ShipRecord{});
  std::int32_t distance = 0x5a0;
  for (ShipRecord& s : units.ships) {
    s.orders = ShipOrders::kFishing;
    distance = distance + random.random_int(distance / 2) + distance / 2;
    const fx::SinCos r = fx::angle_sincos(Angle10(random.random_int(3600)));
    s.x = fx::mul(distance, r.sin);
    s.y = fx::mul(distance, r.cos);
    const std::int32_t h = random.random_int(2) == 0
                               ? fx::angle_sub(Angle10(heading), Angle10(random.random_int(250))).tenths()
                               : angle_add(heading, random.random_int(250));
    s.heading = h / 10;
    const ShipClass& c = generator.pick_small_craft_class();
    s.speed = atoi_c(c.top_speed_kts);
    s.ship_class = c.index_name;
  }
  for (ShipRecord& s : units.ships) {
    s.x += x;
    s.y += z;
  }
}

void ContactGroups::materialize(ContactGroup& g, const Scenario& units) {
  g.vehicles = spawner_.spawn(units);
  for (Vehicle* v : g.vehicles) {
    owner_[v] = &g;
  }
  g.spawned = static_cast<std::int32_t>(g.vehicles.size());
}

void ContactGroups::dematerialize(ContactGroup& g) {
  // contacts_dematerialize_group (0x2e097): the group's vehicles leave the
  // simulation, then slot +0x1c decides from the survivors whether the
  // group is finished.
  std::int32_t survivors = 0;
  for (Vehicle* v : g.vehicles) {
    owner_.erase(v);
    if (spawner_.contains(v)) {
      ++survivors;
      spawner_.remove(*v);
    }
  }
  g.vehicles.clear();
  g.materialized = false;
  switch (g.kind) {
    case ContactGroupKind::kRoute:    // contact_group_always_finished
    case ContactGroupKind::kSampans:
      g.finished = true;
      break;
    case ContactGroupKind::kPatrol:   // contacts_patrol_destroyed (0x2fe08)
      g.finished = survivors < g.spawned * 50 / 100;
      break;
    case ContactGroupKind::kAnchored:  // contacts_anchored_never_finished
      g.finished = false;
      break;
  }
}

void ContactGroups::forget(const Vehicle& removed) {
  const auto it = owner_.find(&removed);
  if (it == owner_.end()) {
    return;
  }
  std::erase(it->second->vehicles, &removed);
  owner_.erase(it);
}

ContactGroup* ContactGroups::group_of(const Vehicle& vehicle) const {
  const auto it = owner_.find(&vehicle);
  return it == owner_.end() ? nullptr : it->second;
}

void ContactGroups::engaged(Vehicle& ship) {
  if (ContactGroup* g = group_of(ship)) {
    g->engaged = true;
  }
}

void ContactGroups::detected(Vehicle& vehicle) {
  if (ContactGroup* g = group_of(vehicle)) {
    g->seen = true;
  }
}

}  // namespace sh

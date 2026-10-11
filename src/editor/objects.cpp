#include "editor/objects.h"

#include <algorithm>

namespace sh::editor {
namespace {

// Entry counts of the `char*` tables of se_addr.
constexpr std::size_t kEngineOrderCount = 10;
constexpr std::size_t kShipOrderCount = 8;
constexpr std::size_t kConvoyLetterCount = 9;
constexpr std::size_t kYesNoCount = 2;
constexpr std::size_t kStrikeSizeCount = 3;
constexpr std::size_t kAircraftRoleCount = 4;
constexpr std::size_t kShipTypeCount = 22;
constexpr std::size_t kShipTypeStride = 8;

// The first ship type named by its long name (`sbship_describe`): 12..20
// are the merchant types, 21 the sampan.
constexpr int kFirstMerchantType = 12;
constexpr int kSampanType = 21;

// Returns entry `index` of a `char*` table of `count` entries; the original
// reads past the table for other values, here they give an empty text.
std::string_view table_entry(const SeExe& se, std::uint32_t table, std::int64_t index, std::size_t count,
                             std::size_t stride = 4) {
  if (index < 0 || static_cast<std::size_t>(index) >= count) {
    return {};
  }
  return se.table_str(table, static_cast<std::size_t>(index), stride);
}

// "Loc: <latitude>, <longitude>" of a world point.
std::string location_line(const ObjectEnv& env, std::int32_t x, std::int32_t y) {
  const SeExe& se = env.data.se;
  const World& world = env.world.world();
  std::string out(se.str(se_addr::kLocLabel));
  out += format_latlon(se, world.latitude_at(y));
  out += se.str(se_addr::kLatLonSeparator);
  out += format_latlon(se, world.longitude_at(x));
  return out;
}

// "Orders: <orders>", with " [<convoy letter>]" for a positive group.
std::string orders_line(const SeExe& se, std::int32_t orders, std::int32_t group) {
  std::string out(se.str(se_addr::kOrdersLabel));
  out += table_entry(se, se_addr::kShipOrders, orders, kShipOrderCount);
  if (group > 0) {
    out += se.str(se_addr::kOpenBracket);
    out += table_entry(se, se_addr::kConvoyLetters, group, kConvoyLetterCount);
    out += se.str(se_addr::kCloseBracket);
  }
  return out;
}

// "<label><hh>:<mm>" of a duration in minutes.
std::string time_line(const SeExe& se, std::string_view label, std::int32_t minutes) {
  std::string out(label);
  out += format_padded(minutes / 60, 2);
  out += se.str(se_addr::kTimeSeparator);
  out += format_padded(minutes % 60, 2);
  return out;
}

std::string speed_line(const SeExe& se, std::int32_t speed) {
  std::string out(se.str(se_addr::kSpeedLabel));
  out += format_padded(speed, 2);
  out += se.str(se_addr::kKnots);
  return out;
}

std::string heading_line(const SeExe& se, std::int32_t heading) {
  return std::string(se.str(se_addr::kHeadingLabel)) + format_padded(heading, 3);
}

std::string depth_line(const SeExe& se, std::int32_t depth) {
  return std::string(se.str(se_addr::kDepthLabel)) + std::to_string(depth) + std::string(se.str(se_addr::kFeet));
}

// The SHIPS.DAT ship type of a class, or -1 for an unknown class.
int ship_type_of(const EditorData& data, const std::optional<std::size_t>& cls) {
  return cls ? static_cast<int>(static_cast<std::int8_t>(data.tables.ships[*cls].ship_type)) : -1;
}

// The visibility of a target from the player (`sbship_update_detection`
// 0x420ca, `sbsub_update_detection` 0x42936): the sighting check from the
// periscope eye, the range cut by (2 - crew) x 10 %, the surface radar.
struct Detection {
  bool seen = false;
  bool in_radar_range = false;
  std::int32_t range_yd = 0;
};

// Returns `feet` in world units (x 16), wrapping on overflow as the
// original's 32-bit arithmetic (the depths of a damaged file).
std::int32_t feet_to_units(std::int32_t feet) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(feet) * 16u);
}

Detection detect(const DetectionEnv& env, std::int32_t x, std::int32_t y, std::int32_t target_height) {
  const ScnPlayerSub& p = env.player;
  const std::optional<std::size_t> sub = env.data.find_sub(scn_text(p.sub_class));
  // An unknown class reads past SUBS.DAT in the original; no eye here.
  const std::int32_t eye_ft =
      sub ? static_cast<std::int32_t>(env.data.tables.subs[*sub].periscope_eye_ft) : 0;
  const fx::Vec3 observer{p.x, fx::wadd(fx::wsub(0, feet_to_units(p.depth)), eye_ft * 16), p.y};
  const SightingCheck check = sighting_check(env.conditions, env.grid, observer, {x, target_height, y});
  Detection d;
  const std::int32_t radar = player_radar_range_yd(env.equipment);
  const std::int32_t distance = geom::range_yards({p.x, p.y}, {x, y});
  d.in_radar_range = radar > 0 && distance <= radar;
  const auto crew = static_cast<std::int32_t>(p.crew);
  d.range_yd = check.range.range_yd - check.range.range_yd * 10 * (2 - crew) / 100;
  d.seen = check.distance_yd >= 0 && check.distance_yd <= d.range_yd;
  return d;
}

}  // namespace

std::int32_t player_radar_range_yd(std::uint32_t equipment) {
  const std::uint32_t radar = equipment & 6;  // SJa bit 1, SJ-1 bit 2
  if (radar == 0) {
    return 0;
  }
  return radar > 2 ? 35000 : 15000;
}

std::string format_padded(std::int32_t value, int digits) {
  digits = std::min(digits, 10);
  value = std::max(value, 0);
  std::string out(static_cast<std::size_t>(digits), '0');
  std::int64_t divisor = 1;
  for (int i = digits - 1; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = static_cast<char>('0' + value / divisor % 10);
    divisor *= 10;
  }
  return out;
}

std::string format_latlon(const SeExe& se, const GeoAngle& a) {
  std::string out = std::to_string(a.whole_degrees);
  out += se.str(se_addr::kDegreeMark);
  out += format_padded(a.minutes, 2);
  out += se.str(se_addr::kMinuteMark);
  out += a.hemisphere;
  return out;
}

// ------------------------------------------------------------ EditorObject

std::string_view EditorObject::type_name(const SeExe& se) const {
  switch (type()) {
    case ObjectType::kPlayer:
      return se.str(se_addr::kPlayer);
    case ObjectType::kShip:
      return se.str(se_addr::kShip);
    case ObjectType::kSubmarine:
      return se.str(se_addr::kSubmarine);
    case ObjectType::kAircraft:
      return se.str(se_addr::kAircraft);
    case ObjectType::kAirstrike:
      return se.str(se_addr::kAirstrike);
    case ObjectType::kPhotoRecon:
      return se.str(se_addr::kPhotoRecon);
  }
  return {};
}

std::int32_t EditorObject::convoy_group(bool) const { return 0; }
std::int32_t EditorObject::escorted_group() const { return 0; }
bool EditorObject::is_special_mission() const { return false; }
void EditorObject::update_detection(const DetectionEnv&) {}
std::int32_t EditorObject::visual_range() const { return 0; }
bool EditorObject::in_detection_range(bool) const { return false; }
std::string_view EditorObject::model_name1() const { return {}; }
std::string_view EditorObject::model_name2() const { return {}; }

// --------------------------------------------------------------- PlayerSub

DescribeText PlayerSub::describe(const ObjectEnv& env) const {
  // sbplayer_describe (0x416b9)
  DescribeText t;
  if (!env.select_mode) {
    return t;
  }
  const SeExe& se = env.data.se;
  t[0] = scn_text(rec.name);
  t[1] = std::string(se.str(se_addr::kClassLabel)) + scn_text(rec.sub_class);
  t[2] = location_line(env, rec.x, rec.y);
  t[3] = depth_line(se, rec.depth);
  t[4] = heading_line(se, rec.heading);
  t[5] = std::string(se.str(se_addr::kSpeedLabel));
  t[5] += table_entry(se, se_addr::kEngineOrders, rec.engine_order, kEngineOrderCount);
  return t;
}

void PlayerSub::move_to(WorldPoint p, std::int32_t water_depth_ft) {
  if (water_depth_ft <= rec.depth) {
    rec.depth = water_depth_ft - 1;
  }
  rec.x = p.x;
  rec.y = p.y;
}

// ------------------------------------------------------------------ Vessel

WorldPoint Vessel::position(const ObjectEnv&) const {
  return kind == Kind::kShip ? WorldPoint{ship.x, ship.y} : WorldPoint{sub.x, sub.y};
}

std::int32_t Vessel::convoy_group(bool leader) const {
  const ShipOrders wanted = leader ? ShipOrders::kConvoyLeader : ShipOrders::kConvoyMember;
  return kind == Kind::kShip && ship.orders == wanted ? ship.convoy_group : 0;
}

std::int32_t Vessel::escorted_group() const {
  return kind == Kind::kShip && ship.orders == ShipOrders::kEscort ? ship.convoy_group : 0;
}

DescribeText Vessel::describe(const ObjectEnv& env) const {
  const SeExe& se = env.data.se;
  DescribeText t;
  if (kind == Kind::kShip) {
    // sbship_describe (0x41d8d)
    const std::optional<std::size_t> cls = env.data.find_ship(scn_text(ship.ship_class));
    const int type = ship_type_of(env.data, cls);
    if (type >= kSampanType) {
      t[0] = table_entry(se, se_addr::kShipTypeNames, type, kShipTypeCount, kShipTypeStride);
    } else if (type >= 0) {
      t[0] = type < kFirstMerchantType ? env.data.tables.ships[*cls].name
                                       : std::string(table_entry(se, se_addr::kShipTypeNames, type, kShipTypeCount,
                                                                 kShipTypeStride));
      t[0] += se.str(se_addr::kOpenParen);
      t[0] += table_entry(se, se_addr::kShipTypeAbbrevs, type, kShipTypeCount, kShipTypeStride);
      t[0] += se.str(se_addr::kCloseParen);
    }
    t[1] = location_line(env, ship.x, ship.y);
    t[2] = heading_line(se, ship.heading);
    t[3] = speed_line(se, ship.speed);
    t[4] = orders_line(se, static_cast<std::int32_t>(ship.orders), ship.convoy_group);
  } else {
    // sbsub_describe (0x4264d)
    const std::optional<std::size_t> cls = env.data.find_ship(scn_text(sub.sub_class));
    if (cls) {
      t[0] = env.data.tables.ships[*cls].name;
      t[0] += se.str(se_addr::kOpenParen);
      t[0] += table_entry(se, se_addr::kShipTypeAbbrevs, ship_type_of(env.data, cls), kShipTypeCount,
                          kShipTypeStride);
      t[0] += se.str(se_addr::kCloseParenSpace);
    }
    t[1] = location_line(env, sub.x, sub.y);
    t[2] = depth_line(se, sub.depth);
    t[3] = heading_line(se, sub.heading);
    t[4] = speed_line(se, sub.speed);
    t[5] = orders_line(se, static_cast<std::int32_t>(sub.orders), 0);
  }
  return t;
}

void Vessel::update_detection(const DetectionEnv& env) {
  const std::optional<std::size_t> cls =
      env.data.find_ship(kind == Kind::kShip ? scn_text(ship.ship_class) : scn_text(sub.sub_class));
  // An unknown class reads past SHIPS.DAT in the original; no height here.
  const std::int32_t size = cls ? static_cast<std::int32_t>(env.data.tables.ships[*cls].visual_size) : 0;
  const Detection d = kind == Kind::kShip ? detect(env, ship.x, ship.y, size * 16)
                                          : detect(env, sub.x, sub.y, fx::wsub(size * 16, feet_to_units(sub.depth)));
  seen_ = d.seen;
  in_radar_range_ = d.in_radar_range;
  sighting_range_yd_ = d.range_yd;
}

ScnRecord Vessel::record() const {
  if (kind == Kind::kShip) {
    return ship;
  }
  return sub;
}

std::string Vessel::class_name() const {
  return kind == Kind::kShip ? scn_text(ship.ship_class) : scn_text(sub.sub_class);
}

void Vessel::set_heading(std::int32_t heading) {
  ship.heading = heading;
  sub.heading = heading;
}

void Vessel::set_speed(std::int32_t speed) {
  ship.speed = speed;
  sub.speed = speed;
}

void Vessel::set_crew(CrewQuality crew) {
  ship.crew = crew;
  sub.crew = crew;
}

void Vessel::set_damage(std::int32_t damage_pct) {
  ship.damage_pct = damage_pct;
  sub.damage_pct = damage_pct;
}

void Vessel::move_to(WorldPoint p, std::int32_t water_depth_ft) {
  if (kind == Kind::kSubmarine && water_depth_ft <= sub.depth) {
    sub.depth = water_depth_ft - 1;
  }
  sub.x = p.x;
  sub.y = p.y;
  ship.x = p.x;
  ship.y = p.y;
  if (waypoints().count > 0) {
    sub.waypoints.x[0] = p.x;
    sub.waypoints.y[0] = p.y;
    ship.waypoints.x[0] = p.x;
    ship.waypoints.y[0] = p.y;
  }
}

// ---------------------------------------------------------------- Aircraft

std::int32_t Aircraft::escorted_group() const {
  return rec.mission == AircraftMission::kSearchOrbit ? rec.escort_group : 0;
}

DescribeText Aircraft::describe(const ObjectEnv& env) const {
  // sbair_describe (0x42c13)
  const SeExe& se = env.data.se;
  DescribeText t;
  if (const std::optional<std::size_t> cls = env.data.find_aircraft(scn_text(rec.aircraft_type))) {
    const AircraftType& a = env.data.tables.aircraft[*cls];
    t[0] = a.long_name + std::string(se.str(se_addr::kOpenQuote)) + a.name + std::string(se.str(se_addr::kCloseQuote));
    t[1] = table_entry(se, se_addr::kAircraftRoles, static_cast<std::int64_t>(a.role), kAircraftRoleCount);
  }
  t[2] = std::string(se.str(se_addr::kGunsLabel));
  t[2] += table_entry(se, se_addr::kYesNo, rec.strafes, kYesNoCount);
  t[3] = std::string(se.str(se_addr::kBombsLabel)) + std::to_string(rec.bomb_load);
  t[4] = orders_line(se, static_cast<std::int32_t>(rec.mission), rec.escort_group);
  return t;
}

void Aircraft::move_to(WorldPoint p) {
  const std::int32_t dx = p.x - rec.x;
  const std::int32_t dy = p.y - rec.y;
  rec.x = p.x;
  rec.y = p.y;
  for (std::size_t i = 0; i < rec.waypoints.size(); ++i) {
    rec.waypoints.x[i] += dx;
    rec.waypoints.y[i] += dy;
  }
}

// ---------------------------------------------------------- SpecialMission

std::int32_t SpecialMission::base() const {
  return std::visit([](const auto& r) { return r.base; }, rec);
}

WorldPoint SpecialMission::position(const ObjectEnv& env) const {
  // sbmission_get_position (0x43293)
  const std::vector<BaseRecord>& bases = env.world.bases();
  const std::int32_t b = base();
  if (b < 0 || static_cast<std::size_t>(b) >= bases.size()) {
    return {};
  }
  return {bases[static_cast<std::size_t>(b)].station.x, bases[static_cast<std::size_t>(b)].station.z};
}

ObjectType SpecialMission::type() const {
  return std::holds_alternative<BomberGroupRecord>(rec) ? ObjectType::kAirstrike : ObjectType::kPhotoRecon;
}

std::int32_t SpecialMission::memory_cost() const { return type() == ObjectType::kAirstrike ? 500 : 300; }

ScnRecord SpecialMission::record() const {
  return std::visit([](const auto& r) -> ScnRecord { return r; }, rec);
}

DescribeText SpecialMission::describe(const ObjectEnv& env) const {
  // sbmission_describe (0x42fb2)
  const SeExe& se = env.data.se;
  DescribeText t;
  t[0] = type_name(se);
  std::int32_t size = -1;
  std::int32_t delay = 0;
  std::int32_t second = 0;
  std::string_view second_label;
  if (const auto* strike = std::get_if<BomberGroupRecord>(&rec)) {
    size = static_cast<std::int32_t>(strike->size);
    delay = strike->launch_delay_min;
    second = strike->hold_min;
    second_label = env.hold_time_label ? own_text::kHoldTimeLabel : se.str(se_addr::kFlightDelayLabel);
  } else {
    const auto& recon = std::get<PhotoReconRecord>(rec);
    delay = recon.delay_min;
    second = recon.duration_min;
    second_label = se.str(se_addr::kTimeLimitLabel);
  }
  const std::vector<BaseRecord>& bases = env.world.bases();
  const std::int32_t b = base();
  t[1] = std::string(se.str(se_addr::kTargetLabel));
  if (b >= 0 && static_cast<std::size_t>(b) < bases.size()) {
    const BaseRecord& target = bases[static_cast<std::size_t>(b)];
    t[1] += target.name;
    t[2] = location_line(env, target.station.x, target.station.z);
  }
  std::size_t line = 3;
  if (size >= 0) {
    t[3] = table_entry(se, se_addr::kStrikeSizes, size, kStrikeSizeCount);
    line = 4;
  }
  t[line] = time_line(se, se.str(se_addr::kTimeDelayLabel), delay);
  t[line + 1] = time_line(se, second_label, second);
  return t;
}

// -------------------------------------------------------------- ModelCache

ModelCache::Entry* ModelCache::find(std::string_view name) {
  for (Entry& e : entries_) {
    if (e.count != 0 && equals_ignore_case(e.name, name)) {
      return &e;
    }
  }
  return nullptr;
}

std::int32_t ModelCache::add(std::string_view name, const ModelCatalog& catalog) {
  if (Entry* e = find(name)) {
    ++e->count;
    return 0;
  }
  const auto free = std::ranges::find_if(entries_, [](const Entry& e) { return e.count == 0; });
  if (free == entries_.end()) {
    return 0;
  }
  const std::optional<std::uint32_t> size = catalog.size_of(name);
  if (!size) {
    return 0;
  }
  free->name = std::string(name);
  free->count = 1;
  free->size = static_cast<std::int32_t>(*size);
  return free->size;
}

std::int32_t ModelCache::release(std::string_view name) {
  Entry* e = find(name);
  if (e == nullptr) {
    return 0;
  }
  if (--e->count > 0) {
    return 0;
  }
  const std::int32_t size = e->size;
  e->size = 0;
  return size;
}

void ModelCache::clear() {
  for (Entry& e : entries_) {
    e.count = 0;
  }
}

std::int32_t ModelCache::references(std::string_view name) const {
  for (const Entry& e : entries_) {
    if (e.count != 0 && equals_ignore_case(e.name, name)) {
      return e.count;
    }
  }
  return 0;
}

}  // namespace sh::editor

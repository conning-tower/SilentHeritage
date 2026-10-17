#include "game/mission.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <utility>

#include "core/error.h"
#include "game/class_params.h"
#include "game/patrol_contacts.h"
#include "game/scengen.h"
#include "sim/ai_ship.h"
#include "sim/sensors.h"
#include "sim/geometry.h"
#include "sim/motion.h"
#include "sim/radar.h"
#include "sim/sighting.h"

namespace sh {

namespace {

// Returns the option bits of the scenario equipment bits
// (`init_apply_scenario_header`): SJa -> 16, SJ-1 -> 17, SD -> 18, then
// bits 3-5 -> 19-21.
std::uint32_t equipment_options(std::uint32_t equipment) {
  const std::uint32_t sd = equipment & 1u;
  const std::uint32_t sj = (equipment >> 1) & 3u;
  const std::uint32_t rest = (equipment >> 3) & 7u;
  return (sj | sd << 2 | rest << 3) << option::kEquipmentShift;
}

// Not in the original: whether the boat has the extended ST periscope
// radar. A career boat has it once delivered (equipment bit 8); in a single
// mission every boat with an SJ has it from November 1944.
bool st_radar_fitted(const ScenarioHeader& h, const MissionRules& rules, bool career) {
  if (!rules.st_radar || (!h.has(Equipment::kSjaRadar) && !h.has(Equipment::kSj1Radar))) {
    return false;
  }
  if (career) {
    return h.has(Equipment::kStRadar);
  }
  return h.has(Equipment::kStRadar) || h.year * 12 + h.month >= kStRadarYear * 12 + kStRadarMonth;
}

// Returns the `.ENV` file for `weather` (`init_select_env_file`, 0x4f9e2):
// tropical below 20 deg south latitude (of the map loaded before), else a
// summer file from April to September, else one of two winter files.
std::string select_environment(std::int32_t weather, Fixed16 lat_south, std::int32_t month, WatcomRandom& random) {
  struct Choice {
    const char* tropical;
    const char* summer;
    const char* winter0;  // random_int(2) == 0
    const char* winter1;
  };
  static constexpr std::array<Choice, 3> kByWeather = {{
      {"env-12.env", "env-13.env", "env-09.env", "env-03.env"},
      {"env-14.env", "env-11.env", "env-02.env", "env-05.env"},
      {"env-06.env", "env-10.env", "env-08.env", "env-07.env"},
  }};
  if (weather == 3) {
    return random.random_int(2) == 0 ? "env-04.env" : "env-01.env";
  }
  if (weather < 0 || weather > 3) {
    // The name is left as it was: the initial "ENV-05.ENV".
    return "ENV-05.ENV";
  }
  const Choice& c = kByWeather[static_cast<std::size_t>(weather)];
  if (lat_south.raw() < 0x140001) {
    return c.tropical;
  }
  if (month < 10 && month > 2) {
    return c.summer;
  }
  return random.random_int(2) == 0 ? c.winter0 : c.winter1;
}

// Returns the sea picture of a new map (`init_load_world`): moderate from
// October to February north of 25 deg, heavy one time in two from December
// to January north of 40 deg.
std::int32_t select_sea_picture(Fixed16 lat_north, std::int32_t month, WatcomRandom& random) {
  std::int32_t sea = 0;
  if ((month > 8 || month < 3) && lat_north.raw() > 0x18ffff) {
    sea = 1;
    if ((month > 10 || month < 2) && lat_north.raw() > 0x27ffff && random.random_int(2) != 0) {
      sea = 2;
    }
  }
  return sea;
}

// `scen_setup_single_mission` (0x6839c) for a scenario: the PZ.DAT zone
// whose map is the scenario's (case-insensitively, the first one) becomes
// the mission's (0x11b808), and `bases_load` reads its bases, which the
// scenario's air strikes and photo recons refer to; none without such a
// zone.
void use_scenario_zone(const Install& install, std::string_view world, MissionSetup& setup) {
  const auto same = [](char a, char b) {
    return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
  };
  for (const PatrolZone& zone : parse_patrol_zones(install.read("DATA/PZ.DAT"), "DATA/PZ.DAT")) {
    if (std::ranges::equal(zone.world, world, same)) {
      setup.bases = ZoneData::load(install, zone).bases;
      setup.air_edges = zone.air_start_edges;
      return;
    }
  }
}

}  // namespace

Mission::Mission(const GameTables& tables, Scenario scenario) : tables_(tables), scenario_(std::move(scenario)) {}

Mission::~Mission() = default;

MissionStartDraws MissionStartDraws::draw(WatcomRandom& random) {
  MissionStartDraws d;
  d.restore_seed = static_cast<std::uint32_t>(random.rand());
  d.fog_density = random.random_int(30) + 60;
  return d;
}

std::unique_ptr<Mission> Mission::create(const Install& install, const GameTables& tables, Scenario scenario,
                                         MissionSetup setup) {
  if (setup.patrol == nullptr && !setup.saved_world && setup.bases.empty()) {
    use_scenario_zone(install, scenario.header.world, setup);
  }
  std::unique_ptr<Mission> mission(new Mission(tables, std::move(scenario)));
  mission->build(install, setup, nullptr, -1);
  return mission;
}

std::unique_ptr<Mission> Mission::create(const Install& install, const GameTables& tables,
                                         GeneratedMission generated, MissionStartDraws draws, MissionSetup setup) {
  std::unique_ptr<Mission> mission(new Mission(tables, std::move(generated.scenario)));
  setup.bases = std::move(generated.zone.bases);
  mission->build(install, setup, &draws, generated.start_bearing);
  return mission;
}

std::unique_ptr<Mission> Mission::load(const Install& install, const GameTables& tables, std::string_view dos_path,
                                       MissionSetup setup) {
  const std::vector<std::uint8_t> data = install.read(dos_path);
  return create(install, tables, parse_scenario(data, dos_path), std::move(setup));
}

void Mission::build(const Install& install, MissionSetup& setup, const MissionStartDraws* generated,
                    std::int32_t start_bearing) {
  // init_simulation (0x4f078), single-mission and career branches.
  PatrolHooks* const patrol = setup.patrol;
  const std::uint32_t mode = setup.options & (option::kModeCareer | option::kModeSavedMission);
  const bool saved_mission = patrol == nullptr && mode == option::kModeSavedMission && setup.saved_world;
  if (patrol != nullptr ? mode != option::kModeCareer : mode != 0 && !saved_mission) {
    throw DataError(patrol != nullptr ? "a patrol needs the career mode" : "a scenario mission needs the single-mission mode");
  }
  const ScenarioHeader& h = scenario_.header;
  WatcomRandom random = setup.random;
  MissionStartDraws draws;
  PatrolWeatherState patrol_weather;
  player_fuel_ = setup.player_fuel;
  install_ = &install;
  if (patrol != nullptr) {
    // wp_load_patrol_data (0x979a0): scen_instantiate_units rolls first
    // (the units themselves draw nothing), then wp_init_weather; the
    // scenario's weather is the patrol's.
    draws = MissionStartDraws::draw(random);
    conditions_.scenario_roll = random.random_int(100);
    patrol_weather = patrol->init_weather(random);
    scenario_.header.weather = static_cast<Weather>(patrol_weather.weather);
    scenario_.header.sea_state = static_cast<SeaState>(patrol_weather.sea_state);
    scenario_.header.fog = patrol_weather.fog;
  } else if (generated != nullptr) {
    // The generator ran in place of scen_load_ascn after the start draws.
    draws = *generated;
  } else {
    draws = MissionStartDraws::draw(random);
    // scen_load_ascn (0x676b5), run by scen_setup_single_mission.
    if (h.randomize == 0) {
      random.srand(static_cast<std::uint32_t>(h.saved_time));
    }
    conditions_.scenario_roll = random.random_int(100);
  }
  restore_seed_ = draws.restore_seed;
  const std::int32_t fog_density = draws.fog_density;

  // init_apply_scenario_header (0x5044a); a load the player chose instead
  // (not in the original).
  torpedoes_ = setup.torpedo_load ? *setup.torpedo_load : scenario_torpedo_load(h);
  boat_fit_ = setup.boat_fit;
  conditions_.options = (setup.options & ~option::kEquipmentMask) | equipment_options(h.equipment);
  conditions_.speech = setup.speech;
  conditions_.rules = setup.rules;
  if (st_radar_fitted(h, setup.rules, setup.patrol != nullptr)) {
    conditions_.options |= sensor_option::kStPeriscopeRadar;
  }
  conditions_.sea_state = static_cast<std::int32_t>(h.sea_state);
  conditions_.weather = static_cast<std::int32_t>(h.weather);
  conditions_.fog = h.fog;
  conditions_.end_on_lost_contact = h.end_on_lost_contact != 0;

  load_world(install, setup, random);
  bases_ = std::move(setup.bases);
  air_theatre_.bases = bases_;
  air_theatre_.locations = world_.locations;
  air_theatre_.grid_width = world_.grid.width;
  air_theatre_.grid_height = world_.grid.height;
  air_theatre_.scale_shift = static_cast<std::int32_t>(world_.scale_shift);
  air_theatre_.air_edges = setup.air_edges;
  air_theatre_.sea_picture = sea_picture_;

  // init_thermal_layer (0x4fba9); a patrol's comes with its weather.
  conditions_.thermal_layer_ft =
      patrol != nullptr ? patrol_weather.thermal_layer_ft : init_thermal_layer(random, h.gradient);
  conditions_.map = MapExtent{world_.grid.width * world_.cell_size(), world_.grid.height * world_.cell_size()};

  // load_ephemeris (0xc2dbd). init_spawn_scenario_units (0x4f373) starts
  // with clock_init_time_of_day, whose clock_update_sky sees the sky from
  // the previous boat's position, then scen_instantiate_units (0x67400).
  ephemeris_ = Ephemeris::load(install);
  sky_ = std::make_unique<Sky>(ephemeris_, world_, random, conditions_.options, h.fog, fog_density);
  sky_->set_rules(&conditions_.rules);
  sky_->set_observer(setup.previous_player_x, setup.previous_player_z);
  if (h.hour == -1) {
    // clock_init_time_of_day (0x7df1d): a generated mission starts at its
    // time-of-day setting (in the minute field) on the date, seen from the
    // scenario's boat position.
    const PlayerSubRecord* boat = scenario_.player_sub ? &*scenario_.player_sub : nullptr;
    if (boat == nullptr) {
      throw DataError("scenario has no player submarine");
    }
    if (h.minute < 0 || h.minute > 3) {
      throw DataError("scenario start time-of-day setting out of range");
    }
    const StartTime t = sky_->choose_start_time(static_cast<StartTimeSetting>(h.minute), h.year, h.month, h.day,
                                                boat->x, boat->y);
    scenario_.header.hour = t.hour;
    scenario_.header.minute = t.minute;
  }
  if (patrol == nullptr) {
    GameClock start;
    start.init(h.year, h.month, h.day, h.hour, h.minute);
    sky_->update_day(start);
    conditions_.scenario_roll = random.random_int(100);
  }
  sim_ = std::make_unique<Simulation>(conditions_, floor_);
  sim_->random() = random;
  sky_->set_random(sim_->random());
  sky_->set_next_observer(setup.clock_observer);
  sky_->set_listener(&sky_light_);
  sim_->set_clock_observer(sky_.get());
  SensorEnvironment& sensors = sim_->sensors().environment();
  sensors.grid = &grid_;
  sensors.weapons = &ship_weapons_;
  sensors.auto_sj = setup.auto_sj;
  sensors.auto_sd = setup.auto_sd;
  // The header's special_mission flag is 0x125f4c, which the career's
  // contact groups take over.
  sim_->sensors().reports().set_group_activity(h.special_mission != 0);
  sim_->clock().init(h.year, h.month, h.day, h.hour, h.minute);
  instantiate_units(sim_->context());
  if (patrol != nullptr) {
    patrol->init_contacts(*this);
  } else if (!saved_mission && (!scenario_.ships.empty() || !scenario_.enemy_subs.empty())) {
    // init_spawn_scenario_units: the units' route group, when the .SCN has
    // ships or enemy submarines (0x12607c / 0x126080).
    form_scenario_group();
  }
  // sighting_compute_max_range (0x15e14): the aircraft take-off range from
  // where the boat starts.
  air_sorties_.reset(sighting_reference_range(player_->position(), player_->sub().bridge_height_ft));

  torpedo_types_ = setup.torpedo_load
                       ? kept_torpedo_types(torpedoes_, *player_class_, tables_)
                       : tdc_upgrade_mark18_by_date(torpedoes_, *player_class_, tables_, h.year, h.month,
                                                    conditions_.rules);
  start_torpedo_types_ = carried_torpedo_types(torpedoes_, *player_class_);
  combat_->tdc.set_type_index(torpedo_types_.selected);
  combat_->sensing = [this](const Vehicle& target) { return sense(target); };
  for (std::size_t i = 0; i < static_cast<std::size_t>(combat_->tubes.count()); ++i) {
    combat_->tubes.set_tube(i, torpedoes_.tubes.at(i));
  }
  for (auto [rack, load] : {std::pair{&combat_->tubes.bow_rack(), &torpedoes_.bow_racks},
                            std::pair{&combat_->tubes.stern_rack(), &torpedoes_.stern_racks}}) {
    std::copy_n(load->begin(), std::min(rack->size(), load->size()), rack->begin());
  }
  // deckgun_reset_state (0x37cdc), with the rounds the player init takes
  // from SUBS.DAT +276.
  combat_->deck_gun.reset(*player_class_, sub_deck_gun_rounds(*player_class_), player_->systems());
  // radar_init (0x660a7): no draws, so its place before the units (where
  // the original runs it on the cleared boat state) makes no difference.
  SimContext radar_ctx = sim_->context();
  sim_->sensors().radar().init(radar_ctx, *player_, sensors);
  for (std::size_t i = 0; i < torpedo_types_.types.size(); ++i) {
    const TorpedoTypeChoice& t = torpedo_types_.types[i];
    combat_->torpedo_types[i] = {std::string(t.name), t.available, static_cast<std::uint32_t>(t.id)};
  }

  if (patrol != nullptr) {
    patrol->place_player(*this);
  } else {
    place_player(start_bearing);
  }

  // init_regroup_convoys (0x4fbcf): convoy groups 1..8.
  SimContext ctx = sim_->context();
  for (std::int32_t group = 1; group < 9; ++group) {
    convoy_regroup_on_leader(ctx, group);
  }

  // clock_init (0x7dae4): clock_update_sky, then the fog of a fog day.
  sim_->clock().init(h.year, h.month, h.day, h.hour, h.minute);
  sky_->set_observer(player_->position().x, player_->position().z);
  sky_->clock_init(sim_->clock());
  if (setup.clock_observer != nullptr) {
    setup.clock_observer->on_new_day(sim_->clock());
  }
  if (patrol != nullptr) {
    patrol->start(*this);
  }
}

void Mission::place_player(std::int32_t start_bearing) {
  // init_place_player_sub (0x4fbe4), new single mission: from the target
  // (the contact seen furthest off), the boat moves out to a radius on
  // the start bearing, turning it 5 deg at a time (and shrinking the
  // radius each full turn) until the water is 3 drafts deep, on the map
  // and, unless a special mission, in sight of the scenario position;
  // then to the middle of that open arc. Without radar, retries closer
  // until a target can be seen from there.
  if (start_bearing == -1) {
    return;
  }
  const PlayerSubRecord& record = *scenario_.player_sub;
  const ScenarioHeader& h = scenario_.header;
  SensorEnvironment& sensors = sim_->sensors().environment();
  WatcomRandom& random = sim_->random();
  const std::int32_t saved_fog = sensors.fog_pct;
  if (h.fog == 1) {
    sensors.fog_pct = sky_->fog_density();
  }
  // The contacts the placement looks at: not the boat, aircraft or
  // bomber groups.
  const auto candidates = [this] {
    std::vector<Vehicle*> out;
    for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
      if (v->is_contact() && v.get() != player_ && !v->is_aircraft() && v->plane_count() == 0) {
        out.push_back(v.get());
      }
    }
    return out;
  };
  const auto sighting = [&](const Vehicle& v) {
    const fx::Vec3 p = v.position();
    return player_sighting_check(player_lookout_state(*player_, player_crew_), sensors.sighting(conditions_), &grid_,
                                 p.x, v.visual_height(), p.z, v.exposure());
  };
  const std::int32_t sj_range = sj_fitted_range(conditions_.options);
  const std::int32_t shallow = player_->sub().draft_ft * -48;
  const std::int32_t edge = 0x164130;
  const std::int32_t map_x = world_.grid.width * world_.cell_size() - edge;
  const std::int32_t map_z = world_.grid.height * world_.cell_size() - edge;
  const bool check_land = h.special_mission == 0;

  std::int32_t origin_x = record.x;
  std::int32_t origin_z = record.y;
  std::int32_t radius = 0;
  if (h.special_mission == 0) {
    bool radar_target = false;
    for (Vehicle* v : candidates()) {
      const SightingCheck c = sighting(*v);
      if (v->radar_cross_section(player_->position(), player_->heading()) > 0) {
        radar_target = true;
      }
      if (radius < c.range.range_yd) {
        origin_x = v->position().x;
        origin_z = v->position().z;
        radius = c.range.range_yd;
      }
    }
    radius = radius * 17 / 20;
    std::int32_t radar_radius = 0;
    if (radar_target) {
      radar_radius = sj_range * 8 / 10;
      radius = std::max(radius, radar_radius);
    }
    if (radar_radius == 0) {
      std::int32_t farthest = 0;
      for (Vehicle* v : candidates()) {
        const fx::Vec3 p = v->position();
        farthest = std::max(
            farthest, static_cast<std::int32_t>(fx::approx_distance(p.x - origin_x, p.z - origin_z) / kUnitsPerYard));
      }
      radius += farthest;
    }
  } else {
    radius = random.random_int(0x2260) + 0x8980;
  }

  // The point at `bearing` and `radius` yd from the origin, and whether it
  // is off the map or (outside special missions) hidden from the scenario
  // position by land.
  const auto point_at = [&](std::int32_t bearing, std::int32_t& x, std::int32_t& z) {
    const fx::SinCos sc = fx::angle_sincos(Angle10(bearing));
    x = origin_x + fx::mul(radius, sc.sin) * kUnitsPerYard;
    z = origin_z + fx::mul(radius, sc.cos) * kUnitsPerYard;
    return x < edge || x > map_x || z < edge || z > map_z ||
           (check_land && los_blocked_by_land(grid_, {x, z}, {record.x, record.y}, -1));
  };
  // angle_add (0x5447c) wraps upward only.
  const auto wrap_up = [](std::int32_t a) { return fx::angle_add(Angle10(a), Angle10(0)).tenths(); };

  std::int32_t bearing = start_bearing;
  std::int32_t x = record.x;
  std::int32_t z = record.y;
  bool done = true;
  std::int32_t step = 0;
  for (std::int32_t attempt = 0;; ++attempt) {
    const std::int32_t first = bearing;
    step = -(radius / 100);
    if (std::abs(step) < 10) {
      step = -10;
    }
    std::int32_t tries = 0;
    std::int32_t height = 0;
    bool bad = false;
    do {
      bad = point_at(bearing, x, z);
      height = floor_.height_at(x, z);
      const std::int32_t next = wrap_up(bearing + 50);
      if (next == first) {
        radius += step;
      }
      ++tries;
      bearing = next;
    } while ((height >= shallow || bad) && tries < 2000);
    if (tries > 1 && tries < 2000) {
      // The open arc runs on from the first good bearing (+5 deg) a degree
      // at a time; its middle is taken unless that is shallow. The arc
      // ends where the original's loop ends (a bad point); the bound only
      // guards a ring without one.
      const std::int32_t arc_start = bearing;
      std::int32_t end = arc_start;
      std::int32_t ex = 0;
      std::int32_t ez = 0;
      for (;; end += 10) {
        bool end_bad = point_at(wrap_up(end), ex, ez);
        const std::int32_t end_height = floor_.height_at(ex, ez);
        if (end + 10 == arc_start || end - arc_start >= 3600) {
          end_bad = true;
        }
        if (end_height >= shallow || end_bad) {
          break;
        }
      }
      bearing = wrap_up(arc_start + (end - arc_start) / 2);
      point_at(bearing, x, z);
      if (floor_.height_at(x, z) >= shallow) {
        // As the original: back to the arc's start bearing, with the point
        // of the arc's end.
        bearing = arc_start;
        x = ex;
        z = ez;
      }
    }
    if (tries == 2000) {
      x = record.x;
      z = record.y;
    }
    MotionState& m = player_->motion();
    m.position.x = x;
    m.position.z = z;
    m.pos_x = static_cast<std::int64_t>(x) << 16;
    m.pos_z = static_cast<std::int64_t>(z) << 16;

    done = true;
    if (h.special_mission == 0 && sj_range == 0) {
      // Retry closer to the nearest target out of sight, unless one is seen
      // or within 1000 yd (submarines aside).
      done = false;
      std::uint32_t nearest = 1000000;
      bool nearest_sub = false;
      for (Vehicle* v : candidates()) {
        const SightingCheck c = sighting(*v);
        if (c.distance_yd >= 1) {
          done = true;
          break;
        }
        const fx::Vec3 p = v->position();
        const std::uint32_t yd = fx::approx_distance(x - p.x, z - p.z) / kUnitsPerYard;
        if (!v->is_submarine() && yd <= 999) {
          done = true;
          break;
        }
        if (yd < nearest) {
          radius = c.range.range_yd * 19 / 20;
          nearest_sub = v->is_submarine();
          origin_x = p.x;
          origin_z = p.z;
          nearest = yd;
        }
      }
      if (!done) {
        radius += step * attempt;
        if (!nearest_sub && radius < 1000) {
          radius = 1000;
        }
      }
    }
    if (done || attempt >= 20) {
      break;
    }
  }
  sensors.fog_pct = saved_fog;
  if (h.special_mission != 0) {
    // A special mission's boat faces the scenario position (motion_init).
    MotionState& m = player_->motion();
    m.heading = fx::heading_from_delta(record.x - x, record.y - z);
    m.ordered_heading = m.heading;
    const std::int32_t power = m.power;  // motion_init scales it again
    motion_init(m, floor_);
    m.power = power;
  }
}

void Mission::SkyLight::on_sky_changed() {
  // world_set_sun / world_set_moon end with world_apply_daylight_palette(0),
  // which sets the light 0x170f84 the lookouts read with the fog 0x170f88.
  Mission& m = mission_;
  m.light_model_.set_sky(m.sky_bodies());
  m.light_model_.update_light(m.environment_);
  SensorEnvironment& env = m.sim_->sensors().environment();
  env.light_pct = m.light_model_.light_percent();
  env.fog_pct = m.sky_->fog_pct();
}

TargetSensing Mission::sense(const Vehicle& target) const {
  const fx::Vec3 own = player_->position();
  const fx::Vec3 p = target.position();
  return {.spotted = target.sighted(),
          .visual_height = target.visual_height(),
          .track_time = Fixed16::from_raw(target.track_time()),
          .line_of_sight = !los_blocked_by_land(grid_, {own.x, own.z}, {p.x, p.z}, -1)};
}

SkyBodies Mission::sky_bodies() const {
  const SkyState& s = sky_->state();
  return {s.sun_azimuth, s.sun_elevation, s.moon_azimuth, s.moon_elevation, s.moon_phase};
}

void Mission::load_world(const Install& install, MissionSetup& setup, WatcomRandom& random) {
  // init_load_world (0x4f4bd): the environment is chosen before the map is
  // loaded.
  const ScenarioHeader& h = scenario_.header;
  // A saved game reloads its saved files instead (`init_load_world(0)`).
  const MissionSetup::SavedWorld* saved = setup.saved_world ? &*setup.saved_world : nullptr;
  environment_file_ = saved != nullptr ? saved->environment
                                       : select_environment(static_cast<std::int32_t>(h.weather),
                                                            setup.previous_map_lat_south, h.month, random);
  const std::string world_path = "DATA/" + h.world;
  world_ = parse_world(install.read(world_path), world_path);
  const Fixed16 lat_north = world_.bounds ? world_.bounds->lat_north : Fixed16();
  sea_picture_ = saved != nullptr ? saved->sea_picture : select_sea_picture(lat_north, h.month, random);
  const std::string env_path = "DATA/" + environment_file_;
  environment_ = parse_environment(install.read(env_path), env_path);
  const std::string objects_path = "DATA/" + (saved != nullptr ? saved->objects : std::string("WORLD-1.OBS"));
  objects_ = parse_objects(install.read(objects_path), objects_path);
}

void Mission::instantiate_units(SimContext ctx) {
  const ScenarioHeader& h = scenario_.header;
  difficulty_rating_ += static_cast<std::int32_t>(h.sea_state) * 20 + h.fog * 30 + (h.gradient == 0 ? 30 : 0);
  if (!scenario_.player_sub) {
    throw DataError("scenario has no player submarine");
  }
  spawn_player(*scenario_.player_sub);
  for (const ShipRecord& r : scenario_.ships) {
    spawn_ship(r);
  }
  for (const EnemySubRecord& r : scenario_.enemy_subs) {
    spawn_enemy_sub(r);
  }
  for (const AircraftRecord& r : scenario_.aircraft) {
    spawn_aircraft(r);
  }
  for (const BomberGroupRecord& r : scenario_.bomber_groups) {
    spawn_bomber_group(r, ctx.random);
  }
  for (const PhotoReconRecord& r : scenario_.photo_recons) {
    spawn_photo_recon(r);
  }
  difficulty_rating_ = std::min(difficulty_rating_, 1000);
}

void Mission::form_scenario_group() {
  // convoy_group_from_scenario (0x2f1e2): a materialized group whose size
  // and warship type are unknown, a task force with a warship among the
  // units, else a convoy with a merchant, a patrol with an escort, or
  // submarines.
  auto g = std::make_unique<ContactGroup>();
  g->kind = ContactGroupKind::kRoute;
  g->materialized = true;
  g->size = -1;
  g->warship_type = -1;
  std::int32_t warships = 0;
  std::int32_t merchants = 0;
  std::int32_t escorts = 0;
  std::int32_t submarines = 0;
  bool hulls = false;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    if (v.get() == player_ || v->removed()) {
      continue;
    }
    const auto* ship = dynamic_cast<const Ship*>(v.get());
    if (ship != nullptr || dynamic_cast<const EnemySub*>(v.get()) != nullptr ||
        dynamic_cast<const Aircraft*>(v.get()) != nullptr) {
      // scen_instantiate_units hands the group to these (+0xc8).
      g->vehicles.push_back(v.get());
      hulls = hulls || !v->is_aircraft();
    }
    if (!v->is_contact() || v->is_aircraft()) {
      continue;
    }
    const ShipRole role = ship != nullptr ? ship->ai().cls.role : ShipRole::kSubmarine;
    warships += ship != nullptr && role == ShipRole::kWarship ? 1 : 0;
    merchants += ship != nullptr && role == ShipRole::kMerchant ? 1 : 0;
    escorts += v->is_escort() || v->escorted_group() != 0 ? 1 : 0;
    submarines += v->is_submarine() ? 1 : 0;
  }
  ContactGroupHook*& hook = sim_->sensors().environment().contact_groups;
  if (!hulls) {
    scenario_group_.reset();
    if (hook == &scenario_hook_) {
      hook = nullptr;
    }
    return;
  }
  if (hook == nullptr) {
    hook = &scenario_hook_;
  }
  // With none of these (small craft only) the original leaves the kind
  // unset (-1), which the chart draws as a convoy of unknown size.
  g->route_kind = warships != 0     ? RouteGroupKind::kTaskForce
                  : merchants != 0  ? RouteGroupKind::kConvoy
                  : escorts != 0    ? RouteGroupKind::kPatrol
                  : submarines != 0 ? RouteGroupKind::kSubmarines
                                    : RouteGroupKind::kConvoy;
  scenario_group_ = std::move(g);
}

void Mission::ScenarioGroupHook::forget(const Vehicle& removed) {
  if (ContactGroup* g = mission_.scenario_group_.get()) {
    std::erase(g->vehicles, &removed);
  }
}

void Mission::spawn_player(const PlayerSubRecord& record) {
  // scen_spawn_player_sub (0x53c62)
  player_class_ = tables_.find_sub(record.sub_class);
  if (player_class_ == nullptr) {
    throw DataError("SUBS.DAT: no class " + record.sub_class);
  }
  if (!boat_fit_.empty()) {
    fitted_class_ = fitted_sub_class(*player_class_, boat_fit_);
    player_class_ = &*fitted_class_;
  }
  const SubClassParams params = sub_class_params(*player_class_, conditions_.rules);
  // Raises the start depth until the keel clears the terrain (mixing feet
  // and world units, as the original); it always rises by at least 1 ft.
  const std::int32_t terrain = floor_.height_at(record.x, record.y);
  std::int32_t depth = record.depth;
  do {
    --depth;
  } while (-params.draft_ft - depth <= terrain);
  player_name_ = record.name;
  player_crew_ = static_cast<std::int32_t>(record.crew);
  combat_ = std::make_unique<PlayerCombat>(tables_, *player_class_);
  combat_->crew = player_crew_;
  sim_->set_player_combat(combat_.get());
  sim_->sensors().environment().crew = player_crew_;
  difficulty_rating_ += (3 - player_crew_) * 40;
  const Angle10 heading(record.heading * 10);
  auto sub = std::make_unique<PlayerSub>(params, fx::Vec3{record.x, depth * -kUnitsPerFoot, record.y}, heading,
                                         record.engine_order, player_fuel_);
  sub->motion().ordered_heading = heading;
  sub->motion().steering = true;
  player_ = &sim_->add(std::move(sub));
}

void Mission::spawn_ship(const ShipRecord& record) {
  // scen_spawn_ship (0x53d81)
  const ShipClass* ship_class = tables_.find_ship(record.ship_class);
  if (ship_class == nullptr) {
    throw DataError("SHIPS.DAT: no class " + record.ship_class);
  }
  // Left out in water shallower than 1.5 drafts: the original compares feet
  // with world units (16 to the foot), so it only refuses about 3 ft of
  // water or less; in world units with the rules' corrections (not in the
  // original).
  const std::int32_t unit = conditions_.rules.fix_obvious_errors ? kUnitsPerFoot : 1;
  if (static_cast<std::int32_t>(ship_class->draft_ft) * -3 / 2 * unit < floor_.height_at(record.x, record.y)) {
    ++skipped_ships_;
    return;
  }
  const Angle10 heading(record.heading * 10);
  auto ship = std::make_unique<Ship>(ship_motion_params(*ship_class), fx::Vec3{record.x, 0, record.y}, heading,
                                     Fixed16::from_int(record.speed), static_cast<ShipOrder>(record.orders));
  ship->motion().ordered_heading = heading;
  ship->motion().steering = true;
  ShipSetup& setup = ship->setup();
  setup.ship_class = ship_class;
  setup.crew = static_cast<std::int32_t>(record.crew);
  const auto hull = static_cast<std::int32_t>(ship_class->hull_strength);
  setup.damage = hull * record.damage_pct / 100;
  setup.hit_points = hull - setup.damage;
  ship->set_alert(0);
  ship->set_alert(record.alert);
  if (record.orders == ShipOrders::kPatrol) {
    ship->set_alert(-1);
  }

  const std::int32_t crew_factor = setup.crew + 1;
  switch (ship_class->role) {
    case ShipRole::kMerchant:
      difficulty_rating_ += 5;
      if (ship_class->main_gun_count != 0) {
        difficulty_rating_ += crew_factor * 5;
      }
      break;
    case ShipRole::kWarship:
      difficulty_rating_ += static_cast<std::int32_t>(ship_class->displacement_tons) / 500 + crew_factor * 40;
      break;
    case ShipRole::kEscort:
    case ShipRole::kSubmarine:
      difficulty_rating_ += crew_factor * 40 + (record.alert != 0 ? 20 : 0);
      break;
    case ShipRole::kSmallCraft:
      break;
  }

  // All 25 points are copied: a stale x of -1 after the last one makes the
  // route loop.
  std::copy(record.waypoints.x.begin(), record.waypoints.x.end(), setup.route.x.begin());
  std::copy(record.waypoints.y.begin(), record.waypoints.y.end(), setup.route.z.begin());
  setup.route.count = static_cast<std::int32_t>(record.waypoints.size());
  setup.route_index = 1;
  setup.convoy_group = record.convoy_group;
  setup.zigzag = record.zigzag;
  if (record.orders == ShipOrders::kConvoyLeader && record.zigzag != 0) {
    difficulty_rating_ += 50;
  }
  sim_->add(std::move(ship));
}

void Mission::spawn_enemy_sub(const EnemySubRecord& record) {
  // scen_spawn_enemy_sub (0x54033)
  const ShipClass* ship_class = tables_.find_ship(record.sub_class);
  if (ship_class == nullptr) {
    throw DataError("SHIPS.DAT: no class " + record.sub_class);
  }
  auto sub = std::make_unique<EnemySub>(fx::Vec3{record.x, record.depth * -kUnitsPerFoot, record.y},
                                       Angle10(record.heading * 10), Fixed16::from_int(record.speed),
                                       static_cast<ShipOrder>(record.orders), record.side);
  EnemySubSetup& setup = sub->setup();
  setup.ship_class = ship_class;
  setup.torpedo = tables_.find_torpedo("TYPE95-1");
  setup.crew = static_cast<std::int32_t>(record.crew);
  const auto hull = static_cast<std::int32_t>(ship_class->hull_strength);
  setup.damage = hull * record.damage_pct / 100;
  setup.hit_points = hull - setup.damage;
  setup.target_x = record.target_x;
  setup.target_z = record.target_y;
  sub->set_alert(record.alert);
  difficulty_rating_ += (setup.crew + 1) * 40 + (record.alert != 0 ? 20 : 0);
  std::copy(record.waypoints.x.begin(), record.waypoints.x.end(), setup.route.x.begin());
  std::copy(record.waypoints.y.begin(), record.waypoints.y.end(), setup.route.z.begin());
  setup.route.count = static_cast<std::int32_t>(record.waypoints.size());
  setup.route_index = 1;
  sim_->add(std::move(sub));
}

void Mission::spawn_aircraft(const AircraftRecord& record) {
  // scen_spawn_aircraft (0x5425b)
  const AircraftType* type = tables_.find_aircraft(record.aircraft_type);
  if (type == nullptr) {
    throw DataError("AIRCRAFT.DAT: no type " + record.aircraft_type);
  }
  auto aircraft = std::make_unique<Aircraft>();
  AircraftSetup& setup = aircraft->setup();
  setup.type = type;
  setup.crew = static_cast<std::int32_t>(record.crew);
  setup.orbit_x = record.x;
  setup.orbit_z = record.y;
  setup.orbit_radius = record.orbit_radius;
  setup.mission = static_cast<AircraftMissionCode>(record.mission);
  setup.strafes = record.strafes != 0;
  setup.bomb_load = record.bomb_load;
  setup.sorties = record.relaunch == 0 ? 1 : 10000;
  std::copy(record.waypoints.x.begin(), record.waypoints.x.end(), setup.route.x.begin());
  std::copy(record.waypoints.y.begin(), record.waypoints.y.end(), setup.route.z.begin());
  setup.route.count = static_cast<std::int32_t>(record.waypoints.size());
  setup.route_index = 0;
  setup.escort_group = record.escort_group;
  const Aircraft& added = sim_->add(std::move(aircraft));
  difficulty_rating_ += (added.setup().crew + 1) * 10;
}

void Mission::spawn_bomber_group(const BomberGroupRecord& record, WatcomRandom& random) {
  // scen_spawn_bomber_group (0x543a4)
  static constexpr std::array<std::int32_t, 3> kPlanes = {10, 30, 50};  // 0x1030fc
  const auto size = static_cast<std::size_t>(record.size);
  if (size >= kPlanes.size()) {
    throw DataError("bomber group size out of range");
  }
  const std::int32_t n = kPlanes[size];
  const std::int32_t planes = n + random.random_int(n / 2);
  sim_->add(std::make_unique<AirGroup>(air_theatre_, record.base, planes, record.launch_delay_min, record.hold_min));
}

void Mission::spawn_photo_recon(const PhotoReconRecord& record) {
  // scen_spawn_photo_recon (0x5442a)
  sim_->add(std::make_unique<IntelReport>(air_theatre_, IntelReport::Kind::kPhotoRecon, record.base, record.delay_min,
                                          record.duration_min));
}

MissionEvents Mission::simulate_frame(std::int32_t timer_ticks, bool station_opened) {
  if (paused_) {
    return {};
  }
  SensorEnvironment& sensors = sim_->sensors().environment();
  sensors.tdc_target = combat_->tdc.target();
  sensors.gun_target = combat_->deck_gun.target();
  // The sky is seen from where the boat ended the last frame.
  sky_->set_observer(player_->position().x, player_->position().z);
  sim_->step(timer_ticks, station_opened);
  // The crew's contact pass sets the lookouts' range of the TDC and gun
  // targets (0x129a24, 0x11c1d4) the solutions are estimated with.
  const ContactReports& reports = sim_->sensors().reports();
  combat_->tdc.set_sighting_range(reports.tdc_target_range_yd());
  combat_->deck_gun.set_sighting_range(reports.gun_target_range_yd());
  SimContext ctx = sim_->context();
  combat_->update_frame(ctx, *player_);
  air_sorties_.tick(ctx);
  for (FrameTask* task : frame_tasks_) {
    task->tick();
  }
  MissionEvents events = sim_->take_events();
  if (events.game_over && !game_over_) {
    game_over_ = static_cast<GameOverReason>(*events.game_over);
  }
  return events;
}

std::int32_t Mission::finish() {
  // mission_shutdown -> mission_map_end_code (0x508be)
  if ((conditions_.options & option::kModeCareer) == 0 && end_code_ == 0x12) {
    end_code_ = 6;
  }
  return end_code_;
}

std::vector<Vehicle*> Mission::add_units(const Scenario& units) {
  // scen_instantiate_units (0x67400) with no player record, then
  // init_regroup_convoys (0x4fbcf).
  SimContext ctx = sim_->context();
  conditions_.scenario_roll = ctx.random.random_int(100);
  difficulty_rating_ += conditions_.sea_state * 20 + conditions_.fog * 30 + (scenario_.header.gradient == 0 ? 30 : 0);
  const std::size_t first = sim_->vehicles().size();
  for (const ShipRecord& r : units.ships) {
    spawn_ship(r);
  }
  for (const EnemySubRecord& r : units.enemy_subs) {
    spawn_enemy_sub(r);
  }
  for (const AircraftRecord& r : units.aircraft) {
    spawn_aircraft(r);
  }
  for (const BomberGroupRecord& r : units.bomber_groups) {
    spawn_bomber_group(r, ctx.random);
  }
  for (const PhotoReconRecord& r : units.photo_recons) {
    spawn_photo_recon(r);
  }
  difficulty_rating_ = std::min(difficulty_rating_, 1000);
  for (std::int32_t group = 1; group < 9; ++group) {
    convoy_regroup_on_leader(ctx, group);
  }
  std::vector<Vehicle*> added;
  for (std::size_t i = first; i < sim_->vehicles().size(); ++i) {
    added.push_back(sim_->vehicles()[i].get());
  }
  return added;
}

void Mission::add_bomber_group(const BomberGroupRecord& record) { spawn_bomber_group(record, sim_->random()); }

void Mission::add_photo_recon(const PhotoReconRecord& record) { spawn_photo_recon(record); }

void Mission::change_weather(std::int32_t weather, std::int32_t sea_state, std::int32_t thermal_layer_ft,
                             bool reload) {
  conditions_.weather = weather;
  conditions_.sea_state = sea_state;
  conditions_.thermal_layer_ft = thermal_layer_ft;
  MissionConditions& sim_conditions = sim_->conditions();
  sim_conditions.weather = weather;
  sim_conditions.sea_state = sea_state;
  sim_conditions.thermal_layer_ft = thermal_layer_ft;
  scenario_.header.weather = static_cast<Weather>(weather);
  scenario_.header.sea_state = static_cast<SeaState>(sea_state);
  if (reload) {
    // init_select_env_file (0x4f9e2) on the loaded map, then
    // world_load_aenv.
    const Fixed16 lat_south = world_.bounds ? world_.bounds->lat_south : Fixed16();
    environment_file_ = select_environment(weather, lat_south, sim_->clock().month(), sim_->random());
    const std::string env_path = "DATA/" + environment_file_;
    environment_ = parse_environment(install_->read(env_path), env_path);
    sky_light_.on_sky_changed();
  }
}

void Mission::place_player_at(std::int32_t x, std::int32_t z, Angle10 heading) {
  MotionState& m = player_->motion();
  m.position.x = x;
  m.position.z = z;
  m.pos_x = static_cast<std::int64_t>(x) << 16;
  m.pos_z = static_cast<std::int64_t>(z) << 16;
  m.heading = heading;
  m.ordered_heading = heading;
  const std::int32_t power = m.power;  // motion_init scales it again
  motion_init(m, floor_);
  m.power = power;
}

std::vector<EnemySub*> Mission::enemy_subs() const {
  std::vector<EnemySub*> subs;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    if (v->class_id() == VehicleClass::kEnemySub) {
      subs.push_back(static_cast<EnemySub*>(v.get()));
    }
  }
  return subs;
}

std::vector<Aircraft*> Mission::aircraft() const {
  std::vector<Aircraft*> planes;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    if (v->class_id() == VehicleClass::kAircraft) {
      planes.push_back(static_cast<Aircraft*>(v.get()));
    }
  }
  return planes;
}

std::vector<AirGroup*> Mission::bomber_groups() const {
  std::vector<AirGroup*> groups;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    if (v->class_id() == VehicleClass::kBomberGroup) {
      groups.push_back(static_cast<AirGroup*>(v.get()));
    }
  }
  return groups;
}

std::vector<IntelReport*> Mission::photo_recons() const {
  std::vector<IntelReport*> reports;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    auto* report = dynamic_cast<IntelReport*>(v.get());
    if (report != nullptr && report->kind() == IntelReport::Kind::kPhotoRecon) {
      reports.push_back(report);
    }
  }
  return reports;
}

std::vector<Ship*> Mission::ships() const {
  std::vector<Ship*> ships;
  for (const std::unique_ptr<Vehicle>& v : sim_->vehicles()) {
    if (v->class_id() == VehicleClass::kShip) {
      ships.push_back(static_cast<Ship*>(v.get()));
    }
  }
  return ships;
}

}  // namespace sh

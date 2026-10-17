#include "game/patrol.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "assets/campaign_month.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "game/career_requests.h"
#include "game/career_rules.h"
#include "game/vehicle_state.h"
#include "sim/geometry.h"
#include "sim/radar.h"

namespace sh {

namespace {

// Grid values below this are water deep enough to start in.
constexpr std::uint8_t kDeepWater = 0x1d;
// The edge search's neighbour offsets (0x103090 / 0x1030a0) and spiral
// steps (0x1030b0 / 0x1030c0).
constexpr std::array<std::int32_t, 4> kNeighbourColumn = {0, 0, 1, 1};
constexpr std::array<std::int32_t, 4> kNeighbourRow = {1, 0, 0, 1};
constexpr std::array<std::int32_t, 4> kStepColumn = {-1, 0, 1, 0};
constexpr std::array<std::int32_t, 4> kStepRow = {0, 1, 0, -1};
// Edge picks before giving up where the original would loop forever.
constexpr int kMaxEdgeDraws = 100000;

// One hour of 16.16 seconds (0xe100000).
constexpr std::int32_t kHour16 = 3600 << 16;
// The daily chance of a photo recon (`wp_recon_update`).
constexpr std::int32_t kReconPct = 20;
// The crew's fuel report.
constexpr std::int32_t kFuelReport = 0x1c;
// The raids and recons of one patrol.
constexpr std::int32_t kMaxRaids = 2;

// Copies `s` into a NUL-padded char field of `n` bytes, as strcpy does
// into the original's buffers (truncated here).
void put_name(std::span<std::uint8_t> field, std::string_view s) {
  const std::size_t len = std::min(s.size(), field.size() - 1);
  std::memcpy(field.data(), s.data(), len);
  std::fill(field.begin() + static_cast<std::ptrdiff_t>(len), field.end(), std::uint8_t{0});
}

}  // namespace

PatrolPlan build_patrol_scenario(Career& career, const PatrolZone& zone, std::int32_t zone_index,
                                 std::int32_t transit_fuel, std::int32_t transit_hours, const GameTables& tables,
                                 WatcomRandom& random) {
  // career_build_patrol_scenario (0x1c1e2): the unit arrays emptied, the
  // header and player record filled from the career.
  CareerRecord& r = career.record;
  PatrolPlan plan;
  plan.zone = zone;
  plan.zone_index = zone_index;
  Scenario& s = plan.scenario;
  s.version = 7;
  ScenarioHeader& h = s.header;
  // patrol_accept_zone (0x636b8) set the world.
  h.world = zone.world;
  h.month = r.month;
  h.day = r.day;
  h.year = r.year;
  if (r.on_patrol == 0) {
    h.hour = random.random_int(24);
    h.minute = random.random_int(60);
  } else {
    h.hour = r.hour;
    h.minute = r.minute;
  }
  // The equipment bits 0-5 (and the extended ST's bit 8, which only an
  // extended career has); the SJ-1 replaces the SJa.
  std::uint32_t equipment = r.equipment & (0x3fu | static_cast<std::uint32_t>(Equipment::kStRadar));
  if ((equipment & static_cast<std::uint32_t>(Equipment::kSj1Radar)) != 0) {
    equipment &= ~static_cast<std::uint32_t>(Equipment::kSjaRadar);
  }
  h.equipment = equipment;
  h.fog = 0;

  PlayerSubRecord boat;
  boat.sub_class = r.class_name;
  boat.name = r.boat;
  // The crew quality is the commander's rank.
  boat.crew = static_cast<CrewQuality>(r.rank);
  s.player_sub = boat;

  const SubClass* sub = tables.find_sub(r.class_name);
  if (sub == nullptr) {
    throw DataError("SUBS.DAT: no class " + r.class_name);
  }
  // The career's rules (the extended variant's torpedo rules) with the
  // same draws as the original.
  fill_torpedo_load(h, *sub, tables, random, career.rules.value_or(MissionRules{}),
                    static_cast<SubCommand>(career.summary.current_command));
  // The outbound transit's fuel, give or take 2.5 %.
  const std::int32_t spread = transit_fuel * 5 / 100;
  const std::int32_t spent = transit_fuel + spread / 2 - random.random_int(spread);
  plan.start_fuel = static_cast<std::int32_t>(sub->fuel_capacity) - spent;
  plan.transit_fuel = transit_fuel;
  plan.return_hours = transit_hours;
  plan.home_port = r.home_port;

  // The career's requests (not in the original), which draw nothing.
  if (requests_enabled(career)) {
    plan.boat_fit = career_boat_fit(career);
    const std::optional<TorpedoLoad> chosen =
        requests_for_boat(career.requests, r) ? career.requests.torpedo_load : std::nullopt;
    if (chosen && r.on_patrol == 0) {
      const MissionRules rules = *career.rules;
      const TorpedoLoad generated = generated_torpedo_load(h, *sub, tables, rules);
      const std::vector<TorpedoOffer> offers =
          torpedo_offers(*sub, tables, h.year, h.month, static_cast<SubCommand>(career.summary.current_command),
                         rules, in_good_standing(r), generated);
      if (torpedo_load_allowed(*chosen, *sub, offers)) {
        plan.torpedo_load = chosen;
      }
    }
    career.requests.torpedo_load.reset();
    career.requests.patrol_zone.reset();
  }

  r.location = zone.name.substr(0, 19);
  r.status = CareerStatus::kOnPatrol;
  if (r.on_patrol == 0) {
    r.raids_this_patrol = 0;
  }
  return plan;
}

PatrolPlan depart_on_patrol(Career& career, PatrolAssignment& assignment, const GameTables& tables,
                            WatcomRandom& random) {
  accept_patrol_zone(career, assignment);
  CareerRecord& r = career.record;
  if (r.on_patrol == 0) {
    r.transit_hours = 0;
    if (assignment.transit_hours != 0) {
      const std::int32_t hours = jitter_transit_hours(assignment.transit_hours, random);
      career_advance_hours(r, hours);
      r.transit_hours = hours;
    }
  }
  return build_patrol_scenario(career, assignment.zone, assignment.zone_index, assignment.transit_fuel,
                               assignment.transit_hours, tables, random);
}

std::optional<PatrolPlan> preview_patrol(const Career& career, const CampaignData& data, const GameTables& tables) {
  Career copy = career;
  copy.requests.torpedo_load.reset();
  WatcomRandom random;
  random.srand(copy.record.seed);
  std::optional<PatrolAssignment> assigned = assign_requested_patrol(copy, data, random);
  if (!assigned) {
    return std::nullopt;
  }
  return depart_on_patrol(copy, *assigned, tables, random);
}

PlayerStart career_player_start(const ZoneData& zone, std::string_view home_port,
                                const std::array<std::int32_t, 4>& open_edges, const World& world,
                                const SeaFloor& floor, WatcomRandom& random, const MissionRules& rules) {
  // bases_place_player_start (0x16be4), new patrol.
  for (std::size_t i = 0; i < zone.bases.size(); ++i) {
    const BaseRecord& b = zone.bases[i];
    if (b.has(BaseFlag::kActive) && equals_ignore_case(b.name, home_port)) {
      return {b.harbor.x, b.harbor.z, zone.owners.at(i).harbor_heading};
    }
  }
  const IndexedRaster& grid = world.grid;
  const std::int32_t w = grid.width;
  const std::int32_t h = grid.height;
  const auto deep = [&grid](std::int32_t column, std::int32_t row) { return grid.at(column, row) < kDeepWater; };
  // Non-zero: the edge is not drawn. Each edge drawn sets the flag of the
  // edge it checked for water, the wrong one for the two row edges (so a
  // wet south edge also opens the north one), as the original; each its own
  // with `rules.fix_obvious_errors` (not in the original).
  const std::size_t south_mark = rules.fix_obvious_errors ? 0 : 1;
  const std::size_t north_mark = rules.fix_obvious_errors ? 1 : 0;
  std::array<std::int32_t, 4> excluded{};
  for (std::size_t e = 0; e < 4; ++e) {
    excluded[e] = open_edges[e] == 0 ? 1 : 0;
  }
  const std::int32_t cell = world.cell_size();
  int draws = 0;
  while (true) {
    const std::int32_t margin = random.random_int(30) + 10;
    std::int32_t edge = 0;
    do {
      if (++draws > kMaxEdgeDraws || std::ranges::all_of(excluded, [](std::int32_t x) { return x != 0; })) {
        throw DataError("no map edge to start the patrol from");
      }
      edge = random.random_int(4);
    } while (excluded[static_cast<std::size_t>(edge)] != 0);
    std::int32_t column = 0;
    std::int32_t row = 0;
    bool wet = false;
    switch (edge) {
      case 0:  // the last row (south)
        for (std::int32_t c = 0; c < w && !wet; ++c) {
          wet = deep(c, h - 1);
        }
        excluded[south_mark] = wet ? 0 : -1;
        if (!wet) {
          continue;
        }
        do {
          column = margin + random.random_int(w - margin * 2);
          row = h - margin;
        } while (!deep(column, row));
        break;
      case 1:  // the first row (north)
        for (std::int32_t c = 0; c < w && !wet; ++c) {
          wet = deep(c, 0);
        }
        excluded[north_mark] = wet ? 0 : -1;
        if (!wet) {
          continue;
        }
        do {
          column = margin + random.random_int(w - margin * 2);
          row = margin;
        } while (!deep(column, row));
        break;
      case 2:  // the first column (west)
        for (std::int32_t r = 0; r < h && !wet; ++r) {
          wet = deep(0, r);
        }
        excluded[2] = wet ? 0 : -1;
        if (!wet) {
          continue;
        }
        do {
          row = margin + random.random_int(h - margin * 2);
          column = margin;
        } while (!deep(column, row));
        break;
      default:  // the last column (east)
        for (std::int32_t r = 0; r < h && !wet; ++r) {
          wet = deep(w - 1, r);
        }
        excluded[3] = wet ? 0 : -1;
        if (!wet) {
          continue;
        }
        do {
          row = margin + random.random_int(h - margin * 2);
          column = w - margin;
        } while (!deep(column, row));
        break;
    }
    // Walks a square spiral until the 2x2 cells there are all deep.
    std::int32_t run = 1;
    std::size_t dir = 0;
    std::int32_t steps = 0;
    std::int32_t turns = 0;
    while (true) {
      bool clear = true;
      for (std::size_t k = 0; k < 4; ++k) {
        if (!deep(column + kNeighbourColumn[k], row + kNeighbourRow[k])) {
          clear = false;
          break;
        }
      }
      if (clear) {
        break;
      }
      column = std::max(std::min(column + kStepColumn[dir], w - 10), 10);
      row = std::max(std::min(row + kStepRow[dir], h - 10), 10);
      if (run <= ++steps) {
        dir = (dir + 1) % 4;
        steps = 0;
        ++turns;
      }
      if (turns > 1) {
        turns = 0;
        ++run;
      }
    }
    const std::int32_t x = column * cell + cell / 2;
    const std::int32_t z = cell * h - (row * cell + cell / 2);
    if (floor.height_at(x, z) <= -0x500) {
      return {x, z, fx::heading_from_delta(w / 2 * cell - x, h / 2 * cell - z)};
    }
  }
}

ReturnFuel resolve_return_fuel(std::int32_t end_code, std::int32_t fuel, std::int32_t transit_fuel,
                               std::int32_t return_hours, std::int32_t x, std::int32_t z, const ZoneData& zone,
                               WatcomRandom& random, const MissionRules& rules) {
  // wp_resolve_return_fuel (0x98279)
  ReturnFuel out{end_code, fuel, return_hours};
  if (end_code != patrol_end::kReturn && end_code != patrol_end::kAbortPatrol) {
    return out;
  }
  // The side of the nearest base's station.
  std::uint32_t best = 100000;
  BaseSide side = BaseSide::kAllied;
  for (std::size_t i = 0; i < zone.bases.size(); ++i) {
    const BasePoint& p = zone.bases[i].station;
    const std::uint32_t d = fx::approx_distance(x - p.x, z - p.z) / 0x14a00;
    if (d < best) {
      side = zone.owners.at(i).side;
      best = d;
    }
  }
  const bool allied = side == BaseSide::kAllied;
  if (end_code == patrol_end::kAbortPatrol || fuel < 101) {
    if (!allied) {
      out.end_code = patrol_end::kInterned;
    } else if (end_code == patrol_end::kReturn) {
      out.fuel = 0;
      out.end_code = patrol_end::kTowed;
    } else {
      out.end_code = patrol_end::kRecalled;
    }
    return out;
  }
  if (fuel < transit_fuel) {
    const std::int32_t reserve = transit_fuel - transit_fuel / 3;
    if (fuel <= reserve) {
      // The original's test of the fuel left against the reserve divides
      // the fuel it has just emptied (0): off an Allied base the boat is
      // always interned. With the rules' corrections (not in the original)
      // the fuel is read first, and the boat is interned only at 20 % of
      // the reserve or less, else towed.
      const bool interned = !allied && (!rules.fix_obvious_errors || fuel * 100 / reserve <= 20);
      out.fuel = 0;
      out.end_code = interned ? patrol_end::kInterned : patrol_end::kTowed;
      return out;
    }
    out.return_hours += (200 - (fuel - reserve) * 200 / (transit_fuel - reserve)) * return_hours / 100;
  } else {
    out.fuel = fuel - transit_fuel;
    if (out.fuel > 0) {
      return out;
    }
  }
  out.fuel = random.random_int(50) + 1;
  return out;
}

bool fuel_report_due(std::int32_t fuel, std::int32_t last_fuel, std::int32_t transit_fuel) {
  // wp_patrol_tick (0x97b0e): 110 %, 100 % .. 0 % of the transit fuel.
  std::int32_t level = transit_fuel * 110;
  for (std::int32_t pct = 110; pct >= 0; pct -= 10) {
    if (fuel < 1) {
      return false;
    }
    if (level / 100 < last_fuel && fuel <= level / 100) {
      return true;
    }
    level -= transit_fuel * 10;
  }
  return false;
}

// ------------------------------------------------------------ the spawner

std::int32_t MissionContactSpawner::free_convoy_group() const {
  const auto used = [this](std::int32_t group) {
    for (const std::unique_ptr<Vehicle>& v : mission_.sim().vehicles()) {
      if (!v->removed() && (v->convoy_group_by_role(false) == group || v->convoy_group_by_role(true) == group ||
                            v->escorted_group() == group)) {
        return true;
      }
    }
    return false;
  };
  std::int32_t group = 1;
  while (used(group)) {
    ++group;
  }
  return group;
}

std::vector<Vehicle*> MissionContactSpawner::spawn(const Scenario& units) { return mission_.add_units(units); }

void MissionContactSpawner::remove(Vehicle& vehicle) {
  // The simulation drops the TDC and deck-gun targets and the other
  // pointers to it before deleting it.
  vehicle.remove();
}

bool MissionContactSpawner::contains(const Vehicle* vehicle) const {
  return std::ranges::any_of(mission_.sim().vehicles(), [vehicle](const std::unique_ptr<Vehicle>& v) {
    return v.get() == vehicle && !v->removed();
  });
}

void MissionContactSpawner::limit_compression(std::int32_t max) {
  GameClock& clock = mission_.sim().clock();
  if (clock.compression_cap() > max) {
    clock.set_compression_cap(max);
  }
}

// ------------------------------------------------------------- the patrol

Patrol::Patrol(const Install& install, const GameTables& tables, Career& career, PatrolPlan plan)
    : install_(install),
      tables_(tables),
      career_(career),
      plan_(std::move(plan)),
      zone_(ZoneData::load(install, plan_.zone)) {}

Patrol::~Patrol() = default;

std::unique_ptr<Mission> Patrol::create_mission(MissionSetup setup) {
  setup.bases = zone_.bases;
  setup.air_edges = plan_.zone.air_start_edges;
  setup.player_fuel = plan_.start_fuel;
  setup.patrol = this;
  // A career saved on patrol (wp_load_patrol_data): its world files and
  // sea picture, and the state lists for place_player.
  const CareerRecord& r = career_.record;
  if (r.on_patrol != 0 && !career_.patrol_state.empty()) {
    ByteReader reader(career_.patrol_state, "ROSTER patrol state");
    resume_ = SimStateFile::parse(reader, career_.patrol_state_version);
    constexpr std::size_t kName = 0x90;
    const auto name = [&r](std::size_t i) {
      const auto begin = r.world_files.begin() + static_cast<std::ptrdiff_t>(i * kName);
      return std::string(begin, std::find(begin, begin + static_cast<std::ptrdiff_t>(kName), std::uint8_t{0}));
    };
    setup.saved_world = MissionSetup::SavedWorld{name(1), name(2), r.sea_picture};
  }
  // The career's requests (not in the original): the fit the patrol was
  // saved with, and the chosen load of a new patrol.
  setup.boat_fit = resume_ && !resume_->boat_extension.empty() ? decode_boat_fit(resume_->boat_extension)
                                                               : plan_.boat_fit;
  if (!resume_) {
    setup.torpedo_load = plan_.torpedo_load;
  }
  std::unique_ptr<Mission> mission = Mission::create(install_, tables_, plan_.scenario, std::move(setup));
  mission->add_frame_task(*this);
  return mission;
}

MonthWeather Patrol::month_weather(std::int32_t month) const {
  const auto m = static_cast<std::size_t>(month);
  return {plan_.zone.fog_pct.at(m), plan_.zone.weather_pct.at(m)};
}

PatrolWeatherState Patrol::init_weather(WatcomRandom& random) {
  // wp_init_weather (0x97bfc), then wp_air_raid_reset / wp_recon_reset.
  const ScenarioHeader& h = plan_.scenario.header;
  // On patrol, the weather saved in the career.
  const CareerRecord& r = career_.record;
  const PatrolWeatherState saved{r.weather_days_left, r.weather, r.sea_state, r.daily_fog, r.thermal_layer_ft};
  weather_.init(month_weather(h.month), h.day, random, resume_ ? &saved : nullptr);
  raid_ = AirMission{.last_day = h.day};
  recon_ = AirMission{.last_day = h.day};
  return weather_.state();
}

void Patrol::init_contacts(Mission& mission) {
  // wp_load_patrol_data (0x979a0) after init_load_world: the world files
  // into the career, contacts_init, bases_update_daily(0).
  mission_ = &mission;
  mission.set_transit_fuel(plan_.transit_fuel);
  mission.sky().set_daily_fog(weather_.daily_fog());
  save_world_state();
  spawner_ = std::make_unique<MissionContactSpawner>(mission);
  groups_ = std::make_unique<ContactGroups>(tables_, zone_, mission.world(), *spawner_, false,
                                            &mission.sim().conditions().rules);
  WatcomRandom& random = mission.sim().random();
  const GameClock& clock = mission.sim().clock();
  groups_->init(clock, random);
  groups_->update_bases(clock, random, false);
  mission.sim().sensors().environment().contact_groups = groups_.get();
  last_fuel_ = 1000000;
}

void Patrol::place_player(Mission& mission) {
  if (resume_) {
    // bases_place_player_start on patrol: the contact groups and routes
    // deleted, career_load_roster_lists, srand(the career's seed); the fog
    // density comes from the career (wp_load_patrol_data). A save with the
    // reimplementation's extension is restored whole at the end of the
    // set-up instead (`start`), after the convoys' regrouping and the
    // clock's initialisation that would change it.
    if (resume_->sim_extension.empty()) {
      restore_sim_state(mission, *resume_, SimStateParts{.patrol = this});
      const CareerRecord& r = career_.record;
      StateIo::restore_original_globals(mission, career_.summary.fog_pct, r.daily_fog, r.thermal_layer_ft, r.seed);
    }
    return;
  }
  const PlayerStart start = career_player_start(zone_, plan_.home_port, plan_.zone.start_edges, mission.world(),
                                                mission.floor(), mission.sim().random(),
                                                mission.sim().conditions().rules);
  mission.place_player_at(start.x, start.z, start.heading);
}

void Patrol::start(Mission& mission) {
  // init_simulation's bases_update_daily(-1); a patrol restored from the
  // reimplementation's saves is restored now, as it was.
  if (resume_ && !resume_->sim_extension.empty()) {
    restore_sim_state(mission, *resume_, SimStateParts{.patrol = this});
    return;
  }
  if (resume_ && mission.sim().conditions().rules.fix_obvious_errors) {
    // The original's save: its fog, which clock_init has cleared, with the
    // rules' corrections (not in the original).
    mission.sky().resume_fog(mission.sim().clock(), career_.summary.fog_pct);
  }
  groups_->update_bases(mission.sim().clock(), mission.sim().random(), true);
}

void Patrol::tick() {
  Mission& m = *mission_;
  const GameClock& clock = m.sim().clock();
  WatcomRandom& random = m.sim().random();
  // mission_main_loop's bases_update_daily(0).
  groups_->update_bases(clock, random, false);

  // wp_patrol_tick (0x97b0e)
  CareerRecord& r = career_.record;
  r.day = clock.day();
  r.month = clock.month();
  r.year = clock.year();
  r.hour = clock.hour();
  r.minute = clock.minute();
  r.second = clock.second();
  r.on_patrol = -1;
  if (date_after_war_end(clock.day(), clock.month(), clock.year())) {
    m.set_end_code(patrol_end::kWarOver);
    return;
  }
  update_weather();
  groups_->update(clock, contact_view(), random);
  update_air(raid_, false);
  update_air(recon_, true);
  const std::int32_t fuel = m.player().fuel();
  if (fuel_report_due(fuel, last_fuel_, plan_.transit_fuel)) {
    m.sim().context().crew_message(kFuelReport);
  }
  last_fuel_ = fuel;
}

ContactView Patrol::contact_view() const {
  const Mission& m = *mission_;
  const PlayerSub& player = m.player();
  const SensorEnvironment& sensors = m.sim().sensors().environment();
  const std::uint32_t options = m.sim().conditions().options;
  ContactView v;
  v.player_x = player.position().x;
  v.player_z = player.position().z;
  v.observer_height_ft = player.periscope_raised() ? player.sub().periscope_height_ft : player.sub().bridge_height_ft;
  v.weather = m.sim().conditions().weather;
  v.options = options;
  v.frame_hundredths = m.sim().clock().frame_ticks();
  v.fog_pct = sensors.fog_pct;
  v.light_pct = sensors.light_pct;
  // sj_radar_range (0x66c39): the set on and its system working.
  const bool sj_on = m.sim().sensors().radar().sj().mode != SjMode::kOff;
  const auto& systems = player.systems();
  v.sj_range_yd = sj_on && !systems[static_cast<std::size_t>(PlayerSystem::kSjRadar)].impaired()
                      ? sj_fitted_range(options)
                      : 0;
  // player_radio_available (0x5e73f)
  v.radio_available = player.depth_ft() <= player.sub().radar_depth_ft &&
                      !systems[static_cast<std::size_t>(PlayerSystem::kRadio)].impaired();
  return v;
}

void Patrol::update_weather() {
  // wp_update_weather (0x97d25)
  Mission& m = *mission_;
  const GameClock& clock = m.sim().clock();
  career_.summary.fog_pct = m.fog_pct();
  const bool shows_world = shows_world_ && shows_world_();
  const MissionConditions& c = m.sim().conditions();
  weather_.update(month_weather(clock.month()), clock.day(), shows_world, m.fog_pct(), m.sim().random(), [&] {
    // init_select_env_file and world_load_aenv with the new weather, the
    // world files copied into the career.
    m.change_weather(weather_.state().weather, c.sea_state, c.thermal_layer_ft, true);
    save_world_state();
  });
  const PatrolWeatherState& s = weather_.state();
  m.change_weather(s.weather, s.sea_state, s.thermal_layer_ft, false);
  m.sky().set_daily_fog(weather_.daily_fog());
  const PatrolWeatherState& saved = weather_.saved();
  CareerRecord& r = career_.record;
  r.weather_days_left = saved.days_left;
  r.weather = saved.weather;
  r.sea_state = saved.sea_state;
  r.daily_fog = saved.fog;
  r.thermal_layer_ft = saved.thermal_layer_ft;
}

void Patrol::update_air(AirMission& a, bool recon) {
  // wp_air_raid_update (0x97f18) / wp_recon_update (0x980b7)
  Mission& m = *mission_;
  const GameClock& clock = m.sim().clock();
  WatcomRandom& random = m.sim().random();
  const bool activity = m.sim().sensors().reports().group_activity();
  CareerRecord& r = career_.record;
  a.hundredths += clock.frame_ticks();
  if (a.hundredths > 4) {
    const auto dt = static_cast<std::int32_t>((static_cast<std::int64_t>(a.hundredths) << 16) / 100);
    a.hundredths = 0;
    if (a.due) {
      a.timer -= dt;
      if (a.timer < 1) {
        a.timer = kHour16;
        if (a.hours != 0) {
          --a.hours;
        } else if (!activity) {
          a.due = false;
          const BasePick pick = pick_random_japanese_base(zone_, recon, random);
          if (recon) {
            const std::int32_t duration = random.random_int(900) + 0x708 + m.sim().conditions().sea_state * 600;
            if (pick.base >= 0) {
              m.add_photo_recon({.base = pick.base, .delay_min = 0, .duration_min = duration});
            }
          } else {
            const std::int32_t hold = random.random_int(600) + 0x5a0;
            if (pick.base >= 0) {
              m.add_bomber_group({.base = pick.base,
                                  .size = static_cast<BomberGroupSize>(pick.size),
                                  .launch_delay_min = 0,
                                  .hold_min = hold});
            }
          }
          ++r.raids_this_patrol;
        }
      }
    }
  }
  if (a.last_day != clock.day() && !activity) {
    a.last_day = clock.day();
    if (r.raids_this_patrol < kMaxRaids) {
      const std::int32_t pct =
          recon ? kReconPct
                : plan_.zone.air_raid_pct.at(static_cast<std::size_t>(
                      campaign_month_index(clock.year() + kCalendarBaseYear, clock.month() + 1)));
      a.due = random.random_int(100) < pct;
      if (a.due) {
        a.hours = random.random_int(24);
        a.timer = random.random_int(3600) << 16;
      }
    }
  }
}

void Patrol::save_world_state() {
  // The world-files block 0x104e80 (five char[0x90] names) and the sea
  // picture 0x105150 copied into the career.
  const Mission& m = *mission_;
  CareerRecord& r = career_.record;
  constexpr std::size_t kName = 0x90;
  const std::array<std::string, 5> names = {"SEA.PCC", m.environment_file(), "WORLD-1.OBS", m.scenario().header.world,
                                            "TTEXT.PCC"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    put_name(std::span(r.world_files).subspan(i * kName, kName), names[i]);
  }
  r.sea_picture = m.sea_picture();
}

PatrolEnd Patrol::finish(Mission& mission) {
  // wp_free_patrol_data (0x97a79): contacts_shutdown, then the fuel home.
  mission.sim().sensors().environment().contact_groups = nullptr;
  groups_.reset();
  const PlayerSub& player = mission.player();
  const ReturnFuel f = resolve_return_fuel(mission.end_code(), player.fuel(), plan_.transit_fuel, plan_.return_hours,
                                           player.position().x, player.position().z, zone_, mission.sim().random(),
                                           mission.sim().conditions().rules);
  mission.set_end_code(f.end_code);
  return {f.end_code, f.fuel, f.return_hours};
}

}  // namespace sh

#include "assets/scenario.h"

#include <algorithm>
#include <variant>

#include "assets/scn_file.h"

namespace sh {
namespace {

// Appends the game's view of one record of the lossless file to `s`.
struct RecordToScenario {
  Scenario& s;

  // Each tag-1 record overwrites the previous one, as in the game.
  void operator()(const ScnPlayerSub& p) const {
    s.player_sub = PlayerSubRecord{
        .x = p.x,
        .y = p.y,
        .depth = p.depth,
        .heading = p.heading,
        .engine_order = p.engine_order,
        .preserved_14 = p.preserved_14,
        .sub_class = scn_text(p.sub_class),
        .name = scn_text(p.name),
        .crew = p.crew,
    };
  }
  void operator()(const ScnShip& r) const {
    s.ships.push_back(ShipRecord{
        .x = r.x,
        .y = r.y,
        .heading = r.heading,
        .speed = r.speed,
        .orders = r.orders,
        .crew = r.crew,
        .damage_pct = r.damage_pct,
        .ship_class = scn_text(r.ship_class),
        .name = scn_text(r.name),
        .waypoints = r.waypoints,
        .convoy_group = r.convoy_group,
        .zigzag = r.zigzag,
        .alert = r.alert,
    });
  }
  void operator()(const ScnAircraft& a) const {
    s.aircraft.push_back(AircraftRecord{
        .mission = a.mission,
        .crew = a.crew,
        .aircraft_type = scn_text(a.aircraft_type),
        .x = a.x,
        .y = a.y,
        .orbit_radius = a.orbit_radius,
        .strafes = a.strafes,
        .bomb_load = a.bomb_load,
        .waypoints = a.waypoints,
        .escort_group = a.escort_group,
        .relaunch = a.relaunch,
    });
  }
  void operator()(const ScnEnemySub& e) const {
    s.enemy_subs.push_back(EnemySubRecord{
        .x = e.x,
        .y = e.y,
        .depth = e.depth,
        .heading = e.heading,
        .speed = e.speed,
        .orders = e.orders,
        .crew = e.crew,
        .damage_pct = e.damage_pct,
        .sub_class = scn_text(e.sub_class),
        .waypoints = e.waypoints,
        .target_x = e.target_x,
        .target_y = e.target_y,
        .alert = e.alert,
        .side = e.side,
    });
  }
  void operator()(const BomberGroupRecord& b) const { s.bomber_groups.push_back(b); }
  void operator()(const PhotoReconRecord& p) const { s.photo_recons.push_back(p); }
};

Scenario to_scenario(const ScnFile& f) {
  Scenario s;
  s.version = f.version;
  s.briefing = scn_text(f.briefing);
  ScenarioHeader& h = s.header;
  h.special_mission = f.special_mission;
  h.world = scn_text(f.world);
  h.weather = f.weather;
  h.sea_state = f.sea_state;
  h.fog = f.fog;
  h.gradient = f.gradient;
  h.month = f.month;
  h.day = f.day;
  h.year = f.year;
  h.hour = f.hour;
  h.minute = f.minute;
  h.end_on_lost_contact = f.end_on_lost_contact;
  h.randomize = f.randomize;
  h.saved_time = f.random_seed;
  // The stored (encoded) bytes up to the first zero byte.
  h.password.assign(f.password.begin(), std::find(f.password.begin(), f.password.end(), std::uint8_t{0}));
  h.required_memory = f.required_memory;
  h.tube_torpedoes = f.tube_torpedoes;
  h.reloads = f.reloads;
  h.equipment = f.equipment;
  for (const ScnRecord& record : f.records) {
    std::visit(RecordToScenario{s}, record);
  }
  return s;
}

}  // namespace

std::size_t Waypoints::size() const {
  return static_cast<std::size_t>(std::clamp<std::int32_t>(count, 0, static_cast<std::int32_t>(kMaxScenarioWaypoints)));
}

Scenario parse_scenario(std::span<const std::uint8_t> data, std::string_view context) {
  return to_scenario(read_scn_file(data, context));
}

}  // namespace sh

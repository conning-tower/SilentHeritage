// The scenarios of the synthetic install (SCEN/*.SCN and CUSTOM/*.SCN):
// a boat and a target group in the open water of a synthetic map, of a
// kind varying with the scenario (convoys, task forces, aircraft, an enemy
// submarine, a lone target), at various dates, hours and weathers.
#include <cmath>
#include <format>
#include <stdexcept>

#include "assets/scn_file.h"
#include "synthetic/generators.h"

namespace sh::test::synth {

namespace {

// Units are world units (48 to the yard).
constexpr std::int32_t kYard = 48;

template <std::size_t N>
ScnChars<N> chars(std::string_view text) {
  if (text.size() >= N) {
    throw std::invalid_argument("scenario text longer than its field");
  }
  ScnChars<N> out{};
  std::ranges::copy(text, out.begin());
  return out;
}

const std::string& pick(const std::vector<std::string>& names, std::size_t i) {
  if (names.empty()) {
    throw std::runtime_error("synthetic scenario: no class of a needed kind");
  }
  return names[i % names.size()];
}

ScnFile scenario(const Model& model, std::size_t number, bool custom) {
  const Model::MapInfo& map = model.maps.at(number % model.maps.size());
  if (map.deep_points.size() < 4) {
    throw std::runtime_error("synthetic scenario: too little open water on " + map.world);
  }
  SynthRandom random(name_hash(std::format("scenario{}", number)));
  ScnFile f;
  const int kind = static_cast<int>(number % 6);
  constexpr const char* kKinds[] = {"Convoy", "Task force", "Air attack", "Enemy submarine", "Lone target",
                                    "Escorted tanker"};
  f.briefing = chars<kScnBriefingSize>(std::format(
      "{} {}\nA synthetic scenario of the test install: the boat meets a {} on map {}.\n\nResults:\nNone yet.",
      kKinds[kind], number + 1, std::string(kKinds[kind]), map.world));
  f.world = chars<13>(map.world);
  f.weather = static_cast<Weather>(number % 4);
  f.sea_state = static_cast<SeaState>(number % 3);
  f.fog = number % 7 == 3 ? 1 : 0;
  f.month = static_cast<std::int32_t>(number * 5 % 12);
  f.day = 1 + static_cast<std::int32_t>(number * 7 % 28);
  f.year = 12 + static_cast<std::int32_t>(number % 4);  // 1942..1945
  f.hour = static_cast<std::int32_t>((6 + number * 5) % 24);
  f.minute = static_cast<std::int32_t>(number * 13 % 60);
  f.end_on_lost_contact = 0;
  f.randomize = 0;
  f.random_seed = static_cast<std::int32_t>(1000 + number);
  f.required_memory = 0;
  f.tube_torpedoes.fill(1);
  f.reloads[0] = {1, 2, 8};
  f.equipment = f.year >= 13 ? 0x3u : 0x1u;

  // The boat: a class in service at the date.
  const std::int32_t full_year = 1930 + f.year;
  std::string boat;
  for (const auto& [name, year] : model.boats) {
    if (static_cast<std::int32_t>(year) <= full_year) {
      boat = name;
    }
  }
  const auto& start = map.deep_points[number % map.deep_points.size()];
  const auto& target = map.deep_points[(number + 1 + number / 7) % map.deep_points.size()];
  // The group 6000 yd from the boat towards the target point.
  const double dx = target[0] - start[0];
  const double dy = target[1] - start[1];
  const double len = std::max(1.0, std::hypot(dx, dy));
  const auto gx = static_cast<std::int32_t>(start[0] + dx / len * 6000 * kYard);
  const auto gy = static_cast<std::int32_t>(start[1] + dy / len * 6000 * kYard);
  const auto heading_to_group = static_cast<std::int32_t>(std::lround(std::atan2(dx, dy) * 180 / 3.14159265358979));
  ScnPlayerSub player;
  player.x = start[0];
  player.y = start[1];
  player.depth = 15;
  player.heading = (heading_to_group + 360) % 360;
  player.engine_order = 3;
  player.sub_class = chars<10>(boat);
  player.name = chars<31>(std::format("Synthetic {}", number + 1));
  player.crew = static_cast<CrewQuality>(number % 3);
  f.records.emplace_back(player);

  const std::int32_t course = (heading_to_group + 90 + static_cast<std::int32_t>(number % 3) * 45) % 360;
  const double cr = course * 3.14159265358979 / 180;
  const auto ship = [&](const std::string& cls, std::int32_t ox, std::int32_t oy, ShipOrders orders) {
    ScnShip s;
    s.x = gx + ox;
    s.y = gy + oy;
    s.heading = course;
    s.speed = 8 + static_cast<std::int32_t>(number % 5);
    s.orders = orders;
    s.crew = static_cast<CrewQuality>((number + 1) % 3);
    s.ship_class = chars<25>(cls);
    s.name = chars<31>(std::format("{} {}", cls.substr(0, 20), f.records.size()));
    s.convoy_group = orders == ShipOrders::kNone ? 0 : 1;
    if (orders == ShipOrders::kConvoyLeader || orders == ShipOrders::kFollowPath) {
      // Straight on along the course for 40000 yd.
      for (int k = 1; k <= 3; ++k) {
        s.waypoints.x[static_cast<std::size_t>(k - 1)] = s.x + static_cast<std::int32_t>(std::sin(cr) * 13000 * k * kYard);
        s.waypoints.y[static_cast<std::size_t>(k - 1)] = s.y + static_cast<std::int32_t>(std::cos(cr) * 13000 * k * kYard);
      }
      s.waypoints.count = 3;
    }
    f.records.emplace_back(s);
  };
  const std::int32_t spacing = 600 * kYard;
  switch (kind) {
    case 0:  // a convoy of four with two escorts
      ship(pick(model.merchants, number), 0, 0, ShipOrders::kConvoyLeader);
      for (int i = 1; i < 4; ++i) {
        ship(pick(model.merchants, number + static_cast<std::size_t>(i)), (i % 2) * spacing, -(i / 2 + i % 2) * spacing,
             ShipOrders::kConvoyMember);
      }
      ship(pick(model.escorts, number), 2 * spacing, spacing, ShipOrders::kEscort);
      ship(pick(model.escorts, number + 1), -2 * spacing, -spacing, ShipOrders::kEscort);
      break;
    case 1:  // a warship and two escorts
      ship(pick(model.warships, number), 0, 0, ShipOrders::kConvoyLeader);
      ship(pick(model.escorts, number), spacing, spacing, ShipOrders::kEscort);
      ship(pick(model.escorts, number + 2), -spacing, spacing, ShipOrders::kEscort);
      break;
    case 2: {  // a merchant under an air patrol
      ship(pick(model.merchants, number), 0, 0, ShipOrders::kFollowPath);
      ScnAircraft a;
      a.mission = AircraftMission::kSearchOrbit;
      a.crew = CrewQuality::kVeteran;
      a.aircraft_type = chars<25>(pick(model.aircraft, number));
      a.x = gx;
      a.y = gy;
      a.orbit_radius = 4000 * kYard;
      a.strafes = 1;
      a.bomb_load = 1;
      f.records.emplace_back(a);
      break;
    }
    case 3: {  // an enemy submarine on patrol
      ScnEnemySub e;
      e.x = gx;
      e.y = gy;
      e.depth = 0;
      e.heading = course;
      e.speed = 10;
      e.orders = ShipOrders::kPatrol;
      e.crew = CrewQuality::kGreen;
      e.sub_class = chars<25>(pick(model.enemy_subs, number));
      f.records.emplace_back(e);
      break;
    }
    case 4:  // a lone ship
      ship(pick(model.merchants, number + 3), 0, 0, ShipOrders::kFollowPath);
      break;
    default:  // a merchant with one escort
      ship(pick(model.merchants, number + 5), 0, 0, ShipOrders::kConvoyLeader);
      ship(pick(model.escorts, number + 3), spacing, 0, ShipOrders::kEscort);
      break;
  }
  if (custom) {
    // The custom one: a target dead in the water close by.
    f.records.resize(1);
    ship(pick(model.merchants, 0), 0, 0, ShipOrders::kNone);
    std::get<ScnShip>(f.records.back()).speed = 0;
  }
  return f;
}

}  // namespace

void generate_scenarios(Context& context) {
  std::size_t number = 0;
  for (const FileManifest* m : context.entries("SCEN/")) {
    context.files[m->path] = write_scn_file(scenario(context.model, number++, false));
  }
  for (const FileManifest* m : context.entries("CUSTOM/")) {
    context.files[m->path] = write_scn_file(scenario(context.model, number++, true));
  }
}

}  // namespace sh::test::synth

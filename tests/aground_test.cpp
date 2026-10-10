// Running aground: the Run Aground option's damage
// (`player_apply_system_damage(4)` from `player_update_helm_and_dive`) and the
// grinding sfx 0x20 played while the screen shakes and the boat touches bottom.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game_data.h"
#include "sim/player_combat.h"
#include "sim/player_damage.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"

namespace sh {

namespace {

constexpr std::int32_t kOrigin = 2000000;
constexpr std::int32_t kMsgRanAground = 0x1e;
constexpr std::int32_t kMsgDamaged = 0x1f;
constexpr std::int32_t kSfxGrinding = 0x20;
// One player update (pass 1 takes up to 0.2 s per sub-step).
const Fixed16 kOneStep = Fixed16::from_raw(0x2000);

// A sea floor the test raises under the boat.
class ShoalFloor : public SeaFloor {
public:
  std::int32_t height_at(std::int32_t, std::int32_t) const override {
    return height;
  }
  std::int32_t height = -3000 * kUnitsPerFoot;
};

// The player's Gato at flank speed on the surface over `floor`.
struct Boat {
  Boat(const GameTables &t, const ShoalFloor &floor, std::uint32_t options,
       std::uint32_t seed)
      : gato(*t.find_sub("GATO")), sim(conditions(options), floor, seed),
        combat(t, gato) {
    sim.clock().init(13, 6, 1, 12, 0);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(
        sub_class_params(gato), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 5));
  }

  static MissionConditions conditions(std::uint32_t options) {
    MissionConditions env;
    env.options = options;
    return env;
  }

  const SubClass &gato;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub *player = nullptr;
};

std::int32_t count_sfx(const MissionEvents &e, std::int32_t id) {
  return static_cast<std::int32_t>(
      std::count_if(e.sfx.begin(), e.sfx.end(),
                    [id](const MissionEvents::Sfx &s) { return s.id == id; }));
}

std::int32_t count_messages(const MissionEvents &e, std::int32_t id) {
  return static_cast<std::int32_t>(
      std::count_if(e.crew_messages.begin(), e.crew_messages.end(),
                    [id](const CrewReport &r) { return r.id == id; }));
}

// The damage `player_run_aground` (0x60511) deals at `speed` (tenths of a
// knot) with crew 0, drawn from `r` in the original's order: the system
// (0x10cb70: sound heads, steering gear, fuel tanks, bow tubes), the base
// severity, then per system or bow tube its severity and repair time.
struct Expected {
  std::size_t system = 0;
  std::array<SystemState, 24> systems{};
  std::array<TubeDamage, 10> tubes{};
};

Expected expected_grounding(WatcomRandom r, std::int32_t speed,
                            std::int32_t bow_tubes) {
  constexpr std::array<std::size_t, 4> kTable = {2, 6, 13, 11};
  auto repair = [&r](std::int32_t v) {
    return std::max(v * 3 * 108 + 1800 - r.random_int(3600), 60);
  };
  Expected e;
  e.system = kTable[static_cast<std::size_t>(r.random_int(4))];
  const std::int32_t base = speed * 70 / 220 + 10 - r.random_int(20);
  if (e.system == 11) {
    const std::int32_t chance = base / 50 + 1;
    for (std::int32_t t = 0; t < bow_tubes; ++t) {
      if (r.random_int(chance) == 0) {
        continue;
      }
      const std::int32_t v = std::clamp(10 - r.random_int(20) + base, 0, 100);
      TubeDamage &d = e.tubes[static_cast<std::size_t>(t)];
      d.flags =
          static_cast<std::uint16_t>(TubeDamage::kOuterDoor | (v > 85 ? 2 : 1));
      d.severity = v;
      d.repair_time = repair(v);
    }
    return e;
  }
  std::int32_t v = std::clamp(10 - r.random_int(20) + base, 0, 100);
  if (e.system == 13) {
    v = std::min(v, 85); // The fuel tanks are damaged, never destroyed.
  }
  SystemState &s = e.systems[e.system];
  s.repair_time = repair(v);
  s.flags = v > 85 ? 2 : 1;
  s.severity = v;
  return e;
}

} // namespace

TEST_CASE(
    "With Run Aground on, touching bottom damages the boat as the original",
    "[aground]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  bool tube_case = false;
  bool system_case = false;
  for (std::uint32_t seed = 1; seed <= 12; ++seed) {
    CAPTURE(seed);
    ShoalFloor floor;
    Boat b(tables, floor, option::kRunAground, seed);
    b.sim.simulate(Fixed16::from_int(5));
    b.sim.take_events();
    const std::int32_t bow_tubes = b.combat.tubes.bow_count();

    floor.height = -5 * kUnitsPerFoot;
    const WatcomRandom before = b.sim.random();
    b.sim.simulate(kOneStep);
    const std::int32_t speed = b.player->motion().speed_tenths;
    REQUIRE(speed > 19);
    const MissionEvents events = b.sim.take_events();
    const Expected want = expected_grounding(before, speed, bow_tubes);

    REQUIRE(!events.crew_messages.empty());
    CHECK(events.crew_messages.front().id == kMsgRanAground);
    CHECK(events.screen_shake);
    CHECK_FALSE(events.ran_aground);
    CHECK(b.player->telegraph() == PlayerSub::kTelegraphStop);
    for (std::size_t s = 0; s < 24; ++s) {
      CAPTURE(s);
      CHECK(b.player->systems()[s].flags == want.systems[s].flags);
      CHECK(b.player->systems()[s].severity == want.systems[s].severity);
      CHECK(b.player->systems()[s].repair_time == want.systems[s].repair_time);
    }
    std::int32_t damaged_tubes = 0;
    for (std::int32_t t = 0; t < bow_tubes; ++t) {
      const TubeDamage &d = b.combat.tubes.damage(static_cast<std::size_t>(t));
      const TubeDamage &w = want.tubes[static_cast<std::size_t>(t)];
      CHECK(d.flags == w.flags);
      CHECK(d.severity == w.severity);
      CHECK(d.repair_time == w.repair_time);
      damaged_tubes += w.flags != 0 ? 1 : 0;
    }
    if (want.system == 11) {
      tube_case = true;
      CHECK(count_messages(events, kMsgDamaged) == damaged_tubes);
    } else {
      system_case = true;
      REQUIRE(count_messages(events, kMsgDamaged) == 1);
      CHECK(events.crew_messages[1].text == system_info()[want.system].name);
    }
    // Grounded only once: staying on the bottom does no more damage.
    b.sim.simulate(Fixed16::from_int(2));
    const MissionEvents after = b.sim.take_events();
    CHECK(count_messages(after, kMsgRanAground) == 0);
    CHECK(count_messages(after, kMsgDamaged) == 0);
  }
  CHECK(tube_case);
  CHECK(system_case);
}

TEST_CASE("With Run Aground off, touching bottom does no damage", "[aground]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  ShoalFloor floor;
  Boat b(tables, floor, 0, 3);
  b.sim.simulate(Fixed16::from_int(5));
  b.sim.take_events();
  floor.height = -5 * kUnitsPerFoot;
  b.sim.simulate(kOneStep);
  const MissionEvents events = b.sim.take_events();
  CHECK(events.ran_aground);
  CHECK(count_messages(events, kMsgRanAground) == 0);
  CHECK(count_messages(events, kMsgDamaged) == 0);
  CHECK(count_sfx(events, kSfxGrinding) == 1);
  for (const SystemState &s : b.player->systems()) {
    CHECK(s.flags == 0);
  }
  for (std::int32_t t = 0; t < b.combat.tubes.count(); ++t) {
    CHECK(b.combat.tubes.damage(static_cast<std::size_t>(t)).flags == 0);
  }
}

TEST_CASE("The boat grinds on the bottom while the screen shakes",
          "[aground]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  ShoalFloor floor;
  Boat b(tables, floor, option::kRunAground, 5);

  // Afloat, a running shake plays nothing.
  b.sim.conditions().screen_shaking = true;
  b.sim.simulate(kOneStep);
  CHECK(count_sfx(b.sim.take_events(), kSfxGrinding) == 0);
  b.sim.conditions().screen_shaking = false;
  b.sim.simulate(Fixed16::from_int(5));
  b.sim.take_events();

  // The grounding starts the shake: the grinding plays at once and every
  // following step of the frame (1 s: five player steps).
  floor.height = -5 * kUnitsPerFoot;
  b.sim.simulate(Fixed16::from_int(1));
  const MissionEvents grounded = b.sim.take_events();
  CHECK(grounded.screen_shake);
  CHECK(count_sfx(grounded, kSfxGrinding) == 5);

  // Still on the bottom: it stops with the shake and comes back with the
  // next one.
  b.sim.simulate(kOneStep);
  CHECK(count_sfx(b.sim.take_events(), kSfxGrinding) == 0);
  b.sim.conditions().screen_shaking = true;
  b.sim.simulate(kOneStep);
  CHECK(count_sfx(b.sim.take_events(), kSfxGrinding) == 1);
}

} // namespace sh

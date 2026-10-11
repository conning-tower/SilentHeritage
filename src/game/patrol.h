// A career war patrol as a mission (modules campaign/career, campaign/wp
// and campaign/bases): the patrol's scenario (`career_build_patrol_scenario`,
// 0x1c1e2), the career branch of `init_simulation` (`wp_load_patrol_data`,
// `bases_place_player_start`), the per-frame patrol work
// (`bases_update_daily`, `wp_patrol_tick`: the career clock, the war's end,
// the weather, the strategic contact groups, the air raids and photo
// recons, the fuel reports) and the end of the patrol
// (`wp_free_patrol_data`, `wp_resolve_return_fuel`).
//
//   PatrolPlan plan = build_patrol_scenario(career, zone, zone_index, fuel, hours, tables, random);
//   Patrol patrol(install, tables, career, std::move(plan));
//   std::unique_ptr<Mission> mission = patrol.create_mission(setup);  // career options word
//   ... run the mission's frames (patrol.set_shows_world(...)) ...
//   mission->finish();
//   PatrolEnd end = patrol.finish(*mission);
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "assets/scenario.h"
#include "core/fixed.h"
#include "core/install.h"
#include "core/random.h"
#include "game/career.h"
#include "game/frontend_results.h"
#include "game/mission.h"
#include "game/patrol_contacts.h"
#include "game/saves_options.h"
#include "game/scengen.h"
#include "game/sim_state.h"
#include "sim/weather.h"

namespace sh {

// End codes of a patrol (`0x11a890`) besides the game overs.
namespace patrol_end {
inline constexpr std::int32_t kInterned = 6;      // out of fuel or returned far from an Allied base: captured
inline constexpr std::int32_t kRecalled = 7;      // 0x12 resolved at an Allied base
inline constexpr std::int32_t kTowed = 8;         // out of fuel near an Allied base
inline constexpr std::int32_t kWarOver = 9;       // the clock passed 15 August 1945
inline constexpr std::int32_t kReturn = 5;        // the patrol ended at sea (fuel checked)
inline constexpr std::int32_t kAbortPatrol = 0x12;
}  // namespace patrol_end

// The patrol's scenario and the values the original keeps in globals
// for the end of the patrol.
struct PatrolPlan {
  Scenario scenario;
  PatrolZone zone;              // 0x11b808
  std::int32_t zone_index = 0;  // PZ.DAT index
  // The start fuel (0x126060): the class's tank less the outbound transit.
  std::int32_t start_fuel = 0;
  std::int32_t transit_fuel = 0;  // 0x1260b0: one transit's fuel
  std::int32_t return_hours = 0;  // 0x1260b4: the return transit's hours
  std::string home_port;          // 0x1260ac: the career's home port
};

// `career_build_patrol_scenario` (0x1c1e2) for `career` in patrol zone
// `zone` (PZ.DAT index `zone_index`) with the transit fuel 0x125070 and
// hours 0x125074 of `patrol_compute_transit`: the boat of the career
// (class, name, crew = rank, equipment), the career's date with a random
// start time, the torpedo load, the start fuel (the transit fuel ±2.5 %
// spent). Sets the career's location to the zone and its status to on
// patrol, clearing the patrol's raid count. Throws `DataError` when the
// boat's class or a torpedo is missing.
PatrolPlan build_patrol_scenario(Career& career, const PatrolZone& zone, std::int32_t zone_index,
                                 std::int32_t transit_fuel, std::int32_t transit_hours, const GameTables& tables,
                                 WatcomRandom& random);

// A start position of the boat.
struct PlayerStart {
  std::int32_t x = 0;
  std::int32_t z = 0;
  Angle10 heading;
};

// `bases_place_player_start` (0x16be4) for a new patrol: the harbour of
// the active base named `home_port`, facing out of it, else a random point
// in deep water near an open map edge (`open_edges`, the zone's
// `start_edges` flags, non-zero = open), walked in a spiral until clear
// and repeated until the terrain there is 0x500 below the surface, facing
// the map's centre. Throws `DataError` when no edge can be used (the
// original loops forever). The original marks a tried south edge as the
// north one and the reverse; `rules.fix_obvious_errors` marks each its own
// (not in the original).
PlayerStart career_player_start(const ZoneData& zone, std::string_view home_port,
                                const std::array<std::int32_t, 4>& open_edges, const World& world,
                                const SeaFloor& floor, WatcomRandom& random, const MissionRules& rules = {});

// What `wp_resolve_return_fuel` (0x98279) leaves.
struct ReturnFuel {
  std::int32_t end_code = 0;
  std::int32_t fuel = 0;          // 0x11ac88
  std::int32_t return_hours = 0;  // 0x1260b4
};

// `wp_resolve_return_fuel` for a patrol ending with `end_code` at world
// point (`x`, `z`) with `fuel` left: only end codes 5 and 0x12 change.
// With fuel for the trip home it is spent (the tank then holds 1..50);
// short of it the return takes longer, up to 3 times as long; too short or
// on 0x12, the boat is towed home (8, 7 on 0x12) when the nearest base is
// Allied, else interned (6). The original interns it off a non-Allied base
// whenever it has 2/3 of the transit fuel or less; with
// `rules.fix_obvious_errors` (not in the original), only at 20 % of that
// reserve or less (about 13 % of the transit fuel), else it is towed.
ReturnFuel resolve_return_fuel(std::int32_t end_code, std::int32_t fuel, std::int32_t transit_fuel,
                               std::int32_t return_hours, std::int32_t x, std::int32_t z, const ZoneData& zone,
                               WatcomRandom& random, const MissionRules& rules = {});

// Returns whether `wp_patrol_tick` posts the fuel report (#0x1c): the
// fuel dropped to or below a multiple of 10 % (110 % down to 0 %) of the
// transit fuel it was above at the previous frame (`last_fuel`).
bool fuel_report_due(std::int32_t fuel, std::int32_t last_fuel, std::int32_t transit_fuel);

// The contact groups' spawner over a running mission.
class MissionContactSpawner final : public ContactSpawner {
 public:
  explicit MissionContactSpawner(Mission& mission) : mission_(mission) {}

  std::int32_t free_convoy_group() const override;
  std::vector<Vehicle*> spawn(const Scenario& units) override;
  void remove(Vehicle& vehicle) override;
  bool contains(const Vehicle* vehicle) const override;
  void limit_compression(std::int32_t max) override;

 private:
  Mission& mission_;
};

// What the career reads after a patrol.
struct PatrolEnd {
  std::int32_t end_code = 0;
  std::int32_t fuel = 0;          // 0x11ac88, after the return-fuel check
  std::int32_t return_hours = 0;  // 0x1260b4
};

// One patrol: the mission's career set-up and its frame task. The career
// record is kept up to date as the original's (date, weather, world files,
// raids, on-patrol flag).
class Patrol final : public PatrolHooks, public FrameTask {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // `install`, `tables` and `career` must outlive the patrol. Loads the
  // zone's routes and bases; throws `DataError` when they are missing.
  Patrol(const Install& install, const GameTables& tables, Career& career, PatrolPlan plan);
  Patrol(const Patrol&) = delete;
  Patrol& operator=(const Patrol&) = delete;
  ~Patrol() override;

  // Builds the patrol's mission from `setup` (its options must have the
  // career mode bit; the bases, air edges, start fuel and hooks are set
  // here) and adds the frame task. The patrol must outlive the mission.
  std::unique_ptr<Mission> create_mission(MissionSetup setup);
  // Sets the query of station slot +0x1c (the current station shows the
  // outside world: the weather waits); false when unset.
  void set_shows_world(std::function<bool()> shows_world) { shows_world_ = std::move(shows_world); }
  // `wp_free_patrol_data` after `Mission::finish`: drops the contact
  // groups and resolves the return fuel.
  PatrolEnd finish(Mission& mission);

  const PatrolPlan& plan() const { return plan_; }
  const ZoneData& zone() const { return zone_; }
  const PatrolWeather& weather() const { return weather_; }
  // The contact groups, once the mission is built.
  ContactGroups* groups() { return groups_.get(); }
  // The career whose patrol this is.
  Career& career() { return career_; }
  // The state lists of a career saved on patrol (its `patrol_state`),
  // which `place_player` restores as `bases_place_player_start` does on
  // patrol (game/sim_state.h); null for a new patrol.
  const SimStateFile* resumed_state() const { return resume_ ? &*resume_ : nullptr; }

  // PatrolHooks.
  PatrolWeatherState init_weather(WatcomRandom& random) override;
  void init_contacts(Mission& mission) override;
  void place_player(Mission& mission) override;
  void start(Mission& mission) override;

  // FrameTask: `bases_update_daily(0)` then `wp_patrol_tick` (0x97b0e).
  void tick() override;

 private:
  // A daily air raid or photo recon (`wp_air_raid_*` / `wp_recon_*`).
  struct AirMission {
    std::int32_t hundredths = 0;  // 0x187220 / 0x187228
    std::int32_t last_day = 0;    // 0x18721c / 0x187224
    bool due = false;             // 0x11bce4 / 0x11bcf0
    std::int32_t timer = 0;       // 0x11bce8 / 0x11bcf4: 16.16 s to the next hour
    std::int32_t hours = 0;       // 0x11bcec / 0x11bcf8
  };

  MonthWeather month_weather(std::int32_t month) const;
  ContactView contact_view() const;
  void update_weather();
  // Runs one of the two air missions; `recon` selects the photo recon.
  void update_air(AirMission& m, bool recon);
  // Copies the weather and the loaded world into the career record.
  void save_world_state();

  const Install& install_;
  const GameTables& tables_;
  Career& career_;
  PatrolPlan plan_;
  ZoneData zone_;
  PatrolWeather weather_;
  Mission* mission_ = nullptr;
  std::unique_ptr<MissionContactSpawner> spawner_;
  std::unique_ptr<ContactGroups> groups_;
  std::function<bool()> shows_world_;
  AirMission raid_;
  AirMission recon_;
  std::int32_t last_fuel_ = 1000000;  // 0x18722c
  std::optional<SimStateFile> resume_;
};

// A patrol to play (`MissionRunner::run_patrol`).
struct PatrolRequest {
  Career* career = nullptr;
  PatrolPlan plan;
  // The options word: the career mode bit and the career's realism bits.
  std::uint32_t options = 0;
  GameOptions game_options;
  // The game's generator as the career leaves it.
  WatcomRandom random;
  // The extended variant's rules of the patrol (sim/context.h), the
  // career's; a patrol resumed from the roster keeps those of its save when
  // it has them.
  MissionRules rules;
};

// What a played patrol returns besides its end code.
struct PatrolResult {
  PatrolEnd end;
  // The outcome for `compute_mission_results` (career mode), with the
  // resolved end code and fuel.
  MissionOutcome outcome;
};

}  // namespace sh

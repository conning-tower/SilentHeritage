// A mission in play, built from a `.SCN` scenario the way `init_simulation`
// (0x4f078) builds a single (historical or custom) mission: the scenario
// header applied to the clock, conditions and torpedo load, the world and
// environment loaded, then every unit instantiated (`scen_instantiate_units`,
// 0x67400) into the simulation, with the random draws in the original order
// so that a given generator state reproduces the original's mission.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/bases.h"
#include "assets/dat_tables.h"
#include "assets/environment.h"
#include "assets/ephemeris.h"
#include "assets/objects.h"
#include "assets/scenario.h"
#include "assets/world.h"
#include "core/fixed.h"
#include "core/install.h"
#include "core/random.h"
#include "game/boat_fit.h"
#include "game/torpedo_load.h"
#include "sim/air_sorties.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/clock.h"
#include "sim/enemy_sub.h"
#include "sim/context.h"
#include "sim/line_of_sight.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/ship_weapons.h"
#include "sim/simulation.h"
#include "sim/sky.h"
#include "sim/weather.h"
#include "sim/terrain.h"
#include "view/world_palette.h"

namespace sh {

// Bits of the options word `0x11a85c` set up by the mission (see also
// sim/context.h).
namespace option {
inline constexpr std::uint32_t kModeCareer = 0x1;        // Bits 0-1: 0 single mission.
inline constexpr std::uint32_t kModeSavedMission = 0x2;
// The scenario's equipment bits 0-5 (`Equipment`) land at bits 16-21.
inline constexpr int kEquipmentShift = 16;
inline constexpr std::uint32_t kEquipmentMask = 0x3fu << kEquipmentShift;
}  // namespace option

// Why the boat was lost (`0x11a894`; the GAME OVER texts at 0x10e9c4).
enum class GameOverReason : std::int32_t {
  kDepthCharge = 1,    // "Killed by a depth charge!"
  kOxygen = 2,         // "Killed by lack of oxygen!"
  kPressureHull = 3,   // "Killed by pressure hull failure!"
  kTorpedo = 4,        // "Killed by a torpedo!"
  kShell = 5,          // "Killed by a shell hit!"
  kBomb = 6,           // "Killed by a bomb!"
  kContactBroken = 7,  // "Contact with enemy broken."
  kAbandonShip = 8,    // "Abandoning ship!"
  kMissionOver = 9,    // "Mission over."
};

// End codes `0x11a890` (0: the mission goes on).
inline constexpr std::int32_t kEndRestart = 2;            // Back to the menu, units freed.
inline constexpr std::int32_t kEndGameOverBase = 10;      // + GameOverReason.

// The inputs of a mission besides its scenario.
struct MissionSetup {
  // The realism bits of the options word (single missions read them from
  // REALISM.SAV); the mode bits must be 0 (career mode with `patrol`),
  // the equipment bits are added.
  std::uint32_t options = 0;
  bool speech = true;
  // GOPTION.SAV's "Auto SJ Radar" / "Auto SD Radar" (0x11a8c8 / 0x11a8cc).
  bool auto_sj = false;
  bool auto_sd = false;
  // The game's generator as `init_simulation` finds it (the front end has
  // drawn from it: listing the scenarios loads each of them).
  WatcomRandom random;
  // The southern latitude of the map loaded by the previous mission, 0
  // after start-up: `init_select_env_file` reads the map bounds before the
  // new map is loaded.
  Fixed16 previous_map_lat_south;
  // The player boat's world position (x, z) at the end of the previous
  // mission, 0 after start-up: the first sky update of `init_simulation`
  // runs before the new boat is placed (its fog draws depend on it).
  std::int32_t previous_player_x = 0;
  std::int32_t previous_player_z = 0;
  // Told of the clock's day and minute events; its `on_new_day` also runs
  // at `clock_init`, where the original calls `clock_update_sky`.
  ClockObserver* clock_observer = nullptr;
  // The base table of the map (`MAP<n>BASE.DAT`, 0x116240) the bomber
  // groups and photo recons refer to, and the zone's air edge flags
  // (`AirTheatre::air_edges`): the generator's zone; left empty for a
  // scenario mission, `create` takes those of the PZ.DAT zone on the
  // scenario's map (`scen_setup_single_mission`), none without one (a
  // scenario with such records then throws).
  std::vector<BaseRecord> bases;
  std::array<std::int32_t, 4> air_edges{1, 1, 1, 1};
  // The boat's fuel at the start (the record's +0x48, 0x126060); -1 fills
  // the tank.
  std::int32_t player_fuel = -1;
  // The extended variant's rules of the mission (the original's by
  // default); a saved mission takes those of its save when it has them.
  MissionRules rules;
  // The career boat's deck gun fit (not in the original): the boat's class
  // becomes `fitted_sub_class` of its SUBS.DAT class (game/boat_fit.h).
  BoatFit boat_fit;
  // The torpedo load the player chose for a career patrol (not in the
  // original; game/career_requests.h): carried as it is in place of the
  // header's, with no Mark 18 replacement nor type limit by date; nothing
  // for the header's.
  std::optional<TorpedoLoad> torpedo_load;
  // A career patrol (`init_simulation` in career mode, options bit 0):
  // the patrol's part of the set-up (game/patrol.h); null for a single
  // mission. Must outlive the build.
  class PatrolHooks* patrol = nullptr;
  // A saved game (MISSn.SAV, options bit 1, or a career resumed on
  // patrol): the environment, objects and sea picture of the saved
  // world-files block, used in place of choosing them (`init_load_world(0)`
  // after `init_load_saved_mission_world` / `wp_load_patrol_data`). The
  // state lists then restore the units (game/sim_state.h).
  struct SavedWorld {
    std::string environment;  // e.g. "env-03.env"
    std::string objects;      // e.g. "WORLD-1.OBS"
    std::int32_t sea_picture = 0;
  };
  std::optional<SavedWorld> saved_world;
};

class Mission;

// The career patrol's steps of `init_simulation` (0x4f078) and
// `wp_load_patrol_data` (0x979a0), called by the mission build in the
// original's order.
class PatrolHooks {
 public:
  virtual ~PatrolHooks() = default;
  // `wp_init_weather` (0x97bfc), after the scenario roll: returns the
  // patrol's weather, sea state, fog flag and thermal layer.
  virtual PatrolWeatherState init_weather(WatcomRandom& random) = 0;
  // After the world and the units (`contacts_init`, `bases_update_daily(0)`).
  virtual void init_contacts(Mission& mission) = 0;
  // `bases_place_player_start` (0x16be4), in place of the single
  // mission's placement.
  virtual void place_player(Mission& mission) = 0;
  // After `clock_init`: `bases_update_daily(-1)`.
  virtual void start(Mission& mission) = 0;
};

// The draws `init_simulation` makes before it loads or generates the
// scenario: the seed the game's generator restarts from after the mission
// (`srand(0x122234)`) and the fog-day density, 60 + rand(30) (`0x11b7d8`).
struct MissionStartDraws {
  std::uint32_t restore_seed = 0;
  std::int32_t fog_density = 0;

  // Makes the two draws from `random`.
  static MissionStartDraws draw(WatcomRandom& random);
};

struct GeneratedMission;

// Work the mission frame runs after the vehicles and the fire control,
// unless paused, after the mission's own `air_sorties_tick`, in the
// original's order: `bases_update_daily`, `wp_patrol_tick`.
class FrameTask {
 public:
  virtual ~FrameTask() = default;
  virtual void tick() = 0;
};

struct ContactGroup;

class Mission {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Builds the mission of `scenario` (`init_simulation` in single-mission
  // mode). `tables` must outlive the mission. Throws `DataError` when a
  // file or a class is missing or the setup's options have mode bits.
  static std::unique_ptr<Mission> create(const Install& install, const GameTables& tables, Scenario scenario,
                                         MissionSetup setup);
  // Builds the encounter mission `generated` (`init_simulation` with the
  // generator in place of `scen_load_ascn`): `draws` were made before the
  // generator ran, `setup.random` is the generator's state after it, and
  // the zone's bases replace `setup.bases`. The boat is then moved out
  // along the generator's start bearing (`init_place_player_sub`).
  static std::unique_ptr<Mission> create(const Install& install, const GameTables& tables,
                                         GeneratedMission generated, MissionStartDraws draws, MissionSetup setup);
  // Parses the `.SCN` at `dos_path` (e.g. "SCEN/HIST01.SCN"), then create().
  static std::unique_ptr<Mission> load(const Install& install, const GameTables& tables, std::string_view dos_path,
                                       MissionSetup setup);

  Mission(const Mission&) = delete;
  Mission& operator=(const Mission&) = delete;
  ~Mission();

  // Runs one frame of the simulation unless paused (`clock_advance`,
  // `veh_simulate_frame` with the sensors seeing the TDC and gun targets,
  // the boat's `tdc_sim_update` and `deckgun_sim_update`, then the frame
  // tasks), `station_opened` on a
  // station's first frame; records a game over the vehicles raised and
  // returns the frame's events.
  MissionEvents simulate_frame(std::int32_t timer_ticks, bool station_opened);
  // Adds work run after the vehicles every simulated frame (not owned).
  void add_frame_task(FrameTask& task) { frame_tasks_.push_back(&task); }

  // Tells the sensors which station is on screen (`0x11a860`: the chart
  // and radar change what they report).
  void set_station(SensorStation station) { sim_->sensors().environment().station = station; }

  // The pause flag `0x129c00` (chart zoom box, in-game menus).
  bool paused() const { return paused_; }
  void set_paused(bool paused) { paused_ = paused; }

  // The game-over trigger `0x11a894`, acted on by the mission loop.
  void trigger_game_over(GameOverReason reason) { game_over_ = reason; }
  std::optional<GameOverReason> game_over() const { return game_over_; }
  // The end code `0x11a890`; the loop stops once it is non-zero.
  std::int32_t end_code() const { return end_code_; }
  void set_end_code(std::int32_t code) { end_code_ = code; }
  // Ends the mission (`mission_shutdown`): outside a career, end code 0x12
  // becomes 6. Returns the final end code.
  std::int32_t finish();
  // The seed the game's generator restarts from after the mission
  // (`srand(0x122234)`): the first value `init_simulation` drew.
  std::uint32_t restore_seed() const { return restore_seed_; }

  // The scenario and data.
  const Scenario& scenario() const { return scenario_; }
  const GameTables& tables() const { return tables_; }
  const World& world() const { return world_; }
  const SeaFloor& floor() const { return floor_; }
  // The `.ENV` picked by weather, season and latitude, e.g. "ENV-03.ENV".
  const std::string& environment_file() const { return environment_file_; }
  const Environment& environment() const { return environment_; }
  const std::vector<MapObject>& objects() const { return objects_; }

  // The simulation and its units.
  Simulation& sim() { return *sim_; }
  const Simulation& sim() const { return *sim_; }
  PlayerSub& player() { return *player_; }
  const PlayerSub& player() const { return *player_; }
  // The boat's class, with the career boat's fit (`MissionSetup::boat_fit`).
  const SubClass& player_class() const { return *player_class_; }
  const BoatFit& boat_fit() const { return boat_fit_; }
  // The boat's weapons and damage (`Simulation::player_combat`), set up
  // with the crew, the torpedo load and the TDC's type table.
  PlayerCombat& player_combat() { return *combat_; }
  const PlayerCombat& player_combat() const { return *combat_; }
  // The crew's view of `target` for the fire control (`PlayerCombat::sensing`):
  // its sighted flag (slot +0x80), visual height (+0x8c), tracking time
  // (+0x84) and whether land hides it from the boat (`los_blocked_by_land`
  // with height -1).
  TargetSensing sense(const Vehicle& target) const;
  // The boat's name (0x11ac08), e.g. "(SS-305) Skate", and crew quality
  // (0x11ac2b, 0 green .. 2 elite).
  const std::string& player_name() const { return player_name_; }
  std::int32_t player_crew() const { return player_crew_; }
  // Returns the ships in the simulation, in spawn order.
  std::vector<Ship*> ships() const;
  // Returns the enemy submarines and the aircraft in the simulation, in
  // spawn order.
  std::vector<EnemySub*> enemy_subs() const;
  std::vector<Aircraft*> aircraft() const;
  // The aircraft take-offs, run first among the frame tasks.
  const AirSorties& air_sorties() const { return air_sorties_; }
  // Returns the bomber groups and photo-recon reports in the simulation.
  std::vector<AirGroup*> bomber_groups() const;
  std::vector<IntelReport*> photo_recons() const;
  // The bases and landmarks the air strikes use.
  const AirTheatre& air_theatre() const { return air_theatre_; }
  // The ships left out because the water at their position is too shallow.
  std::int32_t skipped_ships() const { return skipped_ships_; }

  TorpedoLoad& torpedoes() { return torpedoes_; }
  const TorpedoLoad& torpedoes() const { return torpedoes_; }
  const TorpedoTypes& torpedo_types() const { return torpedo_types_; }
  // The torpedo types carried at the start of the mission (WEAPONS.DAT
  // ids, increasing; not in the original): the status page's boxes of the
  // extended variant.
  const std::vector<std::uint32_t>& start_torpedo_types() const { return start_torpedo_types_; }

  // Scenario state.
  // The difficulty rating `0x125ff0` (0..1000, shown / 10).
  std::int32_t difficulty_rating() const { return difficulty_rating_; }
  // A career patrol's fuel for one transit (`0x1260b0`, set by the patrol;
  // 0 in a single mission), the gauges' second fuel needle.
  std::int32_t transit_fuel() const { return transit_fuel_; }
  void set_transit_fuel(std::int32_t gallons) { transit_fuel_ = gallons; }
  // The fog density 0..100 now (`0x170f88`).
  std::int32_t fog_pct() const { return sky_->fog_pct(); }
  // The fog-day density, 60 + rand(30) (`0x11b7d8`).
  std::int32_t fog_day_pct() const { return sky_->fog_density(); }
  // The sun, the moon and the fog (the clock's sky hooks).
  const Sky& sky() const { return *sky_; }
  Sky& sky() { return *sky_; }
  // Returns the sun and the moon for the 3D view's lighting.
  SkyBodies sky_bodies() const;
  // The light percentage from the sun and the moon (`0x170f84`, also in
  // the sensors' environment).
  std::int32_t light_pct() const { return light_model_.light_percent(); }
  // The sea picture 0 calm, 1 moderate, 2 heavy (`0x105150`), from the
  // season and the map's latitude (independent of the scenario sea state).
  std::int32_t sea_picture() const { return sea_picture_; }

  // Career patrols: the units the contact groups and the air raids add
  // during the mission.
  // `scen_instantiate_units(0, group)` (0x67400) then
  // `init_regroup_convoys`: spawns the ships, enemy submarines, aircraft,
  // bomber groups and photo recons of `units` (the scenario roll drawn,
  // the difficulty raised); returns the vehicles added, in order.
  std::vector<Vehicle*> add_units(const Scenario& units);
  // `scen_spawn_bomber_group` (0x543a4) / `scen_spawn_photo_recon`
  // (0x5442a) on their own (the career's air raids and recons).
  void add_bomber_group(const BomberGroupRecord& record);
  void add_photo_recon(const PhotoReconRecord& record);
  // A patrol's weather change (`wp_update_weather`): sets the weather, sea
  // state and thermal layer, then picks and loads a new `.ENV` for the
  // weather (`init_select_env_file`, drawing from the game's generator)
  // when `reload` is set (from the `Install` the mission was built from,
  // which must still exist).
  void change_weather(std::int32_t weather, std::int32_t sea_state, std::int32_t thermal_layer_ft, bool reload);
  // Moves the boat to world point (`x`, `z`) facing `heading` (the career
  // start, `bases_place_player_start`).
  void place_player_at(std::int32_t x, std::int32_t z, Angle10 heading);
  // The base table of the map (`MissionSetup::bases`).
  const std::vector<BaseRecord>& bases() const { return bases_; }

  // The route group of a single mission's units (`init_spawn_scenario_units`
  // 0x4f373, list 0x119248), which the chart shows zoomed out: null for a
  // career patrol (its groups are the patrol's) or a scenario without
  // ships or enemy submarines. Its `vehicles` are its members (they are
  // never replaced by new ships or submarines during a single mission).
  ContactGroup* scenario_group() const { return scenario_group_.get(); }
  // Makes the scenario group of the current ships, enemy submarines and
  // aircraft when there is a ship or a submarine (a saved single mission
  // restored by the state files, whose own group is not kept).
  void form_scenario_group();

 private:
  Mission(const GameTables& tables, Scenario scenario);

  // Recomputes the light (`world_apply_daylight_palette(0)`) and hands the
  // light and the fog to the sensors whenever the sky changes.
  class SkyLight : public SkyListener {
   public:
    explicit SkyLight(Mission& mission) : mission_(mission) {}
    void on_sky_changed() override;

   private:
    Mission& mission_;
  };

  // `generated`: the draws of a generated mission, which skips the
  // scenario loader's; null for a loaded scenario.
  void build(const Install& install, MissionSetup& setup, const MissionStartDraws* generated,
             std::int32_t start_bearing);
  // init_place_player_sub (0x4fbe4) for a new single mission.
  void place_player(std::int32_t start_bearing);
  void load_world(const Install& install, MissionSetup& setup, WatcomRandom& random);
  void instantiate_units(SimContext ctx);
  void spawn_player(const PlayerSubRecord& record);
  void spawn_ship(const ShipRecord& record);
  void spawn_enemy_sub(const EnemySubRecord& record);
  void spawn_aircraft(const AircraftRecord& record);
  void spawn_bomber_group(const BomberGroupRecord& record, WatcomRandom& random);
  void spawn_photo_recon(const PhotoReconRecord& record);

  const GameTables& tables_;
  Scenario scenario_;
  World world_;
  WorldSeaFloor floor_{world_};
  WorldTerrainGrid grid_{world_};
  std::string environment_file_;
  Environment environment_;
  std::vector<MapObject> objects_;
  MissionConditions conditions_;
  std::unique_ptr<PlayerCombat> combat_;
  // The AI ships' guns and depth charges (`SensorEnvironment::weapons`).
  ShipWeapons ship_weapons_{tables_};
  std::unique_ptr<Simulation> sim_;
  PlayerSub* player_ = nullptr;
  const SubClass* player_class_ = nullptr;
  // `MissionSetup::boat_fit` and, when it is not empty, the class it makes
  // (`player_class_` then points to it).
  BoatFit boat_fit_;
  std::optional<SubClass> fitted_class_;
  std::string player_name_;
  std::int32_t player_crew_ = 0;
  AirSorties air_sorties_;
  std::vector<BaseRecord> bases_;
  AirTheatre air_theatre_;
  std::int32_t skipped_ships_ = 0;
  TorpedoLoad torpedoes_;
  TorpedoTypes torpedo_types_;
  std::vector<std::uint32_t> start_torpedo_types_;
  std::vector<FrameTask*> frame_tasks_;
  std::uint32_t restore_seed_ = 0;
  std::int32_t difficulty_rating_ = 0;
  Ephemeris ephemeris_;
  std::unique_ptr<Sky> sky_;
  SkyLight sky_light_{*this};
  WorldLighting light_model_;
  std::int32_t sea_picture_ = 0;
  std::int32_t player_fuel_ = -1;  // MissionSetup::player_fuel
  std::int32_t transit_fuel_ = 0;
  const Install* install_ = nullptr;  // the files `change_weather` reloads from
  bool paused_ = false;
  std::optional<GameOverReason> game_over_;
  std::int32_t end_code_ = 0;
  std::unique_ptr<ContactGroup> scenario_group_;
  // Drops the vehicles the simulation deletes from the scenario group, as
  // the sensors' contact-group hook of a single mission (the original's
  // members point back to the group).
  class ScenarioGroupHook final : public ContactGroupHook {
   public:
    explicit ScenarioGroupHook(Mission& mission) : mission_(mission) {}
    void forget(const Vehicle& removed) override;

   private:
    Mission& mission_;
  };
  ScenarioGroupHook scenario_hook_{*this};
};

}  // namespace sh

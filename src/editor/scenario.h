// The editor's scenario document: the `.SCN` header globals, the object list
// (the player always first, then the other objects in insertion order), the
// selection, the 3D-model cache with the memory estimate, the torpedo load,
// the modified flag and the patrol map; File / New (`sb_new_scenario`), the
// load (`cmd_load_scenario`'s steps and the `sbfile_spawn_*` converters) and
// the save (`scen_write_ascn`, with what it rebuilds), and the editing
// operations of the chart and the toolbox that need no UI.
//
// Typical use: `Scenario s(install, data, options, clock)` starts with a new
// scenario like SE; `s.load(read_scn_file(...))`, edits through the methods
// below and the objects' public records, then
// `write_scn_file(s.save())`. Confirmations, prompts and the screen checks
// (click distances) stay with the chart and the dialogs.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "assets/scn_file.h"
#include "core/install.h"
#include "editor/data.h"
#include "editor/objects.h"
#include "editor/options.h"

namespace sh::editor {

// A scenario date as the header keeps it.
struct ScenarioDate {
  std::int32_t day = 1;
  std::int32_t month = 0;  // 0-based
  std::int32_t year = 0;   // since 1930

  bool operator==(const ScenarioDate&) const = default;
};

// The war the editor allows (0x9e9d0 / 0x9e9dc): 7 December 1941 and
// 15 August 1945; also a new scenario's date.
inline constexpr ScenarioDate kWarStart{7, 11, 11};
inline constexpr ScenarioDate kWarEnd{15, 7, 15};

// Where a date lies against the war (`env_date_in_war`, 0x28f20).
enum class WarPeriod { kBefore, kDuring, kAfter };
WarPeriod war_period(const ScenarioDate& date);

// Returns the length of `month` (0-based) of `year` (since 1930) (table
// 0x9ea94).
std::int32_t days_in_month(std::int32_t month, std::int32_t year);

// Returns whether SUBS.DAT class `sub` is in service on (`month` 0-based,
// `year` since 1930): from its intro month/year (+0x48 / +0x4c) to its end
// month/year (+0x50 / +0x54), both included (`sb_create_default_player`,
// `sub_type_list_build`).
bool sub_in_service(const SubClass& sub, std::int32_t month, std::int32_t year);

// Returns the top speed of SHIPS.DAT class `ship` in whole knots (its
// ASCII `top_speed_kts` rounded half to even, as the x87 store).
std::int32_t ship_top_speed(const ShipClass& ship);

// Returns the chart view's home centre (16.16 chart pixels, x then y) that
// `sb_setup_world` sets at zoom 1: half the view's map extents 0xa4e20 /
// 0xa4e24, the chart's declared width and height less one (the declared
// width being one less than the rows', `world.chart.width`).
std::array<std::int32_t, 2> view_home_center(const World& world);

// A course click on a ship or enemy sub course (`sbchart_add_course_waypoint`).
enum class CourseClick {
  kFull,    // 24 points already: ignored
  kAdded,   // the point was appended
  kClosed,  // the click closed the open course into a loop (no point added)
};

// The scenario document; one per editor session.
class Scenario {
 public:
  // Returns the seconds since the epoch, as `time()`.
  using Clock = std::function<std::int32_t()>;
  // The most points a course or path takes.
  static constexpr std::int32_t kMaxCoursePoints = 24;
  // The chart's map margin (0x76b10 world units): the player and vessels
  // stay inside.
  static constexpr std::int32_t kMapMargin = 0x76b10;

  // SE's start-up (`sb_load_data`): WORLD-1.WLD and a new scenario. The
  // references must outlive the scenario; `options` gives a new scenario's
  // end-on-lost-contact and randomize. Throws `DataError` if the world
  // cannot be read.
  Scenario(const Install& install, const EditorData& data, const EditorOptions& options, Clock clock);

  // ------------------------------------------------------------ state

  // The header globals (0xaf4a4..0xaf958): `required_memory` is the live
  // memory estimate, `version` the last file's, and `special_mission` and
  // `reloads` hold what the last save or load left.
  ScnHeader header;
  // The "Save changes to scenario?" flag (0xa50c4).
  bool modified = false;
  // The reload counts per TORPEDOES list entry (0xa50b4 aft, 0xa50b8
  // forward), indexed like WEAPONS.DAT.
  std::vector<std::int32_t> reload_aft;
  std::vector<std::int32_t> reload_forward;
  // The sighting fog (0xf6228) and light (0xf6224) of the visibility checks,
  // set by the environment (Edit / Environment).
  std::int32_t sighting_fog_pct = 0;
  std::int32_t sighting_light_pct = 100;

  const std::vector<std::unique_ptr<EditorObject>>& objects() const { return objects_; }
  PlayerSub& player() const;
  // Returns the index of `object` in the list; throws `std::invalid_argument`
  // if it is not in it.
  std::size_t index_of(const EditorObject& object) const;
  // The selected object (0xa5098); the player after New and loads.
  EditorObject* selected() const { return selected_; }
  void select(EditorObject* object) { selected_ = object; }

  const WorldData& world() const { return *world_; }
  const EditorData& data() const { return data_; }
  const ModelCache& model_cache() const { return cache_; }
  ObjectEnv object_env() const { return ObjectEnv{data_, *world_}; }
  ScenarioDate date() const { return {header.day, header.month, header.year}; }

  // The memory budget of the current world (0xa5108, `memory_budget`).
  std::int32_t memory_budget() const { return memory_budget_; }
  // Whether the estimate exceeds the budget ("SCENARIO IS TOO LARGE",
  // `check_scenario_size`).
  bool too_large() const { return header.required_memory > memory_budget_; }

  // ------------------------------------------------------------ File

  // `sb_reset_scenario` (0x45113): deletes every object, clears the memory
  // estimate and the model cache; `full` also resets the environment
  // (Clear, Calm, 7 Dec 1941 12:00), the first byte of the briefing, the
  // password, the options' defaults, a new seed time() + 1, equipment bits
  // 0-5 and the modified flag. Then the bases of the date.
  void reset(bool full);
  // File / New (`sb_new_scenario`, 0x450f5): a full reset, the default
  // player (selected), its torpedo load and the light of the date
  // (`environment_refresh`).
  void new_scenario();
  // `sb_reset_torpedo_load` (0x4569b): every tube gets MARK14 when the
  // player's class has modern torpedoes, else MARK10; that torpedo's reload
  // counts are the class's stern (aft) and bow (forward) reloads, the
  // others' 0.
  void reset_torpedo_load();

  // Throws `DataError` when `file` holds a value the editor would index
  // its tables or arrays with out of their range: the weather, sea state,
  // date (1930-1946) and time outside ENVIRONMENT's choices, a waypoint count outside 0..24
  // (the 25th slot holds the terminator), a crew or orders value outside
  // the editor's lists, a heading outside 0..359 (the dial), a convoy
  // outside 0..8. Deviation: SE reads such files and runs past its
  // tables.
  static void check_file(const ScnFile& file);

  // Load (`cmd_load_scenario`): `check_file` (nothing changed when it
  // throws), a full reset, the file's header (the fields
  // an older version lacks keep what the reader leaves: a new seed, no
  // password), then `accept` (the password prompt;
  // none accepts), the objects in the reader's order (player, ships, enemy
  // subs, aircraft, airstrikes, photo recons; the cost of each added to the
  // estimate below version 7), the file's world, the light of its date
  // (`environment_refresh`), the visibility and the reload counts copied
  // back by position. Returns false when `accept`
  // refuses. A refused file or a world that cannot be read leaves a new
  // scenario on the previous world (a `DataError` is then rethrown); after
  // a file that cannot be read the caller makes a new scenario likewise.
  bool load(const ScnFile& file, const std::function<bool(const ScnHeader&)>& accept = {});
  // `scen_write_ascn` (0x464c8): the file to write, after rebuilding
  // `special_mission` (-1 with an airstrike or photo recon) and the reload
  // table (entry i of the TORPEDOES list, then type -1 entries keeping
  // their counts). The modified flag is left to the caller.
  ScnFile save();

  // ENVIRONMENT's OK (`bases_update_owners` + `sb_build_base_list`): the
  // base owners at the header's date.
  void update_base_owners();
  // PATROL AREA's new zone (`cmd_patrol_area`): the world name, the world
  // reloaded (`scenario_reload_world`), the objects cleared
  // (`sb_reset_scenario(0)`) and the default player, selected
  // (`sb_create_default_player`; centring the chart is the chart's). The
  // torpedo load stays. Throws `DataError` if the world cannot be read.
  void change_world(std::string_view world_name);

  // ------------------------------------------------------------ editing

  // Whether vessel class `ship_class` can be placed at `p`: water deeper
  // than its draft (`sbchart_add_ship_at`).
  bool can_place_vessel(std::size_t ship_class, WorldPoint p) const;
  // `sbchart_add_ship_at` (0x3cd5f) after its check: a new ship or enemy
  // sub of `ship_class` at `p` (heading, speed, orders 0, Green, a sub on
  // side 1 with no target), appended, counted, selected, its visibility
  // updated.
  Vessel& add_vessel(std::size_t ship_class, WorldPoint p, Vessel::Kind kind);
  // `sbchart_tool_add_aircraft` (0x3e5a4) after its dialogs: a new aircraft
  // of `aircraft_type` (bombs and strafes from AIRCRAFT.DAT, relaunch on),
  // appended and counted; its position comes from the path or patrol centre.
  Aircraft& add_aircraft(std::size_t aircraft_type, AircraftMission mission);
  // `sbchart_tool_add_airstrike` / `_photo_recon` (0x3e78a / 0x3e895): a new
  // mission without a target (base -1; a photo recon's time limit 1440 min).
  SpecialMission& add_airstrike();
  SpecialMission& add_photo_recon();

  // Whether removing `object` releases a convoy ("REMOVE CONVOY LEADER?"):
  // a Convoy Leader with members or escorts.
  bool removal_releases_convoy(const EditorObject& object) const;
  // `sbchart_remove_hovered_object` (0x3d3ee) after its prompts: a leader's
  // escorts go to Patrol (aircraft to mission 2) and its members to None;
  // the object leaves the list (the selection falls back to the player) and
  // the estimate. Throws `std::invalid_argument` for the player.
  void remove(EditorObject& object);
  // Unlinks `object` from the list without any other change, returning it
  // (the SELECT A CITY cancel keeps the object alive).
  std::unique_ptr<EditorObject> unlink(EditorObject& object);

  // The vtable `move_to` slot after the screen conversion: the player and
  // vessels only move inside the map margin into water deeper than their
  // draft + 1 ft (returns false otherwise); aircraft move with their path;
  // missions only mark the scenario modified. Updates the visibility.
  bool move_object(EditorObject& object, WorldPoint p);

  // Whether changing `vessel` to `new_class` releases its convoy (a leader
  // with followers changing role group: "REMOVE CONVOY LEADER?").
  bool class_change_releases_convoy(const Vessel& vessel, std::size_t new_class) const;
  // `sbchart_change_ship_class` (0x44114) after the dialogs: another role
  // group drops the orders (a leader releases its convoy); a submarine
  // class switches the kind; the live part gets the class and both parts
  // the new models.
  void change_vessel_class(Vessel& vessel, std::size_t new_class);
  // `toolbox_air_type_button` (0x40356): the type, its bombs, strafes and
  // model.
  void change_aircraft_type(Aircraft& aircraft, std::size_t aircraft_type);

  // ------------------------------------------------------------ convoys

  // The convoy helpers 0x43d44..0x440bb (`sb_convoy_*`); convoys are ids
  // 1-8. Returns the lowest id without a leader, or -1.
  std::int32_t first_free_convoy_id() const;
  // Returns whether a convoy other than `id` has a leader.
  bool other_convoy_exists(std::int32_t id) const;
  // Returns the leader vessel of convoy `id`, or nullptr (always for 0).
  Vessel* convoy_leader(std::int32_t id) const;
  // Returns whether a member or an escort refers to convoy `id`.
  bool convoy_has_followers(std::int32_t id) const;
  // Members get the leader's heading and speed (ship records).
  void copy_convoy_course(std::int32_t id);
  // Escort ships get twice the leader's speed, at most their top speed
  // (escorting aircraft are left alone).
  void update_escort_speed(std::int32_t id);
  // Returns the lowest id that has a leader, or 0.
  std::int32_t first_existing_convoy_id() const;
  // Returns the lowest top speed of the leader and members of convoy `id`.
  std::int32_t convoy_slowest_speed(std::int32_t id) const;
  // Escorts of `id` go to Patrol, escorting aircraft to mission 2.
  void release_escorts(std::int32_t id);
  // Members of `id` go to None.
  void release_members(std::int32_t id);
  // Returns a ship's top speed for the speed entry (`sb_ship_max_speed`): a
  // leader's is its convoy's slowest.
  std::int32_t ship_max_speed(const Vessel& vessel) const;
  // `sbchart_apply_convoy_membership` (0x3d84e), after a convoy pick or
  // change: no convoy takes the first existing one (else orders None); a
  // member copies the leader's heading and speed and drops its course; an
  // escort runs at twice the leader's speed, at most its top speed.
  void apply_convoy_membership(Vessel& vessel);
  // A PICK CONVOY click (`sbchart_pick_convoy_hovered` and the mode end):
  // `picked` must be a ship; its convoy goes to `selected` (a ship, then
  // `apply_convoy_membership`, or an aircraft's escort group). Returns false
  // when the click is ignored.
  bool pick_convoy(EditorObject& selected, const EditorObject& picked);

  // ------------------------------------------------------------ courses

  // The first course click: an empty course starts at the unit's position.
  static void start_course(Waypoints& course, WorldPoint start);
  // Appends a point checked by the chart (water, legs clear of land) to a
  // ship or enemy-sub course started by `start_course`; `closes_loop` when
  // the click is within 10 px of the start of an open course of more than 2
  // points. The entry after the last point is the terminator: -1 for a
  // looped course, 0 for an open one.
  static CourseClick add_course_point(Waypoints& course, WorldPoint p, bool looped, bool closes_loop);
  // An aircraft path point (`sbchart_add_aircraft_waypoint`): the first one
  // also places the aircraft; paths are always looped. Returns false when
  // the path is full.
  static bool add_aircraft_point(Aircraft& aircraft, WorldPoint p);

  // ------------------------------------------------------------ visibility

  // `sbchart_update_all_visibility` (0x44379).
  void update_all_visibility();
  DetectionEnv detection_env() const;

 private:
  template <class T>
  T& append(std::unique_ptr<T> object);
  void setup_world();
  void load_world(std::string_view name);
  void create_default_player();
  void spawn(const ScnRecord& record);
  void add_models(Vessel& vessel, std::size_t ship_class);
  void fail_load(const ScnChars<13>& previous_world);
  std::int32_t water_depth_ft(WorldPoint p) const;

  const Install& install_;
  const EditorData& data_;
  const EditorOptions& options_;
  Clock clock_;
  std::unique_ptr<WorldData> world_;
  std::vector<std::unique_ptr<EditorObject>> objects_;
  EditorObject* selected_ = nullptr;
  ModelCache cache_;
  std::int32_t memory_budget_ = 0;
  // The player record global 0xaf95c: the last file's, which a file without
  // a player record also spawns (the default player's before any load).
  ScnPlayerSub player_record_;
};

}  // namespace sh::editor

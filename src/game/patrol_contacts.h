// The strategic contact groups of a career patrol (module campaign/contacts,
// `campaign\contacts.cpp`, list 0x119248, and the base spawns of module
// campaign/bases): convoys and task forces on the zone's `.CR` routes,
// patrols and anchored ships at the bases, sampans. Each group moves on the
// strategic map; when the player comes within sighting or radar range it
// materializes into vehicles built by the scenario generator's group
// builders (`ScenarioGenerator`, the same `.SCN` records a loaded mission
// spawns), and dematerializes when the player loses it.
// docs/structures.md "Strategic contact groups".
//
// Usage over a running patrol (the order of `wp_load_patrol_data`,
// `init_simulation` and the mission frame):
//   ContactGroups groups(tables, zone, world, spawner);
//   groups.init(clock, random);                 // contacts_init
//   groups.update_bases(clock, random, false);  // bases_update_daily(0)
//   groups.update_bases(clock, random, true);   // bases_update_daily(-1)
//   sensors.environment().contact_groups = &groups;  // with sensor_option::kContactGroups
//   each frame: groups.update_bases(clock, random, false);
//               groups.update(clock, view, random);  // from wp_patrol_tick
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "assets/scenario.h"
#include "core/random.h"
#include "game/scengen.h"
#include "sim/ai_ship.h"
#include "sim/clock.h"
#include "sim/vehicle.h"

namespace sh {

struct World;

// What the contact groups see of the mission each frame.
struct ContactView {
  // The player boat's world position (0x11a9c4 / 0x11a9cc).
  std::int32_t player_x = 0;
  std::int32_t player_z = 0;
  // The lookouts' eye height in feet: SUBS.DAT +0xac with the periscope
  // raised (0x11ac33), else +0xb0.
  std::int32_t observer_height_ft = 0;
  std::int32_t weather = 0;    // 0x125f60
  std::uint32_t options = 0;   // 0x11a85c: the realism bits 2-3
  // The game time of the frame in hundredths of a second (0x129c04).
  std::int32_t frame_hundredths = 0;
  // The lookouts' conditions of `sighting_check` (0x170f88, 0x170f84).
  std::int32_t fog_pct = 0;
  std::int32_t light_pct = 100;
  // `sj_radar_range` (0x66c39): the fitted SJ range (`sj_fitted_range`)
  // while the set is on and undamaged, else 0.
  std::int32_t sj_range_yd = 0;
  // `player_radio_available` (0x5e73f): shallow enough and the radio
  // undamaged (HQ contact reports).
  bool radio_available = false;
};

// The mission side of the contact groups: spawns and removes their
// vehicles. Implemented over the running `Mission` (game/patrol.h) and by
// the tests.
class ContactSpawner {
 public:
  virtual ~ContactSpawner() = default;
  // Returns the lowest convoy group id from 1 that no live vehicle uses as
  // leader, member or escort (the loop over `convoy_find_leader` /
  // `convoy_next_member` / `convoy_next_escort`).
  virtual std::int32_t free_convoy_group() const = 0;
  // Spawns the ships, enemy submarines and aircraft of `units` as
  // `scen_instantiate_units(0, group)` does (its `random_int(100)` scenario
  // roll and difficulty first), then regroups the convoys
  // (`init_regroup_convoys`); returns the vehicles spawned, in order.
  virtual std::vector<Vehicle*> spawn(const Scenario& units) = 0;
  // Removes `vehicle` from the simulation at once, dropping the TDC and
  // deck-gun targets that point to it (`contacts_dematerialize_group`).
  virtual void remove(Vehicle& vehicle) = 0;
  // Returns whether `vehicle` is still in the simulation's vehicle list
  // (sinking ones included). `vehicle` may have been deleted: it is compared
  // by address only, never dereferenced.
  virtual bool contains(const Vehicle* vehicle) const = 0;
  // Lowers the time compression cap (a group within 40000 yd: 0x200).
  virtual void limit_compression(std::int32_t max) = 0;
};

// The kind of a group (vtable slot +4), also the index of the materialized
// counts 0x1191f4.
enum class ContactGroupKind : std::int32_t {
  kRoute = 1,     // convoy route group (0x198 bytes, vtable 0x102728)
  kPatrol = 2,    // base patrol (0x188, 0x101a9c)
  kAnchored = 4,  // ships anchored at a base (0x184, 0x101acc)
  kSampans = 8,   // fishing sampans (0x174, 0x102758)
};

// What a route group carries (`+0x13c`).
enum class RouteGroupKind : std::int32_t {
  kConvoy = 0,
  kTaskForce = 1,
  kPatrol = 2,      // forced once the route's alert reaches 3
  kSubmarines = 3,  // the 2 % roll
};

// The last-known contact of a group shown on the chart: a route group's HQ
// contact report (`+0x168..+0x194`), refreshed like the other kinds' from
// the nearest member by the chart while the group is materialized (patrol
// `+0x154..+0x17c`, anchored ships `+0x150..+0x178`, sampans
// `+0x144..+0x16c`, in the same field order).
struct HqReport {
  bool valid = false;            // +0x168
  std::int32_t x = 0;            // +0x16c
  std::int32_t z = 0;            // +0x170
  std::int32_t heading = -1;     // +0x174: -1 unknown (accuracy below 25)
  std::int32_t size = -1;        // +0x178: -1 unknown (accuracy below 50)
  std::int32_t hour = 0;         // +0x17c
  std::int32_t minute = 0;       // +0x180
  std::int32_t month = 0;        // +0x184 (0-based)
  std::int32_t day = 0;          // +0x188
  std::int32_t timer = 0;        // +0x18c: 16.16 s left
  // +0x190: the CHART.SHP frame of the symbol last drawn, 0 before the
  // first draw (no hit rectangle until then).
  std::int32_t symbol_frame = 0;
  std::int32_t accuracy = 0;     // +0x194: % (route groups only)
};

// A route waypoint of a group (x, y, z world units).
struct GroupPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

// One strategic contact group. The fields of the four classes share one
// record; those of another kind stay at their defaults.
struct ContactGroup {
  ContactGroupKind kind = ContactGroupKind::kRoute;
  GroupPoint position;                // +0x04
  GroupPoint target;                  // +0x10: the current waypoint
  std::vector<GroupPoint> waypoints;  // +0x1c
  std::int32_t waypoint_index = 0;    // +0x10c
  std::int32_t heading = 0;           // +0x114: tenths of a degree
  std::int32_t speed = 0;             // +0x118: 16.16 knots
  bool finished = false;              // +0x11c: deleted at the end of the update
  bool materialized = false;          // +0x120
  bool seen = false;                  // +0x124: detected by the player this frame
  bool engaged = false;               // +0x128: one of its ships saw the player
  bool near = false;                  // +0x130: within 40000 yd at the last try
  // +0x12c: set when the group is created, cleared after its first update:
  // a group created in range is dropped instead of materialized.
  bool blocked = true;
  std::uint32_t seed = 0;             // srand() seed of the materialization
  // The vehicles materialized (`+0xc8` of each points back to the group);
  // possibly deleted by the simulation since: check with
  // `ContactSpawner::contains` first.
  std::vector<Vehicle*> vehicles;
  std::int32_t spawned = 0;  // +0x160 / +0x14c / +0x140: vehicles materialized

  // Route groups.
  std::int32_t route = 0;  // +0x138
  RouteGroupKind route_kind = RouteGroupKind::kConvoy;  // +0x13c
  std::int32_t size = 0;          // +0x140: convoy size 0..2, or 1..3 boats
  std::int32_t warship_type = 0;  // +0x144: 0 BB, 1 CV, 2 CA, 3 CL
  std::int32_t escort = 0;        // +0x148
  std::int32_t crew = 0;          // +0x14c
  // +0x150: a fast convoy (kind 0) or intercept orders (kind 3).
  bool fast_or_intercept = false;
  // +0x154: a coastal route (kinds 0, 1) or submerged boats (kind 3).
  bool coastal_or_submerged = false;
  std::int32_t aircraft = 0;      // +0x158
  bool alert_counted = false;     // +0x164
  // The chart's last-known contact (all kinds).
  HqReport report;

  // Base groups (patrols and anchored ships) and sampans.
  std::int32_t base = -1;          // +0x184 / +0x180
  std::int32_t patrol_size = 0;    // patrol +0x13c (base +0x6f)
  bool patrol_tight = false;       // patrol +0x144 (base +0x77)
  std::int32_t crew_quality = 0;   // patrol +0x140
  std::int32_t anchored_size = 0;  // anchored +0x13c (base +0x67)
  std::int32_t guard_size = 0;     // anchored +0x140, capped by base +0x6b
  std::int32_t merchant_pct = 0;   // anchored +0x138: 100, 20 with a small guard
  std::int32_t sampan_size = 0;    // sampans +0x138: 0..2
  bool engaged_counted = false;    // patrol +0x150
};

// The route state 0x1191e8 (0x1c bytes per route).
struct RouteState {
  bool active = false;            // +0x00: the route runs today
  std::int32_t frequency = 0;     // +0x04: this month's
  std::int32_t spawn_timer = 0;   // +0x08: seconds to the next group
  std::int32_t convoy_pct = 0;    // +0x0c: this month's
  std::uint8_t flags = 0;         // +0x10
  std::int32_t alert = 0;         // +0x14: route groups engaged
  std::int32_t alert_timer = 0;   // +0x18: seconds to the next decay
};

class ContactGroups final : public ContactGroupHook {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // The groups of `zone` (its routes, bases and owner table, which
  // `update_bases` updates) on the patrol map `world`, spawning through
  // `spawner`. `limit_classes` is the low-memory flag 0x1242d0. All must
  // outlive the groups.
  ContactGroups(const GameTables& tables, ZoneData& zone, const World& world, ContactSpawner& spawner,
                bool limit_classes = false);

  // `contacts_init` (0x2d9fd): the route states, the per-role largest
  // visual sizes 0x119228 and the first day's routes and sampans.
  void init(const GameClock& clock, WatcomRandom& random);
  // `bases_update_daily` (0x16745): on a new day, or when `force`, the
  // base owners (`bases_update_owners`) and each base's anchored ships and
  // patrol (`bases_spawn_contact_groups`).
  void update_bases(const GameClock& clock, WatcomRandom& random, bool force);
  // `contacts_update` (0x2db52): the monthly warship mix, the route
  // spawns, the moves, the materializations and dematerializations and the
  // removal of the finished groups. Clears the detection flags for the next
  // frame's `player_report_contacts`.
  void update(const GameClock& clock, const ContactView& view, WatcomRandom& random);
  // The chart's debug move of the boat (0x11a7cc): the next update
  // materializes the groups in range however close.
  void force_materialize() { force_ = true; }

  // ContactGroupHook (`contact_group_mark_engaged` /
  // `contact_group_mark_detected`), called by the sensors and the AI.
  void engaged(Vehicle& ship) override;
  void detected(Vehicle& vehicle) override;
  // Drops `removed` from its group (the original's list holds no pointer to
  // a deleted vehicle), before its address can be reused.
  void forget(const Vehicle& removed) override;
  // Returns the group `vehicle` was materialized for (its `+0xc8`), else
  // null.
  ContactGroup* group_of(const Vehicle& vehicle) const;

  const std::vector<std::unique_ptr<ContactGroup>>& groups() const { return groups_; }
  const std::vector<RouteState>& routes() const { return routes_; }
  // The materialized groups per kind index (0x1191f4[9]).
  const std::array<std::int32_t, 9>& materialized() const { return materialized_; }
  // The BB / CV / CA / CL mix of the task forces in % (0x103808).
  const std::array<std::uint8_t, 4>& warship_mix() const { return warship_mix_; }
  // The largest SHIPS.DAT visual size per sighting slot (0x119228): escort,
  // merchant, sub, small craft, BB, CV, CA, CL.
  const std::array<std::int32_t, 8>& visual_sizes() const { return visual_sizes_; }
  // The generator parameters 0x1265cc..0x126604 the builders read; they
  // persist between materializations as the original's globals.
  GeneratorParams& generator_params() { return params_; }

 private:
  void routes_new_day(const GameClock& clock, WatcomRandom& random);
  void routes_update(const GameClock& clock, const ContactView& view, WatcomRandom& random);
  void move_groups(const GameClock& clock, const ContactView& view, WatcomRandom& random);
  void update_warship_mix(const GameClock& clock);
  bool route_active(std::size_t route, const GameClock& clock) const;
  bool build_on_route(ContactGroup& g, std::size_t route, bool backward, const GameClock& clock,
                      WatcomRandom& random);
  void update_group(ContactGroup& g, std::int32_t dt, const GameClock& clock, const ContactView& view,
                    WatcomRandom& random);
  void move_route_group(ContactGroup& g, std::int32_t dt);
  bool try_materialize(ContactGroup& g, const GameClock& clock, const ContactView& view, WatcomRandom& random);
  // Returns the range (yd) at which the boat finds a group of visual size
  // `size`: the lookouts' range, or the SJ's when larger and `radar`.
  std::int32_t detection_range(const ContactView& view, std::int32_t size, std::int32_t radar_yd) const;
  // Spawns `units` as `g`'s vehicles.
  void materialize(ContactGroup& g, const Scenario& units);
  void dematerialize(ContactGroup& g);
  std::int32_t kinds_at_base(std::int32_t base);
  void spawn_base_groups(std::size_t base, WatcomRandom& random, const GameClock& clock);
  bool sampans_init(ContactGroup& g, std::int32_t size, WatcomRandom& random);
  void build_sampans(ScenarioGenerator& generator, Scenario& units, std::int32_t x, std::int32_t z,
                     std::int32_t heading, WatcomRandom& random) const;

  const GameTables& tables_;
  ZoneData& zone_;
  const World& world_;
  ContactSpawner& spawner_;
  bool limit_classes_;

  std::vector<std::unique_ptr<ContactGroup>> groups_;  // 0x119248
  std::vector<RouteState> routes_;                     // 0x1191e8
  std::unordered_map<const Vehicle*, ContactGroup*> owner_;
  std::array<std::int32_t, 9> materialized_{};         // 0x1191f4
  std::array<std::int32_t, 8> visual_sizes_{};         // 0x119228
  std::array<std::uint8_t, 4> warship_mix_{15, 20, 25, 40};  // 0x103808
  GeneratorParams params_;
  std::int32_t last_day_ = -1;     // 0x1191ec
  std::int32_t last_month_ = -1;   // 0x1191f0
  std::int32_t move_acc_ = 0;      // 0x119220: hundredths
  std::int32_t route_acc_ = 0;     // 0x119224: hundredths
  std::int32_t bases_day_ = -1;    // 0x11624c
  bool force_ = false;             // 0x11a7cc
};

}  // namespace sh

// The vehicles of the state files (sim/state_io.h): the per-class layouts
// of `*_write_state` / `*_read_state` (vtable slots +0xdc / +0xe0, the
// common part `veh_write_state` 0x7f026), and `StateIo`, the friend of the
// simulation and mission classes that maps their fields to the original
// layouts (vehicle_state.cpp, sim_state.cpp).
//
// The original saves every vehicle class (`*_can_save`, slot +0xd8,
// returns non-zero for all of them).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "sim/effects.h"
#include "sim/state_io.h"
#include "sim/vehicle.h"

namespace sh {

struct GameTables;
struct AirTheatre;
class Ship;
class EnemySub;
class Aircraft;
class Smoke;
class AirGroup;
class IntelReport;
class DownedPilot;
class TorpedoVehicle;
class DepthCharge;
class Shell;
class Bomb;
class Splash;
class ExplosionFx;
class UnderwaterBlast;
class PlayerSub;
class PlayerDamage;
class Tdc;
class DeckGun;
class Sensors;
class ContactReports;
class GameClock;
class Sky;
class AirSorties;
class Simulation;
class Mission;
class RadioRoom;
class ContactGroups;
class PatrolWeather;
class Patrol;
class ControlBar;
class CabinStation;
class MissionScreen;
struct SimStateFile;
struct SimStateParts;
struct SessionStateParts;

// The layout of a saved vehicle class: the size of the original object,
// the ranges its writer emits (the common part first). The values written
// apart from the object live past `size`: the contact-group index at
// `size` (all classes), a torpedo's weapon name (10 bytes) or a pilot's
// attacker id at `size + 4`.
struct VehicleStateLayout {
  VehicleClass cls = VehicleClass::kShip;
  std::uint32_t size = 0;
  std::vector<StateRange> ranges;

  // The image size, extra values included.
  std::uint32_t image_size() const { return size + 0x10; }
  std::uint32_t group_offset() const { return size; }
};

// Returns the layout of saved class `cls` (1..15); throws `DataError` for
// any other.
const VehicleStateLayout& vehicle_state_layout(VehicleClass cls);

// What restoring a vehicle needs from the mission.
struct VehicleRestoreContext {
  const GameTables& tables;
  // The bases and landmarks of the air strikes (must outlive the vehicles).
  const AirTheatre& theatre;
  EffectFrames frames;
};

class StateIo {
 public:
  // Fills `out`'s image (laid out by `vehicle_state_layout`) from `v` (not
  // the player boat); the contact-group index is the caller's.
  static void capture_vehicle(Vehicle& v, StateOut& out);
  // Returns the vehicle of class `cls` read from `in`, its pointers queued
  // in `in.links()`. Throws `DataError` when its SHIPS.DAT / AIRCRAFT.DAT /
  // WEAPONS.DAT class is missing.
  static std::unique_ptr<Vehicle> restore_vehicle(VehicleClass cls, StateIn& in, const VehicleRestoreContext& ctx);

  // The objects' bindings, both ways (`Io` is `StateOut` or `StateIn`).
  template <class Io>
  static void bind(Io& io, Ship& v);
  template <class Io>
  static void bind(Io& io, EnemySub& v);
  template <class Io>
  static void bind(Io& io, Aircraft& v);
  template <class Io>
  static void bind(Io& io, Smoke& v);
  template <class Io>
  static void bind(Io& io, AirGroup& v);
  template <class Io>
  static void bind(Io& io, IntelReport& v);
  template <class Io>
  static void bind(Io& io, DownedPilot& v);
  template <class Io>
  static void bind(Io& io, TorpedoVehicle& v);
  template <class Io>
  static void bind(Io& io, DepthCharge& v);
  template <class Io>
  static void bind(Io& io, Shell& v);
  template <class Io>
  static void bind(Io& io, Bomb& v);
  template <class Io>
  static void bind(Io& io, Splash& v);
  template <class Io>
  static void bind(Io& io, ExplosionFx& v);
  template <class Io>
  static void bind(Io& io, UnderwaterBlast& v);
  template <class Io>
  static void bind(Io& io, Debris& v);
  // The player block of `player_write_state` (game/sim_state.cpp).
  template <class Io>
  static void bind(Io& io, PlayerSub& v);
  template <class Io>
  static void bind(Io& io, PlayerDamage& v);
  template <class Io>
  static void bind(Io& io, Tdc& v);
  template <class Io>
  static void bind(Io& io, DeckGun& v);
  // The state the original does not save (extension only).
  template <class Io>
  static void bind(Io& io, Sensors& v);
  template <class Io>
  static void bind(Io& io, ContactReports& v);
  template <class Io>
  static void bind(Io& io, GameClock& v);
  template <class Io>
  static void bind(Io& io, Sky& v);
  template <class Io>
  static void bind(Io& io, AirSorties& v);
  template <class Io>
  static void bind(Io& io, Simulation& v);
  template <class Io>
  static void bind(Io& io, Mission& v);
  template <class Io>
  static void bind(Io& io, RadioRoom& v);
  template <class Io>
  static void bind(Io& io, PatrolWeather& v);
  template <class Io>
  static void bind(Io& io, Patrol& v);
  template <class Io>
  static void bind(Io& io, ControlBar& v);
  template <class Io>
  static void bind(Io& io, CabinStation& v);
  template <class Io>
  static void bind(Io& io, ContactGroups& v);
  template <class Io>
  static void bind(Io& io, MissionScreen& v);

  // The state lists (game/sim_state.cpp: `capture_sim_state`,
  // `restore_sim_state`, `restore_session_state`).
  static SimStateFile capture(Mission& mission, const SimStateParts& parts, const SessionStateParts& session);
  static void restore(Mission& mission, const SimStateFile& file, const SimStateParts& parts);
  static void restore_session(const SimStateFile& file, const SessionStateParts& session);
  // What `init_load_saved_mission_world` and `init_restore_saved_mission`
  // set from a save without the extension (game/mission_save.cpp): the fog
  // density, the daily fog flag (0x129c68), the thermal layer and the
  // generator's seed.
  static void restore_original_globals(Mission& mission, std::int32_t fog_pct, std::int32_t daily_fog,
                                       std::int32_t thermal_layer_ft, std::uint32_t seed);

 private:
  template <class Io>
  static void bind_common(Io& io, Vehicle& v);
};

}  // namespace sh

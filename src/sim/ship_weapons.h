// The AI ships' weapons (`ai_ship_fire_guns`, `ai_ship_drop_depth_charges`),
// the ShipWeaponsHook the ship AI calls (install it in the sensors'
// environment, `SensorEnvironment::weapons`).
#pragma once

#include <cstdint>
#include <span>

#include "assets/dat_tables.h"
#include "sim/ai_ship.h"

namespace sh {

class ShipWeapons : public ShipWeaponsHook {
 public:
  // `tables` (the WEAPONS.DAT guns) must outlive it.
  explicit ShipWeapons(const GameTables& tables) : tables_(&tables) {}

  // `ai_ship_fire_guns` (0x12f7a): each mount the class has (SHIPS.DAT
  // +148/+152, a third of the length fore and aft) fires its WEAPONS.DAT gun
  // (+156) at the contact between 200 yd and the gun's range (cut for
  // merchants and green crews): crews above green lead the target, the
  // bearing scatters with the accuracy, which tightens with each shot, and
  // no shot goes within 8 deg of a friendly ship or more than 120 deg off
  // the mount's side; reloads take (3 - crew) x gun +60 s (x 3 for
  // merchants) + rand(3/4 of it).
  void fire_guns(Ship& ship, SimContext& ctx, std::int32_t dt, std::span<const Angle10> friendly_bearings) override;
  // `ai_ship_drop_depth_charges` (0x12dae): the stern racks drop their
  // charges (set within +-15 ft) 20 and 40 ft either side of the stern,
  // then the throwers throw theirs (+-25 ft) at 45 and 90 deg.
  void drop_depth_charges(Ship& ship, SimContext& ctx, std::int32_t depth_ft) override;

 private:
  const GameTables* tables_;
};

}  // namespace sh

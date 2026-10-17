// A career boat's deck gun fit (not in the original): what the extended
// variant's DECK GUN request (game/career_requests.h) changes on the boat,
// applied to a mission through a modified copy of its SUBS.DAT class, so
// that the deck guns, the damage systems, the status page and the command
// bar read it as they read the class. It is kept in the career's
// ROSTER<n>.EXT and in the "BOAT" section of a patrol's saved state
// (game/sim_state.h).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "assets/dat_tables.h"

namespace sh {

struct BoatFit {
  // The mount of a one-gun boat's gun (0 forward, 1 aft); nothing keeps the
  // class's (SUBS.DAT +144).
  std::optional<std::uint32_t> gun_mount;
  // A second gun of the first's calibre: guns forward and aft, the class's
  // rounds raised to `kTwoGunRounds` at least.
  bool second_gun = false;

  // Whether the fit leaves the class as it is.
  bool empty() const { return !gun_mount && !second_gun; }
  friend bool operator==(const BoatFit&, const BoatFit&) = default;
};

// The rounds a boat with a second gun carries at least.
inline constexpr std::uint32_t kTwoGunRounds = 100;

// Returns whether `sub` is a fleet boat with one gun (modern torpedoes and
// one deck gun: the P, Salmon, T, Gato and Balao), whose gun can be moved
// and doubled.
bool fleet_boat_with_one_gun(const SubClass& sub);

// Returns `sub` with `fit` applied: the gun on its mount, or two guns, at
// least `kTwoGunRounds` rounds and, for a boat drawn with the Gato's
// profile (the Gato and the Balao), the Tench's (P_TENCH.SHP, the Gato's
// with a forward gun) for the port pictures. A fit `sub` cannot take (not
// `fleet_boat_with_one_gun`) leaves it as it is.
SubClass fitted_sub_class(const SubClass& sub, const BoatFit& fit);

// Returns the "BOAT" section of `fit`: i32 mount (-1: the class's), i32
// second gun (0 or 1).
std::vector<std::uint8_t> encode_boat_fit(const BoatFit& fit);
// Returns the fit of a "BOAT" section. Throws `DataError` when it is
// truncated.
BoatFit decode_boat_fit(std::span<const std::uint8_t> data);

}  // namespace sh

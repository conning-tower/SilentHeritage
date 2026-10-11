// Writers of the game's tables (DAT, routes, zones, bases, sounds,
// ephemeris), the inverses of the parsers of src/assets; each round-trips
// through its parser (synthetic_encoders_test.cpp). A record's `raw` bytes
// are written first, then its decoded fields over them.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "assets/bases.h"
#include "assets/dat_tables.h"
#include "assets/ephemeris.h"
#include "assets/mel.h"
#include "assets/patrol_zones.h"
#include "assets/routes.h"

namespace sh::test {

std::vector<std::uint8_t> encode_ships_dat(std::span<const ShipClass> ships);
std::vector<std::uint8_t> encode_subs_dat(std::span<const SubClass> subs);
std::vector<std::uint8_t> encode_aircraft_dat(std::span<const AircraftType> aircraft);
std::vector<std::uint8_t> encode_weapons_dat(const Weapons& weapons);
std::vector<std::uint8_t> encode_carsub_dat(std::span<const CarsubBoat> boats);
std::vector<std::uint8_t> encode_ijnships_dat(std::span<const IjnShip> ships);
std::vector<std::uint8_t> encode_cmdlist_dat(std::span<const Commander> commanders);
std::vector<std::uint8_t> encode_homeport_dat(std::span<const HomePort> ports);
std::vector<std::uint8_t> encode_rmsg_dat(std::span<const std::string> templates);

std::vector<std::uint8_t> encode_patrol_zones(std::span<const PatrolZone> zones);
std::vector<std::uint8_t> encode_routes(std::span<const ConvoyRoute> routes);
std::vector<std::uint8_t> encode_bases(std::span<const BaseRecord> bases);

std::vector<std::uint8_t> encode_mel(const MelSample& sample);
// Returns a `.BNK` of `entries` and its terminating free record.
std::vector<std::uint8_t> encode_bnk(std::span<const BankEntry> entries);

// Returns `PLANETS.NDX` for `bodies`' theories (an empty theory for the
// others), and a body's theory file.
std::vector<std::uint8_t> encode_planets_ndx(const std::array<const PlanetTheory*, kPlanetBodies>& bodies);
std::vector<std::uint8_t> encode_planet_theory(const PlanetTheory& theory);

}  // namespace sh::test

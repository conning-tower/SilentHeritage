// Game tables (DATA/*.DAT), the ephemeris files and the SH.EXE data. Expected
// values were cross-checked with independent reference parsers and
// docs/structures.md.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "assets/crew_messages.h"
#include "assets/dat_tables.h"
#include "assets/ephemeris.h"
#include "assets/exe_image.h"
#include "core/error.h"
#include "game_data.h"

namespace sh {

namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
}

}  // namespace

TEST_CASE("SUBS.DAT decodes the Gato class", "[dat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto tables = GameTables::load(install);
  REQUIRE(tables.subs.size() == 9);
  const SubClass* gato = tables.find_sub("gato");
  REQUIRE(gato == &tables.subs[6]);
  CHECK(gato->name == "GATO");
  CHECK(gato->class_index == 6);
  CHECK(gato->sprite_silhouette == "SUB-7.SHP");
  CHECK(gato->sprite_profile == "P_GATO.SHP");
  CHECK(gato->picture_frame == 2);
  CHECK(gato->intro_month == 4);
  CHECK(gato->intro_year == 1942);
  CHECK(gato->end_month == 10);
  CHECK(gato->range_nm == 11800);
  CHECK(gato->range_at_kts == 10);
  CHECK(gato->bow_tubes == 6);
  CHECK(gato->stern_tubes == 4);
  CHECK(gato->bow_reloads == 10);
  CHECK(gato->stern_reloads == 4);
  CHECK(gato->modern_torpedoes);
  CHECK(gato->deck_gun_count == 1);
  CHECK(gato->length_ft == 312);
  CHECK(gato->beam_ft == 27);
  CHECK(gato->draft_ft == 15);
  CHECK(gato->test_depth_ft == 300);
  CHECK(gato->periscope_depth_ft == 65);
  CHECK(gato->radar_depth_ft == 48);
  CHECK(gato->periscope_eye_ft == 68);
  CHECK(gato->bridge_eye_ft == 40);
  CHECK(gato->awash_depth_ft == 30);
  CHECK(gato->noise_factor_pct == 100);
  CHECK(gato->surface_speed_kts == "20.75");
  CHECK(gato->submerged_speed_kts == "8.75");
  CHECK(gato->displacement_surfaced == 1825);
  CHECK(gato->displacement_submerged == 2410);
  CHECK(gato->displacement_max == 2420);
  CHECK(gato->diesel_hp == 5400);
  CHECK(gato->motor_hp == 2740);
  CHECK(gato->drag_cubic[0].raw() == 351);
  CHECK(gato->drag_cubic[1].raw() == 688);
  CHECK(gato->drag_linear[1].raw() == 2097);
  CHECK(gato->lateral_drag == Fixed16::from_int(1000));
  CHECK(gato->astern_drag_factor.raw() == 688128);  // 10.5
  CHECK(gato->turn_factor.raw() == 1389363);        // 21.2
  CHECK(gato->pitch_factor.raw() == 2503475);       // 38.2
  CHECK(gato->diesel_count == 4);
  CHECK(gato->motor_count == 4);
  CHECK(gato->deck_gun_rounds == 95);
  CHECK(gato->fuel_capacity == 117000);
  CHECK(gato->fuel_use == 7209);
  CHECK(gato->power_submerged == std::array<std::int32_t, 11>{0, 50, 120, 190, 270, 400, -372, -279, -186, -93, 10});
  CHECK(gato->power_surfaced[3] == 186);
  CHECK(gato->description.starts_with("Developed from the P, Salmon/Sargo and, especially,\n"));
  CHECK(gato->compartment_rects[1].x1 == 492);
  CHECK(gato->compartment_rects[1].y2 == 124);
  CHECK(gato->raw[160] == 44);  // test depth 300 = 0x12c, low byte

  CHECK(tables.subs[7].test_depth_ft == 400);  // Balao
  CHECK(tables.subs[4].display_name == "SALMON/SARGO");
  CHECK_FALSE(tables.subs[0].modern_torpedoes);  // S class: Mark 10 only
  CHECK(tables.subs[2].deck_gun_count == 2);     // Narwhal
  CHECK(tables.subs[2].displacement_submerged == 4050);
  CHECK(tables.subs[1].end_month == 3);  // Barracuda
}

TEST_CASE("SHIPS.DAT decodes classes and the motion coefficients", "[dat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto tables = GameTables::load(install);
  REQUIRE(tables.ships.size() == 55);
  const ShipClass* fuso = tables.find_ship("Fuso");
  REQUIRE(fuso == &tables.ships[0]);
  CHECK(fuso->name == "FUSO");
  CHECK(fuso->ship_type == ShipType::kBattleship);
  CHECK(fuso->role == ShipRole::kWarship);
  CHECK(fuso->sprite1 == "FUSO.SHP");
  CHECK(fuso->sprite2 == "FUSOS.SHP");
  CHECK(fuso->idbook_picture == "FUSO.SHP");
  CHECK(fuso->start_year == 1914);
  CHECK(fuso->end_year == 1944);
  CHECK(fuso->start_month == 3);
  CHECK(fuso->end_month == 10);
  CHECK(fuso->selection_weight == 2);
  CHECK(fuso->category == 4);
  CHECK(fuso->screw_sound == ScrewSound::kLowSpeed);
  CHECK(fuso->hull_strength == 400);
  CHECK(fuso->max_power.raw() == 9000);
  CHECK_FALSE(fuso->has_sonar);
  CHECK(fuso->main_gun_count == 12);
  CHECK(fuso->main_gun_caliber_in == "14");
  CHECK(fuso->top_speed_kts == "22.5");
  CHECK(fuso->length_ft == 673);
  CHECK(fuso->beam_ft == 94);
  CHECK(fuso->draft_ft == 28);
  CHECK(fuso->visual_size == 270);
  CHECK(fuso->displacement_tons == 35900);
  CHECK(fuso->drag_cubic.raw() == 426);
  CHECK(fuso->drag_linear.raw() == 2097);
  CHECK(fuso->lateral_drag == Fixed16::from_int(3000));
  CHECK(fuso->astern_drag_factor.raw() == 1671168);  // 25.5
  CHECK(fuso->turn_factor.raw() == 3879731);         // 59.2
  CHECK(fuso->render_size == 33000u << 16);
  CHECK(fuso->bow_wave_size == 34000u << 16);
  CHECK(fuso->fire_order == 3);
  CHECK(fuso->submerged_power.raw() == 0);
  CHECK(fuso->depth_charges == 0);
  CHECK(fuso->gun_mounts == std::array<bool, 2>{true, true});
  CHECK(fuso->gun_id == 6);
  CHECK(fuso->submerged_speed_kts.empty());
  CHECK(fuso->deck_height_ft == 45);
  CHECK(fuso->ballast_neutral == 0);

  const ShipClass* fubuki = tables.find_ship("FUBUKI");
  REQUIRE(fubuki != nullptr);
  CHECK(fubuki->ship_type == ShipType::kDestroyer);
  CHECK(fubuki->role == ShipRole::kEscort);
  CHECK(fubuki->has_sonar);
  CHECK(fubuki->screw_sound == ScrewSound::kHighSpeed);
  CHECK(fubuki->depth_charges == 36);
  CHECK(fubuki->dc_racks == 0);
  CHECK(fubuki->dc_throwers == 2);

  const ShipClass* kaidai = tables.find_ship("KAIDAI TYPE");
  REQUIRE(kaidai != nullptr);
  CHECK(kaidai->drag_cubic_submerged.raw() == 688);
  CHECK(kaidai->drag_linear_submerged.raw() == 2097);
  CHECK(kaidai->render_size == 17000u << 16);
  CHECK(kaidai->submerged_power.raw() == 2005);
  CHECK(kaidai->depth_charges == 14);  // its torpedoes
  CHECK(kaidai->gun_mounts == std::array<bool, 2>{false, true});
  CHECK(kaidai->gun_id == 2);
  CHECK(kaidai->submerged_speed_kts == "8");
  CHECK(kaidai->deck_height_ft == 19);
  CHECK(kaidai->displacement_tons == 1810);
  CHECK(kaidai->ballast_neutral == 2602);
  CHECK(kaidai->ballast_max == 2612);
  CHECK(tables.find_ship("SAMPAN")->selection_weight == -1);
  CHECK(tables.find_ship("SAMPAN")->screw_sound == ScrewSound::kNone);
  CHECK(tables.find_ship("NOPE") == nullptr);
}

TEST_CASE("WEAPONS.DAT decodes torpedoes with their run model, and guns", "[dat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto tables = GameTables::load(install);
  REQUIRE(tables.weapons.torpedoes.size() == 6);
  REQUIRE(tables.weapons.guns.size() == 6);
  const Torpedo* mk14 = tables.find_torpedo("mark14");
  REQUIRE(mk14 == &tables.weapons.torpedoes[1]);
  CHECK(mk14->long_name == "MARK 14");
  CHECK(mk14->id == 2);
  CHECK(mk14->nation == Nation::kUs);
  CHECK(mk14->year == 1930);
  CHECK(mk14->range_long_yd == 9000);
  CHECK(mk14->range_short_yd == 4500);
  CHECK(mk14->speed_low_kts == 31);
  CHECK(mk14->speed_high_kts == 46);
  CHECK(mk14->diameter_in == 21);
  CHECK(mk14->warhead_lb == 668);
  CHECK(mk14->length_ft == 12);
  CHECK(mk14->beam_ft == 2);
  CHECK(mk14->propulsion == TorpedoPropulsion::kSteam);
  CHECK(mk14->mk14_18_flag == 1);
  CHECK(mk14->speed_settings == 2);
  CHECK(mk14->displacement == 5);
  CHECK(mk14->reach_ft == std::array<std::int32_t, 2>{60, 90});
  CHECK(mk14->power[0].raw() == 1600);
  CHECK(mk14->power[1].raw() == 1640);
  CHECK(mk14->drag_cubic[0].raw() == 123);
  CHECK(mk14->drag_linear[1].raw() == 721);
  CHECK(mk14->lateral_drag[0] == Fixed16::from_int(50));
  CHECK(mk14->lateral_drag[1] == Fixed16::from_int(1000));
  CHECK(mk14->turn_factor.raw() == 1651507);  // 25.2
  CHECK(mk14->run_time_s == std::array<std::int32_t, 2>{530, 210});
  CHECK(mk14->consumption[0] == Fixed16::from_int(1));
  CHECK(mk14->consumption[1].raw() == 78643);  // 1.2

  CHECK(tables.find_torpedo("MARK18-1")->propulsion == TorpedoPropulsion::kElectric);
  const Torpedo* type95 = tables.find_torpedo_by_id(7);
  REQUIRE(type95 != nullptr);
  CHECK(type95->short_name == "TYPE95-1");
  CHECK(type95->nation == Nation::kJapan);
  CHECK(type95->propulsion == TorpedoPropulsion::kOxygen);

  const Gun& gun = tables.weapons.guns[5];
  CHECK(gun.long_name == "8 INCH 3RD YEAR TYPE");
  CHECK(gun.id == 6);
  CHECK(gun.caliber == "8");
  CHECK(gun.max_range_yd == 31600);
  CHECK(gun.muzzle_velocity_fps == 2756);
  CHECK(gun.shell_caliber_in == 6);
  CHECK(gun.reload_time_s == 15);
  CHECK(tables.weapons.guns[0].shell_caliber_in == 5);
  CHECK(tables.weapons.guns[0].reload_time_s == 4);
}

TEST_CASE("The rosters: CARSUB, IJNSHIPS, AIRCRAFT, CMDLIST, HOMEPORT", "[dat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto tables = GameTables::load(install);

  REQUIRE(tables.carsub.size() == 249);
  CHECK(tables.carsub[0].name == "Balao");
  CHECK(tables.carsub[0].hull_number == 285);
  CHECK(tables.carsub[0].class_index == 7);
  const CarsubBoat& wahoo = tables.carsub[114];
  CHECK(wahoo.name == "Wahoo");
  CHECK(wahoo.hull_number == 238);
  CHECK(wahoo.avail_year == 42);
  CHECK(wahoo.avail_month == 8);
  CHECK(wahoo.end_year == 43);
  CHECK(wahoo.end_month == 10);
  CHECK(tables.subs[wahoo.class_index].name == "GATO");

  REQUIRE(tables.ijn_ships.size() == 210);
  CHECK(tables.ijn_ships[0].ship_name == "Agano");
  CHECK(tables.ijn_ships[0].commission_year == 42);
  CHECK(tables.ijn_ships[0].end_month == 2);
  const IjnShip& yamato = tables.ijn_ships[185];
  CHECK(yamato.ship_name == "Yamato");
  CHECK(yamato.commission_year == 41);
  CHECK(yamato.commission_month == 12);
  CHECK(yamato.end_year == 45);
  CHECK(yamato.end_month == 4);
  CHECK(tables.find_ship(yamato.class_name)->length_ft == 862);

  REQUIRE(tables.aircraft.size() == 5);
  const AircraftType* zeke = tables.find_aircraft("ZEKE");
  REQUIRE(zeke != nullptr);
  CHECK(zeke->long_name == "MITSUBISHI A6M");
  CHECK(zeke->role == AircraftRole::kFighter);
  CHECK(zeke->sprite == "ZEKE.SHP");
  CHECK(zeke->intro_year == 1940);
  CHECK(zeke->max_speed_mph == 288);
  CHECK(zeke->ceiling_ft == 32800);
  CHECK(zeke->range_mi == 1940);
  CHECK(zeke->guns == 2);
  CHECK(zeke->bombs == 2);
  CHECK(zeke->cruise_speed_mph == 180);
  CHECK(zeke->torpedoes == 0);
  CHECK(zeke->roll_rate == 10);
  CHECK(zeke->render_size == 4500u << 16);
  const AircraftType* betty = tables.find_aircraft("BETTY");
  REQUIRE(betty != nullptr);
  CHECK(betty->length_ft == 67);
  CHECK(betty->span_ft == 82);
  CHECK(betty->height_ft == 19);
  CHECK(betty->torpedoes == 1);
  CHECK(betty->roll_rate == 4);

  REQUIRE(tables.commanders.size() == 308);
  CHECK(tables.commanders[0].name == "Fluckey, E. B.");
  std::int32_t total = 0;
  for (const std::int32_t t : tables.commanders[0].tonnage_by_month) {
    total += t;
  }
  CHECK(total == 94409);

  REQUIRE(tables.home_ports.size() == 10);
  const HomePort& pearl = tables.home_ports[0];
  CHECK(pearl.name == "Pearl Harbor");
  CHECK(pearl.flags == 1);
  CHECK(pearl.open_date.month == 11);
  CHECK(pearl.open_date.day == 7);
  CHECK(pearl.open_date.year == 1941);
  CHECK(pearl.close_date.month == 6);
  CHECK(pearl.close_date.year == 1942);
  CHECK(pearl.transfer_weight[6] == 50);
  CHECK(pearl.zone_weight[0][0] == 60);
  CHECK(pearl.zone_weight[0][44] == 50);
  CHECK(pearl.zone_weight[1][0] == 40);
  CHECK(pearl.zone_distance_nm[3] == 5200);
  CHECK(pearl.command == SubCommand::kComSubPac);
  CHECK(pearl.map_point == std::array<std::int32_t, 2>{401, 162});
  CHECK(pearl.label_offset == std::array<std::int32_t, 2>{-23, -4});
  CHECK(tables.home_ports[9].name == "Guam");
}

TEST_CASE("RMSG.DAT holds the radio-message templates", "[dat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto tables = GameTables::load(install);
  REQUIRE(tables.radio_messages.size() == 3);
  CHECK(tables.radio_messages[1] == "CALLING \x06...PILOT DOWN...LAST REPORTED POSITION CLOSE TO \x11,\x12");
  CHECK(tables.radio_messages[0].starts_with("AT \x02:\x02, \x01 \x04 BE ON LIFEGUARD STATION"));
}

TEST_CASE("DAT framing errors throw DataError", "[dat]") {
  SECTION("radio templates are reproduced and checked against the header size") {
    std::vector<std::uint8_t> data;
    put_u32(data, 6);
    put_u32(data, 2);
    put_u32(data, 3);
    data.insert(data.end(), {'A', 0x06, 0});
    put_u32(data, 3);
    data.insert(data.end(), {'B', 'C', 0});
    const auto texts = parse_rmsg_dat(data);
    REQUIRE(texts.size() == 2);
    CHECK(texts[0] == "A\x06");
    CHECK(texts[1] == "BC");
    data[0] = 7;
    CHECK_THROWS_AS(parse_rmsg_dat(data), DataError);
  }
  SECTION("a flat table must be a whole number of records") {
    const std::vector<std::uint8_t> data(CarsubBoat::kSize * 2 + 1, 0);
    CHECK_THROWS_AS(parse_carsub_dat(data), DataError);
    CHECK(parse_carsub_dat(std::span(data).first(CarsubBoat::kSize * 2)).size() == 2);
  }
  SECTION("an index entry pointing past the end") {
    std::vector<std::uint8_t> data{1};
    data.insert(data.end(), 28, 'X');
    put_u32(data, 33);
    data.resize(33 + AircraftType::kSize - 1);
    CHECK_THROWS_AS(parse_aircraft_dat(data), DataError);
    data.push_back(0);
    const auto aircraft = parse_aircraft_dat(data);
    REQUIRE(aircraft.size() == 1);
    CHECK(aircraft[0].index_name == std::string(28, 'X'));
  }
  SECTION("WEAPONS.DAT with trailing bytes") {
    std::vector<std::uint8_t> data;
    put_u32(data, 0);
    put_u32(data, 0);
    CHECK(parse_weapons_dat(data).torpedoes.empty());
    data.push_back(0);
    CHECK_THROWS_AS(parse_weapons_dat(data), DataError);
  }
}

TEST_CASE("Ephemeris files give Meeus' example 25.b", "[ephemeris][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto index = parse_planets_ndx(install.read("PLANETS.NDX"));
  const PlanetIndex& earth_index = index[static_cast<std::size_t>(PlanetBody::kEarth)];
  CHECK(earth_index[0][0].count == 64);
  CHECK(earth_index[0][5].count == 1);
  CHECK(earth_index[1][1].count == 2);
  CHECK(index[static_cast<std::size_t>(PlanetBody::kSun)][1][1].count == 7);

  const Ephemeris eph = Ephemeris::load(install);
  CHECK(eph.earth.series[0][0][0].a == Catch::Approx(1.75347045673));
  const double jde = 2448908.5;  // 1992-10-13.0 TD
  const HeliocentricPosition earth = eph.earth.heliocentric(jde);
  CHECK(earth.longitude_rad == Catch::Approx(-43.634847957556).epsilon(1e-12));
  CHECK(earth.latitude_rad == Catch::Approx(-0.000003122280).margin(1e-11));
  CHECK(earth.radius_au == Catch::Approx(0.997607745764).epsilon(1e-12));
  const HeliocentricPosition sun = eph.sun.heliocentric(jde);
  CHECK(sun.longitude_rad == Catch::Approx(-43.633088027277).epsilon(1e-12));
  CHECK(sun.radius_au == Catch::Approx(0.997607745642).epsilon(1e-12));
}

TEST_CASE("Ephemeris parsing rejects malformed files", "[ephemeris]") {
  CHECK_THROWS_AS(parse_planets_ndx(std::vector<std::uint8_t>(0x48 * 9 - 2, 0)), DataError);
  std::vector<std::uint8_t> ndx(0x48 * 9, 0);
  ndx[2] = 1;  // the Sun's first series has one term, the next starts at 0
  CHECK_THROWS_AS(parse_planets_ndx(ndx), DataError);
  PlanetIndex one{};
  one[0][0].count = 1;
  CHECK_THROWS_AS(parse_planet_theory(std::vector<std::uint8_t>(23, 0), one), DataError);
  CHECK(parse_planet_theory(std::vector<std::uint8_t>(24, 0), one).series[0][0].size() == 1);
}

TEST_CASE("SH.EXE is mapped and relocated at its linear addresses", "[exe][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  CHECK(exe.base() == 0x10000);
  REQUIRE(exe.objects().size() == 2);
  CHECK(exe.objects()[0].is_exec());
  CHECK(exe.objects()[1].base_address == 0xf0000);
  CHECK(exe.end() == 0xf0000 + 0xa4290);
  CHECK(exe.fixup_consistency() == 1.0);
  CHECK(exe.fixups_applied() == 46567);
  // The file names the loaders build paths from (load_subs_dat).
  CHECK(exe.c_string(0x104bb4) == "DATA\\");
  CHECK(exe.c_string(0x109500) == "SUBS.DAT");
  // A relocated pointer: the speaker table's first name.
  CHECK(exe.string_pointer(CrewMessages::kSpeakersAddress) == "OOD");
  CHECK(exe.u32(CrewMessages::kSpeakersAddress + 4) == 1);
  CHECK_THROWS_AS(exe.u32(exe.end() - 2), DataError);
  CHECK_THROWS_AS(exe.u8(0xfff), DataError);
}

TEST_CASE("Crew messages come from SH.EXE", "[exe][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const CrewMessages messages(ExeImage::load(install));
  REQUIRE(messages.all().size() == 77);
  const CrewMessage* deep = messages.find(71);
  REQUIRE(deep != nullptr);
  CHECK(deep->speaker == CrewSpeaker::kDivingOfficer);
  CHECK(deep->speaker_name == "Diving Officer");
  CHECK(deep->sound == "XO0001");
  CHECK(deep->text == "We're below test depth, sir!");
  const CrewMessage* screws = messages.find(41);
  REQUIRE(screws != nullptr);
  CHECK(screws->speaker == CrewSpeaker::kSound);
  CHECK(screws->sound == "RAD0003");
  CHECK(screws->text == "Hi-speed screws bearing \t relative.");
  CHECK(messages.find(0)->sound.empty());
  CHECK(messages.find(0)->text == "Skipper?");
  CHECK(messages.find(76)->speaker == CrewSpeaker::kDebug);
  CHECK(messages.find(200) == nullptr);
}

TEST_CASE("ExeImage rejects a file without an LE module", "[exe]") {
  std::vector<std::uint8_t> data(0x400, 0);
  data[0] = 'M';
  data[1] = 'Z';
  CHECK_THROWS_AS(ExeImage(data), DataError);
}

}  // namespace sh

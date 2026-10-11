// The game tables of the synthetic install (DATA/*.DAT): classes invented
// from a few archetypes, their numbers from rules (the motion model's power
// from the top speed, sizes from the type), in service over the whole war.
#include <cmath>
#include <format>

#include "assets/dat_tables.h"
#include "synthetic/generators.h"
#include "synthetic/table_encoders.h"

namespace sh::test::synth {

namespace {

constexpr double kUnitsPerSecondPerKnot = 27.008;
constexpr double kHorsepower = 550.0;

// The motion model's power cap for `knots` (`motion_power_for_speed`'s
// drag (c v)^3 + b v over 550).
double power_for_speed(double knots, double c, double b) {
  const double v = knots * kUnitsPerSecondPerKnot;
  return (std::pow(c * v, 3) + b * v) / kHorsepower;
}

std::string knots_text(double knots) {
  return std::fmod(knots, 1.0) == 0 ? std::format("{}", static_cast<int>(knots)) : std::format("{:.2f}", knots);
}

// A ship archetype: one per type, with its proportions.
struct ShipArchetype {
  ShipType type;
  ShipRole role;
  const char* code;
  int classes;
  int length_ft;
  double speed_kts;
  int hull;
  int guns;  // main battery
  int caliber_in;
  int depth_charges;
};

constexpr ShipArchetype kShipArchetypes[] = {
    {ShipType::kCarrier, ShipRole::kWarship, "CV", 2, 820, 32, 300, 0, 0, 0},
    {ShipType::kEscortCarrier, ShipRole::kWarship, "CVE", 1, 500, 21, 160, 0, 0, 0},
    {ShipType::kSeaplaneCarrier, ShipRole::kWarship, "CVS", 1, 600, 29, 180, 4, 5, 0},
    {ShipType::kBattleship, ShipRole::kWarship, "BB", 3, 720, 25, 400, 8, 14, 0},
    {ShipType::kHybridBattleship, ShipRole::kWarship, "BBV", 1, 700, 25, 380, 8, 14, 0},
    {ShipType::kHeavyCruiser, ShipRole::kWarship, "CA", 3, 630, 34, 250, 10, 8, 0},
    {ShipType::kLightCruiser, ShipRole::kWarship, "CL", 2, 530, 35, 200, 7, 6, 0},
    {ShipType::kDestroyer, ShipRole::kEscort, "DD", 4, 380, 35, 100, 6, 5, 36},
    {ShipType::kDestroyerEscort, ShipRole::kEscort, "DE", 2, 300, 27, 80, 3, 5, 60},
    {ShipType::kKaibokan, ShipRole::kEscort, "KB", 2, 250, 19, 70, 2, 5, 120},
    {ShipType::kSubmarine, ShipRole::kSubmarine, "SS", 2, 330, 23, 60, 1, 5, 18},
    {static_cast<ShipType>(12), ShipRole::kMerchant, "AK", 1, 400, 12, 90, 0, 0, 0},
    {static_cast<ShipType>(13), ShipRole::kMerchant, "AP", 1, 420, 14, 90, 1, 4, 0},
    {static_cast<ShipType>(14), ShipRole::kMerchant, "AO", 1, 480, 13, 110, 0, 0, 0},
    {static_cast<ShipType>(15), ShipRole::kMerchant, "AKL", 1, 300, 11, 70, 0, 0, 0},
    {static_cast<ShipType>(16), ShipRole::kMerchant, "AKS", 1, 350, 10, 80, 1, 3, 0},
    {static_cast<ShipType>(17), ShipRole::kMerchant, "ATR", 1, 450, 15, 100, 0, 0, 0},
    {static_cast<ShipType>(18), ShipRole::kMerchant, "ATC", 1, 260, 10, 60, 0, 0, 0},
    {static_cast<ShipType>(19), ShipRole::kMerchant, "AKV", 1, 380, 12, 85, 0, 0, 0},
    {static_cast<ShipType>(20), ShipRole::kMerchant, "AKX", 1, 340, 11, 75, 0, 0, 0},
    {ShipType::kSampan, ShipRole::kSmallCraft, "SMP", 1, 50, 7, 10, 0, 0, 0},
};

ShipClass ship_class(const ShipArchetype& a, int k, const std::string& index_name, const Model::ShipClassNames& names) {
  const double scale = 1.0 + 0.08 * k;
  const auto length = static_cast<std::uint32_t>(a.length_ft * scale);
  const auto beam = static_cast<std::uint32_t>(length / (a.role == ShipRole::kSmallCraft ? 4 : 9));
  const auto draft = static_cast<std::uint32_t>(beam / 3 + 2);
  const auto tons = static_cast<std::uint32_t>(length * beam * draft / 90);
  const double speed = a.speed_kts - k;
  const double c = 0.0015 + length / 150000.0;
  const double b = 0.032;
  const bool sub = a.role == ShipRole::kSubmarine;
  ShipClass s;
  s.index_name = index_name;
  s.name = names.name;
  s.ship_type = a.type;
  s.role = a.role;
  s.sprite1 = names.sprite;
  s.sprite2 = names.silhouette;
  s.idbook_picture = names.idbook;
  s.start_year = 1930 + static_cast<std::uint32_t>(k);
  s.start_month = 1;
  s.end_year = 1945;
  s.end_month = 12;
  s.selection_weight = a.role == ShipRole::kMerchant ? -1 : 10 + 5 * k;
  s.render_size = length * 0x1000;
  s.bow_wave_size = beam * 0x2000;
  s.category = static_cast<std::uint32_t>(a.type);
  s.screw_sound = speed > 20 ? ScrewSound::kHighSpeed : ScrewSound::kLowSpeed;
  s.hull_strength = static_cast<std::uint32_t>(a.hull);
  s.max_power = Fixed16::from_double(power_for_speed(speed, c, b));
  s.submerged_power = sub ? Fixed16::from_double(power_for_speed(8, c * 1.4, b)) : Fixed16();
  s.depth_charges = static_cast<std::uint32_t>(a.depth_charges);
  s.dc_racks = a.depth_charges > 0 ? 2 : 0;
  s.dc_throwers = a.depth_charges > 0 ? 2 + static_cast<std::uint32_t>(k) : 0;
  s.has_sonar = a.role == ShipRole::kEscort;
  s.main_gun_count = static_cast<std::uint32_t>(a.guns);
  s.main_gun_caliber_in = a.guns > 0 ? std::to_string(a.caliber_in) : "";
  s.gun_mounts = {a.guns > 0, a.guns > 1};
  s.gun_id = a.guns > 0 ? static_cast<std::uint32_t>(1 + (a.caliber_in > 5 ? 2 : 0)) : 0;
  s.top_speed_kts = knots_text(speed);
  s.submerged_speed_kts = sub ? "8" : "";
  s.length_ft = length;
  s.beam_ft = beam;
  s.draft_ft = draft;
  s.visual_size = std::min<std::uint32_t>(300, 60 + length / 3);
  s.deck_height_ft = sub ? 12 : 10 + length / 30;
  s.displacement_tons = tons;
  s.ballast_neutral = sub ? tons + tons / 3 : 0;
  s.ballast_max = sub ? tons + tons / 3 + 10 : 0;
  s.drag_cubic = Fixed16::from_double(c);
  s.drag_cubic_submerged = sub ? Fixed16::from_double(c * 1.4) : Fixed16();
  s.drag_linear = Fixed16::from_double(b);
  s.drag_linear_submerged = sub ? Fixed16::from_double(b * 1.2) : Fixed16();
  s.lateral_drag = Fixed16::from_int(length > 500 ? 3000 : 8000);
  s.astern_drag_factor = Fixed16::from_double(1.8);
  s.turn_factor = Fixed16::from_double(10.0 + length / 15.0);
  return s;
}

// The boat classes of the player, from the oldest to the newest.
std::vector<SubClass> sub_classes(const Context& context) {
  std::vector<SubClass> out;
  const std::vector<const FileManifest*> profiles = context.entries("ART/P_", "shp");
  for (int k = 0; k < 9; ++k) {
    SubClass s;
    s.index_name = std::format("SYN-{}", static_cast<char>('A' + k));
    s.name = s.index_name;
    s.display_name = std::format("SYNTHETIC {}", static_cast<char>('A' + k));
    s.class_index = static_cast<std::uint32_t>(k);
    s.picture_frame = static_cast<std::uint8_t>(k % 8);
    s.sprite_silhouette = std::format("SUB-{}.SHP", 1 + k % 8);
    s.sprite_profile = profiles.empty() ? "" : profiles[static_cast<std::size_t>(k) % profiles.size()]->path.substr(4);
    // In service from before the war (the older ones) or a later year,
    // to the war's end.
    s.intro_month = 1 + static_cast<std::uint32_t>(k % 12);
    s.intro_year = k < 5 ? 1930 + static_cast<std::uint32_t>(k) : 1937 + static_cast<std::uint32_t>(k);
    s.end_month = 10;
    s.end_year = 1945;
    const int length = 230 + 12 * k;
    const double surface_kts = 14.5 + 0.8 * k;
    const double submerged_kts = 8.0 + 0.15 * k;
    s.range_nm = 8000 + 500u * static_cast<std::uint32_t>(k);
    s.range_at_kts = 10;
    s.bow_tubes = k < 3 ? 4 : 6;
    s.stern_tubes = k < 2 ? 1 : k < 5 ? 2 : 4;
    s.bow_reloads = 8;
    s.stern_reloads = k < 5 ? 4 : 6;
    s.modern_torpedoes = k >= 2;
    s.deck_gun_count = 1;
    s.deck_gun_caliber_in = k < 6 ? 3 : 5;
    s.single_gun_mount = static_cast<std::uint32_t>(k % 2);
    s.length_ft = static_cast<std::uint32_t>(length);
    s.beam_ft = static_cast<std::uint32_t>(length / 12);
    s.draft_ft = 15;
    s.test_depth_ft = 200 + 25u * static_cast<std::uint32_t>(k);
    s.periscope_depth_ft = 60;
    s.radar_depth_ft = 45;
    s.periscope_eye_ft = 65;
    s.bridge_eye_ft = 25;
    s.awash_depth_ft = 25;
    s.noise_factor_pct = 100 - 5u * static_cast<std::uint32_t>(k);
    s.surface_speed_kts = knots_text(surface_kts);
    s.submerged_speed_kts = knots_text(submerged_kts);
    s.displacement_surfaced = 900 + 70u * static_cast<std::uint32_t>(k);
    s.displacement_submerged = s.displacement_surfaced + 400;
    s.displacement_max = s.displacement_submerged + 10;
    const double c_surface = 0.0035 + length / 200000.0;
    const double c_submerged = c_surface * 1.6;
    const double b = 0.032;
    // The boat's power figures are the motion model's 16.16 power as an
    // integer (SUBS.DAT keeps "hp" in those units).
    s.diesel_hp = static_cast<std::uint32_t>(power_for_speed(surface_kts, c_surface, b) * 65536 + 0.5);
    s.motor_hp = static_cast<std::uint32_t>(power_for_speed(submerged_kts, c_submerged, b) * 65536 + 0.5);
    s.drag_cubic = {Fixed16::from_double(c_surface), Fixed16::from_double(c_submerged)};
    s.drag_linear = {Fixed16::from_double(b), Fixed16::from_double(b * 1.2)};
    s.lateral_drag = Fixed16::from_int(6000);
    s.astern_drag_factor = Fixed16::from_double(1.8);
    s.turn_factor = Fixed16::from_double(20.0 + length / 20.0);
    s.pitch_factor = Fixed16::from_double(38.0);
    s.diesel_count = k < 4 ? 2 : 4;
    s.motor_count = 2;
    s.fuel_capacity = 90000 + 5000u * static_cast<std::uint32_t>(k);
    s.fuel_use = 3;
    s.deck_gun_rounds = 100;
    s.power_surfaced = {0, 20, 40, 60, 80, 100, -20, -40, -60, -100, 10};
    s.power_submerged = {0, 15, 30, 50, 75, 100, -15, -30, -50, -100, 5};
    s.description = std::format("A synthetic submarine class, number {}.", k + 1);
    for (std::size_t i = 0; i < SubClass::kCompartments; ++i) {
      const auto x = static_cast<std::int32_t>(40 + 27 * i);
      s.compartment_rects[i] = {x, 200, x + 24, 260};
    }
    out.push_back(std::move(s));
  }
  return out;
}

std::vector<AircraftType> aircraft_types(Context& context) {
  struct Archetype {
    AircraftRole role;
    int speed_mph;
    int guns;
    int bombs;
    int torpedoes;
  };
  constexpr Archetype kTypes[] = {{AircraftRole::kDiveBomber, 240, 2, 1, 0},
                                  {AircraftRole::kTorpedoBomber, 230, 1, 0, 1},
                                  {AircraftRole::kFighter, 330, 4, 0, 0},
                                  {AircraftRole::kTorpedoBomber, 270, 2, 1, 1},
                                  {AircraftRole::kLevelBomber, 280, 4, 4, 0}};
  std::vector<AircraftType> out;
  for (std::size_t i = 0; i < std::size(kTypes); ++i) {
    const Archetype& a = kTypes[i];
    AircraftType t;
    t.index_name = std::format("AIR-{}", i + 1);
    t.name = t.index_name;
    t.long_name = std::format("SYNTHETIC AIRCRAFT {}", i + 1);
    t.name2 = t.long_name;
    t.role = a.role;
    t.sprite = std::format("AIR{}.SHP", i + 1);
    t.intro_year = 1937;
    t.end_year = 1945;
    t.start_month = 1;
    t.end_month = 12;
    t.render_size = 0x40000;
    t.aircraft_id = static_cast<std::uint32_t>(i);
    t.cruise_speed_mph = static_cast<std::uint32_t>(a.speed_mph * 3 / 4);
    t.max_speed_mph = static_cast<std::uint32_t>(a.speed_mph);
    t.ceiling_ft = 30000;
    t.length_ft = 33 + 2 * static_cast<std::uint32_t>(i);
    t.span_ft = 40 + 3 * static_cast<std::uint32_t>(i);
    t.height_ft = 12;
    t.range_mi = 1200;
    t.guns = static_cast<std::uint32_t>(a.guns);
    t.bombs = static_cast<std::uint32_t>(a.bombs);
    t.torpedoes = static_cast<std::uint32_t>(a.torpedoes);
    t.roll_rate = 300;
    context.model.aircraft_sprites.push_back(t.sprite);
    context.model.aircraft.push_back(t.index_name);
    out.push_back(std::move(t));
  }
  return out;
}

// The torpedoes the code names (MARK10, MARK14, MARK18-1, MARK18-2, the
// Japanese TYPE95-1) and their run models.
Weapons weapons() {
  struct Spec {
    const char* name;
    Nation nation;
    std::uint32_t year, month;
    std::uint32_t slow_kts, fast_kts, long_yd, short_yd;
    TorpedoPropulsion propulsion;
  };
  constexpr Spec kTorpedoes[] = {
      {"MARK10", Nation::kUs, 1930, 1, 30, 36, 3500, 3500, TorpedoPropulsion::kSteam},
      {"MARK14", Nation::kUs, 1931, 1, 31, 46, 9000, 4500, TorpedoPropulsion::kSteam},
      {"MARK18-1", Nation::kUs, 1943, 10, 29, 29, 4000, 4000, TorpedoPropulsion::kElectric},
      {"MARK18-2", Nation::kUs, 1944, 8, 29, 29, 4000, 4000, TorpedoPropulsion::kElectric},
      {"TYPE95-1", Nation::kJapan, 1935, 1, 45, 49, 13000, 6500, TorpedoPropulsion::kOxygen},
  };
  Weapons w;
  std::uint32_t id = 1;
  for (const Spec& s : kTorpedoes) {
    Torpedo t;
    t.short_name = s.name;
    t.long_name = std::string("SYN ") + s.name;
    t.id = id++;
    t.nation = s.nation;
    t.month = s.month;
    t.year = s.year;
    t.range_long_yd = s.long_yd;
    t.range_short_yd = s.short_yd;
    t.speed_low_kts = s.slow_kts;
    t.speed_high_kts = s.fast_kts;
    t.diameter_in = 21;
    t.warhead_lb = 600;
    t.length_ft = 20;
    t.beam_ft = 2;
    t.propulsion = s.propulsion;
    t.mk14_18_flag = s.nation == Nation::kUs ? 1 : 0;
    t.speed_settings = s.slow_kts == s.fast_kts ? 1 : 2;
    t.displacement = 1;
    t.reach_ft = {100, 100};
    constexpr double c = 0.002;
    constexpr double b = 0.01;
    t.power = {Fixed16::from_double(power_for_speed(s.slow_kts, c, b)),
               Fixed16::from_double(power_for_speed(s.fast_kts, c, b))};
    t.drag_cubic = {Fixed16::from_double(c), Fixed16::from_double(c)};
    t.drag_linear = {Fixed16::from_double(b), Fixed16::from_double(b)};
    t.lateral_drag = {Fixed16::from_int(1000), Fixed16::from_int(1000)};
    t.turn_factor = Fixed16::from_double(4.0);
    // Run time: range / speed.
    t.run_time_s = {static_cast<std::int32_t>(s.long_yd * 3 * 3600 / (6077.0 * s.slow_kts)),
                    static_cast<std::int32_t>(s.short_yd * 3 * 3600 / (6077.0 * s.fast_kts))};
    t.consumption = {Fixed16::from_int(1), Fixed16::from_int(1)};
    w.torpedoes.push_back(std::move(t));
  }
  for (std::uint32_t g = 1; g <= 6; ++g) {
    Gun gun;
    gun.long_name = std::format("SYNTHETIC GUN {}", g);
    gun.id = g;
    gun.caliber = std::format("{}", 2 + g);
    gun.shell_caliber_in = 2 + g;
    gun.max_range_yd = 8000 + 2000 * g;
    gun.muzzle_velocity_fps = 2400;
    gun.reload_time_s = 4 + g;
    w.guns.push_back(std::move(gun));
  }
  return w;
}

}  // namespace

void generate_tables(Context& context) {
  // Ship classes and their sprite names.
  std::vector<ShipClass> ships;
  int number = 1;
  for (const ShipArchetype& a : kShipArchetypes) {
    for (int k = 0; k < a.classes; ++k, ++number) {
      Model::ShipClassNames names{.name = std::format("SYN {} {}", a.code, k + 1),
                                  .sprite = std::format("SHIP{:02}.SHP", number),
                                  .silhouette = std::format("SHIP{:02}S.SHP", number),
                                  .idbook = std::format("SHIP{:02}.SHP", number)};
      ships.push_back(ship_class(a, k, std::format("{}-{}", a.code, k + 1), names));
      context.model.ships.push_back(names);
      const std::string& index_name = ships.back().index_name;
      switch (a.role) {
        case ShipRole::kMerchant:
          context.model.merchants.push_back(index_name);
          break;
        case ShipRole::kEscort:
          context.model.escorts.push_back(index_name);
          break;
        case ShipRole::kWarship:
          context.model.warships.push_back(index_name);
          break;
        case ShipRole::kSubmarine:
          context.model.enemy_subs.push_back(index_name);
          break;
        default:
          break;
      }
    }
  }
  context.files["DATA/SHIPS.DAT"] = encode_ships_dat(ships);

  const std::vector<SubClass> subs = sub_classes(context);
  for (const SubClass& s : subs) {
    context.model.boats.emplace_back(s.index_name, s.intro_year);
  }
  context.files["DATA/SUBS.DAT"] = encode_subs_dat(subs);
  context.files["DATA/AIRCRAFT.DAT"] = encode_aircraft_dat(aircraft_types(context));
  context.files["DATA/WEAPONS.DAT"] = encode_weapons_dat(weapons());

  // Boats of each class, a new one every few months.
  std::vector<CarsubBoat> boats;
  for (std::uint32_t i = 0; i < 120; ++i) {
    const SubClass& c = subs[i % subs.size()];
    CarsubBoat b;
    b.name = std::format("Boat {}", i + 1);
    b.hull_number = 100 + i;
    b.avail_year = std::max<std::uint32_t>(41, c.intro_year - 1900 + i / 40);
    b.avail_month = 1 + i % 12;
    b.end_year = 45;
    b.end_month = 10;
    b.class_index = static_cast<std::uint8_t>(c.class_index);
    boats.push_back(std::move(b));
  }
  context.files["DATA/CARSUB.DAT"] = encode_carsub_dat(boats);

  // Named ships of the warship classes.
  std::vector<IjnShip> named;
  for (const ShipClass& c : ships) {
    if (c.role != ShipRole::kWarship) {
      continue;
    }
    for (int i = 0; i < 4; ++i) {
      IjnShip s;
      s.ship_name = std::format("{} No {}", c.index_name, i + 1);
      s.class_name = c.index_name;
      s.commission_year = 30 + static_cast<std::uint32_t>(i);
      s.commission_month = 1;
      s.end_year = 45;
      s.end_month = 12;
      named.push_back(std::move(s));
    }
  }
  context.files["DATA/IJNSHIPS.DAT"] = encode_ijnships_dat(named);

  // Commanders with steadily growing tonnage.
  std::vector<Commander> commanders;
  for (int i = 0; i < 40; ++i) {
    Commander c;
    c.name = std::format("Commander {}", i + 1);
    for (std::size_t m = 0; m < c.tonnage_by_month.size(); ++m) {
      c.tonnage_by_month[m] = static_cast<std::int32_t>(m) * (500 + 37 * i);
    }
    commanders.push_back(std::move(c));
  }
  context.files["DATA/CMDLIST.DAT"] = encode_cmdlist_dat(commanders);

  // Three home ports, one per command, open all war.
  std::vector<HomePort> ports;
  for (int i = 0; i < 3; ++i) {
    HomePort p;
    p.name = std::format("Port {}", static_cast<char>('A' + i));
    p.flags = 1;
    p.open_date = {0, 1, 1941};
    p.close_date = {11, 31, 1946};
    p.transfer_weight.fill(10);
    for (std::size_t z = 0; z < HomePort::kZones; ++z) {
      p.zone_weight[z].fill(z < 15 && static_cast<int>(z % 3) == i ? 10 : (z < 15 ? 2 : 0));
      p.zone_distance_nm[z] = 1500 + 200 * static_cast<std::int32_t>(z);
    }
    p.command = static_cast<SubCommand>(i);
    p.map_point = {100 + 150 * i, 150 + 40 * i};
    p.label_offset = {8, -8};
    ports.push_back(std::move(p));
  }
  context.files["DATA/HOMEPORT.DAT"] = encode_homeport_dat(ports);

  // The radio templates with each kind of placeholder.
  const std::vector<std::string> radio = {
      std::string("Convoy sighted at \x11 \x12, course \x03."),
      std::string("Patrol off \x05 until the \x02 of \x04."),
      std::string("Boat \x06: return to base, \x01 days."),
  };
  context.files["DATA/RMSG.DAT"] = encode_rmsg_dat(radio);
}

}  // namespace sh::test::synth

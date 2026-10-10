// The game tables of `DATA/*.DAT` (ship, submarine, aircraft and weapon
// classes, boat and ship rosters, commanders, home ports, radio messages).
// Layouts in `docs/structures.md`. `MAP<n>BASE.DAT` and `PZ.DAT` live
// elsewhere.
//
// Each record type names the decoded fields (offsets within the on-disk
// record in the comments) and keeps the record's `raw` bytes, so fields not
// decoded yet stay reachable. In memory the original uses the file layout
// as-is. Years marked "19xx" are stored as two digits.
//
// The original loaders (loadsave.cpp unless noted):
// - `load_subs_dat` (0x50e53): one SUBS.DAT class by name -> 0x11b120.
// - `load_ships_dat` (0x50f72) / `load_all_ships_dat` (0x51416): SHIPS.DAT.
// - `load_aircraft_dat` (0x5158e) / `load_all_aircraft_dat` (0x519c4).
// - `load_weapons_dat` (0x51b29): WEAPONS.DAT, both sections.
// - `load_cmdlist_dat` (0x51dc7), `load_carsub_dat` (0x51e7e),
//   `load_ijnships_dat` (0x51f3f), `load_rmsg_dat` (0x521d9).
// - `homeport_load_dat` (0x4d43a, campaign/homeport.cpp): HOMEPORT.DAT.
// The indexed tables are looked up by a case-insensitive name compare against
// the index entry (`stricmp`); a missing name is a fatal error there.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/campaign_month.h"
#include "core/fixed.h"
#include "core/install.h"

namespace sh {

// ---------------------------------------------------------------- SHIPS.DAT

// SHIPS.DAT `ship_type` (+25).
enum class ShipType : std::uint8_t {
  kCarrier = 0,          // CV
  kEscortCarrier = 1,    // CVE
  kSeaplaneCarrier = 2,  // CVS
  kBattleship = 3,       // BB
  kHybridBattleship = 4,  // BBV
  kHeavyCruiser = 5,     // CA
  kLightCruiser = 6,     // CL
  kDestroyer = 7,        // DD
  kDestroyerEscort = 8,  // DE
  kKaibokan = 9,
  kSubmarine = 11,  // SS
  // 12..20 are the merchant types (18 CTANKER).
  kSampan = 21,
};

// SHIPS.DAT `role` (+26): the contact/sighting class.
enum class ShipRole : std::uint8_t {
  kMerchant = 0,
  kEscort = 1,
  kWarship = 2,
  kSmallCraft = 3,
  kSubmarine = 4,
};

// SHIPS.DAT `screw_sound` (+100): what the hydrophones report.
enum class ScrewSound : std::uint32_t {
  kHighSpeed = 0,
  kLowSpeed = 1,
  kNone = 2,
};

// A SHIPS.DAT surface-ship (or IJN submarine) class, 244 bytes.
struct ShipClass {
  static constexpr std::size_t kSize = 0xf4;

  std::string index_name;  // the index entry's name (lookup key)
  std::string name;        // +0 char[25] "FUSO"
  ShipType ship_type{};    // +25
  ShipRole role{};         // +26
  std::string sprite1;     // +27 char[13] "FUSO.SHP" (3DSHIPS.CAT entry)
  std::string sprite2;     // +40 char[13] "FUSOS.SHP", may be empty
  std::string idbook_picture;  // +53 char[13] ART\IDBOOK.CAT picture ("FUSO.SHP")
  std::uint32_t start_year = 0;   // +68 service start
  std::uint32_t end_year = 0;     // +72
  std::uint32_t start_month = 0;  // +76
  std::uint32_t end_month = 0;    // +80
  std::int32_t selection_weight = 0;  // +84 generator weight within a role; -1 on merchants
  // +88 the 16.16 render size of the hull sprite (`ship_add_render` 0x8197e,
  // `enemy_sub_add_render` 0x7fb89: render_list_add's size; sprite scale = size /
  // distance); +92 that of the bow wave (`ship_render_cb` 0x81a84).
  std::uint32_t render_size = 0;
  std::uint32_t bow_wave_size = 0;
  std::uint32_t category = 0;         // +96 silhouette/type group
  ScrewSound screw_sound{};           // +100
  // +104 bit 0/1: the fire overlay is drawn behind the hull in the bow /
  // stern views (`ship_render_cb` -> `veh_draw_ship_sprite` 0x81b91).
  std::uint32_t fire_order = 0;
  std::uint32_t hull_strength = 0;    // +108 hit points (Yamato 600, BB 400)
  // +112 engine power cap of the motion model, compared with
  // ((c v)^3 + b v) / 550 (c, b below; v in world units/s); sets the top speed.
  Fixed16 max_power;
  // +116 the submerged power cap of the submarine classes (0 on surface
  // ships; `ai_sub_think` 0x13471 orders it below the surface).
  Fixed16 submerged_power;
  // +120 depth charges carried (torpedoes on the submarine classes), +124
  // racks and +128 throwers (charges per run: `ai_ship_drop_depth_charges`
  // 0x12dae; the SE class sheet's rows).
  std::uint32_t depth_charges = 0;
  std::uint32_t dc_racks = 0;
  std::uint32_t dc_throwers = 0;
  bool has_sonar = false;            // +132 (-1 on escorts)
  std::uint32_t main_gun_count = 0;  // +136
  std::string main_gun_caliber_in;   // +140 char[8] ASCII inches "14"
  // +148 / +152 non-zero: the fore / aft mount fires, +156 the WEAPONS.DAT
  // gun id it fires (`ai_ship_fire_guns` 0x12f7a); the warships' guns are
  // their secondary battery (FUSO fires gun 6).
  std::array<bool, 2> gun_mounts{};
  std::uint32_t gun_id = 0;
  std::string top_speed_kts;         // +160 char[8] ASCII knots "22.5"
  std::string submerged_speed_kts;   // +168 char[8] submarine classes ("8"), else empty
  std::uint32_t length_ft = 0;       // +176 (motion hull block starts here, +0xb0)
  std::uint32_t beam_ft = 0;         // +180
  std::uint32_t draft_ft = 0;        // +184
  std::uint32_t visual_size = 0;     // +188 sighting target size (BB 250-280, DD 100)
  // +192 the deck height above the waterline: the lookouts' and guns' height
  // (`ai_ship_fire_guns`), the fire and bow-wave height (`ship_render_cb`);
  // a submarine's awash depth (`enemy_sub_init` 0x7f12f -> +0x400).
  std::uint32_t deck_height_ft = 0;
  // +196 full load; mass = x 2200 lb. For the submarine classes, the
  // surfaced displacement of the dive model (`enemy_sub_init` -> +0x41c).
  std::uint32_t displacement_tons = 0;
  // +200 / +204 the submarine classes' neutral and maximum ballast (tons;
  // `enemy_sub_init` -> +0x420 / +0x424), as SUBS.DAT +212 / +216; 0 on
  // surface ships.
  std::uint32_t ballast_neutral = 0;
  std::uint32_t ballast_max = 0;
  Fixed16 drag_cubic;          // +208 c (BB 0.0062, DD 0.0025)
  // +212/+220 the submerged drag pair of the submarine classes (0 on surface
  // ships), the values SUBS.DAT keeps as its [surfaced, submerged] pairs.
  Fixed16 drag_cubic_submerged;
  Fixed16 drag_linear;         // +216 b (0.032)
  Fixed16 drag_linear_submerged;
  Fixed16 lateral_drag;        // +224 sideslip damping (3000 big, 8000 light ships)
  Fixed16 astern_drag_factor;  // +228 drag multiplier going astern
  Fixed16 turn_factor;         // +232 yaw-rate divisor, waypoint radius (BB 59.2)
  // +66 two bytes of padding (0). +236/+240 are filled at load time with the
  // 3DSHIPS.CAT sprite pointers of sprite1/sprite2 (`load_ships_dat`).
  std::array<std::uint8_t, kSize> raw{};
};

// ----------------------------------------------------------------- SUBS.DAT

// A SUBS.DAT US submarine class (the player's boat), 1704 bytes. The player
// object reads it at 0x11b120.
struct SubClass {
  static constexpr std::size_t kSize = 0x6a8;
  static constexpr std::size_t kTelegraphSettings = 11;
  static constexpr std::size_t kCompartments = 21;

  // Status-board hotspot of a compartment (pixels).
  struct Rect {
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t x2 = 0;
    std::int32_t y2 = 0;
  };

  std::string index_name;
  std::string name;               // +0 char[10] "GATO"
  std::string display_name;       // +10 char[26] "SALMON/SARGO"
  std::uint32_t class_index = 0;  // +36 this class's index (CARSUB `class_index`)
  // +40 the class's ART\SUBPICS.SHP picture (`subscrn_draw_sub_info` 0x718b7).
  std::uint8_t picture_frame = 0;
  std::string sprite_silhouette;  // +41 char[14] "SUB-7.SHP"
  std::string sprite_profile;     // +55 char[17] "P_GATO.SHP"
  std::uint32_t intro_month = 0;  // +72 1-based
  std::uint32_t intro_year = 0;   // +76
  std::uint32_t end_month = 0;    // +80 1-based (10: Oct 1945)
  std::uint32_t end_year = 0;     // +84 (1945)
  std::uint32_t range_nm = 0;     // +100 endurance
  std::uint32_t range_at_kts = 0;  // +104
  std::uint32_t bow_tubes = 0;     // +116
  std::uint32_t stern_tubes = 0;   // +120
  std::uint32_t bow_reloads = 0;   // +124 torpedoes in the forward racks
  std::uint32_t stern_reloads = 0;  // +128 torpedoes in the aft racks
  bool modern_torpedoes = false;    // +132 0: Mark 10 only, else Mark 14/18
  std::uint32_t deck_gun_count = 0;  // +136
  std::uint32_t deck_gun_caliber_in = 0;  // +140
  std::uint32_t single_gun_mount = 0;     // +144 mount of a one-gun boat (0 fwd, 1 aft)
  std::uint32_t length_ft = 0;            // +148
  std::uint32_t beam_ft = 0;              // +152
  std::uint32_t draft_ft = 0;             // +156 keel depth surfaced; SE's default player depth
  std::uint32_t test_depth_ft = 0;        // +160 (Gato 300, Balao 400)
  std::uint32_t periscope_depth_ft = 0;   // +164 (65)
  std::uint32_t radar_depth_ft = 0;       // +168 SD/SJ usable above it (48)
  std::uint32_t periscope_eye_ft = 0;     // +172 periscope height for sighting (68)
  std::uint32_t bridge_eye_ft = 0;        // +176 bridge lookout height
  std::uint32_t awash_depth_ft = 0;       // +180 decks awash below it
  std::uint32_t noise_factor_pct = 0;     // +184 sonar noise scaling (Tench 50)
  std::string surface_speed_kts;          // +188 char[10] ASCII "20.75"
  std::string submerged_speed_kts;        // +198 char[10] ASCII "8.75"
  // Displacements (tons); the dive model's ballast is `displacement_surfaced`
  // on the surface, rests at `displacement_submerged` (neutral) and floods up
  // to `displacement_max`.
  std::uint32_t displacement_surfaced = 0;   // +208 (SE's "Displace")
  std::uint32_t displacement_submerged = 0;  // +212 historical (Tambor 2370)
  std::uint32_t displacement_max = 0;        // +216 always +212 + 10
  // +220/+224 engine power caps of the motion model (same units as
  // `ShipClass::max_power`): diesels on the surface, motors submerged.
  std::uint32_t diesel_hp = 0;
  std::uint32_t motor_hp = 0;
  // +228..+252 the motion hull coefficients `sub_dive_update` passes to
  // `motion_step`; the pairs are [surfaced, deeper than the draft].
  std::array<Fixed16, 2> drag_cubic{};   // +228
  std::array<Fixed16, 2> drag_linear{};  // +236
  Fixed16 lateral_drag;                  // +244
  Fixed16 astern_drag_factor;            // +248
  Fixed16 turn_factor;                   // +252
  Fixed16 pitch_factor;                  // +256 (38.2)
  std::uint32_t diesel_count = 0;        // +260
  std::uint32_t motor_count = 0;         // +264
  std::uint32_t fuel_capacity = 0;       // +268
  std::uint32_t fuel_use = 0;            // +272 per second at full diesel power
  std::uint32_t deck_gun_rounds = 0;     // +276
  // Power % per telegraph setting 0..10 (1-5 ahead, 6-9 astern, 10 silent).
  std::array<std::int32_t, kTelegraphSettings> power_surfaced{};   // +280
  std::array<std::int32_t, kTelegraphSettings> power_submerged{};  // +324
  std::string description;  // +368 char[984] recognition-manual text
  std::array<Rect, kCompartments> compartment_rects{};  // +1352
  std::array<std::uint8_t, kSize> raw{};
};

// ------------------------------------------------------------- AIRCRAFT.DAT

// AIRCRAFT.DAT `role` (+75).
enum class AircraftRole : std::uint8_t {
  kFighter = 0,
  kTorpedoBomber = 1,
  kDiveBomber = 2,
  kLevelBomber = 3,
};

// An AIRCRAFT.DAT Japanese aircraft type, 168 bytes.
struct AircraftType {
  static constexpr std::size_t kSize = 0xa8;

  std::string index_name;
  std::string name;       // +0 char[25] "ZEKE"
  std::string long_name;  // +25 char[25] "MITSUBISHI A6M"
  std::string name2;      // +50 char[25]
  AircraftRole role{};    // +75
  std::string sprite;     // +76 char[16] "ZEKE.SHP"
  std::uint32_t intro_year = 0;   // +92
  std::uint32_t end_year = 0;     // +96
  std::uint32_t start_month = 0;  // +100
  std::uint32_t end_month = 0;    // +104
  // +108 the 16.16 render size of the sprite (`aircraft_add_render` 0x84871).
  std::uint32_t render_size = 0;
  std::uint32_t aircraft_id = 0;  // +116 (record order)
  std::uint32_t cruise_speed_mph = 0;  // +120
  std::uint32_t max_speed_mph = 0;  // +124
  std::uint32_t ceiling_ft = 0;     // +128
  std::uint32_t length_ft = 0;      // +132
  std::uint32_t span_ft = 0;        // +136
  std::uint32_t height_ft = 0;      // +140
  std::uint32_t range_mi = 0;       // +144
  std::uint32_t guns = 0;           // +148 > 0: can strafe
  std::uint32_t bombs = 0;          // +152 bomb load
  std::uint32_t torpedoes = 0;      // +156
  // +160 the flight model's roll rate, tenths (`aircraft_update_flight`
  // divides by 10). +164 is filled at load time with the 3DAIR.CAT sprite
  // pointer of `sprite` (`load_aircraft_dat` 0x5158e; 0 in the file).
  std::int32_t roll_rate = 0;
  std::array<std::uint8_t, kSize> raw{};
};

// -------------------------------------------------------------- WEAPONS.DAT

enum class Nation : std::uint32_t {
  kUs = 0,
  kJapan = 1,
};

enum class TorpedoPropulsion : std::uint8_t {
  kSteam = 0,
  kOxygen = 1,
  kElectric = 2,  // leaves no wake
};

// A WEAPONS.DAT torpedo, 156 bytes. The two speed settings are the in-game
// (long range, low speed) and (short range, high speed) choices; the
// per-setting arrays of the run model are indexed by the setting.
struct Torpedo {
  static constexpr std::size_t kSize = 0x9c;

  std::string short_name;  // +0 char[10] "MARK14" (lookup key)
  std::string long_name;   // +10 char[26] "MARK 14"
  std::uint32_t id = 0;    // +36 1..8 (2 = Mark 14, the dud rules)
  Nation nation{};         // +40
  std::uint32_t month = 0;    // +44 1-based introduction month (Mark 18-1 10, 18-2 8)
  std::uint32_t year = 0;     // +48 introduction
  std::uint32_t range_long_yd = 0;   // +52 at low speed
  std::uint32_t range_short_yd = 0;  // +56 at high speed
  std::uint32_t speed_low_kts = 0;   // +60
  std::uint32_t speed_high_kts = 0;  // +64
  std::uint32_t diameter_in = 0;     // +68
  std::uint32_t warhead_lb = 0;      // +72
  // Motion and run model (torpedo_motion_init/update).
  std::uint32_t length_ft = 0;  // +76 launch offset, collision box
  std::uint32_t beam_ft = 0;    // +80
  TorpedoPropulsion propulsion{};  // +84
  std::uint8_t mk14_18_flag = 0;   // +85 1 on Mark 14/18 (no reader found)
  std::uint32_t speed_settings = 0;  // +88
  std::uint32_t displacement = 0;    // +92
  std::array<std::int32_t, 2> reach_ft{};  // +96 TDC run-time offset
  std::array<Fixed16, 2> power{};          // +104 motion power (as ShipClass::max_power)
  std::array<Fixed16, 2> drag_cubic{};     // +112 hull coefficients, as SHIPS.DAT +208..
  std::array<Fixed16, 2> drag_linear{};    // +120
  std::array<Fixed16, 2> lateral_drag{};   // +128
  Fixed16 turn_factor;                     // +136
  std::array<std::int32_t, 2> run_time_s{};  // +140 (Mark 14 530 / 210)
  std::array<Fixed16, 2> consumption{};      // +148 run-time burn factor
  std::array<std::uint8_t, kSize> raw{};
};

// A WEAPONS.DAT gun (IJN deck and AA guns), 64 bytes.
struct Gun {
  static constexpr std::size_t kSize = 0x40;

  std::string long_name;  // +0 char[32] "5 INCH TYPE 89"
  std::uint32_t id = 0;   // +32 1..6
  std::string caliber;    // +36 char[12] ASCII "5.5"
  // +48 the caliber (inches, 5/6) of the shells `ai_ship_fire_guns` (0x12f7a)
  // spawns: their damage (`shell_spawn`).
  std::uint32_t shell_caliber_in = 0;
  std::uint32_t max_range_yd = 0;         // +52
  std::uint32_t muzzle_velocity_fps = 0;  // +56
  // +60 the AI's reload time (4..15 s): (3 - crew) x it, x 3 on merchants,
  // plus up to 3/4 more at random (`ai_ship_fire_guns`).
  std::uint32_t reload_time_s = 0;
  std::array<std::uint8_t, kSize> raw{};
};

// ------------------------------------------------------------- the rosters

// A CARSUB.DAT US submarine boat, 41 bytes; performance from SUBS.DAT.
struct CarsubBoat {
  static constexpr std::size_t kSize = 41;

  std::string name;                // +0 char[20] "Balao"
  std::uint32_t hull_number = 0;   // +20 SS-xxx
  std::uint32_t avail_year = 0;    // +24 available from (19xx)
  std::uint32_t avail_month = 0;   // +28 (1-12)
  std::uint32_t end_year = 0;      // +32 loss/decommission, 45/10 = war end (19xx)
  std::uint32_t end_month = 0;     // +36
  std::uint8_t class_index = 0;    // +40 SUBS.DAT record index
  std::array<std::uint8_t, kSize> raw{};
};

// An IJNSHIPS.DAT individual Japanese ship, 66 bytes; stats from SHIPS.DAT.
struct IjnShip {
  static constexpr std::size_t kSize = 66;

  std::string ship_name;   // +0 char[25] "Agano"
  std::string class_name;  // +25 char[25], a SHIPS.DAT class name
  std::uint32_t commission_year = 0;   // +50 (19xx)
  std::uint32_t commission_month = 0;  // +54
  std::uint32_t end_year = 0;          // +58 sunk/decommissioned (19xx)
  std::uint32_t end_month = 0;         // +62
  std::array<std::uint8_t, kSize> raw{};
};

// A CMDLIST.DAT historical submarine commander, 230 bytes.
struct Commander {
  static constexpr std::size_t kSize = 230;

  std::string name;  // +0 char[50] "Fluckey, E. B."
  std::array<std::int32_t, kCampaignMonths> tonnage_by_month{};  // +50 (unaligned)
  std::array<std::uint8_t, kSize> raw{};
};

// ------------------------------------------------------------ HOMEPORT.DAT

// A date as HOMEPORT.DAT stores it.
struct PortDate {
  std::int32_t month = 0;  // 0-based
  std::int32_t day = 0;
  std::int32_t year = 0;   // four digits
};

enum class SubCommand : std::int32_t {
  kComSubsAsiatic = 0,
  kComSubPac = 1,
  kComSubSoWesPac = 2,
  kTaskForce42 = 3,
};

// A HOMEPORT.DAT submarine base, 3928 bytes. Open while open <= today < close.
struct HomePort {
  static constexpr std::size_t kSize = 0xf58;
  static constexpr std::size_t kZones = 20;

  std::string name;           // +0x000 char[20] "Pearl Harbor"
  std::uint32_t flags = 0;    // +0x014 bit 0: can be a transfer destination
  PortDate open_date;         // +0x018
  PortDate close_date;        // +0x024
  std::array<std::int32_t, kCampaignMonths> transfer_weight{};  // +0x030
  // +0x0e4 patrol-zone pick weight per [zone][campaign month].
  std::array<std::array<std::int32_t, kCampaignMonths>, kZones> zone_weight{};
  std::array<std::int32_t, kZones> zone_distance_nm{};  // +0xef4 transit time and fuel
  SubCommand command{};                                  // +0xf44
  std::array<std::int32_t, 2> map_point{};               // +0xf48
  std::array<std::int32_t, 2> label_offset{};            // +0xf50
  std::array<std::uint8_t, kSize> raw{};
};

// ---------------------------------------------------------------- RMSG.DAT

// Placeholder bytes of a radio-message template (`cquarter_format_message`,
// 0x33dc7); each but kBoat, kLatitude and kLongitude takes the next argument.
enum class RadioPlaceholder : std::uint8_t {
  kNumber = 0x01,
  kTwoDigits = 0x02,
  kThreeDigits = 0x03,
  kMonthName = 0x04,
  kBaseName = 0x05,       // index into the patrol map's MAP<n>BASE.DAT
  kBoatName = 0x06,       // the player's boat
  kLocationType = 0x07,   // a .WLD location-type name, -1 "Harbor"
  kLatitude = 0x11,       // from the message's position
  kLongitude = 0x12,
};

// --------------------------------------------------------------- parsing

// Parses each file's content; throws `DataError` on a framing mismatch.
std::vector<ShipClass> parse_ships_dat(std::span<const std::uint8_t> data);
std::vector<SubClass> parse_subs_dat(std::span<const std::uint8_t> data);
std::vector<AircraftType> parse_aircraft_dat(std::span<const std::uint8_t> data);
// Returns both sections of WEAPONS.DAT.
struct Weapons {
  std::vector<Torpedo> torpedoes;
  std::vector<Gun> guns;
};
Weapons parse_weapons_dat(std::span<const std::uint8_t> data);
std::vector<CarsubBoat> parse_carsub_dat(std::span<const std::uint8_t> data);
std::vector<IjnShip> parse_ijnships_dat(std::span<const std::uint8_t> data);
std::vector<Commander> parse_cmdlist_dat(std::span<const std::uint8_t> data);
std::vector<HomePort> parse_homeport_dat(std::span<const std::uint8_t> data);
// Returns the radio-message templates (without their NUL), placeholders as
// `RadioPlaceholder` bytes.
std::vector<std::string> parse_rmsg_dat(std::span<const std::uint8_t> data);

// Compares names like the original's `stricmp`.
bool equals_ignore_case(std::string_view a, std::string_view b);

// All the game tables of DATA/, loaded at once.
struct GameTables {
  std::vector<ShipClass> ships;
  std::vector<SubClass> subs;
  std::vector<AircraftType> aircraft;
  Weapons weapons;
  std::vector<CarsubBoat> carsub;
  std::vector<IjnShip> ijn_ships;
  std::vector<Commander> commanders;
  std::vector<HomePort> home_ports;
  std::vector<std::string> radio_messages;

  // Throws `DataError` if a file is missing or malformed.
  static GameTables load(const Install& install);

  // Return the record whose lookup name matches `name` case-insensitively
  // (index name for the indexed tables, `short_name` for torpedoes), or
  // nullptr. The first match wins, as in the original linear searches.
  const ShipClass* find_ship(std::string_view name) const;
  const SubClass* find_sub(std::string_view name) const;
  const AircraftType* find_aircraft(std::string_view name) const;
  const Torpedo* find_torpedo(std::string_view name) const;
  // Returns the first torpedo with `Torpedo::id` == `id`, or nullptr
  // (`weapons_find`, 0x51c54, by id).
  const Torpedo* find_torpedo_by_id(std::uint32_t id) const;
};

}  // namespace sh

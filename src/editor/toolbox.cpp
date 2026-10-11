#include "editor/toolbox.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "gfx/sprite.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

// SE strings of the panels (sbchart_init_dialogs) and their callbacks.
namespace str {
// Player sub (0xab4d8).
constexpr std::uint32_t kDepth = 0x91e6a;      // "Depth:"
constexpr std::uint32_t kFeetDot = 0x91e71;    // "ft."
constexpr std::uint32_t kTest = 0x91e75;       // "Test:"
constexpr std::uint32_t kPeri = 0x91e7b;       // "PERI"
constexpr std::uint32_t kRad = 0x91e80;        // "RAD"
constexpr std::uint32_t kSurf = 0x91e84;       // "SURF"
constexpr std::uint32_t kPlayerLoc = 0x91e89;  // "Location:"
constexpr std::uint32_t kPlayerLocGap = 0x924ad;
// Ship (0xaba54).
constexpr std::uint32_t kShipSpeed = 0x91e93;    // "Speed"
constexpr std::uint32_t kShipKnots = 0x91e99;    // "knots"
constexpr std::uint32_t kShipMax = 0x91e9f;      // "Max:"
constexpr std::uint32_t kShipDamage = 0x91ea4;   // "Damage"
constexpr std::uint32_t kShipPercent = 0x91eab;  // "%"
constexpr std::uint32_t kShipLoc = 0x91ead;      // "Location:"
constexpr std::uint32_t kShipConvoy = 0x91eb7;   // "Convoy:"
constexpr std::uint32_t kShipLocGap = 0x9251b;
constexpr std::uint32_t kAlerted = 0x92034;   // "Alerted:"
constexpr std::uint32_t kZigzag = 0x9202b;    // " Zigzag:"
constexpr std::uint32_t kNoOption = 0x9203d;  // ""
// Enemy sub (0xac0dc).
constexpr std::uint32_t kSubSpeed = 0x91ebf;
constexpr std::uint32_t kSubKnots = 0x91ec5;
constexpr std::uint32_t kSubSpeedMax = 0x91ecb;
constexpr std::uint32_t kSubDepth = 0x91ed0;  // "Depth"
constexpr std::uint32_t kSubFeet = 0x91ed6;   // "ft"
constexpr std::uint32_t kSubDepthMax = 0x91ed9;
constexpr std::uint32_t kSubDamage = 0x91ede;  // "Damage:"
constexpr std::uint32_t kSubPercent = 0x91ee6;
constexpr std::uint32_t kSubLoc = 0x91ee8;
constexpr std::uint32_t kSubLocGap = 0x92532;
// Aircraft (0xac7c4).
constexpr std::uint32_t kBombs = 0x91ef2;         // "Bombs:"
constexpr std::uint32_t kMaxBombs = 0x91ef9;      // "Max Bombs:"
constexpr std::uint32_t kAirConvoy = 0x91f04;     // "Convoy:"
constexpr std::uint32_t kGuns = 0x91f0c;          // "Guns:"
constexpr std::uint32_t kRegenerate = 0x91f12;    // "Regenerate:"
constexpr std::uint32_t kNoRadius = 0x92567;      // ""
constexpr std::uint32_t kPatrolRadius = 0x92574;  // "Patrol Radius: "
constexpr std::uint32_t kYards = 0x9259c;         // " yards"
// Airstrike (0xacba8) and photo recon (0xad090), in that order.
struct MissionStrings {
  std::uint32_t delay, colon1, hrs1, mins1, location, hold, colon2, hrs2, mins2;
};
constexpr MissionStrings kAirstrike{0x91f1e, 0x91f29, 0x91f2b, 0x91f2f, 0x91f34, 0x91f3e, 0x91f4b, 0x91f4d, 0x91f51};
constexpr MissionStrings kPhotoRecon{0x91f56, 0x91f61, 0x91f63, 0x91f67, 0x91f6c, 0x91f76, 0x91f81, 0x91f83, 0x91f87};
constexpr std::uint32_t kMissionLocGap = 0x925af;
// The add tools' errors.
constexpr std::uint32_t kError = 0x923d7;         // "ERROR"
constexpr std::uint32_t kNoAirstrike = 0x923bf;   // "CANNOT SETUP AIRSTRIKE!"
constexpr std::uint32_t kNoBases = 0x923a0;       // "No valid enemy bases available"
constexpr std::uint32_t kErrorRecon = 0x9242b;    // "ERROR"
constexpr std::uint32_t kNoPhotoRecon = 0x92409;  // "CANNOT SETUP PHOTO_RECON MISSION!"
constexpr std::uint32_t kNoBasesRecon = 0x923ea;  // "No valid enemy bases available"
constexpr std::uint32_t kEmpty = 0x923dd;         // ""
}  // namespace str

// `char*` tables (the ints after a menu's labels are its values).
namespace tbl {
constexpr std::uint32_t kDigits = 0x988dc;          // "0" .. "9"
constexpr std::uint32_t kEngine = 0x9e6e0;          // "Stop" .. "Back 1/3", NULL
constexpr std::uint32_t kCrew = 0x9e6c8;            // "Green Crew" ..
constexpr std::uint32_t kPilots = 0x9e6d4;          // "Green Pilot" ..
constexpr std::uint32_t kOrders = 0x9e640;          // "None" .. "Fishing", by value
constexpr std::uint32_t kConvoys = 0x9e730;         // "NA", "A" .. "H"
constexpr std::uint32_t kOffOn = 0x9e754;           // "Off", "On"
constexpr std::uint32_t kNoYes = 0x9e75c;           // "No", "Yes"
constexpr std::uint32_t kStrikeSizes = 0x9e774;     // "Strike Size: SMALL" ..
constexpr std::uint32_t kEscortMenu = 0x9e660;      // None / Escort / Patrol, values 0x9e66c
constexpr std::uint32_t kMerchantMenu = 0x9e678;    // None / Convoy Leader / Convoy Member, 0x9e684
constexpr std::uint32_t kSmallCraftMenu = 0x9e690;  // None / Fishing, 0x9e698
constexpr std::uint32_t kSubMenu = 0x9e6a0;         // None / Patrol, 0x9e6a8
constexpr std::uint32_t kMissionMenu = 0x9e6b0;     // Patrol / Escort / Follow Path, 0x9e6bc
}  // namespace tbl

// SBCHART.SHP frames of the two indicators (seen from the player, inside
// its radar range).
constexpr std::size_t kSeenIcon = 0x12;
constexpr std::size_t kRadarIcon = 0x13;
// The panel face, B[1] (0xa642c), behind the indicators.
constexpr std::uint8_t kPanelFace = 6;
// The heading digits: hundreds, tens, units at y 251.
constexpr std::array<int, 3> kHeadingDigitX{32, 51, 70};
constexpr int kHeadingDigitY = 251;
// The digit cycle buttons are 16 pixels wide.
constexpr int kDigitW = 16;
// Headings are 0..359; an enemy sub dives to 64 ft at most.
constexpr std::int32_t kMaxHeading = 359;
constexpr std::int32_t kMaxSubDepth = 0x40;
constexpr std::int32_t kMaxDamage = 99;
constexpr int kDigitTries = 10;

// The `len` labels and values of the menu at `labels` (its values follow
// the label pointers).
OrdersMenu read_menu(const SeExe& se, std::uint32_t labels, std::size_t count, std::int32_t current) {
  OrdersMenu m;
  for (std::size_t i = 0; i < count; ++i) {
    m.labels.emplace_back(se.table_str(labels, i));
    m.values.push_back(se.i32(labels + static_cast<std::uint32_t>(4 * (count + i))));
    m.disabled.push_back(false);
  }
  // sb_find_int_index (0x3f8c2): 0 when absent.
  for (std::size_t i = 0; i < count; ++i) {
    if (m.values[i] == current) {
      m.initial = static_cast<int>(i);
      break;
    }
  }
  return m;
}

// Appends `orders` to `m` with its label of the orders table, the current
// entry when it is `current` (not in the original).
void append_orders(const SeExe& se, OrdersMenu& m, ShipOrders orders, std::int32_t current) {
  const auto value = static_cast<std::int32_t>(orders);
  if (value == current) {
    m.initial = static_cast<int>(m.labels.size());
  }
  m.labels.emplace_back(se.table_str(tbl::kOrders, static_cast<std::size_t>(value)));
  m.values.push_back(value);
  m.disabled.push_back(false);
}

// strcpy into a record's char field (cut to fit).
template <std::size_t N>
void copy_text(ScnChars<N>& field, std::string_view text) {
  const std::size_t n = std::min(text.size(), N - 1);
  std::copy_n(text.begin(), n, field.begin());
  field[n] = '\0';
}

// The ASCII knots of a SHIPS.DAT speed field at `offset`, rounded half to
// even as the x87 store (`sb_esub_max_speed`).
std::int32_t ascii_knots(const ShipClass& ship, std::size_t offset) {
  constexpr std::size_t kSpeedField = 8;
  std::string text;
  for (std::size_t i = 0; i < kSpeedField && ship.raw[offset + i] != 0; ++i) {
    text += static_cast<char>(ship.raw[offset + i]);
  }
  return static_cast<std::int32_t>(std::lrint(std::strtod(text.c_str(), nullptr)));
}

// The delay and hold fields of an airstrike or photo recon (0xaad80 /
// 0xaad84): the airstrike's launch delay and hold, the photo recon's delay
// and duration.
std::int32_t& mission_delay(SpecialMission& m) {
  return std::visit(
      [](auto& r) -> std::int32_t& {
        if constexpr (std::is_same_v<std::decay_t<decltype(r)>, BomberGroupRecord>) {
          return r.launch_delay_min;
        } else {
          return r.delay_min;
        }
      },
      m.rec);
}

std::int32_t& mission_hold(SpecialMission& m) {
  return std::visit(
      [](auto& r) -> std::int32_t& {
        if constexpr (std::is_same_v<std::decay_t<decltype(r)>, BomberGroupRecord>) {
          return r.hold_min;
        } else {
          return r.duration_min;
        }
      },
      m.rec);
}

}  // namespace

// --- Free helpers ---------------------------------------------------------

std::int32_t cycle_digit(std::int32_t value, std::int32_t place, const std::function<bool(std::int32_t)>& accept) {
  // toolbox_*_digit*: `do { digit++ (9 -> 0); value = ... } while (!fits)`.
  const std::int32_t current = (value / place) % 10;
  std::int32_t digit = current;
  for (int i = 0; i < kDigitTries; ++i) {
    digit = digit == 9 ? 0 : digit + 1;
    const std::int32_t next = value + (digit - current) * place;
    if (accept(next)) {
      return next;
    }
  }
  return value;
}

std::int32_t time_digit(std::int32_t minutes, int digit) {
  const std::int32_t hours = minutes / 60;
  switch (digit) {
    case 0:
      return hours / 10;
    case 1:
      return hours % 10;
    case 2:
      return minutes % 60 / 10;
    default:
      return minutes % 60 % 10;
  }
}

std::int32_t cycle_time_digit(std::int32_t minutes, int digit) {
  // toolbox_strike_{delay,hold}_digit_{h10,h1,m10,m1} (0x408c5..0x40b75).
  const std::int32_t hours = minutes / 60;
  const std::int32_t mins = minutes % 60;
  switch (digit) {
    case 0: {
      const std::int32_t d = hours / 10 == 9 ? 0 : hours / 10 + 1;
      return d * 600 + hours % 10 * 60 + mins;
    }
    case 1: {
      const std::int32_t d = hours % 10 == 9 ? 0 : hours % 10 + 1;
      return mins + (hours / 10 * 10 + d) * 60;
    }
    case 2: {
      const std::int32_t d = mins / 10 >= 5 ? 0 : mins / 10 + 1;
      return mins % 10 + d * 10 + hours * 60;
    }
    default: {
      const std::int32_t d = mins % 10 == 9 ? 0 : mins % 10 + 1;
      return mins / 10 * 10 + d + hours * 60;
    }
  }
}

OrdersMenu vessel_orders_menu(const Scenario& scenario, const Vessel& vessel) {
  const SeExe& se = scenario.data().se;
  if (vessel.kind == Vessel::Kind::kSubmarine) {
    // toolbox_esub_orders_button (0x3ffd8). Editor limit (enemy sub
    // orders): only None and "Patrol" (Follow Path); Intercept and its
    // target are never set, but with the corrections.
    const auto current = static_cast<std::int32_t>(vessel.sub.orders);
    OrdersMenu m = read_menu(se, tbl::kSubMenu, 2, current);
    if (scenario.corrected()) {
      append_orders(se, m, ShipOrders::kIntercept, current);
    }
    return m;
  }
  // toolbox_ship_orders_button (0x3f5ae), by the SHIPS.DAT role.
  const std::optional<std::size_t> cls = scenario.data().find_ship(vessel.class_name());
  if (!cls) {
    // The original reads past SHIPS.DAT; here no menu.
    return {};
  }
  const auto current = static_cast<std::int32_t>(vessel.ship.orders);
  const std::int32_t convoy =
      vessel.ship.orders == ShipOrders::kConvoyLeader ? vessel.ship.convoy_group : scenario.first_free_convoy_id();
  OrdersMenu m;
  switch (scenario.data().tables.ships[*cls].role) {
    case ShipRole::kMerchant:
    case ShipRole::kWarship:
      m = read_menu(se, tbl::kMerchantMenu, 3, current);
      m.disabled[1] = convoy == -1;
      m.disabled[2] = !scenario.other_convoy_exists(convoy);
      break;
    case ShipRole::kEscort:
      m = read_menu(se, tbl::kEscortMenu, 3, current);
      m.disabled[1] = !scenario.other_convoy_exists(0);
      break;
    case ShipRole::kSmallCraft:
      m = read_menu(se, tbl::kSmallCraftMenu, 2, current);
      break;
    default:
      // The submarine role (a ship of it has turned into an enemy sub):
      // the original's menu pointer is uninitialised.
      return m;
  }
  if (scenario.corrected()) {
    // Editor limit (ship orders): SE offers no Follow Path, which the game
    // reads from the shipped scenarios.
    append_orders(se, m, ShipOrders::kFollowPath, current);
  }
  return m;
}

OrdersMenu aircraft_mission_menu(const Scenario& scenario, AircraftMission current) {
  // sbchart_pick_aircraft_mission (0x40c3d): Escort greyed without a convoy.
  OrdersMenu m = read_menu(scenario.data().se, tbl::kMissionMenu, 3, static_cast<std::int32_t>(current));
  m.disabled[1] = !scenario.other_convoy_exists(0);
  return m;
}

// --- Toolbox --------------------------------------------------------------

Toolbox::Toolbox(ChartScreen& chart, const ChartScreenEnv& env, Scenario& scenario, const ChartHooks& hooks)
    : chart_(chart),
      env_(env),
      scenario_(scenario),
      hooks_(hooks),
      heading_entry_(kHeadingEntryX, kHeadingEntryY, 48, 3, "") {}

void Toolbox::add_controls(ui::Dialog& toolbox, EditorObject& selected) {
  // sbchart_toolbox_open (0x3b92c): the type dialog's controls follow the
  // TOOLBOX's (its last control links to them).
  heading_controls_.clear();
  guns_ = nullptr;
  name_button_ = nullptr;
  escort_refresh_ = false;
  if (auto* p = dynamic_cast<PlayerSub*>(&selected)) {
    add_player(toolbox, *p);
  } else if (auto* v = dynamic_cast<Vessel*>(&selected)) {
    leader_speed_ = -1;
    leader_heading_ = -1;
    add_vessel(toolbox, *v);
  } else if (auto* a = dynamic_cast<Aircraft*>(&selected)) {
    add_aircraft(toolbox, *a);
  } else if (auto* m = dynamic_cast<SpecialMission*>(&selected)) {
    add_mission(toolbox, *m);
  }
}

void Toolbox::closed() {
  heading_controls_.clear();
  guns_ = nullptr;
  name_button_ = nullptr;
}

void Toolbox::update() {
  // sbchart_toolbox_update (0x3c1d0) past the gauge.
  auto* v = dynamic_cast<Vessel*>(scenario_.selected());
  if (v == nullptr || v->type() != ObjectType::kShip) {
    return;
  }
  bool changed = false;
  if (leader_speed_ != v->ship.speed) {
    leader_speed_ = v->ship.speed;
    changed = true;
  }
  if (leader_heading_ != v->ship.heading) {
    leader_heading_ = v->ship.heading;
    changed = true;
  }
  if (v->ship.orders != ShipOrders::kConvoyLeader) {
    return;
  }
  if (changed) {
    scenario_.copy_convoy_course(v->ship.convoy_group);
  }
  if (escort_refresh_) {
    // The flag is set again rather than cleared: once a leader's speed has
    // changed, its escorts follow it every frame until the toolbox reopens
    // (harmless: the escorts cannot change meanwhile, the result is the
    // same).
    scenario_.update_escort_speed(v->ship.convoy_group);
  }
}

void Toolbox::tool(ToolboxTool which) {
  const SeExe& se = scenario_.data().se;
  switch (which) {
    case ToolboxTool::kAddShip: {
      // sbchart_tool_add_ship (0x3e51c)
      std::int32_t cls = 0;
      if (hooks_.select_ship_type) {
        run_dialog([&] {
          hooks_.select_ship_type(cls);
          return 0;
        });
      } else {
        cls = -1;
      }
      if (cls != -1) {
        chart_.start_add_vessel(static_cast<std::size_t>(cls));
      }
      break;
    }
    case ToolboxTool::kAddAircraft: {
      // sbchart_tool_add_aircraft (0x3e5a4)
      std::int32_t type = 0;
      const int r = hooks_.select_aircraft_type ? run_dialog([&] { return hooks_.select_aircraft_type(type); })
                                                : ui::kResultCancel;
      if (r == ui::kResultCancel) {
        break;
      }
      const OrdersMenu menu = aircraft_mission_menu(scenario_, AircraftMission::kSearchOrbit2);
      const int choice = run_orders_menu(menu);
      if (choice < 0) {
        break;
      }
      Aircraft& a = scenario_.add_aircraft(static_cast<std::size_t>(type),
                                           static_cast<AircraftMission>(menu.values[static_cast<std::size_t>(choice)]));
      chart_.start_aircraft(a);
      break;
    }
    case ToolboxTool::kAirstrike:
    case ToolboxTool::kPhotoRecon: {
      // sbchart_tool_add_airstrike / _photo_recon (0x3e78a / 0x3e895)
      const bool photo = which == ToolboxTool::kPhotoRecon;
      const std::int32_t targets =
          photo ? scenario_.world().photo_recon_targets() : scenario_.world().airstrike_targets();
      if (targets == 0) {
        if (hooks_.message) {
          chart_.with_panel_closed([&] {
            hooks_.message(se.str(photo ? str::kErrorRecon : str::kError),
                           {se.str(photo ? str::kNoPhotoRecon : str::kNoAirstrike),
                            se.str(photo ? str::kNoBasesRecon : str::kNoBases), se.str(str::kEmpty)});
          });
        }
        break;
      }
      EditorObject* previous = scenario_.selected();
      SpecialMission& m = photo ? scenario_.add_photo_recon() : scenario_.add_airstrike();
      chart_.select_on_panel_open(&m);
      chart_.start_pick_city(photo, previous);
      break;
    }
  }
}

// --- Panels ---------------------------------------------------------------

void Toolbox::add_heading(ui::Dialog& d, std::function<void(std::int32_t)> set, bool disabled) {
  // The dial and its linked entry (`toolbox_*_heading_spin`), then the
  // three digits (`toolbox_*_heading_digit*`) over the entry.
  heading_entry_.on_done = [this, set](std::string& text, int delta) {
    std::int32_t h = std::atoi(text.c_str()) + delta;
    if (h > kMaxHeading) {
      h = 0;
    }
    if (h < 0) {
      h = kMaxHeading;
    }
    text = std::to_string(h);
    set(h);
    edited();
    for (std::size_t i = 1; i < heading_controls_.size(); ++i) {
      heading_controls_[i]->dirty = true;
    }
    return 0;
  };
  auto& dial = d.emplace<ui::DialControl>(60, 197, heading_entry_);
  dial.disabled = disabled;
  heading_controls_.push_back(&dial);
  constexpr std::array<std::int32_t, 3> kPlaces{100, 10, 1};
  for (std::size_t i = 0; i < kPlaces.size(); ++i) {
    const std::int32_t place = kPlaces[i];
    auto& c = digit(d, kHeadingDigitX[i], kHeadingDigitY, [this, set, place](bool advance) {
      std::int32_t h = std::atoi(heading_entry_.text.c_str());
      if (advance) {
        h = cycle_digit(h, place, [](std::int32_t v) { return v <= kMaxHeading; });
        heading_entry_.text = std::to_string(h);
        set(h);
        edited();
        heading_controls_.front()->dirty = true;
      }
      return place == 100 ? h / 100 : h / place % 10;
    });
    c.disabled = disabled;
    heading_controls_.push_back(&c);
  }
}

void Toolbox::add_player(ui::Dialog& d, PlayerSub& player) {
  // Dialog 0xab4d8.
  const SeExe& se = scenario_.data().se;
  PlayerSub* p = &player;
  heading_entry_.text = std::to_string(p->rec.heading);
  player_name_ = p->rec.name;
  d.emplace<ui::GroupBox>(11, 150, 105, 277);
  add_heading(d, [p](std::int32_t h) { p->rec.heading = h; }, false);
  std::vector<std::string> classes;
  for (const SubClass& c : scenario_.data().tables.subs) {
    classes.push_back(c.name);
  }
  cycle(d, 108, 150, 179, std::move(classes), [this, p](bool advance) {
    // toolbox_sub_class_button (0x3ec5a)
    if (advance) {
      player_class(*p);
    }
    const std::optional<std::size_t> cls = scenario_.data().find_sub(scn_text(p->rec.sub_class));
    return cls ? static_cast<int>(*cls) : static_cast<int>(scenario_.data().tables.subs.size());
  });
  cycle(d, 108, 173, 179, table(tbl::kEngine, 11), [this, p](bool advance) {
    // toolbox_sub_engine_cycle (0x3eda5): Stop .. Back 1/3.
    if (advance) {
      p->rec.engine_order = p->rec.engine_order >= 9 ? 0 : p->rec.engine_order + 1;
      edited();
    }
    return p->rec.engine_order;
  });
  cycle(d, 108, 196, 179, table(tbl::kCrew, 3), [this, p](bool advance) {
    // toolbox_sub_crew_cycle (0x3f29e)
    if (advance) {
      const auto crew = static_cast<std::int32_t>(p->rec.crew);
      p->rec.crew = static_cast<CrewQuality>(crew >= 2 ? 0 : crew + 1);
      scenario_.update_all_visibility();
      scenario_.modified = true;
    }
    return static_cast<int>(p->rec.crew);
  });
  d.emplace<ui::GroupBox>(108, 219, 286, 277);
  label(d, 114, 228, str::kDepth, true);
  std::vector<ui::Control*> depth_digits;
  constexpr std::array<std::int32_t, 3> kPlaces{100, 10, 1};
  constexpr std::array<int, 3> kDepthX{165, 183, 201};
  for (std::size_t i = 0; i < kPlaces.size(); ++i) {
    const std::int32_t place = kPlaces[i];
    depth_digits.push_back(&digit(d, kDepthX[i], 223, [this, p, place](bool advance) {
      // toolbox_sub_depth_digit* (0x3efd8..0x3f142): from the class draft
      // to 1.5 x its test depth, above the bottom.
      if (advance) {
        const SubClass* cls = player_class_record(*p);
        const auto draft = cls != nullptr ? static_cast<std::int32_t>(cls->draft_ft) : 0;
        const auto test = cls != nullptr ? static_cast<std::int32_t>(cls->test_depth_ft) : 0;
        std::int32_t max = test + test / 2;
        const std::int32_t water = water_depth({p->rec.x, p->rec.y});
        if (water < max) {
          max = water - 1;
        }
        p->rec.depth =
            cycle_digit(p->rec.depth, place, [draft, max](std::int32_t v) { return v >= draft && v <= max; });
        scenario_.update_all_visibility();
        edited();
      }
      return place == 100 ? p->rec.depth / 100 : p->rec.depth / place % 10;
    }));
  }
  label(d, 220, 228, str::kFeetDot);
  label(d, 243, 223, str::kTest);
  field(d, 243, 233, 273, 243, [this, p] {
    // toolbox_sub_test_depth_text (0x3f1f8)
    const SubClass* cls = player_class_record(*p);
    return std::to_string(cls != nullptr ? cls->test_depth_ft : 0);
  });
  const std::array<std::pair<std::uint32_t, std::uint32_t SubClass::*>, 3> presets{
      {{str::kPeri, &SubClass::periscope_depth_ft},
       {str::kRad, &SubClass::radar_depth_ft},
       {str::kSurf, &SubClass::draft_ft}}};
  constexpr std::array<int, 3> kPresetX{115, 172, 229};
  for (std::size_t i = 0; i < presets.size(); ++i) {
    const auto depth = presets[i].second;
    d.emplace<ui::Button>(kPresetX[i], 249, 51, std::string(se.str(presets[i].first)),
                          [this, p, depth, depth_digits](int) {
                            player_depth_preset(*p, depth);
                            for (ui::Control* c : depth_digits) {
                              c->dirty = true;
                            }
                            return 0;
                          });
  }
  name_button_ = &cycle(d, 11, 281, 276, {scn_text(player_name_)}, [this, p](bool advance) {
    // toolbox_sub_name_button (0x3ebb2): shows the name buffer 0xaad38.
    if (advance) {
      player_name(*p);
    }
    name_button_->strings.front() = scn_text(player_name_);
    return 0;
  });
  d.emplace<ui::GroupBox>(11, 305, 286, 326);
  label(d, 57, 310, str::kPlayerLoc);
  field(d, 117, 310, 234, 321, [this, p] { return location_text({p->rec.x, p->rec.y}, str::kPlayerLocGap); });
}

void Toolbox::add_vessel(ui::Dialog& d, Vessel& vessel) {
  // Dialogs 0xaba54 (ship) and 0xac0dc (enemy sub).
  Vessel* v = &vessel;
  const bool ship = v->kind == Vessel::Kind::kShip;
  const bool member = ship && v->ship.orders == ShipOrders::kConvoyMember;
  heading_entry_.text = std::to_string(ship ? v->ship.heading : v->sub.heading);
  d.emplace<ui::GroupBox>(11, 150, 105, 277);
  add_heading(d, [v](std::int32_t h) { v->set_heading(h); }, member);
  std::vector<std::string> classes;
  for (const ShipClass& c : scenario_.data().tables.ships) {
    classes.push_back(c.name);
  }
  cycle(d, 108, 150, 179, std::move(classes), [this, v](bool advance) {
    // toolbox_unit_class_button (0x3f508)
    if (advance) {
      vessel_class(*v);
    }
    const std::optional<std::size_t> cls = scenario_.data().find_ship(v->class_name());
    return cls ? static_cast<int>(*cls) : static_cast<int>(scenario_.data().tables.ships.size());
  });
  cycle(d, 108, 173, 179, table(tbl::kOrders, 8), [this, v, ship](bool advance) {
    if (advance) {
      if (ship) {
        ship_orders(*v);
      } else {
        sub_orders(*v);
      }
    }
    return static_cast<int>(ship ? v->ship.orders : v->sub.orders);
  });
  cycle(d, 108, 196, 179, table(tbl::kCrew, 3), [this, v, ship](bool advance) {
    // toolbox_unit_crew_cycle (0x3f8dd): both parts.
    auto crew = static_cast<std::int32_t>(ship ? v->ship.crew : v->sub.crew);
    if (advance) {
      crew = crew >= 2 ? 0 : crew + 1;
      v->set_crew(static_cast<CrewQuality>(crew));
      scenario_.modified = true;
    }
    return static_cast<int>(crew);
  });
  d.emplace<ui::GroupBox>(108, 219, 286, 277);
  label(d, 136, 223, ship ? str::kShipSpeed : str::kSubSpeed, true);
  label(d, 164, 244, ship ? str::kShipKnots : str::kSubKnots);
  // toolbox_unit_speed_digit10 / _digit1 (0x3fb79 / 0x3fae7): up to the
  // top speed; a ship's change refreshes its escorts (0xaad10).
  const auto max_speed = [this, v] {
    return v->kind == Vessel::Kind::kShip ? scenario_.ship_max_speed(*v) : enemy_sub_max_speed(*v);
  };
  std::vector<ui::Control*> speed_controls;
  for (const std::int32_t place : {10, 1}) {
    auto& c = digit(d, place == 10 ? 125 : 143, 239, [this, v, place, max_speed](bool advance) {
      const std::int32_t speed = v->kind == Vessel::Kind::kShip ? v->ship.speed : v->sub.speed;
      if (advance) {
        const std::int32_t max = max_speed();
        const std::int32_t next = cycle_digit(speed, place, [max](std::int32_t s) { return s <= max; });
        if (next != speed && v->kind == Vessel::Kind::kShip) {
          escort_refresh_ = true;
        }
        v->set_speed(next);
        scenario_.modified = true;
        return next / place % 10;
      }
      return speed / place % 10;
    });
    c.disabled = member;
    speed_controls.push_back(&c);
  }
  label(d, 137, 264, ship ? str::kShipMax : str::kSubSpeedMax);
  speed_controls.push_back(&field(
      d, 172, 264, 184, 274,
      [v, max_speed, speed_controls] {
        // toolbox_ship_max_speed_text / toolbox_esub_max_speed_text: the
        // speed clamped to the top speed.
        const std::int32_t max = max_speed();
        const std::int32_t speed = v->kind == Vessel::Kind::kShip ? v->ship.speed : v->sub.speed;
        if (max < speed) {
          v->set_speed(max);
          for (ui::Control* c : speed_controls) {
            c->dirty = true;
          }
        }
        return std::to_string(max);
      },
      TextAlign::kLeft));
  const auto add_damage = [this, v](ui::Dialog& dlg, int x10, int x1, int y) {
    // toolbox_unit_damage_digit10 / _digit1 (0x3fc5d / 0x3fbf4): 0..99.
    for (const std::int32_t place : {10, 1}) {
      digit(dlg, place == 10 ? x10 : x1, y, [this, v, place](bool advance) {
        std::int32_t damage = v->kind == Vessel::Kind::kShip ? v->ship.damage_pct : v->sub.damage_pct;
        if (advance) {
          damage = cycle_digit(damage, place, [](std::int32_t x) { return x <= kMaxDamage; });
          v->set_damage(damage);
          scenario_.modified = true;
        }
        return damage / place % 10;
      });
    }
  };
  if (ship) {
    label(d, 210, 223, str::kShipDamage, true);
    label(d, 259, 244, str::kShipPercent);
    add_damage(d, 221, 239, 239);
  } else {
    label(d, 210, 223, str::kSubDepth, true);
    label(d, 248, 244, str::kSubFeet);
    const auto max_depth = [this, v] {
      const std::int32_t water = water_depth({v->sub.x, v->sub.y});
      return water < kMaxSubDepth ? water - 1 : kMaxSubDepth;
    };
    std::vector<ui::Control*> depth_digits;
    for (const std::int32_t place : {10, 1}) {
      depth_digits.push_back(
          &digit(d, place == 10 ? 209 : 227, 239, [this, v, place, max_depth, speed_controls](bool advance) {
            // toolbox_esub_depth_digit10 / _digit1 (0x40194 / 0x400ed):
            // above the bottom, 64 ft at most; the top speed changes.
            if (advance) {
              const std::int32_t max = max_depth();
              v->sub.depth = cycle_digit(v->sub.depth, place, [max](std::int32_t x) { return x <= max; });
              v->update_detection(scenario_.detection_env());
              scenario_.modified = true;
              for (ui::Control* c : speed_controls) {
                c->dirty = true;
              }
            }
            return v->sub.depth / place % 10;
          }));
    }
    label(d, 212, 264, str::kSubDepthMax);
    field(
        d, 247, 264, 270, 274,
        [v, max_depth, depth_digits] {
          // toolbox_esub_max_depth_text (0x402c1): the depth clamped.
          const std::int32_t max = max_depth();
          if (max < v->sub.depth) {
            v->sub.depth = max;
            for (ui::Control* c : depth_digits) {
              c->dirty = true;
            }
          }
          return std::to_string(max);
        },
        TextAlign::kLeft);
    label(d, 18, 286, str::kSubDamage);
    add_damage(d, 75, 93, 281);
    label(d, 113, 286, str::kSubPercent);
  }
  d.emplace<ui::GroupBox>(11, 305, 207, 326);
  label(d, 27, 310, ship ? str::kShipLoc : str::kSubLoc);
  field(d, 87, 310, 201, 321, [this, v, ship] {
    return location_text(v->position(scenario_.object_env()), ship ? str::kShipLocGap : str::kSubLocGap);
  });
  d.emplace<ui::GroupBox>(211, 305, 247, 326);
  d.emplace<ui::GroupBox>(250, 305, 286, 326);
  add_indicators(d);
  if (!ship) {
    return;
  }
  label(d, 18, 286, str::kShipConvoy);
  auto& convoy = cycle(d, 75, 280, 70, table(tbl::kConvoys, 9), [this, v](bool advance) {
    // toolbox_ship_convoy_cycle (0x3fcca)
    if (advance) {
      const std::int32_t old = v->ship.convoy_group;
      v->ship.convoy_group = convoy_cycle(old);
      if (old != v->ship.convoy_group) {
        scenario_.apply_convoy_membership(*v);
        chart_.request_overlay_redraw();
        heading_entry_.text = std::to_string(v->ship.heading);
        chart_.redraw_toolbox();
        scenario_.modified = true;
      }
    }
    return v->ship.convoy_group;
  });
  convoy.disabled = v->ship.orders != ShipOrders::kConvoyMember && v->ship.orders != ShipOrders::kEscort;
  // The orders-dependent field: "Alerted:" for an escort, " Zigzag:" for a
  // leader (both Off / On), nothing else (the cycle is then unlinked).
  const SeExe& se = scenario_.data().se;
  const ShipOrders orders = v->ship.orders;
  const bool alerted = orders == ShipOrders::kEscort;
  const bool zigzag = orders == ShipOrders::kConvoyLeader;
  d.emplace<ui::Label>(154, 286,
                       std::string(se.str(alerted  ? str::kAlerted
                                          : zigzag ? str::kZigzag
                                                   : str::kNoOption)),
                       ui::FontSlot::kCustom, true, &env_.font1);
  if (alerted || zigzag) {
    std::int32_t* option = alerted ? &v->ship.alert : &v->ship.zigzag;
    cycle(d, 210, 280, 70, table(tbl::kOffOn, 2), [this, option](bool advance) {
      // toolbox_ship_alerted_toggle / toolbox_ship_zigzag_toggle
      if (advance) {
        *option = *option >= 1 ? 0 : *option + 1;
        scenario_.modified = true;
      }
      return *option;
    });
  }
}

void Toolbox::add_indicators(ui::Dialog& d) {
  // sbchart_toolbox_draw_indicator (0x3ff36), no hotspot: the panel face,
  // then the eye (seen from the player) and the radar icon (inside its
  // radar range).
  const std::array<std::pair<ui::Rect, std::size_t>, 2> kIndicators{
      {{{0xd5, 0x133, 0xf5, 0x144}, kSeenIcon}, {{0xfc, 0x133, 0x11c, 0x144}, kRadarIcon}}};
  for (const auto& [r, frame] : kIndicators) {
    d.emplace<ui::OwnerDraw>([this, r, frame](const Viewport& gfx, ui::OwnerDraw&) {
      ui::gui_fill_rect(gfx, r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, kPanelFace);
      if (scenario_.selected()->in_detection_range(frame == kRadarIcon)) {
        draw_sprite_clipped(gfx, env_.sbchart.frame(frame), r.x0, r.y0);
      }
    });
  }
}

void Toolbox::add_aircraft(ui::Dialog& d, Aircraft& aircraft) {
  // Dialog 0xac7c4.
  Aircraft* a = &aircraft;
  d.emplace<ui::GroupBox>(11, 150, 105, 277);
  std::vector<std::string> types;
  for (const AircraftType& t : scenario_.data().tables.aircraft) {
    types.push_back(t.long_name);
  }
  cycle(d, 108, 150, 179, std::move(types), [this, a](bool advance) {
    // toolbox_air_type_button (0x40356); its paint also greys Guns for a
    // type without guns.
    if (advance) {
      aircraft_type(*a);
    }
    const std::optional<std::size_t> type = scenario_.data().find_aircraft(scn_text(a->rec.aircraft_type));
    if (guns_ != nullptr) {
      guns_->disabled = !type || scenario_.data().tables.aircraft[*type].guns == 0;
      guns_->dirty = true;
    }
    return type ? static_cast<int>(*type) : static_cast<int>(scenario_.data().tables.aircraft.size());
  });
  cycle(d, 108, 173, 179, table(tbl::kOrders, 8), [this, a](bool advance) {
    if (advance) {
      aircraft_mission(*a);
    }
    return static_cast<int>(a->rec.mission);
  });
  cycle(d, 108, 196, 179, table(tbl::kPilots, 3), [this, a](bool advance) {
    // toolbox_air_pilot_cycle (0x405bf)
    if (advance) {
      const auto crew = static_cast<std::int32_t>(a->rec.crew);
      a->rec.crew = static_cast<CrewQuality>(crew >= 2 ? 0 : crew + 1);
      scenario_.modified = true;
    }
    return static_cast<int>(a->rec.crew);
  });
  d.emplace<ui::GroupBox>(108, 219, 286, 277);
  d.emplace<ui::GroupBox>(11, 305, 286, 326);
  label(d, 115, 230, str::kBombs, true);
  const auto max_bombs = [this, a] {
    const std::optional<std::size_t> type = scenario_.data().find_aircraft(scn_text(a->rec.aircraft_type));
    return type ? static_cast<std::int32_t>(scenario_.data().tables.aircraft[*type].bombs) : 0;
  };
  digit(d, 174, 225, [this, a, max_bombs](bool advance) {
    // toolbox_air_bombs_cycle (0x406c5): 0 .. the type's bombs.
    if (advance) {
      a->rec.bomb_load = a->rec.bomb_load + 1 > max_bombs() ? 0 : a->rec.bomb_load + 1;
      scenario_.modified = true;
    }
    return a->rec.bomb_load;
  });
  label(d, 195, 230, str::kMaxBombs);
  field(d, 268, 230, 281, 240, [max_bombs] { return std::to_string(max_bombs()); });
  field(d, 13, 310, 284, 321, [this, a] {
    // toolbox_air_radius_text (0x40788)
    const SeExe& se = scenario_.data().se;
    if (a->rec.mission == AircraftMission::kFollowPath) {
      return std::string(se.str(str::kNoRadius));
    }
    constexpr std::int32_t kUnitsPerYard = 0x30;
    return std::string(se.str(str::kPatrolRadius)) + std::to_string(a->rec.orbit_radius / kUnitsPerYard) +
           std::string(se.str(str::kYards));
  });
  label(d, 18, 286, str::kAirConvoy);
  auto& convoy = cycle(d, 75, 281, 70, table(tbl::kConvoys, 9), [this, a](bool advance) {
    // toolbox_air_convoy_cycle (0x405eb)
    if (advance) {
      a->rec.escort_group = convoy_cycle(a->rec.escort_group);
      scenario_.modified = true;
    }
    return a->rec.escort_group;
  });
  convoy.disabled = a->rec.mission != AircraftMission::kSearchOrbit;
  label(d, 142, 256, str::kGuns);
  guns_ = &cycle(d, 186, 252, 50, table(tbl::kNoYes, 2), [this, a](bool advance) {
    // toolbox_air_guns_toggle (0x40658)
    if (advance) {
      a->rec.strafes = a->rec.strafes == 0 ? 1 : 0;
      scenario_.modified = true;
    }
    return a->rec.strafes;
  });
  label(d, 151, 286, str::kRegenerate);
  cycle(d, 235, 281, 50, table(tbl::kNoYes, 2), [this, a](bool advance) {
    // toolbox_air_regenerate_toggle (0x4068a)
    if (advance) {
      a->rec.relaunch = a->rec.relaunch == 0 ? 1 : 0;
      scenario_.modified = true;
    }
    return a->rec.relaunch;
  });
}

void Toolbox::add_mission(ui::Dialog& d, SpecialMission& mission) {
  // Dialogs 0xacba8 (airstrike) and 0xad090 (photo recon).
  SpecialMission* m = &mission;
  const bool strike = m->type() == ObjectType::kAirstrike;
  const str::MissionStrings& s = strike ? str::kAirstrike : str::kPhotoRecon;
  std::vector<std::string> bases;
  for (const BaseRecord& b : scenario_.world().bases()) {
    bases.push_back(b.name);
  }
  cycle(d, 36, 150, 227, std::move(bases), [this, m, strike](bool advance) {
    // toolbox_strike_location_button (0x40835): SELECT A CITY again; with
    // the corrections, only a new city marks the scenario modified.
    if (advance) {
      chart_.start_pick_city(!strike, nullptr);
      if (!env_.features.editor_fixes) {
        scenario_.modified = true;
      }
    }
    return m->base();
  });
  if (strike) {
    cycle(d, 36, 174, 227, table(tbl::kStrikeSizes, 3), [this, m](bool advance) {
      // toolbox_strike_size_cycle (0x4089a)
      auto& r = std::get<BomberGroupRecord>(m->rec);
      auto size = static_cast<std::int32_t>(r.size);
      if (advance) {
        size = size >= 2 ? 0 : size + 1;
        r.size = static_cast<BomberGroupSize>(size);
        scenario_.modified = true;
      }
      return static_cast<int>(size);
    });
  }
  // The four digits of a duration: hours tens and units, ":", minutes.
  const auto add_time = [this, m](ui::Dialog& dlg, int y, bool hold) {
    constexpr std::array<int, 4> kX{108, 125, 151, 168};
    for (int i = 0; i < 4; ++i) {
      digit(dlg, kX[static_cast<std::size_t>(i)], y, [this, m, hold, i](bool advance) {
        std::int32_t& minutes = hold ? mission_hold(*m) : mission_delay(*m);
        if (advance) {
          minutes = cycle_time_digit(minutes, i);
          scenario_.modified = true;
        }
        return time_digit(minutes, i);
      });
    }
  };
  label(d, 102, 204, s.delay, true);
  add_time(d, 223, false);
  label(d, 145, 228, s.colon1, true);
  d.emplace<ui::GroupBox>(11, 305, 286, 326);
  field(d, 117, 310, 234, 321, [this, m] {
    // toolbox_strike_location_text (0x40bc7)
    const std::vector<BaseRecord>& bases = scenario_.world().bases();
    const std::int32_t base = m->base();
    if (base < 0 || static_cast<std::size_t>(base) >= bases.size()) {
      // The original reads outside the base table.
      return std::string();
    }
    const BaseRecord& b = bases[static_cast<std::size_t>(base)];
    return location_text({b.station.x, b.station.z}, str::kMissionLocGap);
  });
  d.emplace<ui::GroupBox>(11, 199, 286, 247);
  label(d, 72, 227, s.hrs1);
  label(d, 194, 227, s.mins1);
  label(d, 57, 310, s.location);
  // Editor oddity (airstrike hold label): the airstrike's hold time
  // (`hold_min`) is labelled "Flight Delay", here and in its info popup;
  // "Hold Time" with the corrections (not in the original).
  if (strike && env_.features.editor_fixes) {
    // Centred where SE's label is.
    const Font& small = d.skin().font(ui::FontSlot::kSmall);
    const int x = 99 + (small.text_width(scenario_.data().se.str(s.hold)) - small.text_width(own_text::kHoldTime)) / 2;
    d.emplace<ui::Label>(x, 257, std::string(own_text::kHoldTime), ui::FontSlot::kSmall);
  } else {
    label(d, strike ? 99 : 104, 257, s.hold, true);
  }
  add_time(d, 276, true);
  label(d, 145, 281, s.colon2, true);
  d.emplace<ui::GroupBox>(11, 251, 286, 300);
  label(d, 72, 280, s.hrs2);
  label(d, 194, 280, s.mins2);
}

// --- Field actions --------------------------------------------------------

void Toolbox::player_class(PlayerSub& p) {
  // toolbox_sub_class_button (0x3ec5a): SELECT SUB TYPE (chaining to SELECT
  // SUB NAME), then the class, the name, the torpedo load, the depth kept
  // between the draft and 1.5 x the test depth.
  if (!hooks_.select_sub_type) {
    return;
  }
  const std::optional<std::size_t> current = scenario_.data().find_sub(scn_text(p.rec.sub_class));
  auto cls = static_cast<std::int32_t>(current ? *current : scenario_.data().tables.subs.size());
  ScnChars<31> name = p.rec.name;
  if (run_dialog([&] { return hooks_.select_sub_type(cls, name); }) == ui::kResultCancel) {
    return;
  }
  if (cls < 0 || static_cast<std::size_t>(cls) >= scenario_.data().tables.subs.size()) {
    return;
  }
  const SubClass& c = scenario_.data().tables.subs[static_cast<std::size_t>(cls)];
  chart_.request_overlay_redraw();
  copy_text(p.rec.sub_class, c.name);
  p.rec.name = name;
  player_name_ = name;
  scenario_.reset_torpedo_load();
  if (p.rec.depth < static_cast<std::int32_t>(c.draft_ft)) {
    p.rec.depth = static_cast<std::int32_t>(c.draft_ft);
  }
  const auto test = static_cast<std::int32_t>(c.test_depth_ft);
  if (test + test / 2 < p.rec.depth) {
    p.rec.depth = test + test / 2;
  }
  scenario_.update_all_visibility();
  chart_.redraw_toolbox();
  scenario_.modified = true;
}

void Toolbox::player_name(PlayerSub& p) {
  // toolbox_sub_name_button (0x3ebb2): CANCEL puts the buffer back; a name
  // chosen refreshes the torpedo load. Modified either way.
  const std::optional<std::size_t> cls = scenario_.data().find_sub(scn_text(p.rec.sub_class));
  const auto index = static_cast<std::int32_t>(cls ? *cls : scenario_.data().tables.subs.size());
  const int r = hooks_.select_sub_name ? run_dialog([&] { return hooks_.select_sub_name(index, p.rec.name); })
                                       : ui::kResultCancel;
  if (r == ui::kResultCancel) {
    p.rec.name = player_name_;
  } else {
    player_name_ = p.rec.name;
    scenario_.reset_torpedo_load();
  }
  scenario_.modified = true;
}

void Toolbox::player_depth_preset(PlayerSub& p, std::uint32_t SubClass::* depth) {
  // toolbox_sub_button_peri / _rad / _surf (0x3f2d4 / 0x3f373 / 0x3f427):
  // the class depth, above the bottom.
  const SubClass* cls = player_class_record(p);
  if (cls == nullptr) {
    // The original reads past SUBS.DAT.
    return;
  }
  p.rec.depth = static_cast<std::int32_t>(cls->*depth);
  const std::int32_t water = water_depth({p.rec.x, p.rec.y});
  if (water <= p.rec.depth) {
    p.rec.depth = water - 1;
  }
  scenario_.update_all_visibility();
  edited();
}

void Toolbox::vessel_class(Vessel& v) {
  // toolbox_unit_class_button (0x3f508) and sbchart_change_ship_class
  // (0x44114): SELECT SHIP TYPE, the leader's confirmation when its role
  // group changes, a ship <-> sub switch reopens the toolbox.
  const std::optional<std::size_t> current = scenario_.data().find_ship(v.class_name());
  auto cls = current ? static_cast<std::int32_t>(*current) : -1;
  if (hooks_.select_ship_type) {
    run_dialog([&] {
      hooks_.select_ship_type(cls);
      return 0;
    });
  } else {
    cls = -1;
  }
  // SE marks the scenario modified even on CANCEL; not with the editor's
  // corrections (not in the original).
  if (cls == -1 && env_.features.editor_fixes) {
    return;
  }
  scenario_.modified = true;
  if (cls == -1) {
    return;
  }
  const auto to = static_cast<std::size_t>(cls);
  if (scenario_.class_change_releases_convoy(v, to) && !chart_.confirm_remove_leader()) {
    return;
  }
  const ShipOrders orders = v.orders();
  const Vessel::Kind kind = v.kind;
  scenario_.change_vessel_class(v, to);
  if (v.orders() != orders) {
    chart_.redraw_toolbox();
  }
  chart_.request_overlay_redraw();
  if (v.kind != kind) {
    chart_.request_toolbox_reopen();
  }
}

void Toolbox::ship_orders(Vessel& v) {
  // toolbox_ship_orders_button (0x3f5ae)
  const OrdersMenu menu = vessel_orders_menu(scenario_, v);
  if (menu.labels.empty()) {
    return;
  }
  const std::int32_t convoy =
      v.ship.orders == ShipOrders::kConvoyLeader ? v.ship.convoy_group : scenario_.first_free_convoy_id();
  const int choice = run_orders_menu(menu);
  if (choice < 0) {
    return;
  }
  const auto orders = static_cast<ShipOrders>(menu.values[static_cast<std::size_t>(choice)]);
  if (v.ship.orders == ShipOrders::kConvoyLeader && orders != v.ship.orders && scenario_.convoy_has_followers(convoy)) {
    if (!chart_.confirm_remove_leader()) {
      return;
    }
    scenario_.release_escorts(convoy);
    scenario_.release_members(convoy);
  }
  v.ship.orders = orders;
  v.ship.convoy_group = 0;
  switch (orders) {
    case ShipOrders::kEscort:
      chart_.start_course(ClickMode::kEscortCourse);
      break;
    case ShipOrders::kPatrol:
    case ShipOrders::kFollowPath:
      chart_.start_course(ClickMode::kCourse);
      break;
    case ShipOrders::kConvoyLeader:
      chart_.start_course(ClickMode::kCourse);
      v.ship.zigzag = 0;
      v.ship.convoy_group = convoy;
      break;
    case ShipOrders::kConvoyMember:
      chart_.start_pick_convoy();
      break;
    default:
      // None, Fishing: no course; the toolbox shows the new orders.
      v.ship.waypoints.count = 0;
      chart_.reopen_panel();
      break;
  }
  scenario_.modified = true;
}

void Toolbox::sub_orders(Vessel& v) {
  // toolbox_esub_orders_button (0x3ffd8): "Patrol" is Follow Path with an
  // open course (mode 5).
  const OrdersMenu menu = vessel_orders_menu(scenario_, v);
  const int choice = run_orders_menu(menu);
  if (choice < 0) {
    return;
  }
  v.sub.orders = static_cast<ShipOrders>(menu.values[static_cast<std::size_t>(choice)]);
  if (v.sub.orders == ShipOrders::kFollowPath) {
    chart_.start_course(ClickMode::kSubCourse);
  } else if (v.sub.orders == ShipOrders::kIntercept) {
    // The intercept point, the player's position without one (only with
    // the corrections' menu; not in the original).
    v.sub.waypoints.count = 0;
    v.sub.target_x = -1;
    v.sub.target_y = -1;
    chart_.start_intercept_point();
  } else {
    v.sub.waypoints.count = 0;
    chart_.reopen_panel();
  }
  scenario_.modified = true;
}

void Toolbox::aircraft_type(Aircraft& a) {
  // toolbox_air_type_button (0x40356)
  const std::optional<std::size_t> current = scenario_.data().find_aircraft(scn_text(a.rec.aircraft_type));
  auto type = static_cast<std::int32_t>(current ? *current : scenario_.data().tables.aircraft.size());
  const int r =
      hooks_.select_aircraft_type ? run_dialog([&] { return hooks_.select_aircraft_type(type); }) : ui::kResultCancel;
  if (r == ui::kResultCancel || type < 0 || static_cast<std::size_t>(type) >= scenario_.data().tables.aircraft.size()) {
    return;
  }
  scenario_.change_aircraft_type(a, static_cast<std::size_t>(type));
  chart_.redraw_toolbox();
  chart_.request_overlay_redraw();
  scenario_.modified = true;
}

void Toolbox::aircraft_mission(Aircraft& a) {
  // toolbox_air_mission_button (0x404b5): a new mission drops the patrol
  // radius (and the convoy unless Escort), then plots the path again from
  // the aircraft or asks for the radius.
  const OrdersMenu menu = aircraft_mission_menu(scenario_, a.rec.mission);
  const int choice = run_orders_menu(menu);
  if (choice < 0) {
    return;
  }
  a.rec.mission = static_cast<AircraftMission>(menu.values[static_cast<std::size_t>(choice)]);
  a.rec.orbit_radius = 0;
  chart_.redraw_toolbox();
  if (a.rec.mission != AircraftMission::kSearchOrbit) {
    a.rec.escort_group = 0;
  }
  if (a.rec.mission == AircraftMission::kFollowPath) {
    chart_.start_aircraft_path(a);
  } else {
    chart_.start_patrol_radius();
  }
  edited();
}

std::int32_t Toolbox::convoy_cycle(std::int32_t current) const {
  // toolbox_ship_convoy_cycle / toolbox_air_convoy_cycle: the next convoy
  // id 1-8 with a leader, 0 when there is none.
  if (!scenario_.other_convoy_exists(0)) {
    return 0;
  }
  std::int32_t id = current;
  do {
    id = id >= 8 ? 1 : id + 1;
  } while (scenario_.convoy_leader(id) == nullptr);
  return id;
}

void Toolbox::edited() {
  chart_.request_overlay_redraw();
  scenario_.modified = true;
}

// --- Helpers --------------------------------------------------------------

int Toolbox::run_dialog(const std::function<int()>& dialog) {
  int result = ui::kResultCancel;
  chart_.with_panel_closed([&] { result = dialog(); });
  return result;
}

int Toolbox::run_orders_menu(const OrdersMenu& menu) {
  // orders_popup_run (0x2a03e): -3 when cancelled.
  constexpr int kMenuCancelled = -3;
  if (!hooks_.orders_popup || menu.labels.empty()) {
    return kMenuCancelled;
  }
  return run_dialog([&] { return hooks_.orders_popup(menu.labels, menu.disabled, menu.initial); });
}

ui::Label& Toolbox::label(ui::Dialog& d, int x, int y, std::uint32_t text, bool small) {
  const std::string s(scenario_.data().se.str(text));
  if (small) {
    return d.emplace<ui::Label>(x, y, s, ui::FontSlot::kSmall);
  }
  return d.emplace<ui::Label>(x, y, s, ui::FontSlot::kCustom, true, &env_.font1);
}

ui::TextField& Toolbox::field(ui::Dialog& d, int x0, int y0, int x1, int y1, std::function<std::string()> text,
                              TextAlign align) {
  auto& f = d.emplace<ui::TextField>(x0, y0, x1, y1, std::move(text), ui::FontSlot::kCustom, align);
  f.fill = kPanelFace;
  f.custom = &env_.font1;
  return f;
}

ui::CycleButton& Toolbox::cycle(ui::Dialog& d, int x, int y, int w, std::vector<std::string> strings, IndexFn index) {
  // An index past the strings (an unknown class, a stale field) reads past
  // the original's table; here it shows an empty text.
  const auto n = static_cast<int>(strings.size());
  strings.emplace_back();
  return d.emplace<ui::CycleButton>(x, y, w, std::move(strings), [n, index = std::move(index)](bool advance) {
    const int i = index(advance);
    return i < 0 || i > n ? n : i;
  });
}

ui::CycleButton& Toolbox::digit(ui::Dialog& d, int x, int y, IndexFn index) {
  return cycle(d, x, y, kDigitW, table(tbl::kDigits, 10), std::move(index));
}

std::vector<std::string> Toolbox::table(std::uint32_t address, std::size_t count) const {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < count; ++i) {
    out.emplace_back(scenario_.data().se.table_str(address, i));
  }
  return out;
}

std::string Toolbox::location_text(WorldPoint p, std::uint32_t separator) const {
  // toolbox_*_location_text: "lat<gap>lon".
  const SeExe& se = scenario_.data().se;
  const World& world = scenario_.world().world();
  return format_latlon(se, world.latitude_at(p.y)) + std::string(se.str(separator)) +
         format_latlon(se, world.longitude_at(p.x));
}

std::int32_t Toolbox::water_depth(WorldPoint p) const { return -scenario_.world().floor().height_at(p.x, p.y) / 16; }

const SubClass* Toolbox::player_class_record(const PlayerSub& player) const {
  const std::optional<std::size_t> cls = scenario_.data().find_sub(scn_text(player.rec.sub_class));
  return cls ? &scenario_.data().tables.subs[*cls] : nullptr;
}

std::int32_t Toolbox::enemy_sub_max_speed(const Vessel& vessel) const {
  // sb_esub_max_speed (0x40253): SHIPS.DAT +0xa0 surfaced, +0xa8 submerged.
  constexpr std::size_t kSurfacedSpeed = 0xa0;
  constexpr std::size_t kSubmergedSpeed = 0xa8;
  const std::optional<std::size_t> cls = scenario_.data().find_ship(scn_text(vessel.sub.sub_class));
  if (!cls) {
    return 0;
  }
  return ascii_knots(scenario_.data().tables.ships[*cls], vessel.sub.depth > 0 ? kSubmergedSpeed : kSurfacedSpeed);
}

}  // namespace sh::editor

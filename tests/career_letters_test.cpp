// Tests of the career's letters and patrol screens: the texts of the
// orders, results, transfers, equipment, citations and new-command letters,
// and the screens driven by scripted clicks and keys. With SH_SCREENSHOT_DIR
// set, the screens are also saved there as career_<name>.bmp.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/patrol_zones.h"
#include "assets/shp.h"
#include "core/install.h"
#include "game/career_letters.h"
#include "game/career_rules.h"
#include "game/career_ui.h"
#include "game/equipment_art.h"
#include "game/frontend.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "ui/menu_skin.h"

using namespace sh;

namespace {

// Removes the padding glyphs 1 and 2 `menu_pad_text_to_width` inserts.
std::string unpadded(const std::string& s) {
  std::string out;
  for (const char c : s) {
    if (c != 1 && c != 2) {
      out += c;
    }
  }
  return out;
}

CareerRecord sample_record() {
  CareerRecord r;
  r.name = "Mathias";
  r.month = 5;  // June 1945
  r.day = 1;
  r.year = 15;
  r.boat = "(SS-337) Carbonero";
  r.class_index = 7;
  r.class_name = "BALAO";
  r.class_title = "Balao";
  r.home_port = "Darwin";
  r.location = "Darwin";
  r.rank = 1;
  r.status = CareerStatus::kInPort;
  return r;
}

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_career_letters_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A host replaying clicks and keys one every 40 frames and keeping the last
// frame shown.
class ScriptedHost final : public FrontendHost, public MissionRunner {
 public:
  struct Step {
    std::uint16_t key = 0;  // a key, or a click at (x, y)
    int x = 0;
    int y = 0;
    bool back = false;  // Android's Back instead
  };
  static Step click(int x, int y) { return {0, x, y}; }
  static Step key(std::uint16_t k) { return {k, 0, 0}; }
  static Step back() { return {0, 0, 0, true}; }

  std::deque<Step> script;
  Image8 last;
  VgaPalette last_dac{};
  int frames = 0;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8& screen, const VgaPalette& dac) override {
    ++frames;
    tick_ += 2;
    // The frame before an input is the screen as it was built.
    if (idle_ == 38) {
      last = screen;
      last_dac = dac;
    }
    if (frames > 50'000) {
      throw std::runtime_error("the script did not end the screen");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Step s = script.front();
    script.pop_front();
    if (s.back) {
      gui.mouse().post_back(0);
    } else if (s.key != 0) {
      gui.mouse().post_key(s.key, 0);
    } else {
      gui.mouse().update(s.x, s.y, ui::kButtonLeft, 0);
      gui.mouse().update(s.x, s.y, 0, 0);
    }
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}
  std::int32_t run(const MissionRequest&) override { return 1; }
  MissionOutcome outcome() const override { return {}; }

  // Saves the kept frame as SH_SCREENSHOT_DIR/career_<name>.bmp.
  void save(const std::string& name) const {
    const char* dir = std::getenv("SH_SCREENSHOT_DIR");
    if (dir != nullptr && last.width() > 0) {
      save_bmp(std::filesystem::path(dir) / ("career_" + name + ".bmp"), last, to_palette(last_dac));
    }
  }

 private:
  std::uint32_t tick_ = 0;
  int idle_ = 0;
};

constexpr std::uint16_t kEnter = 0x1c0d;

// The front end and its career context over the game files.
struct Screens {
  explicit Screens(const Install& install)
      : paths(ShPaths::load(install)),
        exe(ExeImage::load(install)),
        saves(dir.path(), &install, paths.dir(GameDir::kSave)),
        front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1),
        ui(front_end),
        letters(ui) {}

  TempDir dir;
  ShPaths paths;
  ExeImage exe;
  SaveDir saves;
  ScriptedHost host;
  FrontEnd front_end;
  CareerUi ui;
  CareerLetters letters;
};

}  // namespace

TEST_CASE("Career letter helpers follow the original's calendar and number format", "[career][content]") {
  CHECK(gcvt3(100.0 / 24.0) == "4.17");
  CHECK(gcvt3(1.0) == "1");
  CHECK(gcvt3(10.0) == "10");
  CHECK(gcvt3(0.5) == "0.5");
  CHECK(campaign_month_index_abs(3, 1941) == 0);
  CHECK(campaign_month_index_abs(0, 1942) == 1);
  CHECK(campaign_month_index_abs(7, 1945) == 44);

  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  CHECK(calendar_day_name(exe, 7, 11, 11) == "Sunday");     // 7 December 1941
  CHECK(calendar_day_name(exe, 15, 7, 15) == "Wednesday");  // 15 August 1945
  CHECK(calendar_day_name(exe, 1, 0, 12) == "Thursday");    // 1 January 1942
  // The base maps by date: index 14 (January 1943) is past SPAC_I's 9.
  CHECK(base_art_file(exe, 11, 11) == "SPAC_A.SHP");
  CHECK(base_art_file(exe, 0, 13) == "SPAC_I.SHP");
  CHECK(base_art_file(exe, 7, 15) == "SPAC_S.SHP");
}

TEST_CASE("Career letters build the original texts", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  const Font& font = menu.small;
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  const CareerRecord r = sample_record();

  SECTION("patrol orders") {
    const PatrolOrders orders{zones.at(0), SubCommand::kComSubSoWesPac, 100, 4200};
    const std::string t = unpadded(patrol_orders_letter(r, orders, exe, font));
    CHECK(t.starts_with(zones[0].name + "\n\nDate:June 1, 1945\nFrom:ComSubSoWesPac\nTo:(SS-337) Carbonero"));
    CHECK(t.find("Commander Mathias:\nProceed to " + zones[0].name +
                 ". Search for and destroy all enemy shipping.\n\n") != std::string::npos);
    CHECK(t.ends_with("Est. travel time:4.17 days\nEst. fuel usage:4200 gal\n"));
  }

  SECTION("realism values from the options word") {
    CareerRecord c = r;
    career_realism_from_options(0x61a8, c);  // EXPERT
    CHECK(c.realism_value(CareerRealism::kCombatLevel) == 2);
    CHECK(c.realism_value(CareerRealism::kSailingModel) == 2);
    CHECK(c.realism_value(CareerRealism::kLimitedVisibility) == 1);
    CHECK(c.realism_value(CareerRealism::kRunAground) == 1);
    CHECK(c.realism_value(CareerRealism::kRealisticReloads) == 1);
    CHECK(c.realism_value(CareerRealism::kDudTorpedoes) == 1);
    CHECK(c.realism_value(CareerRealism::kUnlimitedFuel) == 0);
    CHECK(c.realism_value(CareerRealism::kInvulnerable) == 0);
  }

  SECTION("patrol results") {
    MissionResults m;
    m.sub = r.boat;
    m.class_name = "BALAO";
    m.patrol_area = "South China Sea";
    m.crew = 1;
    m.hull_damage_pct = 40;
    m.system_damage_pct = 0;
    m.minutes_elapsed = 12 * 60;
    m.fuel = 58500;
    m.realism_pct = 100;
    m.ships_sunk = 1;
    m.tonnage = 70;
    m.score = 90;
    SunkListEntry e;
    e.log.type = 21;  // sampan
    e.log.tonnage = 70;
    e.log.latitude = {1, 48, 'N'};
    e.log.longitude = {106, 4, 'E'};
    m.sunk = {e};
    const PatrolResultsInfo info{.transit_hours = 36, .repair_days = 2, .fuel_capacity = 117000};
    const std::string t = unpadded(patrol_results_summary(r, m, info, exe, font));
    CHECK(t.starts_with("Date: June 1, 1945Patrol Area:South China Sea\nSub: (SS-337) CarboneroClass: BALAO\n"
                        "Crew: VeteranStatus: Operational\n"));
    CHECK(t.find("Hull Damage: Moderate (40%)System Damage: None (0%)\n") != std::string::npos);
    CHECK(t.find("Total Patrol Time: 2 daysRepair Time: 2 days\n") != std::string::npos);
    CHECK(t.find("Fuel Used: 58500 gallons  (50%)Realism Rating: 100%\n") != std::string::npos);
    CHECK(t.find("Score: 90\n\nLocationShip Type (CLASS)TonnageStatus\n\n1-48N, 106-04ESampan70Sunk\n\n") !=
          std::string::npos);
    CHECK(t.ends_with("***END OF RESULTS***"));

    // A lost boat: no damage, time or fuel lines, the realism on its own.
    m.fate = BoatFate::kKilled;
    const std::string lost = unpadded(patrol_results_summary(r, m, info, exe, font));
    CHECK(lost.find("Hull Damage") == std::string::npos);
    CHECK(lost.find("Status: Killed in Action\nRealism Rating: 100%\n") != std::string::npos);
  }

  SECTION("home base orders") {
    const GameTables tables = GameTables::load(install);
    const HomePort* pearl = nullptr;
    for (const HomePort& p : tables.home_ports) {
      if (p.name == "Pearl Harbor") {
        pearl = &p;
      }
    }
    REQUIRE(pearl != nullptr);
    const std::string transfer = unpadded(homeport_transfer_orders(r, *pearl, true, exe, font));
    CHECK(transfer.starts_with("\nPearl Harbor\n(SubPac)\n\n\nDate:June 1, 1945\nFrom:Commander Submarine Force\n\n"));
    CHECK(transfer.ends_with("Commander Mathias:\n\n(SS-337) Carbonero is being transferred to the submarine base at "
                             "Pearl Harbor. You will be given further orders by ComSubPac on arrival."));
    const std::string report = unpadded(homeport_transfer_orders(r, *pearl, false, exe, font));
    CHECK(report.ends_with("Commander Mathias:\n\nReport to the submarine base at Pearl Harbor. You will be given "
                           "further orders by ComSubPac on arrival."));
  }

  SECTION("new equipment") {
    const EquipmentEntry sd = equipment_entry(exe, 0);
    CHECK(sd.name == "SD Radar");
    CHECK(sd.bit == 1);
    CHECK(sd.month == 0);
    CHECK(sd.year == 12);
    CHECK_FALSE(sd.supply);
    const EquipmentEntry bt = equipment_entry(exe, 2);
    CHECK(bt.name == "Bathythermograph");
    CHECK(bt.month == 2);
    CHECK(bt.year == 13);
    const EquipmentEntry mk18 = equipment_entry(exe, 5);
    CHECK(mk18.supply);
    CHECK(mk18.month == 9);
    CHECK(mk18.year == 13);
    CHECK(equipment_entry(exe, 6).year == 14);

    CareerRecord c = r;
    c.month = 2;  // March 1942
    c.year = 12;
    c.equipment = 0;
    CHECK(new_equipment_due(exe, c, 1) == 0);
    // Refits come three months later outside ComSubPac.
    CHECK_FALSE(new_equipment_due(exe, c, 0).has_value());
    c.month = 3;
    CHECK(new_equipment_due(exe, c, 0) == 0);
    c.equipment = 1;
    CHECK_FALSE(new_equipment_due(exe, c, 1).has_value());

    const std::string t = unpadded(new_equipment_text(r, mk18, exe, font));
    CHECK(t.starts_with("MARK 18-1 Torpedo\n\nDate:June 1, 1945\nFrom:Base Supply Officer\nDarwin\nTo:"
                        "(SS-337) Carbonero\n\nThe base has been supplied with a new type of torpedo, the MARK 18-1 "
                        "Torpedo. The Mark 18-1"));
  }

  SECTION("citations and letters") {
    const Font typewriter = Font::load(install, "ART/TYPEX.FNT");
    const std::string cross =
        unpadded(award_citation_text(LetterCode::kNavyCross, r, SubCommand::kComSubPac, 0x106, exe, typewriter, font));
    CHECK(cross.starts_with("\nMATHIAS\nCOMMANDER\nUNITED STATES NAVY\n\n\n\n\nCITATION:\n\nFor actions in combat, "
                            "the Secretary of the Navy takes great pleasure in presenting you with the Navy Cross."));
    CareerRecord dead = r;
    dead.status = CareerStatus::kDeceased;
    CHECK(unpadded(award_citation_text(LetterCode::kMedalOfHonor, dead, SubCommand::kComSubPac, 0xfc, exe,
                                       typewriter, font))
              .starts_with("\nMATHIAS\nCOMMANDER\nUNITED STATES NAVY\n(Posthumous)\n\n\n\nCITATION:\n\nOn behalf"));
    const std::string puc =
        unpadded(award_citation_text(LetterCode::kPuc, r, SubCommand::kComSubPac, 0xf9, exe, typewriter, font));
    CHECK(puc.starts_with("\nPRESIDENTIAL UNIT CITATION\n\n(SS-337) CARBONERO\nBALAO CLASS SUBMARINE\n"
                          "UNITED STATES NAVY\n\nCITATION:\n\nOn behalf of the President"));
    const std::string fuel =
        unpadded(award_citation_text(LetterCode::kPoorFuel, r, SubCommand::kComSubSoWesPac, 0xf9, exe, typewriter, font));
    CHECK(fuel.starts_with("\nJune 1, 1945\n\n\nTo:Mathias\nFrom:ComSubSoWesPac\n\nRe: POOR FUEL MANAGEMENT\n\n\n"
                           "Commander:\n\nRunning out of fuel when on patrol"));
    // The headings are centred on the width given, the letters are not.
    const std::string narrow = award_citation_text(LetterCode::kNavyCross, r, SubCommand::kComSubPac, 0xfc, exe,
                                                    typewriter, font);
    CHECK(narrow != award_citation_text(LetterCode::kNavyCross, r, SubCommand::kComSubPac, 0x106, exe, typewriter,
                                        font));
  }

  SECTION("new command orders") {
    const std::string offer = unpadded(new_submarine_orders(r, SubCommand::kComSubPac, true, exe, font));
    CHECK(offer.starts_with("\n(SS-337) Carbonero\n\nDate:June 1, 1945\nFrom:ComSubPac\nBureau of Personnel\n"
                            "Re:New Command\n\nCommander Mathias:\n\n"));
    CHECK(offer.ends_with("You have been offered command of the Balao class submarine (SS-337) Carbonero. If you "
                          "choose to accept command, a new (Veteran) crew will be assigned to you."));
    const std::string report = unpadded(new_submarine_orders(r, SubCommand::kComSubPac, false, exe, font));
    CHECK(report.ends_with("Report to Base Commander at Darwin. There you will assume command of the Balao class "
                           "submarine (SS-337) Carbonero."));
  }
}

TEST_CASE("Career patrol screens return the original codes", "[career]") {
  SH_REQUIRE_INSTALL(install);
  Screens s(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  CareerRecord r = sample_record();
  const HomePort* darwin = nullptr;
  for (const HomePort& p : tables.home_ports) {
    if (p.name == "Darwin") {
      darwin = &p;
    }
  }
  REQUIRE(darwin != nullptr);
  const PatrolOrders orders{zones.at(9), darwin->command, 100, 4200};

  SECTION("patrol orders") {
    s.host.script = {ScriptedHost::click(0x1b2 + 0x63, 0x168 + 12)};
    CHECK(s.letters.patrol_orders(r, orders, darwin) == PatrolOrdersResult::kBegin);
    s.host.save("orders");
    s.host.script = {ScriptedHost::click(0x1b2 + 0x63, 0x194 + 12)};
    CHECK(s.letters.patrol_orders(r, orders, darwin) == PatrolOrdersResult::kCancel);
    s.host.script = {ScriptedHost::click(0x1b2 + 0x63, 0x13c + 12)};
    CHECK(s.letters.patrol_orders(r, orders, darwin) == PatrolOrdersResult::kRealism);
    // On patrol the orders are skipped.
    r.on_patrol = -1;
    CHECK(s.letters.patrol_orders(r, orders, darwin) == PatrolOrdersResult::kBegin);
  }

  SECTION("patrol results, home base, calendar, newspaper") {
    MissionResults m;
    m.sub = r.boat;
    m.patrol_area = zones[9].name;
    m.fuel = 60000;
    s.host.script = {ScriptedHost::key(kEnter)};
    s.letters.patrol_results(r, m, {.transit_hours = 48, .repair_days = 3, .fuel_capacity = 117000});
    s.host.save("results");
    s.host.script = {ScriptedHost::click(0x1b2 + 0x63, 0x194 + 12)};
    CHECK(s.letters.homeport(r, *darwin, true) == -1);
    s.host.save("homeport");
    s.host.script = {ScriptedHost::key(kEnter)};
    s.letters.transit_calendar(r, 4, 12);
    s.host.save("calendar");
    s.host.script = {ScriptedHost::key(kEnter)};
    s.letters.end_newspaper();
    CHECK(s.host.script.empty());
  }

  SECTION("awards and letters") {
    for (const LetterCode code : {LetterCode::kNavyCross, LetterCode::kPuc, LetterCode::kRelieved}) {
      s.host.script = {ScriptedHost::key(kEnter)};
      s.letters.present_award(code, r, darwin->command);
      CHECK(s.host.script.empty());
      s.host.save("award_" + std::to_string(static_cast<int>(code)));
    }
  }

  SECTION("new equipment") {
    r.month = 2;
    r.year = 12;
    r.equipment = 0;
    s.host.script = {ScriptedHost::key(kEnter)};
    CHECK(s.letters.new_equipment(r, 1) == -1);
    s.host.save("equipment");
    CHECK(r.equipment == 1);
    // Nothing more is due that month.
    CHECK(s.letters.new_equipment(r, 1) == 0);
  }

  SECTION("Back takes each screen's way out") {
    const ScriptedHost::Step back = ScriptedHost::back();
    s.host.script = {back};
    CHECK(s.letters.patrol_orders(r, orders, darwin) == PatrolOrdersResult::kCancel);
    s.host.script = {back};
    s.letters.patrol_results(r, MissionResults{}, {.transit_hours = 48, .repair_days = 3, .fuel_capacity = 117000});
    s.host.script = {back};
    CHECK(s.letters.homeport(r, *darwin, true) == -1);
    s.host.script = {back};
    s.letters.transit_calendar(r, 4, 12);
    r.month = 2;
    r.year = 12;
    r.equipment = 0;
    s.host.script = {back};
    CHECK(s.letters.new_equipment(r, 1) == -1);
    Career career;
    career.record = r;
    const auto assign = [&tables](Career& c) {
      c.record.boat = "(SS-212) Gato";
      c.record.class_index = static_cast<std::uint8_t>(tables.find_sub("GATO")->class_index);
      c.record.class_name = "GATO";
      c.record.class_title = tables.find_sub("GATO")->display_name;
      return true;
    };
    // An offer is declined (CANCEL), an assignment accepted (OK).
    s.host.script = {back};
    CHECK(s.letters.new_submarine(career, tables, SubCommand::kComSubPac, true, assign) == -2);
    s.host.script = {back};
    CHECK(s.letters.new_submarine(career, tables, SubCommand::kComSubPac, false, assign) == -1);
    CHECK(s.host.script.empty());
  }

  SECTION("new submarine") {
    Career career;
    career.record = r;
    career.record.promotion_patrols = 4;
    career.record.rank = 2;
    const auto assign = [&tables](Career& c) {
      c.record.boat = "(SS-212) Gato";
      c.record.class_index = static_cast<std::uint8_t>(tables.find_sub("GATO")->class_index);
      c.record.class_name = "GATO";
      c.record.class_title = tables.find_sub("GATO")->display_name;
      return true;
    };
    // An offer rejected gives the old boat back.
    s.host.script = {ScriptedHost::click(10 + 0xf9 + 0x3c, 0x27 + 0x15c + 15)};
    CHECK(s.letters.new_submarine(career, tables, SubCommand::kComSubPac, true, assign) == -2);
    s.host.save("new_submarine");
    CHECK(career.record.boat == "(SS-337) Carbonero");
    CHECK(career.record.promotion_patrols == 4);
    // Accepted: the counters of the new command reset.
    s.host.script = {ScriptedHost::click(10 + 0x1b2 + 0x3c, 0x27 + 0x15c + 15)};
    CHECK(s.letters.new_submarine(career, tables, SubCommand::kComSubPac, true, assign) == -1);
    CHECK(career.record.boat == "(SS-212) Gato");
    CHECK(career.record.promotion_patrols == 0);
    CHECK(career.record.rank == 1);
    // No boat to assign: nothing shown.
    CHECK(s.letters.new_submarine(career, tables, SubCommand::kComSubPac, false, [](Career&) { return false; }) == -2);
  }
}

TEST_CASE("Extended rules deliver the APR-1 and the ST with their letters", "[career][radar_rules]") {
  SH_REQUIRE_INSTALL(install);
  MissionRules rules;
  rules.apr1 = true;
  rules.st_radar = true;
  CareerRecord r = sample_record();
  r.month = 11;
  r.year = 14;
  r.equipment = 0xef;  // Every original delivery.

  SECTION("with the extended rules") {
    Screens s(install);
    s.host.script = {ScriptedHost::key(kEnter)};
    CHECK(s.letters.new_equipment(r, 1, rules) == -1);
    s.host.save("equipment_apr1");
    CHECK(r.equipment == 0xff);
    s.host.script = {ScriptedHost::key(kEnter)};
    CHECK(s.letters.new_equipment(r, 1, rules) == -1);
    s.host.save("equipment_st");
    CHECK(r.equipment == 0x1ff);
    // The ST's picture, composed from EQUIP.SHP, in the SD's place.
    const Image8 picture = st_radar_picture(Shp::load(install, "ART/EQUIP.SHP"));
    for (int y = 0; y < picture.height(); y += 3) {
      for (int x = 0; x < picture.width(); x += 3) {
        CHECK(s.host.last.at(85 + x, 148 + y) == picture.at(x, y));
      }
    }
    CHECK(s.letters.new_equipment(r, 1, rules) == 0);
  }

  SECTION("as the original") {
    Screens s(install);
    CHECK(s.letters.new_equipment(r, 1) == 0);
    CHECK(r.equipment == 0xef);
  }
}

TEST_CASE("The original's NEW EQUIPMENT letters come one a visit in the table's order", "[career]") {
  SH_REQUIRE_INSTALL(install);
  Screens s(install);
  CareerRecord r = sample_record();
  r.month = 11;
  r.year = 15;
  r.equipment = 0;
  // SD, SJa, bathythermograph, PPI, SJ-1, then the Mark 18-1 and 18-2.
  const std::uint32_t order[] = {0x1, 0x2, 0x20, 0x8, 0x4, 0x40, 0x80};
  std::uint32_t fitted = 0;
  for (std::size_t i = 0; i < std::size(order); ++i) {
    s.host.script = {ScriptedHost::key(kEnter)};
    CHECK(s.letters.new_equipment(r, 1) == -1);
    s.host.save("equipment_original_" + std::to_string(i));
    fitted |= order[i];
    CHECK(r.equipment == fitted);
  }
  CHECK(s.letters.new_equipment(r, 1) == 0);
}

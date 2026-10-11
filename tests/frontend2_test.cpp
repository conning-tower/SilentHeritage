// Tests of the single-mission screens: the SUB CLASS list, the results and
// score of `mission_compute_results`, the BESTMISS.SAV table and the flow
// from a mission to MISSION RESULTS and the best missions.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_best.h"
#include "game/frontend_results.h"
#include "game/frontend_subsel.h"
#include "game/mission.h"
#include "game/options_realism.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game_data.h"

using namespace sh;

namespace {

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_frontend2_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::vector<std::string> names_in_service(const std::vector<SubClass>& subs, std::int32_t year, std::int32_t month) {
  std::vector<std::string> names;
  for (const SubClass* s :
       subsel_classes(subs, [&](const SubClass& c) { return sub_class_in_service(c, year, month); })) {
    names.push_back(s->name);
  }
  return names;
}

PatrolLogEntry sunk(std::int32_t type, std::string name, std::int32_t tonnage) {
  PatrolLogEntry e;
  e.day = 21;
  e.month = 5;
  e.year = 15;
  e.type = type;
  e.name = std::move(name);
  e.tonnage = tonnage;
  return e;
}

AfloatVessel afloat(std::int32_t type, std::int32_t tonnage, std::int32_t hull, std::int32_t hp,
                    std::int32_t other = 0) {
  AfloatVessel v;
  v.type = type;
  v.class_name = "X";
  v.tonnage = tonnage;
  v.hull_strength = hull;
  v.hit_points = hp;
  v.other_damage = other;
  return v;
}

// The options of the EXPERT preset (realism 100 %).
constexpr std::uint32_t kExpert = 0x61a8;

}  // namespace

TEST_CASE("The SUB CLASS list keeps the classes in service at the date", "[frontend2][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  // SUBS.DAT service dates: Gato from April 1942, Balao from July 1943,
  // Tench from February 1945.
  CHECK(names_in_service(tables.subs, 11, 11) ==
        std::vector<std::string>{"S CLASS", "BARRACUDA", "NARWHAL", "P CLASS", "SALMON", "T CLASS"});
  CHECK(names_in_service(tables.subs, 12, 2).size() == 6);
  CHECK(names_in_service(tables.subs, 12, 3).back() == "GATO");
  CHECK(names_in_service(tables.subs, 13, 6).back() == "BALAO");
  CHECK(names_in_service(tables.subs, 15, 0).back() == "BALAO");
  CHECK(names_in_service(tables.subs, 15, 1).back() == "TENCH");
  // Without a filter, all of them in file order.
  CHECK(subsel_classes(tables.subs, {}).size() == tables.subs.size());

  const ExeImage exe = ExeImage::load(install);
  const SubClass& gato = *tables.find_sub("GATO");
  const std::vector<SubInfoText> stats = sub_statistics(gato, exe);
  REQUIRE(stats.size() == 19);
  CHECK(stats[0].text == "DISPLACEMENT(tons): ");
  CHECK(stats[0].y == 0x14);
  CHECK(stats[1].x == 0x8d);
  CHECK(stats[9].text == gato.surface_speed_kts + " surfaced; " + gato.submerged_speed_kts + " submerged");
  CHECK(stats[14].text == "95 at 5 knots submerged");
  CHECK(stats[17].text == "GUNS: ");
}

TEST_CASE("Mission results reproduce the scores of the shipped best missions", "[frontend2]") {
  // BESTMISS.SAV #1: 7 sinkings, a merchant damaged 67 %, two escaped,
  // difficulty 56x, realism 100 %: score 26809.
  MissionOutcome o;
  o.end_code = 0x11;
  o.difficulty = 560;
  o.patrol_log = {sunk(8, "TACHIBANA", 1580), sunk(8, "TACHIBANA", 1580), sunk(9, "TYPE D", 940),
                  sunk(8, "MATSU", 1530),     sunk(18, "", 906),          sunk(20, "", 7542),
                  sunk(12, "", 15200)};
  o.afloat = {afloat(14, 5180, 100, 100), afloat(12, 15200, 100, 33), afloat(19, 2370, 100, 100)};
  MissionResults r = compute_mission_results(o, kExpert);
  CHECK(r.ships_sunk == 7);
  CHECK(r.tonnage == 29278);
  CHECK(r.score == 26809);
  CHECK(r.realism_pct == 100);
  CHECK(r.difficulty == 56);
  CHECK(r.warships_sunk == 0);
  CHECK(r.fate == BoatFate::kOperational);
  REQUIRE(r.sunk.size() == 10);
  CHECK(r.sunk[6].status == SunkStatus::kSunk);
  CHECK(r.sunk[7].status == SunkStatus::kEscaped);
  CHECK(r.sunk[7].damage_pct == 0);
  CHECK(r.sunk[8].status == SunkStatus::kDamaged);
  CHECK(r.sunk[8].damage_pct == 67);
  // Merchants keep no class name.
  CHECK(r.sunk[8].log.name.empty());

  // #3: a photo recon (+500), three warships damaged, hull and systems
  // 10 %, difficulty 967: score 23552.
  o = MissionOutcome{};
  o.difficulty = 967;
  o.hull_damage_pct = 10;
  o.system_damage_pct = 10;
  o.patrol_log = {sunk(8, "TACHIBANA", 1580), sunk(9, "TYPE C", 810),
                  sunk(PatrolLogEntry::kPhotoRecon, "Industrial Facilities", 0), sunk(9, "TYPE D", 940)};
  o.afloat = {afloat(3, 42850, 400, 356), afloat(3, 71659, 600, 336), afloat(5, 14980, 100, 61),
              afloat(7, 2520, 100, 100)};
  r = compute_mission_results(o, kExpert);
  CHECK(r.ships_sunk == 3);
  CHECK(r.tonnage == 3330);
  CHECK(r.photo_recons == 1);
  CHECK(r.score == 23552);
  CHECK(r.sunk[4].damage_pct == 11);
  CHECK(r.sunk[5].damage_pct == 44);
  CHECK(r.sunk[4].log.name == "X");  // a battleship keeps its class

  // #4: realism 76 %, FUSO sunk, difficulty 341: score 18293.
  std::uint32_t options76 = 0;
  for (std::uint32_t w = 0; w < 0x10000; w += 4) {
    if (realism_rating_percent(realism_rating(w)) == 76) {
      options76 = w;
      break;
    }
  }
  REQUIRE(options76 != 0);
  o = MissionOutcome{};
  o.difficulty = 341;
  o.patrol_log = {sunk(3, "FUSO", 35900)};
  r = compute_mission_results(o, options76);
  CHECK(r.realism_pct == 76);
  CHECK(r.score == 18293);
}

TEST_CASE("Mission results: fates, the player's share of damage and the career mode", "[frontend2]") {
  MissionOutcome o;
  o.end_code = 0xc;
  o.hull_damage_pct = 20;
  o.difficulty = 1000;
  o.patrol_log = {sunk(PatrolLogEntry::kRescuedPilot, "", 0), sunk(0x15, "", 50), sunk(2, "SHIP", 1000)};
  // Hull 100, 20 points done by others, 60 left: 40 % damaged, of which
  // the player's share is 100 - 60 x 100 / 80 = 25 %.
  o.afloat = {afloat(12, 8000, 100, 60, 20)};
  MissionResults r = compute_mission_results(o, kExpert);
  CHECK(r.fate == BoatFate::kKilled);
  CHECK(r.hull_damage_pct == 100);
  CHECK(r.system_damage_pct == 100);
  CHECK(r.pilots_rescued == 1);
  CHECK(r.sampans_sunk == 1);
  CHECK(r.ships_sunk == 2);
  CHECK(r.warships_sunk == 1);
  CHECK(r.tonnage == 1050);
  CHECK(r.sunk[3].damage_pct == 25);
  // 500 + 1000 + 8000 / 2 x 25 / 100 = 2500, then the boat's damage:
  // - 625 x 100 % and - 234 x 100 %.
  CHECK(r.score == 1641);

  // A career patrol lists only the log and keeps the raw score.
  r = compute_mission_results(o, kExpert | option::kModeCareer);
  CHECK(r.sunk.size() == 3);
  CHECK(r.score == 1500);

  CHECK(fate_of_end_code(6) == BoatFate::kCaptured);
  CHECK(fate_of_end_code(9) == BoatFate::kRecalled);
  CHECK(fate_of_end_code(7) == BoatFate::kOperational);
  o.end_code = 7;
  CHECK(compute_mission_results(o, kExpert).boat_lost == -1);
  CHECK(returns_to_port(0x13));
  CHECK_FALSE(returns_to_port(1));
}

TEST_CASE("The results summary lays out the record in columns", "[frontend2][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const Font font = Font::load(install, "ART/MENU_S.FNT");
  const std::vector<std::uint8_t> data = install.read("SAVE/BESTMISS.SAV");
  const std::optional<BestMissionsTable> table = BestMissionsTable::parse(data);
  REQUIRE(table);
  const std::string text = mission_results_summary(table->entries[0]->results, exe, font);
  CHECK(text.starts_with("Date: "));
  CHECK(text.find("June 21, 1945") != std::string::npos);
  CHECK(text.find("(SS-242) Bluegill") != std::string::npos);
  CHECK(text.find("Escort Destroyer (TACHIBANA)") != std::string::npos);
  CHECK(text.find("Damaged (67%)") != std::string::npos);
  CHECK(text.find("20-52N, 120-04E") != std::string::npos);
  CHECK(text.find("Hull Damage: ") != std::string::npos);
  CHECK(text.ends_with("\n***END OF RESULTS***"));
  // The value column starts at 0x50 pixels.
  const std::string first = text.substr(0, text.find('\n'));
  CHECK(font.text_width(first.substr(0, first.find("June"))) >= 0x50);
  CHECK(font.text_width(first.substr(0, first.find("June"))) < 0x50 + font.glyph_width(1));
}

TEST_CASE("BESTMISS.SAV parses, round-trips and ranks like the original", "[frontend2][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<std::uint8_t> data = install.read("SAVE/BESTMISS.SAV");
  const std::optional<BestMissionsTable> table = BestMissionsTable::parse(data);
  REQUIRE(table);
  const BestMissionEntry& first = *table->entries[0];
  CHECK(first.name == "Mathias");
  CHECK(first.results.sub == "(SS-242) Bluegill");
  CHECK(first.results.class_name == "GATO");
  CHECK(first.results.patrol_area == "South China Sea");
  CHECK(first.results.score == 26809);
  CHECK(first.results.minutes_elapsed == 151);
  CHECK(first.results.fuel == 116372);
  CHECK(first.results.latitude.degrees == 20);
  CHECK(first.results.latitude.minutes == 52);
  CHECK(first.results.longitude.hemisphere == 'E');
  REQUIRE(first.results.sunk.size() == 10);
  CHECK(first.results.sunk[8].status == SunkStatus::kDamaged);
  CHECK(first.results.sunk[8].damage_pct == 67);
  CHECK(table->entries[9]->results.score == 2573);
  CHECK(table->entries[5]->results.fate == BoatFate::kKilled);

  const std::vector<std::uint8_t> out = table->serialize();
  CHECK(out.size() == 1 + 10 * BestMissionsTable::kEntrySize);
  CHECK(out.size() == data.size());
  CHECK(out[0] == 2);
  // The used flag, the name and the record fields are where the game
  // wrote them (the stale bytes after the strings excepted).
  for (const std::size_t at : {std::size_t{1}, std::size_t{1 + 0x19 + 0xdc}, std::size_t{1 + 0x135}}) {
    CHECK(std::equal(out.begin() + static_cast<std::ptrdiff_t>(at), out.begin() + static_cast<std::ptrdiff_t>(at + 4),
                     data.begin() + static_cast<std::ptrdiff_t>(at)));
  }
  CHECK(BestMissionsTable::parse(out)->serialize() == out);

  // A version 1 file has no pilots / photo recons fields.
  std::vector<std::uint8_t> v1{1};
  for (std::size_t i = 0; i < 10; ++i) {
    const auto entry = out.begin() + static_cast<std::ptrdiff_t>(1 + i * BestMissionsTable::kEntrySize);
    v1.insert(v1.end(), entry, entry + 0x19 + 0x114);
    v1.insert(v1.end(), entry + 0x19 + 0x11c, entry + static_cast<std::ptrdiff_t>(BestMissionsTable::kEntrySize));
  }
  const std::optional<BestMissionsTable> old = BestMissionsTable::parse(v1);
  REQUIRE(old);
  CHECK(old->entries[2]->results.photo_recons == 0);
  CHECK(old->entries[2]->results.score == table->entries[2]->results.score);
  CHECK_FALSE(BestMissionsTable::parse(std::span(v1).first(100)));

  // A tie ranks before the older entry; a full table refuses lower scores.
  CHECK(table->rank_of(26809) == 0);
  CHECK(table->rank_of(25000) == 2);
  CHECK(table->rank_of(2573) == 9);
  CHECK_FALSE(table->rank_of(2572));
  BestMissionsTable t = *table;
  BestMissionEntry entry{.name = "New", .results = {}};
  entry.results.score = 25000;
  t.insert(2, entry);
  CHECK(t.entries[2]->name == "New");
  CHECK(t.entries[3]->results.score == 23552);
  CHECK(t.entries[9]->results.score == 3350);
}

TEST_CASE("A missing BESTMISS.SAV is created empty", "[frontend2]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  const std::optional<BestMissionsTable> table = BestMissionsTable::load(saves);
  REQUIRE(table);
  CHECK(table->rank_of(1) == 0);
  CHECK(std::filesystem::file_size(dir.path() / "BESTMISS.SAV") == 1 + 10 * BestMissionsTable::kEntrySize);
}

namespace {

// A host replaying clicks one every 40 frames, whose missions end with a
// fixed outcome.
class ResultsHost final : public FrontendHost, public MissionRunner {
 public:
  std::deque<std::pair<int, int>> clicks;
  MissionOutcome mission;
  int missions = 0;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8&, const VgaPalette&) override {
    tick_ += 2;
    if (++frames_ > 50'000) {
      throw std::runtime_error("the script did not end the front end");
    }
  }
  // The button goes down on one frame and up on the next, like a real
  // click (a text entry's edit loop hands the press back to its dialog).
  void poll_input(ui::Gui& gui) override {
    if (release_) {
      release_ = false;
      gui.mouse().update(x_, y_, 0, 0);
      return;
    }
    if (++idle_ < 40 || clicks.empty()) {
      return;
    }
    idle_ = 0;
    std::tie(x_, y_) = clicks.front();
    clicks.pop_front();
    gui.mouse().update(x_, y_, ui::kButtonLeft, 0);
    release_ = true;
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}
  std::int32_t run(const MissionRequest&) override {
    ++missions;
    return mission.end_code;
  }
  MissionOutcome outcome() const override { return mission; }

 private:
  std::uint32_t tick_ = 0;
  int idle_ = 0;
  int frames_ = 0;
  bool release_ = false;
  int x_ = 0;
  int y_ = 0;
};

}  // namespace

TEST_CASE("A finished mission shows its results and enters the best missions", "[frontend2][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path());
  ResultsHost host;
  host.mission.end_code = 0;
  host.mission.boat_name = "(SS-305) Skate";
  host.mission.class_name = "BALAO";
  host.mission.difficulty = 500;
  host.mission.patrol_log = {sunk(0, "SHOKAKU", 29800)};
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  host.clicks = {
      {20 + 35 + 90, 43 + 130 + 17},   // SINGLE MISSION
      {69 + 38 + 90, 59 + 125 + 17},   // HISTORIC
      {69 + 335 + 60, 19 + 395 + 17},  // BEGIN
      {10 + 0xf5 + 60, 12 + 0x118 + 12},  // MISSION RESULTS: OK
      {0x118 + 0x72 + 60, 5 + 0xf5 + 12},  // HIGH SCORE: OK
      {69 + 261 + 90, 59 + 299 + 17},  // BEST MISSIONS
      {10 + 0xf5 + 60, 12 + 400 + 12},  // OK
      {69 + 38 + 90, 59 + 299 + 17},   // MAIN MENU
      {20 + 35 + 90, 43 + 330 + 17},   // EXIT TO DOS
  };
  front_end.run();
  CHECK(host.clicks.empty());
  CHECK(host.missions == 1);
  // 29800 - 14900 x 500 / 1000 = 22350 at 100 % realism.
  const std::optional<BestMissionsTable> table = BestMissionsTable::load(saves);
  REQUIRE(table);
  REQUIRE(table->entries[0]);
  CHECK(table->entries[0]->name == "Anonymous");
  CHECK(table->entries[0]->results.score == 22350);
  CHECK(table->entries[0]->results.sub == "(SS-305) Skate");
  CHECK_FALSE(table->entries[1]);
}

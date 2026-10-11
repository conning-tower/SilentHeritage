// The settings dialogs and the small boxes driven through the GUI: OK
// commits the working copy and CANCEL does not, the date limits seen from
// the controls, the disabled scope, the torpedo rows, OPTIONS saving
// SBOPTION.SAV, the password stored encoded, and the boxes' results.
// SH_SCREENSHOT_DIR receives a picture of each dialog.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <string>

#include "assets/scn_file.h"
#include "editor/dialogs_boxes.h"
#include "editor/dialogs_settings.h"
#include "editor/loadout.h"
#include "editor_ui_fixture.h"
#include "game/saves_options.h"
#include "game_data.h"
#include "password_key.h"
#include "temp_dir.h"

namespace sh::editor {
namespace {

using test::EditorFixture;

// The screen point inside a control of a dialog at (dx, dy).
struct Point {
  int x;
  int y;
};
constexpr Point at(int dx, int dy, int x, int y, int w, int h) { return {dx + x + w / 2, dy + y + h / 2}; }

// Control positions (dialog corner + control, the cycle buttons 21 and the
// buttons 24 pixels high).
constexpr Point kEnvMonth = at(126, 1, 103, 57, 132, 21);
constexpr Point kEnvDayUnits = at(126, 1, 256, 57, 16, 21);
constexpr Point kEnvHourTens = at(126, 1, 189, 98, 16, 21);
constexpr Point kEnvWeather = at(126, 1, 185, 143, 152, 21);
constexpr Point kEnvCancel = at(126, 1, 34, 417, 100, 24);
constexpr Point kEnvOk = at(126, 1, 255, 417, 100, 24);
constexpr Point kEquipRadar = at(157, 109, 153, 57, 142, 21);
constexpr Point kEquipScope = at(157, 109, 153, 90, 142, 21);
constexpr Point kEquipAirRadar = at(157, 109, 153, 135, 142, 21);
constexpr Point kEquipOk = at(157, 109, 200, 219, 100, 24);
// EQUIPMENT with the radar detector's row: 44 pixels taller, 22 higher.
constexpr Point kEquipDetector = at(157, 87, 153, 223, 142, 21);
constexpr Point kEquipDetectorOk = at(157, 87, 200, 263, 100, 24);
constexpr Point kTorpBowTube0 = at(100, 80, 228, 74, 169, 21);
constexpr Point kTorpOk = at(100, 80, 302, 311, 100, 24);
constexpr Point kTorpCancel = at(100, 80, 14, 311, 100, 24);
// The aft units digit of reload row `row` (0-based).
constexpr Point torp_aft_units(int row) { return at(100, 80, 172, 229 + row * 22, 16, 21); }
constexpr Point kOptShips = {157 + 54 + 6, 52 + 70 + 9};
constexpr Point kOptEquipment = {157 + 54 + 6, 52 + 162 + 9};
constexpr Point kOptRandomize = {157 + 54 + 6, 52 + 286 + 9};
constexpr Point kOptOk = at(157, 52, 202, 320, 100, 24);
constexpr Point kPasswordAccept = at(163, 166, 177, 110, 100, 24);
constexpr Point kBriefingOk = at(53, 80, 322, 265, 100, 24);
constexpr Point kSaveChangesSave = at(203, 142, 67, 131, 100, 24);
constexpr Point kSaveChangesDiscard = at(203, 142, 67, 92, 100, 24);
constexpr Point kSaveChangesCancel = at(203, 142, 67, 169, 100, 24);
constexpr Point kQuitYes = at(211, 191, 19, 77, 183, 20);
constexpr Point kQuitNo = at(211, 191, 19, 53, 183, 20);

void click(EditorFixture& fx, Point p) { fx.click(p.x, p.y); }

void type(EditorFixture& fx, std::string_view text) {
  for (const char c : text) {
    fx.key(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
  }
}

}  // namespace

TEST_CASE("ENVIRONMENT commits on OK only", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const ScnHeader before = s.header;  // 7 December 1941, 12:00, Clear

  SECTION("OK") {
    fx.screenshot("se_environment");
    click(fx, kEnvMonth);    // December 1941 has no other month in the war
    click(fx, kEnvDayUnits);  // the 7th goes to the 8th
    click(fx, kEnvHourTens);  // 12:00 to 22:00
    click(fx, kEnvWeather);   // Partly Cloudy
    fx.screenshot("se_environment_changed");
    click(fx, kEnvOk);
    CHECK(cmd_environment(fx.ui, s, fx.options));
    CHECK(s.header.month == 11);
    CHECK(s.header.day == 8);
    CHECK(s.header.hour == 22);
    CHECK(s.header.weather == Weather::kPartlyCloudy);
    CHECK(s.modified);
  }
  SECTION("CANCEL") {
    const std::int32_t light = s.sighting_light_pct;
    click(fx, kEnvHourTens);  // 22:00: the preview's night light
    click(fx, kEnvWeather);
    click(fx, kEnvCancel);
    CHECK_FALSE(cmd_environment(fx.ui, s, fx.options));
    CHECK(s.header == before);
    CHECK_FALSE(s.modified);
    // Regression test for the original bug "ENVIRONMENT preview light"
    // (SE kept the light of the last previewed time).
    CHECK(s.sighting_light_pct == light);
  }
}

TEST_CASE("EQUIPMENT greys the scope without a surface radar and commits on OK", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.month = 0;
  s.header.year = 1944 - kScenarioYearBase;
  s.header.equipment = 0;
  fx.screenshot("se_equipment");
  click(fx, kEquipScope);     // disabled: stays A-Scope
  click(fx, kEquipAirRadar);  // SD
  click(fx, kEquipRadar);     // SJa
  click(fx, kEquipScope);     // PPI
  click(fx, kEquipOk);
  CHECK(cmd_equipment(fx.ui, s, fx.options));
  CHECK(s.header.equipment == (1u | 2u | 8u));
  CHECK(s.modified);
}

TEST_CASE("TORPEDOES shows the rows of the available torpedoes and commits on OK", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const EditorData& data = fx.data;
  set_scn_text(s.player().rec.sub_class, "GATO");
  s.header.year = 1942 - kScenarioYearBase;
  s.reset_torpedo_load();
  const ScnHeader before = s.header;
  const std::size_t mark14 = *data.find_listed_torpedo("MARK14");

  SECTION("OK") {
    fx.screenshot("se_torpedoes");
    click(fx, kTorpBowTube0);      // MARK14 -> EMPTY (no MARK18-1 in 1942)
    click(fx, torp_aft_units(1));  // the MARK14 row: one more does not fit
    click(fx, kTorpOk);
    CHECK(cmd_torpedoes(fx.ui, s, fx.options));
    CHECK(s.header.tube_torpedoes[0] == 0);
    CHECK(s.header.tube_torpedoes[1] == before.tube_torpedoes[1]);
    CHECK(s.reload_aft[mark14] == 0);
    CHECK(s.modified);
  }
  SECTION("CANCEL") {
    click(fx, kTorpBowTube0);
    click(fx, kTorpCancel);
    CHECK_FALSE(cmd_torpedoes(fx.ui, s, fx.options));
    CHECK(s.header.tube_torpedoes == before.tube_torpedoes);
    CHECK_FALSE(s.modified);
  }
}

TEST_CASE("BRIEFING TEXT edits a copy of the briefing", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  // The text box has the focus at once and edits.
  type(fx, "SINK IT");
  fx.screenshot("se_briefing");
  fx.key(ui::kKeyTab);
  click(fx, kBriefingOk);
  CHECK(cmd_briefing_text(fx.ui, s));
  CHECK(scn_text(s.header.briefing) == "SINK IT");
  CHECK(s.modified);
}

TEST_CASE("OPTIONS saves SBOPTION.SAV and re-validates the equipment", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const test::TempDir dir;
  const SaveDir saves(dir.path());
  fx.options.limit_equipment = 0;
  s.header.equipment = 4;  // SJ-1 in December 1941
  fx.screenshot("se_options");
  click(fx, kOptShips);
  click(fx, kOptEquipment);
  click(fx, kOptRandomize);
  click(fx, kOptOk);
  CHECK(cmd_options(fx.ui, s, fx.options, saves));
  CHECK(fx.options.limit_ships == 0);
  CHECK(fx.options.limit_equipment == -1);
  CHECK(fx.options.randomize == 0);
  CHECK(s.header.randomize == 0);
  CHECK(s.modified);
  CHECK(s.header.equipment == 0);
  const EditorOptions saved = EditorOptions::load(saves);
  CHECK(saved == fx.options);
}

TEST_CASE("PASSWORD stores the password encoded", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;

  SECTION("A new password is confirmed") {
    type(fx, "Secret");
    fx.screenshot("se_password");
    fx.key(ui::kKeyEnter);  // ends the entry; SET mode stays open
    click(fx, kPasswordAccept);
    fx.screenshot("se_password_confirm");
    fx.key(0x1579);  // y
    CHECK(cmd_set_password(fx.ui, s));
    CHECK(s.header.password == encode_scn_password("Secret", sh::test::se_password_key(install)));
    CHECK(s.modified);
  }
  SECTION("NO keeps the old one") {
    type(fx, "Secret");
    fx.key(ui::kKeyEnter);
    click(fx, kPasswordAccept);
    fx.key(0x316e);  // n
    CHECK_FALSE(cmd_set_password(fx.ui, s));
    CHECK(s.header.password == ScnHeader{}.password);
  }
  SECTION("Clearing zeroes every byte") {
    // Regression test for the original bug "Password storage" (SE zeroed
    // the first byte only).
    s.header.password = encode_scn_password("Secret", sh::test::se_password_key(install));
    for (int i = 0; i < 6; ++i) {
      fx.key(ui::kKeyBackspace);
    }
    fx.key(ui::kKeyEnter);
    click(fx, kPasswordAccept);
    CHECK(cmd_set_password(fx.ui, s));
    CHECK(s.header.password == ScnHeader{}.password);
  }
  SECTION("The entry takes 14 characters, which the load accepts") {
    type(fx, "abcdefghijklmnop");
    fx.key(ui::kKeyEnter);
    click(fx, kPasswordAccept);
    fx.key(0x1579);  // y
    CHECK(cmd_set_password(fx.ui, s));
    CHECK(decode_scn_password(s.header.password, sh::test::se_password_key(install)) == "abcdefghijklmn");
    type(fx, "ABCDEFGHIJKLMN");
    fx.key(ui::kKeyEnter);
    CHECK(ask_password(fx.ui, s.header));
  }
}

TEST_CASE("The password is asked three times on load", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  ScnHeader header;
  CHECK(ask_password(fx.ui, header));  // no password: no dialog
  header.password = encode_scn_password("Secret", sh::test::se_password_key(install));

  SECTION("The second try, case-insensitive") {
    type(fx, "wrong");
    fx.key(ui::kKeyEnter);
    fx.screenshot("se_access_denied");
    fx.key(ui::kKeyEnter);  // the message box
    type(fx, "SECRET");
    fx.key(ui::kKeyEnter);
    CHECK(ask_password(fx.ui, header));
  }
  SECTION("Three wrong ones") {
    for (int i = 0; i < 3; ++i) {
      type(fx, "x");
      fx.key(ui::kKeyEnter);
      fx.key(ui::kKeyEnter);
    }
    CHECK_FALSE(ask_password(fx.ui, header));
  }
  SECTION("An empty entry does not end ENTER PASSWORD; Esc does") {
    fx.key(ui::kKeyEnter);
    click(fx, kPasswordAccept);
    fx.key(ui::kKeyEsc);
    fx.key(ui::kKeyEsc);
    CHECK_FALSE(ask_password(fx.ui, header));
  }
}

TEST_CASE("The small boxes return their buttons", "[editor][dialogs]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  EditorUi& ui = fx.ui;

  SECTION("Message box") {
    fx.screenshot("se_message_box");
    fx.key(ui::kKeyEnter);
    show_message_box(ui, "ERROR", "ACCESS DENIED!", "Password is incorrect", "");
  }
  SECTION("Yes / no by keys") {
    fx.screenshot("se_yes_no");
    fx.key(0x1559);  // Y
    CHECK(ask_yes_no(ui, "WARNING", "NO SHIPS IN RANGE?", "No ships are within detection range.", "",
                     ui.str(se_addr::kYes), ui.str(se_addr::kNo)) == ui::kResultAccept);
    fx.key(0x314e);  // N
    CHECK(ask_yes_no(ui, "WARNING", "", "", "", "YES", "NO") == ui::kResultCancel);
  }
  SECTION("Save changes") {
    CHECK(ask_save_changes(ui, false, [] { return 99; }) == ui::kResultAccept);
    fx.screenshot("se_save_changes");
    click(fx, kSaveChangesSave);
    CHECK(ask_save_changes(ui, true, [] { return 99; }) == 99);
    click(fx, kSaveChangesDiscard);
    CHECK(ask_save_changes(ui, true, [] { return 99; }) == ui::kResultAccept);
    click(fx, kSaveChangesCancel);
    CHECK(ask_save_changes(ui, true, [] { return 99; }) == ui::kResultCancel);
  }
  SECTION("Quit to DOS") {
    fx.screenshot("se_quit_to_dos");
    click(fx, kQuitNo);
    CHECK_FALSE(quit_to_dos_prompt(ui));
    click(fx, kQuitYes);
    CHECK(quit_to_dos_prompt(ui));
  }
  SECTION("About") {
    fx.screenshot("se_about");
    fx.key(ui::kKeyEnter);
    show_about_box(ui);
  }
}

TEST_CASE("Extended: EQUIPMENT sets the radar detector, limited by date", "[editor][dialogs][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool limit : {false, true}) {
    EditorFixture fx(install);
    Scenario& s = fx.scenario;
    fx.options.limit_equipment = limit ? -1 : 0;
    s.header.equipment = 0;
    const bool in_service = fit_available(fx.data.se, Fit::kRadarDetector, s.header.month, s.header.year);
    fx.screenshot("se_equipment_detector");
    click(fx, kEquipDetector);
    click(fx, kEquipDetectorOk);
    CHECK(cmd_equipment(fx.ui, s, fx.options, true));
    CHECK(s.header.equipment == (!limit || in_service ? 0x10u : 0u));
  }
  // At the war's end it is in service whatever SE's date.
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.month = 7;
  s.header.year = 1945 - kScenarioYearBase;
  s.header.equipment = 0;
  REQUIRE(fit_available(fx.data.se, Fit::kRadarDetector, s.header.month, s.header.year));
  click(fx, kEquipDetector);
  click(fx, kEquipDetector);  // back to none
  click(fx, kEquipDetector);
  click(fx, kEquipDetectorOk);
  CHECK(cmd_equipment(fx.ui, s, fx.options, true));
  CHECK(s.header.equipment == 0x10u);
}

TEST_CASE("Extended: EQUIPMENT without the row keeps bit 4 as it is", "[editor][dialogs][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.month = 0;
  s.header.year = 1944 - kScenarioYearBase;
  s.header.equipment = 0x10;
  click(fx, kEquipDetector);  // nothing there in SE's dialog
  click(fx, kEquipOk);
  CHECK(cmd_equipment(fx.ui, s, fx.options));
  CHECK(s.header.equipment == 0x10u);
}

TEST_CASE("Extended: OPTIONS reports an SBOPTION.SAV it cannot write", "[editor][dialogs][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  const test::TempDir dir;
  // A directory under a plain file cannot be made.
  std::ofstream(dir.path() / "FILE") << "x";
  const SaveDir saves(dir.path() / "FILE" / "SAVE");
  for (const bool report : {false, true}) {
    EditorFixture fx(install);
    bool boxed = false;
    click(fx, kOptOk);
    fx.then([&boxed] { boxed = true; });
    fx.screenshot("se_options_write_error");
    fx.key(ui::kKeyEnter);
    CHECK(cmd_options(fx.ui, fx.scenario, fx.options, saves, report));
    // The message box ran frames for the steps after OK; SE's dialog ended
    // with OK.
    CHECK(boxed == report);
    CHECK(fx.host.script.empty() == report);
  }
}

}  // namespace sh::editor

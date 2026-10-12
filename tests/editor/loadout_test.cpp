// The EQUIPMENT and TORPEDOES settings: the fit dates of "Limit by date:
// Equipment" (SE's, not the manual's), the scope without a surface radar,
// the re-validation by date, the torpedo choices by class and date, and the
// reload digits cycling until the racks' total fits.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "editor/loadout.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

constexpr std::int32_t year_of(std::int32_t full) { return full - kScenarioYearBase; }

EquipmentForm form_on(std::int32_t month, std::int32_t year, std::uint32_t equipment = 0) {
  ScnHeader h;
  h.month = month;
  h.year = year_of(year);
  return EquipmentForm::unpack(equipment, h, EditorOptions{});
}

std::size_t sub_class(const EditorData& data, std::string_view name) {
  const std::optional<std::size_t> i = data.find_sub(name);
  REQUIRE(i);
  return *i;
}

// The TORPEDOES choice of WEAPONS.DAT torpedo `short_name`.
std::size_t choice_of(const EditorData& data, std::string_view short_name) {
  const std::optional<std::size_t> i = data.find_listed_torpedo(short_name);
  REQUIRE(i);
  return *i + 1;
}

}  // namespace

TEST_CASE("Equipment fits follow SE's dates", "[editor][loadout]") {
  SH_REQUIRE_INSTALL(install);
  const SeExe& se = test::editor_data(install).se;
  // SD Jan 1942, SJa Aug 1942, SJ-1 Aug 1943, PPI Jul 1943, bit 4 May 1944,
  // bathythermograph Mar 1943 (the manual has other dates).
  CHECK_FALSE(fit_available(se, Fit::kSd, 11, year_of(1941)));
  CHECK(fit_available(se, Fit::kSd, 0, year_of(1942)));
  CHECK_FALSE(fit_available(se, Fit::kSja, 6, year_of(1942)));
  CHECK(fit_available(se, Fit::kSja, 7, year_of(1942)));
  CHECK_FALSE(fit_available(se, Fit::kSj1, 6, year_of(1943)));
  CHECK(fit_available(se, Fit::kSj1, 7, year_of(1943)));
  CHECK_FALSE(fit_available(se, Fit::kPpi, 5, year_of(1943)));
  CHECK(fit_available(se, Fit::kPpi, 6, year_of(1943)));
  CHECK_FALSE(fit_available(se, Fit::kRadarDetector, 3, year_of(1944)));
  CHECK(fit_available(se, Fit::kRadarDetector, 4, year_of(1944)));
  CHECK_FALSE(fit_available(se, Fit::kBathythermograph, 1, year_of(1943)));
  CHECK(fit_available(se, Fit::kBathythermograph, 2, year_of(1943)));
}

TEST_CASE("Equipment cycles skip the fits not yet in service", "[editor][loadout]") {
  SH_REQUIRE_INSTALL(install);
  const SeExe& se = test::editor_data(install).se;

  SECTION("January 1942: no surface radar yet, the SD is") {
    EquipmentForm f = form_on(0, 1942);
    CHECK(f.cycle_surface_radar(se, true) == 0);
    CHECK(f.cycle_air_radar(se, true) == 1);
    CHECK(f.cycle_bathythermograph(se, true) == 0);
  }
  SECTION("September 1942: SJa only") {
    EquipmentForm f = form_on(8, 1942);
    CHECK(f.cycle_surface_radar(se, true) == 1);
    CHECK(f.cycle_surface_radar(se, true) == 0);
  }
  SECTION("Without the limit everything cycles") {
    EquipmentForm f = form_on(0, 1942);
    f.limit = false;
    CHECK(f.cycle_surface_radar(se, true) == 1);
    CHECK(f.cycle_surface_radar(se, true) == 2);
    CHECK(f.cycle_surface_radar(se, true) == 0);
  }
  SECTION("The scope shows NA without a surface radar") {
    EquipmentForm f = form_on(0, 1944);
    CHECK(f.scope_disabled());
    CHECK(f.cycle_scope(se, false) == 0);
    f.cycle_surface_radar(se, true);
    CHECK_FALSE(f.scope_disabled());
    CHECK(f.cycle_scope(se, false) == 1);  // A-Scope
    CHECK(f.cycle_scope(se, true) == 2);   // PPI
  }
}

TEST_CASE("The equipment word is unpacked and repacked", "[editor][loadout]") {
  // Bits 0 SD, 1-2 surface radar, 3 PPI, 4 detector, 5 bathythermograph;
  // the other bits stay.
  const EquipmentForm f = form_on(0, 1945, 0xc0u | 0x3fu);
  CHECK(f.air_radar == 1);
  CHECK(f.surface_radar == 3);
  CHECK(f.scope == 1);
  CHECK(f.radar_detector == 1);
  CHECK(f.bathythermograph == 1);
  CHECK(f.pack(0x100u) == 0x13fu);
  EquipmentForm none = form_on(0, 1945);
  CHECK(none.pack(0xffu) == 0xc0u);
}

TEST_CASE("Equipment is re-validated by date", "[editor][loadout]") {
  SH_REQUIRE_INSTALL(install);
  test::EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.month = 8;
  s.header.year = year_of(1942);
  // SJ-1 (4), PPI (8), SD (1), bathythermograph (0x20), detector (0x10).
  s.header.equipment = 0x80u | 4u | 8u | 1u | 0x20u | 0x10u;
  SECTION("Limited: the fits not in service go, the SD stays") {
    validate_equipment_by_date(s, fx.options);
    // The SJ-1 moves on to the next fit in service, which is None (the SJa
    // comes before it in the cycle).
    CHECK(s.header.equipment == (0x80u | 1u));
  }
  SECTION("SJa stays in service") {
    s.header.equipment = 2u;
    validate_equipment_by_date(s, fx.options);
    CHECK(s.header.equipment == 2u);
  }
  SECTION("Not limited: nothing changes") {
    fx.options.limit_equipment = 0;
    validate_equipment_by_date(s, fx.options);
    CHECK(s.header.equipment == (0x80u | 4u | 8u | 1u | 0x20u | 0x10u));
  }
}

TEST_CASE("Torpedo choices follow the class and the date", "[editor][loadout]") {
  SH_REQUIRE_INSTALL(install);
  test::EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const EditorData& data = fx.data;
  const std::size_t mark10 = choice_of(data, "MARK10");
  const std::size_t mark14 = choice_of(data, "MARK14");
  const std::size_t mark18 = choice_of(data, "MARK18-1");
  REQUIRE(data.torpedo_choices.count == 4);

  SECTION("A class without modern torpedoes carries the MARK10 only") {
    std::optional<std::size_t> old;
    for (std::size_t i = 0; i < data.tables.subs.size(); ++i) {
      if (!data.tables.subs[i].modern_torpedoes) {
        old = i;
        break;
      }
    }
    REQUIRE(old);
    set_scn_text(s.player().rec.sub_class, data.tables.subs[*old].name);
    const TorpedoForm f(s, fx.options);
    CHECK_FALSE(f.unavailable(mark10));
    CHECK(f.unavailable(mark14));
    CHECK(f.unavailable(mark18));
  }
  SECTION("The MARK18-1 from November 1943 (its 1-based month against the 0-based date)") {
    set_scn_text(s.player().rec.sub_class, data.tables.subs[sub_class(data, "GATO")].name);
    s.header.year = year_of(1943);
    s.header.month = 9;  // October
    CHECK(TorpedoForm(s, fx.options).unavailable(mark18));
    s.header.month = 10;
    CHECK_FALSE(TorpedoForm(s, fx.options).unavailable(mark18));
    s.header.month = 9;
    fx.options.limit_equipment = 0;
    CHECK_FALSE(TorpedoForm(s, fx.options).unavailable(mark18));
  }
  SECTION("A tube cycles through the available choices and EMPTY") {
    set_scn_text(s.player().rec.sub_class, data.tables.subs[sub_class(data, "GATO")].name);
    s.header.year = year_of(1942);
    s.header.tube_torpedoes[0] = static_cast<std::int32_t>(data.tables.weapons.torpedoes[mark10 - 1].id);
    TorpedoForm g(s, fx.options);
    CHECK(g.tube(0) == static_cast<std::int32_t>(mark10));
    CHECK(g.cycle_tube(0, true) == static_cast<std::int32_t>(mark14));
    CHECK(g.cycle_tube(0, true) == 0);  // the MARK18-1 is not in service in 1942
    CHECK(g.cycle_tube(0, true) == static_cast<std::int32_t>(mark10));
  }
}

TEST_CASE("Extended: the torpedoes come at their WEAPONS.DAT month", "[editor][loadout][extended]") {
  SH_REQUIRE_INSTALL(install);
  test::EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const EditorData& data = fx.data;
  set_scn_text(s.player().rec.sub_class, data.tables.subs[sub_class(data, "GATO")].name);
  const std::size_t mark18 = choice_of(data, "MARK18-1");
  for (const bool corrected : {false, true}) {
    s.set_corrected(corrected);
    // October 1943 (WEAPONS.DAT 10/1943): SE waits for November.
    s.header.year = year_of(1943);
    s.header.month = 9;
    CHECK(TorpedoForm(s, fx.options).unavailable(mark18) == !corrected);
    s.header.month = 8;
    CHECK(TorpedoForm(s, fx.options).unavailable(mark18));
    s.header.month = 10;
    CHECK_FALSE(TorpedoForm(s, fx.options).unavailable(mark18));
  }
}

TEST_CASE("Reload digits cycle until the racks' total fits", "[editor][loadout]") {
  SH_REQUIRE_INSTALL(install);
  test::EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const EditorData& data = fx.data;
  const SubClass& gato = data.tables.subs[sub_class(data, "GATO")];
  set_scn_text(s.player().rec.sub_class, gato.name);
  s.header.year = year_of(1944);
  s.reset_torpedo_load();
  const std::size_t mark14 = choice_of(data, "MARK14");
  const std::size_t mark18 = choice_of(data, "MARK18-1");
  TorpedoForm f(s, fx.options);
  const auto aft = static_cast<std::int32_t>(gato.stern_reloads);
  REQUIRE(aft > 0);
  REQUIRE(aft < 9);
  REQUIRE(f.reloads(true, mark14) == aft);
  CHECK(f.capacity(true) == aft);
  CHECK(f.capacity(false) == static_cast<std::int32_t>(gato.bow_reloads));

  // One more MARK14 does not fit: the units go round to 0.
  CHECK(f.cycle_reload(true, mark14, false, true) == 0);
  CHECK(f.reloads(true, mark14) == 0);
  CHECK(f.cycle_reload(true, mark14, false, true) == 1);
  // The tens never fit below 10 reloads.
  CHECK(f.cycle_reload(true, mark14, true, true) == 0);
  CHECK(f.reloads(true, mark14) == 1);
  // The MARK18-1 shares the racks.
  for (std::int32_t i = 1; i < aft; ++i) {
    f.cycle_reload(true, mark18, false, true);
  }
  CHECK(f.reloads(true, mark18) == aft - 1);
  CHECK(f.cycle_reload(true, mark18, false, true) == 0);
  CHECK_FALSE(f.over_capacity(true));

  // OK writes the counts back by choice; the tubes keep their torpedoes.
  f.commit(s);
  CHECK(s.reload_aft[mark14 - 1] == 1);
  CHECK(s.reload_aft[mark18 - 1] == 0);
  CHECK(s.header.tube_torpedoes[0] == static_cast<std::int32_t>(data.tables.weapons.torpedoes[mark14 - 1].id));
}

}  // namespace sh::editor

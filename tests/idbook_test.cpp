// Tests of the recognition manual: the page texts of SHIPS.DAT classes,
// the page order over IDBOOK.CAT, the tab rows, and the book in the
// periscope's ID panel and the captain's quarters with their hotspots.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "game/stations/attack_services.h"
#include "game/stations/cabin.h"
#include "game/stations/idbook.h"
#include "game/stations/scope.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

std::vector<std::string> strings(const std::vector<IdBookText>& texts) {
  std::vector<std::string> out;
  for (const IdBookText& t : texts) {
    out.push_back(t.text);
  }
  return out;
}

std::int32_t index_of(const GameTables& tables, std::string_view name) {
  for (std::size_t i = 0; i < tables.ships.size(); ++i) {
    if (tables.ships[i].index_name == name) {
      return static_cast<std::int32_t>(i);
    }
  }
  return -1;
}

// The periscope of the mission screen of `rig`, with the services the
// game's session gives it.
struct ScopeRig {
  explicit ScopeRig(ControlRig& rig)
      : world(rig.install, rig.mission()),
        services(rig.install, rig.mission(), rig.gui, rig.screen->env().palette, rig.screen->stations(),
                 rig.screen->attack_host(), world, rig.screen->env().assets.cursors) {
    rig.screen->install(StationId::kScope, std::make_unique<ScopeStation>(services));
  }
  ~ScopeRig() = default;

  WorldViewAdapter world;
  AttackServices services;
};

}  // namespace

TEST_CASE("The ID book pages print the SHIPS.DAT characteristics", "[idbook][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ExeImage exe = ExeImage::load(install);

  const ShipClass* yamato = tables.find_ship("YAMATO");
  REQUIRE(yamato != nullptr);
  const std::vector<IdBookText> page = id_book_page_texts(*yamato, exe, IdBookSite::kScope);
  CHECK(strings(page) == std::vector<std::string>{"BATTLESHIP", "[BB]", "YAMATO CLASS", "TONNAGE:", "LENGTH:",
                                                  "BEAM:", "DRAFT:", "SPEED:", "GUNS:", "DC'S:", "DC RACKS:",
                                                  "DC THROWERS:", "71659", "862'", "121'", "32'", "27", "9 x 18.1\"",
                                                  "NONE", "NONE", "NONE"});
  // The titles at (0x1e0, 1 / 0x12 / 0x1c), labels at 0x1e0 and values at
  // 0x227 from row 0x43 of the panel.
  CHECK(page[0].x == 0x1e0);
  CHECK(page[0].y == 1);
  CHECK(page[0].font == IdBookFont::kTitle);
  CHECK(page[1].y == 0x12);
  CHECK(page[2].y == 0x1c);
  CHECK(page[3].x == 0x1e0);
  CHECK(page[3].y == 0x43);
  CHECK(page[11].y == 0x93);
  CHECK(page[12].x == 0x227);
  CHECK(page[12].y == 0x43);

  // A destroyer's depth charges; the cabin's book layout.
  const std::vector<IdBookText> akizuki =
      id_book_page_texts(*tables.find_ship("AKIZUKI"), exe, IdBookSite::kCabin);
  CHECK(strings(akizuki) == std::vector<std::string>{"DESTROYER", "[DD]", "AKIZUKI CLASS", "TONNAGE:", "LENGTH:",
                                                     "BEAM:", "DRAFT:", "SPEED:", "GUNS:", "DC'S:", "DC RACKS:",
                                                     "DC THROWERS:", "3485", "433'", "38'", "13'", "33",
                                                     "8 x 3.9\"", "72", "2", "4"});
  CHECK(akizuki[0].x == 0x6a);
  CHECK(akizuki[0].y == 0x1a);
  CHECK(akizuki[2].y == 0x34);
  CHECK(akizuki[12].x == 0x61 + 0x19a);
  CHECK(akizuki[12].y == 0x49 + 0x42);

  // A submarine: no " CLASS", the submerged speed, the torpedoes.
  CHECK(strings(id_book_page_texts(*tables.find_ship("KAIDAI TYPE"), exe, IdBookSite::kTbt)) ==
        std::vector<std::string>{"SUBMARINE", "[SS]", "KAIDAI TYPE", "TONNAGE:", "LENGTH:", "BEAM:", "DRAFT:",
                                 "SPEED:", "GUNS:", "TORPEDOES:", "1810", "343'", "27'", "16'", "20 (8)",
                                 "1 x 4.7\"", "14"});
  // A merchant: no class line, no guns.
  CHECK(strings(id_book_page_texts(*tables.find_ship("TROOP"), exe, IdBookSite::kScope)) ==
        std::vector<std::string>{"TROOP TRANSPORT", "[MR]", "TONNAGE:", "LENGTH:", "BEAM:", "DRAFT:", "SPEED:",
                                 "GUNS:", "DC'S:", "DC RACKS:", "DC THROWERS:", "7542", "341'", "49'", "16'", "11",
                                 "NONE", "NONE", "NONE", "NONE"});
}

TEST_CASE("The ID book pages run over SHIPS.DAT but its last class", "[idbook]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  StationEnv& env = rig.screen->env();
  REQUIRE(env.tables.ships.size() == 55);
  // Every page's picture is in IDBOOK.CAT.
  const CatArchive cat = CatArchive::load(install, "ART/IDBOOK.CAT");
  for (const ShipClass& ship : env.tables.ships) {
    CHECK(cat.find(ship.idbook_picture).has_value());
  }

  IdBook book(env, IdBookSite::kScope);
  CHECK(book.page_count() == 54);
  CHECK(env.ui.id_book_page == -1);
  // A closed book opens at the first page on the panels.
  book.open();
  CHECK(book.page() == 0);
  CHECK(book.is_open());
  CHECK(book.prev_cursor() == std::nullopt);
  CHECK(book.next_cursor() == IdBook::kCursorNextPage);
  CHECK(book.tabs_cursor() == IdBook::kCursorTabs);
  book.prev_page();
  CHECK(book.page() == 0);
  for (int i = 0; i < 60; ++i) {
    book.next_page();
  }
  // The sampan (the 55th class) is left out.
  CHECK(book.page() == 53);
  CHECK(env.tables.ships[53].index_name == "CTANKER");
  CHECK(book.next_cursor() == std::nullopt);
  book.prev_page();
  CHECK(book.page() == 52);
  CHECK(book.prev_cursor() == IdBook::kCursorPrevPage);

  // The tabs: rows of 0x13 pixels from y 299 + 4 for BB, CV, CA, CL, DD,
  // DE, kaibokan and troop transport.
  book.index_click(299 + 4 + 1 * 0x13);
  CHECK(book.page() == index_of(env.tables, "KAGA"));
  book.index_click(299 + 4 + 4 * 0x13 + 0x12);
  CHECK(book.page() == index_of(env.tables, "MUTSUKI"));
  book.index_click(299 + 4 + 6 * 0x13);
  CHECK(book.page() == index_of(env.tables, "TYPE C"));
  book.index_click(454);
  CHECK(book.page() == index_of(env.tables, "TROOP"));
  book.index_click(299 + 4);
  CHECK(book.page() == index_of(env.tables, "FUSO"));
  book.index_click(299 + 3);  // above the tabs
  CHECK(book.page() == 0);
}

TEST_CASE("The periscope's ID panel turns the pages with its hotspots", "[idbook]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ScopeRig scope(rig);
  StationEnv& env = rig.screen->env();
  SimContext ctx = rig.mission().sim().context();
  PlayerSub& sub = rig.mission().player();
  REQUIRE(sub.can_raise_periscope());
  sub.toggle_periscope(ctx);
  scope.services.lower_panel = LowerPanel::kIdBook;
  rig.screen->stations().select(StationId::kScope);
  rig.frames(2);
  rig.settle();
  CHECK(env.ui.id_book_page == 0);
  // FUSO's silhouette in the scope's shades: colours 0xc0..0xd1 in the
  // picture's box at (0x8d, 300).
  int shaded = 0;
  for (int y = 300; y < 300 + 157; ++y) {
    for (int x = 0x8d; x < 0x8d + 335; ++x) {
      const std::uint8_t c = rig.screen->screen().at(x, y);
      shaded += c >= 0xc0 && c <= 0xd1 ? 1 : 0;
    }
  }
  CHECK(shaded > 2000);
  rig.save("idbook_scope_fuso");

  // The right part of the page: next page, cursor 0xf.
  rig.gui.mouse().update(400, 380, 0, 0);
  rig.frames(1);
  CHECK(rig.gui.mouse().cursor_frame() == IdBook::kCursorNextPage);
  rig.click(400, 380);
  CHECK(env.ui.id_book_page == 1);
  rig.click(400, 380);
  CHECK(env.ui.id_book_page == 2);
  // The left part: back.
  rig.click(40, 380);
  CHECK(env.ui.id_book_page == 1);
  // The tab strip: destroyers.
  rig.click(0x268, 299 + 4 + 4 * 0x13 + 5);
  CHECK(env.ui.id_book_page == index_of(env.tables, "MUTSUKI"));
  rig.frames(2);
  rig.save("idbook_scope_mutsuki");
  rig.click(400, 380);
  rig.frames(2);
  rig.save("idbook_scope_fubuki");
}

TEST_CASE("The captain's ID book opens from its cover and closes on it", "[idbook]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  StationEnv& env = rig.screen->env();
  auto owned = std::make_unique<CabinStation>(env);
  CabinStation& cabin = *owned;
  rig.screen->install(StationId::kCabin, std::move(owned));
  rig.screen->stations().select(StationId::kCabin);
  rig.frames(2);
  rig.settle();
  // The book on the desk.
  rig.click(0xb0, 0xc0);
  rig.frames(2);
  rig.settle();
  CHECK(cabin.open_page() == 1);
  CHECK(env.ui.id_book_page == -1);
  rig.save("idbook_cabin_cover");
  // The cover's right part opens the book (cursor 0x18), at FUSO.
  rig.gui.mouse().update(300, 200, 0, 0);
  rig.frames(1);
  CHECK(rig.gui.mouse().cursor_frame() == IdBook::kCursorOpenBook);
  rig.click(300, 200);
  CHECK(env.ui.id_book_page == 0);
  rig.frames(2);
  rig.save("idbook_cabin_fuso");
  // The tabs: heavy cruisers.
  rig.click(0x236, 0x75 + 0x17 + 2 * 0x1a + 3);
  CHECK(env.ui.id_book_page == index_of(env.tables, "FURUTAKA"));
  rig.frames(2);
  rig.save("idbook_cabin_furutaka");
  rig.click(300, 200);
  CHECK(env.ui.id_book_page == index_of(env.tables, "AOBA"));
  // Back to the first page, then onto the cover.
  rig.click(0x236, 0x75 + 0x17);
  CHECK(env.ui.id_book_page == 0);
  rig.click(40, 200);
  CHECK(env.ui.id_book_page == -1);
  rig.gui.mouse().update(41, 200, 0, 0);
  rig.frames(1);
  CHECK(rig.gui.mouse().cursor_frame() == static_cast<std::size_t>(ui::CursorId::kPointer));
  // Above the book: back to the cabin; the page is kept for the scope.
  env.ui.id_book_page = 5;
  rig.click(300, 50);
  rig.frames(2);
  CHECK(cabin.open_page() == 0);
  CHECK(env.ui.id_book_page == 5);
}

TEST_CASE("With the rules' corrections a tab clicked on the cover opens the book", "[idbook][rules]") {
  SH_REQUIRE_INSTALL(install);
  for (const bool fixed : {false, true}) {
    ControlRig rig(install);
    StationEnv& env = rig.screen->env();
    rig.mission().sim().conditions().rules = fixed ? MissionRules::extended() : MissionRules{};
    auto owned = std::make_unique<CabinStation>(env);
    rig.screen->install(StationId::kCabin, std::move(owned));
    rig.screen->stations().select(StationId::kCabin);
    rig.frames(2);
    rig.settle();
    rig.click(0xb0, 0xc0);
    rig.frames(2);
    rig.settle();
    REQUIRE(env.ui.id_book_page == -1);
    // A tab on the cover: the heavy cruisers' page.
    rig.click(0x236, 0x75 + 0x17 + 2 * 0x1a + 3);
    CHECK(env.ui.id_book_page == index_of(env.tables, "FURUTAKA"));
    // The page's right part: the next page's cursor when the book is open,
    // the cover's open-book cursor for the original.
    rig.gui.mouse().update(300, 200, 0, 0);
    rig.frames(1);
    CHECK(rig.gui.mouse().cursor_frame() == (fixed ? IdBook::kCursorNextPage : IdBook::kCursorOpenBook));
  }
}

}  // namespace sh

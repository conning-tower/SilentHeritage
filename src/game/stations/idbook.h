// The recognition manual (module idbook, 0x4dffc-0x4f078): one page per
// SHIPS.DAT class with its picture from ART/IDBOOK.CAT (the class's third
// sprite name) and its type, class and characteristics, as the periscope
// and the TBT show it in their lower panel (ID mode, SCOPEID.PCX /
// TBTID.SHP) and as the captain's quarters show it as a book (ID-1.PCX
// cover, ID-2.PCX open pages). The pages turn with hotspots: the right part
// of the page (next, cursor 0xf, 0x18 on the closed book), the left part
// (previous, 0xe) and the tab strip (the first class of the type of the
// tab under the mouse, cursor 0x13). The manual is a reference only: the
// original feeds nothing to the TDC or the contacts from it.
//
// The page shown (0x1094dc) is `MissionUiState::id_book_page`, shared by
// the three stations. The original composes a page over the picture it
// keeps in the screen bitmap and shows it on the VGA; here the book keeps
// that picture (the screen region at opening, or the cabin's PCX) and
// draws the page onto the screen.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/cat.h"
#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/shp.h"
#include "core/image.h"
#include "game/station_env.h"
#include "game/stations/attack_services.h"
#include "gfx/text.h"
#include "ui/dialog.h"

namespace sh {

// Where the manual shows.
enum class IdBookSite : std::uint8_t { kScope, kTbt, kCabin };

// The three fonts of a page (ID_1.FNT, ID_2.FNT, ID_3.FNT).
enum class IdBookFont : std::uint8_t { kTitle = 0, kSubtitle = 1, kText = 2 };

// One text of a page, at book coordinates.
struct IdBookText {
  int x = 0;
  int y = 0;
  IdBookFont font = IdBookFont::kText;
  std::string text;
};

// The layout of a page in the book bitmap (`idbook_draw_ship_page`): the
// picture's position; the type texts are at `title_x`, `title_y`...
struct IdBookPageOrigin {
  int picture_x = 0;
  int picture_y = 0;
  int title_x = 0;
  int title_y = 0;
};

// Returns the page origin of `site`: the cabin's book (0x61, 0x49) with
// the titles at (0x6a, 0x1a), else the panel's (0x8d, 1) with the titles
// at (0x1e0, 1).
IdBookPageOrigin id_book_page_origin(IdBookSite site);

// Returns the texts of the page of `ship` in drawing order
// (`idbook_draw_ship_page`, 0x4e565): the upper-cased type name and
// "[abbreviation]" (table 0x10ea00 of `exe`), the class name with " CLASS"
// (not for submarines; none for merchants and sampans), then the labels
// and values: TONNAGE, LENGTH, BEAM, DRAFT (feet with "'"), SPEED (the
// submerged speed in brackets for submarines), GUNS ("n x cal\"" or
// NONE), then DC'S, DC RACKS, DC THROWERS, or TORPEDOES for submarines
// (NONE for 0).
std::vector<IdBookText> id_book_page_texts(const ShipClass& ship, const ExeImage& exe, IdBookSite site);

// The manual of one station: its fonts, IDBOOK.CAT and the page state.
class IdBook {
 public:
  // The cursors of the hotspots (CURSORS.SHP frames).
  static constexpr std::size_t kCursorPrevPage = 0xe;
  static constexpr std::size_t kCursorNextPage = 0xf;
  static constexpr std::size_t kCursorTabs = 0x13;
  static constexpr std::size_t kCursorOpenBook = 0x18;

  // Loads the fonts and IDBOOK.CAT (`idbook_open`, 0x4eeea), and
  // IDBOOK.SHP for the cabin; `env` must outlive the book. The pages are
  // the SHIPS.DAT classes but the last (the sampan): the original takes the
  // index count minus one. Throws `DataError` when a file is missing.
  IdBook(StationEnv& env, IdBookSite site);

  // Shows the book. On the scope and the TBT (`idbook_show`, 0x4dffc) it
  // keeps the screen under the book as its background, opens a closed book
  // at the first page and draws the page. In the cabin (`idbook_enter`,
  // 0x4df24) it draws ID-1.PCX (closed) or ID-2.PCX and the page, setting
  // the screen palette.
  void open();
  // Turns to the next page that has a picture (`idbook_next_page`,
  // 0x4e13a); a closed book opens at the first page.
  void next_page();
  // Turns back (`idbook_prev_page`, 0x4e1d5); in the cabin the first page
  // closes the book onto its cover.
  void prev_page();
  // Opens the page of the first class of the type of the tab row at screen
  // row `mouse_y` (`idbook_index_click`, 0x4e338): rows of `tab_step`
  // pixels from the book top + `tab_offset`, the types of table 0x1094e0
  // (BB, CV, CA, CL, DD, DE, kaibokan, troop transport). Rows above do
  // nothing; rows below are the last tab.
  void index_click(int mouse_y);
  // Draws the current page again (`idbook_redraw_page`, 0x4e304).
  void redraw();

  // The hover cursors of the next, previous and tab hotspots, nullopt for
  // the default cursor (`idbook_hover_next` / `_prev` / `_index`).
  std::optional<std::size_t> next_cursor() const;
  std::optional<std::size_t> prev_cursor() const;
  std::optional<std::size_t> tabs_cursor() const;

  // Adds the next, previous and tab hotspots of the site's dialog (scope
  // 0x126bf8, TBT 0x128bdc, cabin 0x1197d0) to `dialog`: a press turns the
  // page with the simulation held and asks for a full redraw.
  void add_hotspots(ui::Dialog& dialog);

  // The page (a SHIPS.DAT index; -1 closed) and the page count.
  std::int32_t page() const { return env_.ui.id_book_page; }
  std::int32_t page_count() const { return count_; }
  // Whether the book is open (0x122200).
  bool is_open() const { return open_; }
  // The book's rectangle on the screen: origin and size (the bitmap is one
  // pixel larger each way).
  int x() const { return x_; }
  int y() const { return y_; }

 private:
  // Draws page `page()` onto the screen; returns false (nothing drawn)
  // when the class has no picture.
  bool draw_page();
  // Loads ID-1.PCX or ID-2.PCX above the bar and keeps the book region as
  // the background.
  void load_picture(const char* name);
  // Reloads ID-2.PCX in the cabin after the cover (0x122230).
  void reload_pages_picture();
  // Runs a page action as the hotspot thunks do.
  void turn(void (IdBook::*action)());

  StationEnv& env_;
  IdBookSite site_;
  std::span<const ShipClass> ships_;
  std::int32_t count_ = 0;  // 0x1221f8
  CatArchive cat_;
  Font fonts_[3];
  std::optional<Shp> stamps_;  // IDBOOK.SHP (cabin): the warship / merchant tabs
  GlyphColourTable colours_{};
  int x_ = 0, y_ = 0, w_ = 0, h_ = 0;  // 0x122208..0x122214
  int tab_offset_ = 0;                 // 0x12221c
  int tab_step_ = 0;                   // 0x122220
  bool has_cover_ = false;             // 0x122204
  bool open_ = false;                  // 0x122200
  bool reload_pages_ = true;           // 0x122230
  Image8 background_;
};

// Returns the recognition manual panel of the scope's or the TBT's ID mode
// (`idbook_show(0, 299, 0x260, 0xa0)` and the dialog of the hotspots), for
// `StationEnvHost::id_books`. The bridge has none (nullptr).
std::unique_ptr<StationPanel> make_id_book_panel(StationEnv& env, PanelSite site);

}  // namespace sh

#include "editor/dialogs_select.h"

#include <functional>
#include <string>
#include <vector>

#include "assets/cat.h"
#include "assets/shp.h"
#include "core/install.h"
#include "editor/class_sheets.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

constexpr unsigned kDialogFlags = ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel;
constexpr int kButtonWidth = 100;
// The colours above the greyed ones, which the picture dialogs set.
constexpr int kLastGreyed = 0xe1;
constexpr int kPictureFirstColour = 0xe1;
// The list metrics of the picture dialogs (0x9bc62 / 0x9bc64 / 0x9bc66).
constexpr int kListRows = 6;
constexpr int kListHeight = 100;
constexpr int kListTrack = 0x2b;

// SE file names.
constexpr std::uint32_t kSubPics = 0x9bc78;  // "SUBPICS.SHP"
constexpr std::uint32_t kAirPics = 0x9bc84;  // "AIRPICS.SHP"
constexpr std::uint32_t kArtDir = 0x983b4;   // "ART\"
constexpr std::uint32_t kIdBook = 0x90d56;   // "idbook.cat"

// Returns the DOS path of `name` in ART\ (as `load_file_or_die` finds it).
std::string art_path(const EditorUi& ui, std::uint32_t name) {
  std::string path = ui.str(kArtDir) + ui.str(name);
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  return path;
}

// Draws the texts of a class sheet in FONT1 through the GUI colours.
void draw_texts(EditorUi& ui, const Viewport& gfx, const std::vector<SheetText>& texts) {
  for (const SheetText& t : texts) {
    gui_draw_text(gfx, t.x, t.y, ui.font1(), t.text, &ui.resources().table);
  }
}

// Fills the w x h rectangle at (x, y) with the dialog colour (0xa642c).
void clear(EditorUi& ui, const Viewport& gfx, int x, int y, int w, int h) {
  ui::gui_fill_rect(gfx, x, y, w, h, ui.resources().colours.b[1]);
}

// The bevel around the picture box from (0x10, top) to (0x15e, bottom): the
// top and left edges in B2, the bottom and right ones in B3.
void picture_frame(EditorUi& ui, const Viewport& gfx, int top, int bottom) {
  const ui::GuiColours& c = ui.resources().colours;
  ui::draw_line(gfx, 0x10, top, 0x15e, top, c.b[2]);
  ui::draw_line(gfx, 0x10, bottom + 1, 0x15e, bottom + 1, c.b[3]);
  ui::draw_line(gfx, 0x10, top + 1, 0x10, bottom, c.b[2]);
  ui::draw_line(gfx, 0x15e, top + 1, 0x15e, bottom, c.b[3]);
}

// The list of a picture dialog; its first call of the idle callback
// selects the current row (+0x44 selection, +0x40 top, redrawn).
ui::List& add_list(ui::Dialog& d, int x, int y, int w, const ClassList& list) {
  auto& l = d.emplace<ui::List>(
      x, y, w, [&list] { return static_cast<int>(list.rows.size()); },
      [&list](int row) { return list.rows[static_cast<std::size_t>(row)]; });
  // The on_activate callback `dialog_return_accept`'s result is dropped
  // by `gui_list_activate`: a double click only selects.
  return l;
}

void select_current(ui::List& list, std::int32_t current) {
  list.dirty = true;
  list.selection = current;
  list.top = current;
}

// `remap` 0xa6c14 of `select_sub_type`: SUBPICS drawn in the colours of
// SBCHART frame 31.
ColourRemap subpics_remap() {
  ColourRemap remap{};
  for (std::size_t i = 0; i < 0xff; ++i) {
    remap[i] = static_cast<std::uint8_t>(i);
  }
  remap[177] = 0xf9;
  constexpr std::array<std::array<std::uint8_t, 3>, 9> kRanges{
      {{192, 194, 0xf0}, {195, 196, 0xf1}, {197, 198, 0xf2}, {199, 200, 0xf3}, {201, 202, 0xf4},
       {203, 204, 0xf5}, {205, 207, 0xf6}, {208, 210, 0xf7}, {211, 213, 0xf8}}};
  for (const auto& [first, last, colour] : kRanges) {
    for (std::size_t i = first; i <= last; ++i) {
      remap[i] = colour;
    }
  }
  for (std::size_t i = 0x50; i < 0x60; ++i) {
    remap[i] = 0xfd;
  }
  for (std::size_t i = 0x60; i < 0x70; ++i) {
    remap[i] = 0xfe;
  }
  return remap;
}

// `select_sub_name_run` (SE 0x2655f).
int select_sub_name_run(EditorUi& ui, const EditorData& data, std::int32_t sub_class, ScnChars<31>& name) {
  const ClassList list = sub_name_list(data, static_cast<std::size_t>(sub_class), scn_text(name));
  ui::Dialog d(ui.gui(), 154, 100, 332, 262, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(0x90893), ui::return_cancel);
  ui::List& rows = add_list(d, 14, 54, 278, list);
  d.emplace<ui::Button>(16, 225, kButtonWidth, ui.str(0x908a3), ui::return_cancel);
  d.emplace<ui::Button>(217, 225, kButtonWidth, ui.str(0x908aa), ui::return_accept);
  // sub_name_idle (0x265c8)
  d.set_idle([&rows, &list](const Viewport&, int first) {
    if (first != 0) {
      select_current(rows, list.current);
    }
  });
  const int result = d.run();
  if (result != ui::kResultCancel && rows.selection >= 0) {
    set_scn_text(name, list.rows[static_cast<std::size_t>(rows.selection)]);
  }
  return result;
}

}  // namespace

int select_sub_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options, std::int32_t& sub_class,
                    ScnChars<31>& name) {
  const EditorData& data = scenario.data();
  ui.palette_grey_out(0x32, kLastGreyed);
  ui.apply_frame_colours(ui.sbchart().frame(sbchart_frame::kSubPicturePalette), kPictureFirstColour, 254);
  const ColourRemap remap = subpics_remap();
  std::int32_t result = 0;
  std::int32_t selection = 0;
  std::vector<std::size_t> entries;
  {
    const ListMetricsScope metrics(ui.skin(), kListRows, kListHeight, kListTrack);
    const Shp pictures = Shp::load(ui.install(), art_path(ui, kSubPics));
    const ClassList list = sub_type_list(data, options, scenario.header.month, scenario.header.year,
                                         static_cast<std::size_t>(sub_class));
    entries = list.entries;
    ui::Dialog d(ui.gui(), 137, 47, 366, 356, kDialogFlags);
    // sub_type_sheet_draw (0x25cef): the sheet of list row `row`.
    const auto sheet = [&](const Viewport& gfx, std::int32_t row) {
      if (list.entries.empty()) {
        return;
      }
      const std::size_t cls = list.entries[static_cast<std::size_t>(row)];
      clear(ui, gfx, 0x10, 0x2f, 0x14d, 0xa);
      clear(ui, gfx, 0x10, 0x8e, 0x14d, 0x4a);
      const auto frame = static_cast<std::size_t>(data.tables.subs[cls].picture_frame);
      draw_sprite_remapped(gfx, pictures.frame(frame), 0x11, 0x3b, remap);
      picture_frame(ui, gfx, 0x3a, 0x86);
      draw_texts(ui, gfx, sub_sheet(data, cls, ui.font1()));
      d.add_dirty({0x10, 4, 0x163, 0xcc});
    };
    // The owner draw shows the first row's sheet; the idle callback then
    // shows the current one.
    d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) { sheet(gfx, 0); });
    d.emplace<ui::TitleBar>(ui.str(0x90875), ui::return_cancel);
    ui::List& rows = add_list(d, 15, 215, 309, list);
    rows.on_select = [&](int row) { sheet(d.gfx(), row); };
    d.emplace<ui::GroupBox>(12, 43, 354, 206);
    d.emplace<ui::Button>(16, 320, kButtonWidth, ui.str(0x90885), ui::return_cancel);
    d.emplace<ui::Button>(252, 320, kButtonWidth, ui.str(0x9088c), ui::return_accept);
    // sub_type_idle (0x25cf7)
    d.set_idle([&](const Viewport& gfx, int first) {
      if (first != 0) {
        select_current(rows, list.current);
        sheet(gfx, list.current);
      }
    });
    result = d.run();
    selection = rows.selection;
  }
  // Original bug (docs/editor-design.md "Sub class by list position", fixed): SE
  // stores the list position as the SUBS.DAT class, even on CANCEL, and
  // chains on a changed position; with "Limit by date: Subs" hiding
  // earlier classes (April 1945 on with the shipped SUBS.DAT) the sub got
  // another class and SELECT SUB NAME that class's boats. The row is
  // mapped to its class here, and CANCEL keeps the class.
  if (result != ui::kResultCancel && selection >= 0 && static_cast<std::size_t>(selection) < entries.size()) {
    const auto chosen = static_cast<std::int32_t>(entries[static_cast<std::size_t>(selection)]);
    if (chosen != sub_class) {
      sub_class = chosen;
      result = select_sub_name_run(ui, data, sub_class, name);
    }
  }
  ui.palette_restore_saved(kLastGreyed);
  return result;
}

int select_sub_name(EditorUi& ui, const EditorData& data, std::int32_t sub_class, ScnChars<31>& name) {
  ui.palette_grey_out(0x32, kLastGreyed);
  const int result = select_sub_name_run(ui, data, sub_class, name);
  ui.palette_restore_saved(kLastGreyed);
  return result;
}

void select_ship_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options, std::int32_t& ship_class) {
  const EditorData& data = scenario.data();
  ui.palette_grey_out(0x32, kLastGreyed);
  ui.apply_frame_colours(ui.sbchart().frame(sbchart_frame::kShipPicturePalette), kPictureFirstColour, 248);
  {
    const ListMetricsScope metrics(ui.skin(), kListRows, kListHeight, kListTrack);
    const ClassList list =
        ship_type_list(data, options, ui.resources().small, scenario.header.month, scenario.header.year, ship_class);
    // The original opens ART\idbook.cat for each sheet (a missing file ends
    // the program); it is read once here.
    const CatArchive idbook = CatArchive::load(ui.install(), art_path(ui, kIdBook));
    ship_class = -1;
    ui::Dialog d(ui.gui(), 137, 10, 366, 437, kDialogFlags);
    // ship_type_sheet_draw (0x26a1a)
    const auto sheet = [&](const Viewport& gfx, std::int32_t row) {
      if (list.entries.empty()) {
        return;
      }
      const std::size_t cls = list.entries[static_cast<std::size_t>(row)];
      const ShipClass& ship = data.tables.ships[cls];
      clear(ui, gfx, 0x10, 0x2f, 0x14f, 0xb);
      clear(ui, gfx, 0x10, 0xdc, 0x14f, 0x4a);
      if (!ship.idbook_picture.empty()) {
        const SpriteFrame& background = ui.sbchart().frame(sbchart_frame::kShipPictureBackground);
        draw_sprite_clipped(gfx, background, 0x10, 0x3b);
        draw_sprite_clipped(gfx, background, 0x10, 0x8a);
        const Shp picture = Shp::parse(idbook.member(ship.idbook_picture), ship.idbook_picture);
        draw_sprite_clipped(gfx, picture.frame(0), 0x10, 0x3b);
      }
      draw_texts(ui, gfx, ship_sheet(data, cls, ui.font1()));
      d.add_dirty({0x10, 4, 0x163, 0x11c});
    };
    d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) { sheet(gfx, 0); });
    // The title is set at run time (0xa8fa0).
    d.emplace<ui::TitleBar>(ui.str(0x90ce5), ui::return_cancel);
    ui::List& rows = add_list(d, 15, 292, 309, list);
    rows.on_select = [&](int row) { sheet(d.gfx(), row); };
    d.emplace<ui::GroupBox>(12, 43, 354, 286);
    d.emplace<ui::Button>(16, 400, kButtonWidth, ui.str(0x908b2), ui::return_cancel);
    d.emplace<ui::Button>(252, 400, kButtonWidth, ui.str(0x908b9), ui::return_accept);
    // ship_type_idle (0x26a22)
    d.set_idle([&](const Viewport& gfx, int first) {
      if (first != 0) {
        select_current(rows, list.current);
        sheet(gfx, list.current);
      }
    });
    if (d.run() != ui::kResultCancel && rows.selection >= 0) {
      ship_class = static_cast<std::int32_t>(list.entries[static_cast<std::size_t>(rows.selection)]);
    }
  }
  ui.palette_restore_saved(kLastGreyed);
}

int select_aircraft_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options,
                         std::int32_t& aircraft) {
  const EditorData& data = scenario.data();
  ui.palette_grey_out(0x32, kLastGreyed);
  ui.apply_frame_colours(ui.sbchart().frame(sbchart_frame::kAircraftPicturePalette), kPictureFirstColour, 255);
  int result = 0;
  {
    const Shp pictures = Shp::load(ui.install(), art_path(ui, kAirPics));
    const ListMetricsScope metrics(ui.skin(), kListRows, kListHeight, kListTrack);
    const ClassList list = aircraft_type_list(data, options, scenario.header.month, scenario.header.year, aircraft);
    ui::Dialog d(ui.gui(), 137, 47, 366, 376, kDialogFlags);
    // aircraft_type_sheet_draw (0x275db)
    const auto sheet = [&](const Viewport& gfx, std::int32_t row) {
      if (list.entries.empty()) {
        return;
      }
      const std::size_t type = list.entries[static_cast<std::size_t>(row)];
      const AircraftType& a = data.tables.aircraft[type];
      clear(ui, gfx, 0x10, 0x2f, 0x14d, 0xa);
      clear(ui, gfx, 0x10, 0x8e, 0x14d, 0x50);
      draw_sprite_clipped(gfx, ui.sbchart().frame(sbchart_frame::kAircraftPictureBackground), 0x10, 0x3a);
      draw_sprite_clipped(gfx, pictures.frame(a.aircraft_id), 0x11, 0x3b);
      picture_frame(ui, gfx, 0x3a, 0x98);
      draw_texts(ui, gfx, aircraft_sheet(data, type, ui.font1()));
      d.add_dirty({0x10, 4, 0x163, 0xec});
    };
    d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) { sheet(gfx, 0); });
    d.emplace<ui::TitleBar>(ui.str(0x908c0), ui::return_cancel);
    ui::List& rows = add_list(d, 15, 235, 309, list);
    rows.on_select = [&](int row) { sheet(d.gfx(), row); };
    d.emplace<ui::GroupBox>(12, 43, 354, 226);
    d.emplace<ui::Button>(16, 340, kButtonWidth, ui.str(0x908d5), ui::return_cancel);
    d.emplace<ui::Button>(252, 340, kButtonWidth, ui.str(0x908dc), ui::return_accept);
    // aircraft_type_idle (0x275e3)
    d.set_idle([&](const Viewport& gfx, int first) {
      if (first != 0) {
        select_current(rows, list.current);
        sheet(gfx, list.current);
      }
    });
    result = d.run();
    // Set even on CANCEL, as the original.
    if (rows.selection >= 0) {
      aircraft = static_cast<std::int32_t>(list.entries[static_cast<std::size_t>(rows.selection)]);
    }
  }
  ui.palette_restore_saved(kLastGreyed);
  return result;
}

}  // namespace sh::editor

#include "ui/skin.h"

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "core/install.h"

namespace sh::ui {

GlyphColourTable GuiResources::identity_table() {
  GlyphColourTable t{};
  for (std::size_t i = 0; i < t.size(); ++i) {
    t[i] = static_cast<std::uint8_t>(i);
  }
  return t;
}

SkinMetrics SkinMetrics::from_game_exe() {
  const ExeImage& exe = game_exe();
  const auto get = [&](std::uint32_t address) { return static_cast<int>(static_cast<std::int16_t>(exe.u16(address))); };
  SkinMetrics m;
  m.menu_bar_width = get(0x108a24);
  m.menu_bar_height = get(0x108a26);
  m.popup_width = get(0x108a2a);
  m.border = get(0x108a2c);
  m.close_box_x = get(0x108a2e);
  m.close_box_y = get(0x108a30);
  m.close_box_width = get(0x108a32);
  m.close_box_height = get(0x108a34);
  m.button_height = get(0x108a36);
  m.check_box_height = get(0x108a38);
  m.check_box_label_width = get(0x108a3a);
  m.radio_height = get(0x108a3c);
  m.cycle_height = get(0x108a3e);
  m.entry_height = get(0x108a40);
  m.textbox_origin_x = get(0x108a6c);
  m.textbox_origin_y = get(0x108a6e);
  m.list_row_height = get(0x108a74);
  m.list_rows = get(0x108a76);
  m.list_height = get(0x108a78);
  m.list_track = get(0x108a7a);
  m.list_text_y = get(0x108a7c);
  m.dial_width = get(0x108a84);
  m.dial_height = get(0x108a86);
  m.grid_cell_height = get(0x108a88);
  check_exe_table(m.menu_bar_width == 640 && m.list_rows > 0 && m.list_row_height > 0, "skin metrics", 0x108a24);
  return m;
}

GuiResources GuiResources::load(const Install& install) {
  // load_gui_fonts (0x10cf3) -> gui_init (0x3afc0)
  return GuiResources{Font::load(install, "ART/GUI_S.FNT"), Font::load(install, "ART/GUI_L.FNT"),
                      Font::load(install, "ART/FONT1.FNT"), Font::load(install, "ART/GUI_1.FNT"),
                      Shp::load(install, "ART/GUISPR.SHP"), GuiColours{}, gui_colour_table(), identity_table()};
}

}  // namespace sh::ui

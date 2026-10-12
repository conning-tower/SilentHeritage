// The career's letter and patrol screens: the dialogs of
// `patrol_init_dialogs` (0x62420), `homeport_init_dialogs` (0x4cd24),
// `new_equipment_init_dialogs` (0x5b140), `awards_init_dialogs` (0x55cb8)
// and `subscrn_init_dialogs` (0x70f18), and the full-screen pictures.

#include <algorithm>
#include <cctype>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/shp.h"
#include "core/install.h"
#include "game/career_letters.h"
#include "game/career_ui.h"
#include "game/equipment_art.h"
#include "game/frontend_subsel.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

namespace addr {
constexpr std::uint32_t kOrdersPcx = 0x10cc80;    // "M8.PCX"
constexpr std::uint32_t kOrdersFlags = 0x10cc90;  // "HFLAGS.SHP"
constexpr std::uint32_t kResultsPcx = 0x10cc88;   // "M9.PCX"
constexpr std::uint32_t kHomePcx = 0x109498;      // "M8.PCX"
constexpr std::uint32_t kHomeFlags = 0x1094a0;    // "HFLAGS.SHP"
constexpr std::uint32_t kEquipPcx = 0x10ca44;     // "M8.PCX"
constexpr std::uint32_t kEquipShp = 0x10ca4c;     // "EQUIP.SHP"
constexpr std::uint32_t kAwardsShp = 0x10c358;    // "awards.shp"
constexpr std::uint32_t kTypewriter = 0x10c364;   // "TYPEX.FNT"
constexpr std::uint32_t kMedalPcx = 0x10c330;     // "mdlaward.pcx"
constexpr std::uint32_t kPresidentPcx = 0x10c340;  // "prescit.pcx"
constexpr std::uint32_t kNavyPcx = 0x10c34c;      // "navycit.pcx"
constexpr std::uint32_t kCalendarPcx = 0x103308;  // "CCALENDR.PCX"
constexpr std::uint32_t kCalendarFonts = 0x103318;  // "CALEND1.FNT", +0xc, +0x18
constexpr std::uint32_t kPaperPcx = 0x103344;     // "PAPER-1.PCX"
constexpr std::uint32_t kSubPcx = 0x10e200;       // "M8.PCX"
constexpr std::uint32_t kSubPictures = 0x10e208;  // "SUBPICS.SHP"
constexpr std::uint32_t kMonths = 0x10e910;
constexpr std::uint32_t kBaseYear = 0x10e3a4;
}  // namespace addr

constexpr ui::DialogFlag kMenuFlags = static_cast<ui::DialogFlag>(ui::kDialogTitled | ui::kDialogFramed);

// The text box hooks of the citations (`awards_load_typewriter_font`
// 0x560e4): nothing drawn for the frame and background, the text in
// TYPEX.FNT with GUI glyph 0x3f drawn as 0x3d (`awards_draw_typewriter_text`),
// no caret and no text origin offset. Every other hook is the menu skin's.
class TypewriterSkin : public ui::Skin {
 public:
  TypewriterSkin(ui::Skin& base, const Font& font) : base_(base), font_(font), table_(gui_colour_table()) {
    table_[0x3f] = 0x3d;
  }

  const ui::SkinMetrics& metrics() const override { return base_.metrics(); }
  const Font& font(ui::FontSlot slot, const Font* custom) const override { return base_.font(slot, custom); }
  ui::Rect dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) override {
    return base_.dialog_frame(gfx, x, y, w, h, titled);
  }
  ui::Rect label(const Viewport& gfx, int x, int y, ui::FontSlot f, std::string_view text, bool use_colours,
                 const Font* custom) override {
    return base_.label(gfx, x, y, f, text, use_colours, custom);
  }
  ui::Rect group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) override {
    return base_.group_box(gfx, x0, y0, x1, y1, caption);
  }
  ui::Rect line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) override {
    return base_.line(gfx, x0, y0, x1, y1, colour);
  }
  ui::Rect title_bar(const Viewport& gfx, int w, int h, std::string_view text, bool has_close_box) override {
    return base_.title_bar(gfx, w, h, text, has_close_box);
  }
  ui::Rect close_box(const Viewport& gfx, int x, int y, bool pressed) override {
    return base_.close_box(gfx, x, y, pressed);
  }
  ui::Rect text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, ui::FontSlot f, std::string_view text,
                      int fill, TextAlign align, bool use_colours, const Font* custom) override {
    return base_.text_field(gfx, x0, y0, x1, y1, f, text, fill, align, use_colours, custom);
  }
  ui::Rect button(const Viewport& gfx, int x, int y, int w, std::string_view text, bool pressed, bool highlighted,
                  bool focused, bool disabled) override {
    return base_.button(gfx, x, y, w, text, pressed, highlighted, focused, disabled);
  }
  ui::Rect check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused, bool disabled,
                     std::string_view text) override {
    return base_.check_box(gfx, x, y, pressed, checked, focused, disabled, text);
  }
  int check_box_width(std::string_view text) override { return base_.check_box_width(text); }
  ui::Rect radio_button(const Viewport& gfx, int x, int y, bool pressed, bool selected, bool focused, bool disabled,
                        std::string_view text) override {
    return base_.radio_button(gfx, x, y, pressed, selected, focused, disabled, text);
  }
  int radio_button_width(std::string_view text) override { return base_.radio_button_width(text); }
  ui::Rect cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                        std::string_view text) override {
    return base_.cycle_button(gfx, x, y, w, pressed, focused, disabled, text);
  }
  ui::Rect entry_box(const Viewport& gfx, int x, int y, int w) override { return base_.entry_box(gfx, x, y, w); }
  ui::Rect entry_text(const Viewport& gfx, int x, int y, int w, std::string_view text, bool read_only) override {
    return base_.entry_text(gfx, x, y, w, text, read_only);
  }
  void entry_caret(ui::Dialog& dialog, int x, int y, std::string_view text, int caret, bool on) override {
    base_.entry_caret(dialog, x, y, text, caret, on);
  }
  int entry_char_index_at(int click_x, int x, std::string_view text) override {
    return base_.entry_char_index_at(click_x, x, text);
  }
  ui::Rect spin_entry_box(const Viewport& gfx, int x, int y, int w) override {
    return base_.spin_entry_box(gfx, x, y, w);
  }
  ui::Rect spin_up_rect(int x, int y, int w) override { return base_.spin_up_rect(x, y, w); }
  ui::Rect spin_down_rect(int x, int y, int w) override { return base_.spin_down_rect(x, y, w); }
  ui::Rect spin_arrows(const Viewport& gfx, int x, int y, int w, int pressed, bool read_only) override {
    return base_.spin_arrows(gfx, x, y, w, pressed, read_only);
  }
  // 0x5602a: the rectangle of the frame only.
  ui::Rect textbox_frame(const Viewport&, int x, int y, int w, int h) override {
    return {x - 4, y - 4, x + w + 0x1b, y + h + 0x16};
  }
  // 0x56065
  ui::Rect textbox_background(const Viewport&, int x, int y, int w, int h) override {
    return {x, y, x + w + 0x13, y + h + 0xe};
  }
  // awards_draw_typewriter_text (0x5608f)
  void textbox_text(const Viewport& gfx, int x, int y, std::string_view text, bool) override {
    gui_draw_text(gfx, x, y, font_, text, &table_);
  }
  // 0x560c4
  int textbox_char_width(std::uint8_t ch) override { return font_.glyph_width(ch); }
  void textbox_caret(ui::Dialog&, int, int, std::uint8_t, bool) override {}
  void textbox_text_origin(int&, int&) override {}
  ui::Rect list_frame(const Viewport& gfx, int x, int y, int w) override { return base_.list_frame(gfx, x, y, w); }
  ui::Rect list_up_rect(int x, int y, int w) override { return base_.list_up_rect(x, y, w); }
  ui::Rect list_down_rect(int x, int y, int w) override { return base_.list_down_rect(x, y, w); }
  ui::Rect list_track_rect(int x, int y, int w) override { return base_.list_track_rect(x, y, w); }
  ui::Rect list_item(const Viewport& gfx, int x, int y, int w, std::string_view text, bool selected) override {
    return base_.list_item(gfx, x, y, w, text, selected);
  }
  ui::Rect list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) override {
    return base_.list_scrollbar(gfx, x, y, w, top, count);
  }
  ui::Rect list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) override {
    return base_.list_arrow(gfx, x, y, w, down, pressed);
  }
  ui::Rect list_focus(const Viewport& gfx, int x, int y, int w, bool focused) override {
    return base_.list_focus(gfx, x, y, w, focused);
  }
  ui::Rect slider_frame(const Viewport& gfx, int x, int y, int w, bool disabled) override {
    return base_.slider_frame(gfx, x, y, w, disabled);
  }
  ui::Rect slider_left_rect(int x, int y, int w) override { return base_.slider_left_rect(x, y, w); }
  ui::Rect slider_right_rect(int x, int y, int w) override { return base_.slider_right_rect(x, y, w); }
  ui::Rect slider_track_rect(int x, int y, int w) override { return base_.slider_track_rect(x, y, w); }
  ui::Rect slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool disabled) override {
    return base_.slider_track(gfx, x, y, w, permille, disabled);
  }
  ui::Rect slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool disabled) override {
    return base_.slider_arrow(gfx, x, y, w, right, pressed, disabled);
  }
  ui::Rect slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) override {
    return base_.slider_focus(gfx, x, y, w, focused);
  }
  ui::Rect dial(const Viewport& gfx, const ui::Dial& d, std::string_view entry_text) override {
    return base_.dial(gfx, d, entry_text);
  }
  ui::Rect grid_cell(const Viewport& gfx, const ui::GridCell& cell, bool highlighted) override {
    return base_.grid_cell(gfx, cell, highlighted);
  }

 private:
  ui::Skin& base_;
  const Font& font_;
  GlyphColourTable table_;
};

// The arrows of a read-only text box, greyed when it cannot scroll that
// way (the `*_dialog_draw` callbacks probe a scroll each way and undo it).
struct TextScroll {
  ui::TextBox& text;
  ui::Button& up;
  ui::Button& down;
  bool refresh = true;

  void wire(ui::Dialog& d) {
    for (auto [button, scroll_up] : {std::pair{&up, true}, std::pair{&down, false}}) {
      button->on_click = [this, &d, scroll_up](int) {
        if (scroll_up) {
          text.scroll_up(d);
        } else {
          text.scroll_down(d);
        }
        text.dirty = true;
        refresh = true;
        return 0;
      };
    }
  }
  void update(ui::Dialog& d) {
    if (text.dirty || !refresh) {
      return;
    }
    const bool can_up = text.scroll_up(d);
    if (can_up) {
      text.scroll_down(d);
    }
    const bool can_down = text.scroll_down(d);
    if (can_down) {
      text.scroll_up(d);
    }
    up.disabled = !can_up;
    down.disabled = !can_down;
    up.dirty = true;
    down.dirty = true;
    refresh = false;
  }
};

// Copies colours 128..`last` of the frame's palette into `palette` (the
// screens apply HFLAGS.SHP's or EQUIP.SHP's colours to a scratch palette,
// then copy that range; colours outside the frame's keep the screen's).
void apply_sprite_colours(const SpriteFrame& frame, VgaPalette& palette, int last) {
  VgaPalette scratch = palette;
  sprite_apply_palette(frame, scratch);
  std::copy(scratch.begin() + 0x180, scratch.begin() + (last + 1) * 3, palette.begin() + 0x180);
}

// `homeport_draw_port_marker` (0x4d243): the cross and the flag of `port`
// in the map viewport `map`.
void draw_port_marker(const Viewport& map, const HomePort& port, const Shp& flags) {
  const int x = port.map_point[0];
  const int y = port.map_point[1];
  constexpr std::uint8_t kColour = 0xf;
  ui::draw_line(map, x, y - 1, x, y + 1, kColour);
  ui::draw_line(map, x - 1, y, x + 1, y, kColour);
  const SpriteFrame& flag = flags.frame(1);
  const int lx = x + port.label_offset[0];
  const int ly = y + port.label_offset[1];
  ui::gui_fill_rect(map, lx + 1, ly + 1, flag.x1 - flag.x0 + 1, flag.y1 - flag.y0 + 1, 0);
  draw_sprite_clipped(map, flag, lx, ly);
}

// Returns the viewport of the base map: the art's frame 0 at (0xc, 0x28) of
// the full-screen dialog's `gfx`.
Viewport map_viewport(const Viewport& gfx, const SpriteFrame& art) {
  return {gfx.bitmap, gfx.x0 + 0xc, gfx.y0 + 0x28, gfx.x0 + art.x1 - art.x0 + 1 + 0xb,
          gfx.y0 + art.y1 - art.y0 + 1 + 0x27};
}

}  // namespace

PatrolOrdersResult CareerLetters::patrol_orders(const CareerRecord& record, const PatrolOrders& orders,
                                                const HomePort* home) {
  // patrol_orders_screen (0x630cc), dialog 0x12509c.
  if (record.on_patrol != 0) {
    return PatrolOrdersResult::kBegin;
  }
  CareerUi& ui = ui_;
  ui.load_screen_pcx(ui.exe_string(addr::kOrdersPcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  const Shp flags = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kOrdersFlags)));
  const Shp art = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, base_art_file(ui.exe(), record.month, record.year)));
  apply_sprite_colours(flags.frame(0), ui.palette(), 0xfd);
  const std::string letter = patrol_orders_letter(record, orders, ui.exe(), ui.menu_resources().small);

  ui::Dialog d(ui.gui(), 0, 0, 0x280, 0x1e0, ui::kDialogFramed);
  // patrol_orders_draw_map (0x6320e)
  d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) {
    draw_sprite_clipped(gfx, ui.menu_resources().sprites.frame(8), 0x216, 0x23);
    ui.menu_skin().draw_frame(gfx, 7, 0x23, 0x1ab, 0x194);
    draw_sprite_clipped(gfx, art.frame(0), 0xc, 0x28);
    const auto& r = orders.zone.orders_rect;
    ui::gui_focus_rect(gfx, r[0] + 0xc, r[1] + 0x28, r[2] - r[0] + 1, r[3] - r[1] + 1, 0xf, false);
    if (home != nullptr) {
      draw_port_marker(map_viewport(gfx, art.frame(0)), *home, flags);
    }
    d.add_dirty({0, 0, 0x27f, 0x1df});
  });
  d.emplace<ui::Button>(0x1b2, 0x13c, 0xc6, ui.exe_string(0xf7bb8),
                        [](int) { return static_cast<int>(PatrolOrdersResult::kRealism); });
  d.emplace<ui::Button>(0x1b2, 0x168, 0xc6, ui.exe_string(0xf7bc0),
                        [](int) { return static_cast<int>(PatrolOrdersResult::kBegin); });
  d.set_back(d.emplace<ui::Button>(0x1b2, 0x194, 0xc6, ui.exe_string(0xf7bcd),
                                   [](int) { return static_cast<int>(PatrolOrdersResult::kCancel); }));
  d.emplace<ui::TextBox>(0x1bc, 0x78, 0x9d, 0xa0, 10, 1000, letter).read_only = true;
  d.emplace<ui::Label>(0x1f1, 0x5e, ui.exe_string(0xf7bd4), ui::FontSlot::kFont1);
  ui.add_exit_area(d);
  // patrol_orders_fade_in (0x631f6)
  d.set_idle([&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  });
  return static_cast<PatrolOrdersResult>(ui.run_menu_dialog(d));
}

bool CareerLetters::patrol_orders_realism(CareerRecord& record, std::optional<MissionRules>& rules) {
  // patrol_orders_realism (0x62fdc); the career's rules give the dialog's
  // extended items, and take them back (not in the original).
  ui_.realism_rules() = rules.value_or(MissionRules{});
  if (ui_.realism_menu(false, false) != ui::kResultAccept) {
    return false;
  }
  career_realism_from_options(ui_.options(), record);
  if (rules) {
    rules->keen_escort_sonar = ui_.realism_rules().keen_escort_sonar;
    rules->detectable_radar = ui_.realism_rules().detectable_radar;
  }
  return true;
}

void CareerLetters::patrol_results(const CareerRecord& record, const MissionResults& results,
                                   const PatrolResultsInfo& info) {
  // patrol_results_screen (0x639d8), dialog 0x1252ec.
  CareerUi& ui = ui_;
  ui.load_screen_pcx(ui.exe_string(addr::kResultsPcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  ui::Dialog d(ui.gui(), 10, 0xc, 0x267, 0x14a, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(0xf7be1));
  d.set_back(d.emplace<ui::Button>(0xf5, 0x118, 0x79, ui.exe_string(0xf7bf0), ui::return_accept));
  auto& text = d.emplace<ui::TextBox>(
      0x12, 0x7a, 0x209, 0x86, 10, 5000,
      patrol_results_summary(record, results, info, ui.exe(), ui.menu_resources().small));
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(0x235, 0x76, 0x21, ui.exe_string(0xf7bf3), nullptr, 0, 0x14);
  auto& down = d.emplace<ui::Button>(0x235, 0xef, 0x21, ui.exe_string(0xf7bf5), nullptr, 0, 0x14);
  d.emplace<ui::GroupBox>(0x235, 0x9b, 0x255, 0xec);
  ui.add_exit_area(d);
  TextScroll scroll{text, up, down};
  scroll.wire(d);
  // patrol_results_dialog_draw (0x646b4)
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
      scroll.refresh = true;
    }
    scroll.update(d);
  });
  ui.run_menu_dialog(d);
}

int CareerLetters::homeport(const CareerRecord& record, const HomePort& port, bool transfer) {
  // homeport_screen (0x4d0e3), dialog 0x122000.
  CareerUi& ui = ui_;
  ui.load_screen_pcx(ui.exe_string(addr::kHomePcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  const Shp flags = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kHomeFlags)));
  const Shp art = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, base_art_file(ui.exe(), record.month, record.year)));
  apply_sprite_colours(flags.frame(0), ui.palette(), 0xfe);
  const std::string orders = homeport_transfer_orders(record, port, transfer, ui.exe(), ui.menu_resources().small);

  ui::Dialog d(ui.gui(), 0, 0, 0x280, 0x1e0, ui::kDialogFramed);
  // homeport_draw_map (0x4d385)
  d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) {
    draw_sprite_clipped(gfx, ui.menu_resources().sprites.frame(8), 0x215, 0x23);
    ui.menu_skin().draw_frame(gfx, 7, 0x23, 0x1ab, 0x194);
    const Viewport map = map_viewport(gfx, art.frame(0));
    draw_sprite_clipped(map, art.frame(0), 0, 0);
    draw_port_marker(map, port, flags);
    d.add_dirty({0, 0, 0x27f, 0x1df});
  });
  d.set_back(d.emplace<ui::Button>(0x1b2, 0x194, 0xc6, ui.exe_string(0xf5358), ui::return_accept));
  d.emplace<ui::TextBox>(0x1bc, 0x8c, 0x9d, 0xcf, 10, 1000, orders).read_only = true;
  auto& title = d.emplace<ui::TextField>(0x1b8, 0x66, 0x26f, 0x7c, [&ui] { return ui.exe_string(0xf53c1); },
                                         ui::FontSlot::kFont1, TextAlign::kCentre);
  title.use_colours = false;
  ui.add_exit_area(d);
  // homeport_fade_in (0x4d218)
  d.set_idle([&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  });
  return ui.run_menu_dialog(d);
}

int CareerLetters::new_equipment(CareerRecord& record, std::int32_t current_command, const MissionRules& rules) {
  // new_equipment_run (0x5b501)
  CareerUi& ui = ui_;
  const std::optional<int> due = new_equipment_due(ui.exe(), record, current_command);
  std::optional<EquipmentEntry> found;
  if (due) {
    found = equipment_entry(ui.exe(), *due);
  } else {
    // Not in the original: the extended variant's deliveries.
    found = extended_equipment_due(record, current_command, rules);
  }
  if (!found) {
    return 0;
  }
  const EquipmentEntry& entry = *found;
  record.equipment |= entry.bit;
  const std::string letter = new_equipment_text(record, entry, ui.exe(), ui.menu_resources().small);
  ui.load_screen_pcx(ui.exe_string(addr::kEquipPcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  // new_equipment_draw_art (0x5ba0b): the picture on black in a box 3
  // pixels larger, copied to the screen.
  {
    const Shp equip = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kEquipShp)));
    if (entry.sprite == kStRadarPicture) {
      const Image8 picture = st_radar_picture(equip);
      Image8 box(picture.width() + 3, picture.height() + 3, 0);
      box.blit(picture, 0, 0);
      ui.screen().blit(box, entry.x, entry.y);
    } else {
      const SpriteFrame& frame = equip.frame(static_cast<std::size_t>(entry.sprite));
      Image8 box(frame.x1 - frame.x0 + 1 + 3, frame.y1 - frame.y0 + 1 + 3, 0);
      draw_sprite_clipped(Viewport::full(box), frame, 0, 0);
      ui.screen().blit(box, entry.x, entry.y);
    }
    apply_sprite_colours(equip.frame(0), ui.palette(), 0xfe);
  }
  ui::Dialog d(ui.gui(), 0x140, 0x31, 300, 0x178, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(0xf750c));
  d.set_back(d.emplace<ui::Button>(0x58, 0x140, 0x79, ui.exe_string(0xf751a), ui::return_accept));
  d.emplace<ui::TextBox>(0x14, 0x82, 0xf0, 0xa0, 10, 1000, letter).read_only = true;
  ui.add_exit_area(d);
  // 0x5b5a0
  bool fade_pending = true;  // 0x124cb8
  d.set_idle([&](const Viewport&, int) {
    if (fade_pending) {
      ui.palette_fade_in(ui.palette(), 0, 10);
      fade_pending = false;
    }
  });
  const int result = ui.run_menu_dialog(d);
  return result == ui::kResultCancel ? ui::kResultCancel : ui::kResultAccept;
}

void CareerLetters::present_award(LetterCode code, const CareerRecord& record, SubCommand command) {
  // awards_present (0x55ecd) with awards_build_citation (0x56185).
  CareerUi& ui = ui_;
  const Shp medals = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kAwardsShp)));
  const Font typewriter = Font::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kTypewriter)));
  const std::string text =
      award_citation_text(code, record, command, citation_width_, ui.exe(), typewriter, ui.menu_resources().small);
  const auto c = static_cast<std::int32_t>(code);
  std::uint32_t picture = addr::kNavyPcx;
  int x = 0xc3;
  int y = 0x7d;
  int h = 300;
  citation_width_ = 0xf9;
  if (c < 6) {
    picture = addr::kMedalPcx;
    x = 0x151;
    y = 0x82;
    h = 0xeb;
    citation_width_ = 0xfc;
  } else if (code == LetterCode::kPuc) {
    picture = addr::kPresidentPcx;
    y = 0xaf;
    h = 0xf0;
  }
  ui.load_screen_pcx(ui.exe_string(picture));
  if (c < 6) {
    draw_sprite_clipped(Viewport::full(ui.screen()), medals.frame(static_cast<std::size_t>(c)), 0x47, 0x73);
  }
  ui.palette_fade_out(0, 0x100);
  ui.gui().mouse().hide();
  ui.gui().events().flush();

  ui::Skin& previous = ui.gui().skin();
  TypewriterSkin skin(ui.menu_skin(), typewriter);
  ui.gui().set_skin(skin);
  ui::Dialog d(ui.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &ui.screen());
  d.emplace<ui::TextBox>(x, y, citation_width_, h, 10, 1000, text).read_only = true;
  ui.add_exit_area(d);
  d.open();
  d.process();
  ui.palette_fade_in(ui.palette(), 0, 10);
  ui.wait_for_press();
  d.close();
  ui.gui().set_skin(previous);
  ui.palette_fade_out(1, 5);
  ui.screen().fill(0);
  ui.gui().mouse().show();
}

void CareerLetters::transit_calendar(const CareerRecord& r, std::int32_t transit_days, std::int32_t port_days) {
  // career_show_transit_calendar (0x1c539) and career_transit_calendar
  // (0x1c653): the texts centred in 0xd0 pixels from x 0xdc.
  CareerUi& ui = ui_;
  const auto font = [&ui](std::uint32_t i) {
    return Font::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kCalendarFonts + i * 0xc)));
  };
  const Font big = font(0);
  const Font medium = font(1);
  const Font small = font(2);
  ui.palette_fade_out(0, 0x100);
  ui.gui().mouse().hide();
  ui.gui().events().flush();
  ui.load_screen_pcx(ui.exe_string(addr::kCalendarPcx));
  const Viewport vp = Viewport::full(ui.screen());
  const GlyphColourTable& identity = ui.gui_resources().identity;
  const auto centred = [&](const Font& f, const std::string& text, int y) {
    gui_draw_text(vp, (0xd0 - f.text_width(text)) / 2 + 0xdc, y, f, text, &identity);
  };
  const auto upper = [](std::string s) {
    std::ranges::transform(s, s.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return s;
  };
  centred(medium, calendar_day_name(ui.exe(), r.day, r.month, r.year), 0x88);
  centred(big, std::to_string(r.day), 0xb6);
  centred(medium,
          std::string(ui.exe().string_pointer(addr::kMonths + static_cast<std::uint32_t>(r.month) * 4)) +
              ui.exe_string(0xf0eba) + std::to_string(r.year + ui.exe().i32(addr::kBaseYear)),
          0x100);
  int y = 0x140;
  if (transit_days != 0) {
    std::string s = ui.exe_string(0xf0ee9) + std::to_string(transit_days) + ui.exe_string(0xf0f28);
    if (transit_days > 1) {
      s += ui.exe_string(0xf0f43);
    }
    centred(small, upper(s), 0x140);
    y = 0x154;
  }
  std::string s = ui.exe_string(0xf0f5b) + std::to_string(port_days) + ui.exe_string(0xf0f97);
  if (port_days > 1) {
    s += ui.exe_string(0xf0fb2);
  }
  centred(small, upper(s), y);
  ui.palette_fade_in(ui.palette(), 1, 10);
  ui.wait_for_press();
  ui.palette_fade_out(1, 5);
  ui.screen().fill(0);
  ui.gui().mouse().show();
}

void CareerLetters::end_newspaper() {
  // career_show_end_newspaper (0x1ca30)
  CareerUi& ui = ui_;
  ui.palette_fade_out(0, 0x100);
  ui.gui().mouse().hide();
  ui.gui().events().flush();
  ui.load_screen_pcx(ui.exe_string(addr::kPaperPcx));
  ui.palette_fade_in(ui.palette(), 0, 10);
  ui.wait_for_press();
  ui.palette_fade_out(1, 5);
  ui.screen().fill(0);
  ui.gui().mouse().show();
}

int CareerLetters::new_submarine(Career& career, const GameTables& tables, SubCommand command, bool offer,
                                 const std::function<bool(Career&)>& assign_new_boat) {
  // new_submarine_screen (0x7131c), dialog 0x12772c.
  CareerUi& ui = ui_;
  CareerRecord& r = career.record;
  // The boat an offer may give back.
  const CareerRecord saved = r;
  int result = ui::kResultCancel;
  if (assign_new_boat(career)) {
    ui.load_screen_pcx(ui.exe_string(addr::kSubPcx));
    ui.gui().set_skin(ui.menu_skin());
    const Shp pictures = Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kSubPictures)));
    const SubClass& sub = tables.subs.at(r.class_index);
    const auto toggle_label = [this, &ui] { return ui.exe_string(subscrn_statistics_ ? 0xf90e3 : 0xf90ef); };
    const std::string orders = new_submarine_orders(r, command, offer, ui.exe(), ui.menu_resources().small);
    ui.palette_fade_out(0, 0x100);

    ui::Dialog d(ui.gui(), 10, 0x27, 0x26c, 0x191, kMenuFlags);
    // 0x71839: the frames; `subscrn_draw_sub_info` (0x718b7) draws the
    // picture and the 0x14d x 0x71 info panel (shown at 0x24, 0x101).
    auto& info = d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) {
      ui.menu_skin().draw_frame(gfx, 0x14, 0x7a, 0x159, 0x59);
      ui.menu_skin().draw_frame(gfx, 0x14, 0xd4, 0x159, 0x7d);
      d.add_dirty({0x14, 0x7a, 0x14 + 0x159 - 1, 0xd4 + 0x7d - 1});
      draw_sprite_clipped(gfx, pictures.frame(sub.picture_frame), 0x1a, 0x80);
      constexpr int kPanelWidth = 0x14d;
      constexpr int kPanelHeight = 0x71;
      Image8 panel(kPanelWidth, kPanelHeight, 0xff);
      const Viewport pv = Viewport::full(panel);
      const Font& font = ui.menu_resources().small;
      const GlyphColourTable& colours = ui.gui_resources().identity;
      const std::string title = sub.display_name + ui.exe_string(0xf92c6);
      const int w = font.text_width(title);
      const int x = (kPanelWidth - w) / 2;
      gui_draw_text(pv, x, 3, font, title, &colours);
      ui::draw_line(pv, x, 0xb, x + w - 1, 0xb, 0);
      if (subscrn_statistics_) {
        for (const SubInfoText& t : sub_statistics(sub, ui.exe())) {
          gui_draw_text(pv, t.x, t.y, font, t.text, &colours);
        }
      } else {
        // 0x723d6: glyph by glyph; a '\n' moves to the next line and is
        // drawn there too.
        int cx = 0x11;
        int cy = 0x1e;
        for (const char c : sub.description) {
          if (c == '\n') {
            cx = 0x11;
            cy += 0xb;
          }
          cx += font_draw_char(pv, cx, cy, font, static_cast<std::uint8_t>(c), &colours);
        }
      }
      gfx.bitmap->blit(panel, gfx.x0 + 0x24 - 10, gfx.y0 + 0x101 - 0x27);
    });
    d.emplace<ui::TitleBar>(ui.exe_string(0xf9064));
    // Back declines an offer (CANCEL) and accepts an assignment (OK, its
    // only way out).
    d.set_back(d.emplace<ui::Button>(0x1b2, 0x15c, 0x79, ui.exe_string(0xf9072), ui::return_accept));
    auto& toggle = d.emplace<ui::Button>(0x40, 0x15c, 0x79, toggle_label(), nullptr);
    // subscrn_toggle_stats (0x71870)
    toggle.on_click = [&](int) {
      subscrn_statistics_ = !subscrn_statistics_;
      toggle.label = toggle_label();
      toggle.dirty = true;
      info.dirty = true;
      return 0;
    };
    if (offer) {
      d.set_back(d.emplace<ui::Button>(0xf9, 0x15c, 0x79, ui.exe_string(0xf9080), ui::return_cancel));
    }
    d.emplace<ui::TextBox>(0x17e, 0x7e, 0xc2, 0xc0, 10, 500, orders).read_only = true;
    ui.add_exit_area(d);
    // subscrn_dialog_draw (0x71a6d)
    bool fade_pending = true;  // 0x127728
    d.set_idle([&](const Viewport&, int first) {
      if (first == 0 && fade_pending) {
        ui.palette_fade_in(ui.palette(), 0, 10);
        fade_pending = false;
      }
    });
    result = ui.run_menu_dialog(d);
  }
  if (result == ui::kResultCancel) {
    if (offer) {
      r.boat = saved.boat;
      r.class_index = saved.class_index;
      r.class_name = saved.class_name;
      r.class_title = saved.class_title;
      r.fuel_capacity = saved.fuel_capacity;
      r.fuel_use = saved.fuel_use;
      r.transit_speed = saved.transit_speed;
    }
    return ui::kResultCancel;
  }
  // A new command: the promotion and citation counters start again, and
  // the rank drops to 1 at most.
  r.promotion_patrols = 0;
  r.puc_streak = 0;
  r.rank = r.rank > 0 ? 1 : 0;
  r.nuc_streak = 0;
  return ui::kResultAccept;
}

}  // namespace sh

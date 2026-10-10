// The SUB TOUR screen of the main menu (module subtour): the Gato class
// cutaway in its internal (B-GRND-2.PCX) or external (B-GRND-1.PCX) view,
// its parts highlighted under the pointer and labelled in TYPEX.FNT, and a
// click showing the part's picture and playing its narration. The dialogs
// are those of `subtour_init_dialogs` (0x6e0fc); `subtour_screen`
// (0x6fba1) runs them.

#include <array>
#include <memory>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_extras.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// SH.EXE addresses.
constexpr std::uint32_t kLabels = 0x10d958;  // 29 records of 0x4c bytes
constexpr std::uint32_t kLabelSize = 0x4c;
constexpr std::uint32_t kNarrations = 0x10d834;  // 10-byte names
constexpr std::uint32_t kHighlightX = 0x10d74c;
constexpr std::uint32_t kHighlightY = 0x10d7c0;
constexpr int kParts = 20;
constexpr std::uint32_t kTitle = 0xf8fb8;       // "Gato Class Sub Tour"
constexpr std::uint32_t kMainMenu = 0xf8fcc;    // "MAIN MENU"
constexpr std::uint32_t kTextOn = 0x10d6fc;     // "Text On", "Text Off" 0x14 apart
constexpr std::uint32_t kViewLabel = 0x10d724;  // "Internal View", "External View"
constexpr std::uint32_t kToggleSize = 0x14;
constexpr std::uint32_t kExternalPcx = 0xf9010;  // "B-GRND-1.PCX"
constexpr std::uint32_t kInternalPcx = 0xf9003;  // "B-GRND-2.PCX"
constexpr std::uint32_t kSprites = 0xf901d;      // "subtour.shp"
constexpr std::uint32_t kFont = 0xf9029;         // "typex.fnt"

// The panel's results.
constexpr int kToggleView = 1;
constexpr int kToggleText = 2;
constexpr int kExit = 100;

// The picture box: SUBTOUR.SHP frame 0 (the frame) over a black fill, the
// part's picture inside it.
constexpr int kFrameX = 0x179;
constexpr int kFrameY = 0xe8;
constexpr int kPictureX = 0x17f;
constexpr int kPictureY = 0xee;
constexpr std::size_t kFrameSprite = 0;
constexpr std::size_t kSpeakerSprite = 1;

// The click areas of dialog 0x1270b0: the 12 internal parts, then the 4
// external ones (parts 16..19).
struct PartArea {
  int part;
  int x0, y0, x1, y1;
};
constexpr std::array<PartArea, 16> kAreas{{
    {0, 0x4d, 0xbc, 0xd3, 0x105},   {1, 0xd6, 0xba, 0xfd, 0xdf},    {2, 0xfe, 0xb3, 0x120, 0xdf},
    {3, 0x11b, 0x95, 0x14c, 0xaf},  {4, 0x121, 0xb0, 0x14d, 0xcf},  {5, 0x14e, 0xa8, 0x15a, 0xc7},
    {6, 0x15b, 0xa8, 0x168, 0xc5},  {7, 0x169, 0xa4, 0x17e, 0xc0},  {8, 0x17f, 0x9d, 0x1b3, 0xb9},
    {9, 0x1b4, 0x90, 0x1e9, 0xab},  {10, 0x1eb, 0x88, 0x20b, 0xa0}, {11, 0x22a, 0x75, 0x25b, 0x92},
    {16, 0x181, 0x8b, 0x199, 0x97}, {17, 0x12d, 0x2a, 0x143, 0x87}, {18, 0x10d, 0x79, 0x160, 0x9a},
    {19, 0x74, 0xbf, 0x91, 0xe4},
}};

// The label colour tables of `subtour_load_assets` (0x6fa00): the glyph
// shades 0x3b / 0x3f drawn in colour 0xf (hovered, 0x126d88), 0xd4
// (0x126e88) or 0 (the shadow, 0x126f88); 0xff stays transparent and
// every other value is drawn black.
GlyphColourTable label_colours(std::uint8_t colour) {
  GlyphColourTable t{};
  t[0x3b] = colour;
  t[0x3f] = colour;
  t[0xff] = 0xff;
  return t;
}

}  // namespace

std::vector<SubTourLabel> subtour_labels(const ExeImage& exe) {
  std::vector<SubTourLabel> labels;
  for (int i = 0; i < kSubTourLabels; ++i) {
    const std::uint32_t a = kLabels + static_cast<std::uint32_t>(i) * kLabelSize;
    SubTourLabel l;
    l.name = std::string(exe.c_string(a));
    l.x = exe.i32(a + 0x28);
    l.y = exe.i32(a + 0x2c);
    l.part = exe.i32(a + 0x30) != 0;
    l.picture = exe.i32(a + 0x34);
    l.highlight = exe.i32(a + 0x38);
    l.line_x0 = exe.i32(a + 0x3c);
    l.line_y0 = exe.i32(a + 0x40);
    l.line_x1 = exe.i32(a + 0x44);
    l.line_y1 = exe.i32(a + 0x48);
    if (i < kParts) {
      const auto n = static_cast<std::uint32_t>(i);
      l.narration = std::string(exe.c_string(kNarrations + n * 10));
      l.highlight_x = exe.i32(kHighlightX + n * 4);
      l.highlight_y = exe.i32(kHighlightY + n * 4);
    }
    labels.push_back(std::move(l));
  }
  return labels;
}

// The screen's assets and state, from `subtour_load_assets` (0x6fa00) to
// `subtour_free_assets` (0x6fb12).
class SubTour {
 public:
  explicit SubTour(FrontEnd& fe)
      : fe_(fe),
        labels_(subtour_labels(fe.exe_)),
        sprites_(Shp::load(fe.install_, fe.paths_.path(GameDir::kArt, fe.exe_string(kSprites)))),
        font_(Font::load(fe.install_, fe.paths_.path(GameDir::kArt, fe.exe_string(kFont)))),
        hover_colours_(label_colours(0xf)),
        normal_colours_(label_colours(0xd4)),
        shadow_colours_(label_colours(0)) {
    load_background();
    fe_.palette_fade_out(0, 0x100);
  }
  SubTour(const SubTour&) = delete;
  SubTour& operator=(const SubTour&) = delete;
  ~SubTour() { fe_.host_.stop_speech(); }

  // subtour_screen (0x6fba1)
  void run() {
    while (true) {
      switch (run_dialog()) {
        case kToggleView:
          fe_.subtour_internal_ = !fe_.subtour_internal_;
          selected_ = -1;
          load_background();
          fe_.host_.stop_speech();
          break;
        case kToggleText:
          fe_.subtour_text_ = !fe_.subtour_text_;
          break;
        case kExit:
          fe_.host_.stop_speech();
          return;
        default:
          break;
      }
    }
  }

 private:
  // subtour_load_background (0x6f806): the view's picture into the
  // background buffer, its colours into the screen palette.
  void load_background() {
    const PcxImage pcx = load_pcx(fe_.install_, background_path());
    background_ = pcx.image;
    fe_.palette_ = pcx.palette;
  }

  std::string background_path() const {
    return fe_.paths_.path(GameDir::kArt, fe_.exe_string(fe_.subtour_internal_ ? kInternalPcx : kExternalPcx));
  }

  // Returns true when `part` belongs to the view shown.
  bool in_view(int part) const { return fe_.subtour_internal_ ? part < kSubTourFirstExternal : part >= kSubTourFirstExternal; }

  // subtour_run_dialog (0x6f613): the panel 0x127504 and the parts
  // 0x1270b0 over the view, until a panel button.
  int run_dialog() {
    FrontEnd& fe = fe_;
    ui::Gui& gui = fe.gui_;
    gui.set_skin(fe.menu_skin_);
    // subtour_update_toggle_labels (0x6f7db)
    const std::uint32_t view = fe.subtour_internal_ ? 1 : 0;
    const std::uint32_t text = fe.subtour_text_ ? 1 : 0;
    ui::Dialog panel(gui, 10, 0x134, 0xfa, 0xa2, ui::kDialogTitled | ui::kDialogFramed, &fe.screen_);
    panel.emplace<ui::Label>(0x47, 0xf, fe.exe_string(kTitle), ui::FontSlot::kFont1, false);
    panel.emplace<ui::Button>(0x19, 0x25, 0xc6, fe.exe_string(kViewLabel + view * kToggleSize),
                              [](int) { return kToggleView; });
    panel.emplace<ui::Button>(0x19, 0x4d, 0xc6, fe.exe_string(kTextOn + text * kToggleSize),
                              [](int) { return kToggleText; });
    panel.emplace<ui::Button>(0x19, 0x75, 0xc6, fe.exe_string(kMainMenu), [](int) { return kExit; });
    fe.add_exit_area(panel);

    ui::Dialog parts(gui, 0, 0, 0x27f, 0x1df, 0, &fe.screen_);
    for (const PartArea& a : kAreas) {
      const int part = a.part;
      auto& area = parts.emplace<ui::Area>(
          a.x0, a.y0, a.x1, a.y1,
          [this, part](ui::Dialog&, ui::Area& self, ui::Msg msg, int, const ui::Event&) {
            // hotkey_proc_simple (0x49432)
            if (msg == ui::Msg::kPress && !self.pressed) {
              select(part);
            }
            return 0;
          },
          nullptr);
      area.on_hover = [this, part](bool on, int, const ui::Event&) { hover(part, on); };
      // subtour_enable_view_hotspots (0x6f861): the other view's parts are
      // disabled, and the forward torpedo room's area is emptied in the
      // external view.
      area.disabled = !in_view(part);
      if (part == 0 && !fe.subtour_internal_) {
        area.x0 = area.y0 = area.x1 = area.y1 = 0;
      }
    }

    gui.hotspots().disable_all();
    panel.open();
    parts.open();
    fe.screen_.blit(load_pcx(fe.install_, background_path()).image, 0, 0);
    if (selected_ != -1) {
      draw_picture(selected_);
    }
    if (fe.subtour_text_) {
      // subtour_draw_labels (0x6f439)
      const int first = fe.subtour_internal_ ? 0 : kSubTourFirstExternal;
      const int end = fe.subtour_internal_ ? kSubTourFirstExternal : kSubTourLabels;
      for (int i = first; i < end; ++i) {
        draw_label(i, false);
      }
    }
    bool first_frame = true;
    while (true) {
      int result = 0;
      while (true) {
        bool moved = false;
        if (const std::optional<ui::Event> e = gui.events().get()) {
          panel.handle_event(*e);
          parts.handle_event(*e);
          moved = (e->flags & ui::kEventMoved) != 0;
        }
        parts.process();
        result = panel.process();
        if (result != 0 || !moved || !gui.events().peek()) {
          break;
        }
      }
      if (first_frame) {
        first_frame = false;
        fe.palette_fade_in(fe.palette_, 0, 10);
      }
      if (result != 0) {
        parts.close();
        panel.close();
        gui.hotspots().enable_all();
        gui.set_skin(fe.default_skin_);
        return result;
      }
      fe.frame();
    }
  }

  // The selected part's picture in its frame (the click body 0x6fd52).
  void draw_picture(int part) {
    const Viewport screen = Viewport::full(fe_.screen_);
    const SpriteFrame& frame = sprites_.frame(kFrameSprite);
    ui::gui_fill_rect(screen, kFrameX + 3, kFrameY + 3, frame.width(), frame.height(), 0);
    draw_sprite_clipped(screen, frame, kFrameX, kFrameY);
    draw_sprite_clipped(screen, sprites_.frame(static_cast<std::size_t>(labels_[static_cast<std::size_t>(part)].picture)),
                        kPictureX, kPictureY);
  }

  // subtour_select_* (0x6fce1..0x6fd4d): the part's picture, then its
  // narration (subtour_play_narration, 0x6f5a3: "%s.RAW" from SPEECH).
  void select(int part) {
    draw_picture(part);
    fe_.host_.stop_speech();
    fe_.host_.play_speech(labels_[static_cast<std::size_t>(part)].narration + ".RAW");
    selected_ = part;
  }

  // subtour_hover_part (0x6fcc5): the selection cursor while hovering; a
  // part of the view shown is highlighted (or restored from the
  // background) and its label redrawn.
  void hover(int part, bool on) {
    ui::Gui& gui = fe_.gui_;
    if (on) {
      gui.mouse().set_cursor(fe_.cursors_, 1);
    } else {
      gui.mouse().set_cursor(fe_.cursors_, gui.dialog_cursor_frame());
    }
    if (!in_view(part)) {
      return;
    }
    const SubTourLabel& l = labels_[static_cast<std::size_t>(part)];
    const SpriteFrame& f = sprites_.frame(static_cast<std::size_t>(l.highlight));
    if (on) {
      draw_sprite_clipped(Viewport::full(fe_.screen_), f, l.highlight_x, l.highlight_y);
    } else {
      // viewport_blit of the background rectangle (x, y)-(x + w, y + h).
      for (int y = l.highlight_y; y <= std::min(l.highlight_y + f.height(), kScreenHeight - 1); ++y) {
        for (int x = l.highlight_x; x <= std::min(l.highlight_x + f.width(), kScreenWidth - 1); ++x) {
          fe_.screen_.set(x, y, background_.at(x, y));
        }
      }
    }
    draw_label(part, on);
  }

  // subtour_draw_compartment_label (0x6f226), when the labels are on: the
  // name over its shadow, the speaker icon before a part's name, and the
  // line to the part.
  void draw_label(int index, bool hovered) {
    if (!fe_.subtour_text_ || index < 0 || index >= kSubTourLabels) {
      return;
    }
    const Viewport screen = Viewport::full(fe_.screen_);
    const SubTourLabel& l = labels_[static_cast<std::size_t>(index)];
    const GlyphColourTable& colours = hovered ? hover_colours_ : normal_colours_;
    gui_draw_text(screen, l.x, l.y + 1, font_, l.name, &shadow_colours_);
    gui_draw_text(screen, l.x, l.y, font_, l.name, &colours);
    if (l.part) {
      const SpriteFrame& speaker = sprites_.frame(kSpeakerSprite);
      draw_sprite_remapped(screen, speaker, l.x - (speaker.width() + 3), l.y, colours);
    }
    ui::draw_line(screen, l.line_x0, l.line_y0, l.line_x1, l.line_y1, colours[0x3b]);
  }

  FrontEnd& fe_;
  const std::vector<SubTourLabel> labels_;
  const Shp sprites_;  // SUBTOUR.SHP (0x126d80)
  const Font font_;    // TYPEX.FNT (0x126d84)
  Image8 background_{kScreenWidth, kScreenHeight};  // 0x127088
  const GlyphColourTable hover_colours_;
  const GlyphColourTable normal_colours_;
  const GlyphColourTable shadow_colours_;
  int selected_ = -1;  // 0x10e1fc
};

void FrontEnd::subtour_screen() {
  SubTour(*this).run();
}

}  // namespace sh

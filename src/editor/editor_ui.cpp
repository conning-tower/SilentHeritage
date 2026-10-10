#include "editor/editor_ui.h"

#include <algorithm>

#include "core/install.h"
#include "gfx/sprite.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

// The dialog cursor of the editor's `gui_init` config (0x981f4 words 17 and
// 18): SBCURSOR.SHP frame 1.
constexpr std::size_t kDialogCursorFrame = 1;

}  // namespace

ui::Rect EditorSkin::list_item(const Viewport& gfx, int x, int y, int w, std::string_view s, bool selected) {
  if (list_font == nullptr) {
    return DefaultSkin::list_item(gfx, x, y, w, s, selected);
  }
  // gui_draw_list_item with the font pointer 0x9bc6c changed.
  const int rh = metrics().list_row_height;
  if (selected) {
    ui::gui_fill_rect(gfx, x + 2, y + 2, w - 4, rh, res_.colours.a[3]);
  }
  text(gfx, x + 5, y + metrics().list_text_y, *list_font, s, res_.table);
  return {x, y, x + w - 1, y + rh - 1};
}

ListMetricsScope::ListMetricsScope(EditorSkin& skin, int rows, int height, int track)
    : skin_(skin), saved_(skin.metrics()) {
  ui::SkinMetrics& m = skin_.mutable_metrics();
  m.list_rows = rows;
  m.list_height = height;
  m.list_track = track;
}

ListMetricsScope::~ListMetricsScope() { skin_.mutable_metrics() = saved_; }

EditorUi::EditorUi(const Install& install, const SeExe& se, FrontendHost& host)
    : install_(install),
      se_(se),
      host_(host),
      screen_(kScreenWidth, kScreenHeight),
      // load_gui_fonts -> gui_init (config 0x981f4): SE has GUI_M where SH
      // has FONT1.
      resources_{Font::load(install, "ART/GUI_S.FNT"), Font::load(install, "ART/GUI_L.FNT"),
                 Font::load(install, "ART/GUI_M.FNT"), Font::load(install, "ART/GUI_1.FNT"),
                 Shp::load(install, "ART/GUISPR.SHP"), ui::GuiColours{}, gui_colour_table(),
                 ui::GuiResources::identity_table()},
      font1_(Font::load(install, "ART/FONT1.FNT")),
      msg_font_(Font::load(install, "ART/MSG.FNT")),
      menu_font_(Font::load(install, "ART/MENU_S.FNT")),
      sbchart_(Shp::load(install, "ART/SBCHART.SHP")),
      cursors_(Shp::load(install, "ART/SBCURSOR.SHP")),
      skin_(resources_),
      gui_(screen_, [this] { return host_.ticks(); }, skin_) {
  gui_.set_pump([this] { frame(); });
  gui_.set_dialog_cursor(&cursors_, kDialogCursorFrame);
  gui_.mouse().set_cursor(cursors_, kDialogCursorFrame);
  gui_.mouse().show();
}

std::vector<std::string> EditorUi::strings(std::uint32_t table, std::size_t count) const {
  std::vector<std::string> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.emplace_back(se_.table_str(table, i));
  }
  return out;
}

void EditorUi::frame() {
  ui::Mouse& mouse = gui_.mouse();
  mouse.draw(screen_);
  host_.present(screen_, dac_);
  mouse.erase(screen_);
  host_.poll_input(gui_);
}

void EditorUi::palette_grey_out(int percent, int last) {
  // palette_grey_out (0x2f815): the grey of the lightness (max + min) / 2.
  for (int i = kFirstGreyedColour; i <= last; ++i) {
    const auto at = static_cast<std::size_t>(i) * 3;
    std::copy_n(dac_.begin() + static_cast<std::ptrdiff_t>(at), 3,
                grey_saved_.begin() + static_cast<std::ptrdiff_t>(at));
    const int r = dac_[at];
    const int g = dac_[at + 1];
    const int b = dac_[at + 2];
    const int hi = std::max({r, g, b});
    const int lo = std::min({r, g, b});
    const int grey = std::max(percent * (hi - (hi - lo) / 2) / 100, 0);
    std::fill_n(dac_.begin() + static_cast<std::ptrdiff_t>(at), 3, static_cast<std::uint8_t>(grey));
  }
}

void EditorUi::palette_restore_saved(int last) {
  for (int i = kFirstGreyedColour; i <= last; ++i) {
    const auto at = static_cast<std::ptrdiff_t>(i) * 3;
    std::copy_n(grey_saved_.begin() + at, 3, dac_.begin() + at);
  }
}

void EditorUi::palette_fade_out(std::uint32_t delay, int step) {
  VgaPalette cur = dac_;
  while (true) {
    bool done = true;
    for (std::size_t i = 0; i < cur.size(); i += 3) {
      if (cur[i] == 0 && cur[i + 1] == 0 && cur[i + 2] == 0) {
        continue;
      }
      done = false;
      for (std::size_t c = i; c < i + 3; ++c) {
        cur[c] = static_cast<std::uint8_t>(std::max(cur[c] - step, 0));
      }
    }
    if (done) {
      return;
    }
    dac_ = cur;
    const std::uint32_t start = gui_.clock();
    do {
      frame();
    } while (gui_.clock() - start < delay);
  }
}

void EditorUi::palette_fade_in(const VgaPalette& target, std::uint32_t delay, int step) {
  VgaPalette cur = dac_;
  while (true) {
    bool done = true;
    for (std::size_t i = 0; i < cur.size(); i += 3) {
      if (cur[i] == target[i] && cur[i + 1] == target[i + 1] && cur[i + 2] == target[i + 2]) {
        continue;
      }
      done = false;
      for (std::size_t c = i; c < i + 3; ++c) {
        cur[c] = static_cast<std::uint8_t>(std::min(cur[c] + step, static_cast<int>(target[c])));
      }
    }
    if (done) {
      return;
    }
    dac_ = cur;
    const std::uint32_t start = gui_.clock();
    do {
      frame();
    } while (gui_.clock() - start < delay);
  }
}

void EditorUi::set_dac_from_palette(int first, int last) {
  std::copy(palette_.begin() + first * 3, palette_.begin() + (last + 1) * 3, dac_.begin() + first * 3);
}

VgaPalette EditorUi::frame_colours(const SpriteFrame& frame) const {
  VgaPalette colours = palette_;
  sprite_apply_palette(frame, colours);
  return colours;
}

void EditorUi::apply_frame_colours(const SpriteFrame& frame, int first, int last) {
  const VgaPalette colours = frame_colours(frame);
  std::copy(colours.begin() + first * 3, colours.begin() + (last + 1) * 3, palette_.begin() + first * 3);
  set_dac_from_palette(first, last);
}

void EditorUi::set_dac_from_frame(const SpriteFrame& frame, int first, int last) {
  const VgaPalette colours = frame_colours(frame);
  std::copy(colours.begin() + first * 3, colours.begin() + (last + 1) * 3, dac_.begin() + first * 3);
}

}  // namespace sh::editor

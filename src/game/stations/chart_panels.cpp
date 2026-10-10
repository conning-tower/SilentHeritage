#include "game/stations/chart_panels.h"

#include <algorithm>
#include <utility>

#include "core/fixed_math.h"
#include "ui/draw.h"

namespace sh {

namespace {

// Returns the pixel of `image` at (x, y), or 0 outside.
std::uint8_t pixel_at(const Image8& image, int x, int y) { return image.contains(x, y) ? image.at(x, y) : 0; }

}  // namespace

// --- ChartPanelHost -----------------------------------------------------------

ChartPanelHost::ChartPanelHost(ui::Gui& gui, ui::Rect bounds, Hooks hooks)
    : gui_(gui), bounds_(bounds), hooks_(std::move(hooks)) {}

int ChartPanelHost::add(ChartPanelSlot slot) {
  if (slots_.size() == kMaxPanels) {
    return kNone;
  }
  slots_.push_back(std::move(slot));
  return static_cast<int>(slots_.size()) - 1;
}

void ChartPanelHost::reset() {
  open_ = false;
  active_ = kNone;
  hovered_ = false;
  dragging_ = false;
  locked_ = false;
  last_mouse_.reset();
}

std::optional<ui::Rect> ChartPanelHost::active_rect() const {
  if (active_ < 0 || static_cast<std::size_t>(active_) >= slots_.size()) {
    return std::nullopt;
  }
  const ChartPanelSlot& s = slots_[static_cast<std::size_t>(active_)];
  return ui::Rect{s.position->x, s.position->y, s.position->x + s.width - 1, s.position->y + s.height - 1};
}

void ChartPanelHost::set_hovered(bool hovered) {
  hovered_ = hovered;
  const int hotspot = gui_.hotspots().first_of(view_dialog_);
  if (hovered) {
    gui_.hotspots().disable(hotspot);
  } else {
    gui_.hotspots().enable(hotspot);
  }
  if (hooks_.hover) {
    hooks_.hover(hovered);
  }
}

void ChartPanelHost::switch_panel() {
  // chart_view_switch_panel (0x2aee4)
  if (active_ == requested_) {
    return;
  }
  close_panel();
  hovered_ = false;
  locked_ = false;
  dragging_ = false;
  active_ = requested_;
  gui_.hotspots().enable(gui_.hotspots().first_of(view_dialog_));
  if (hooks_.hover) {
    hooks_.hover(false);
  }
  if (active_ != kNone) {
    open_panel();
  }
}

void ChartPanelHost::open_panel() {
  // chart_view_open_panel (0x2af33)
  if (active_ == kNone || open_) {
    return;
  }
  open_ = true;
  slots_[static_cast<std::size_t>(active_)].open();
}

void ChartPanelHost::close_panel() {
  // chart_view_close_panel (0x2af5d)
  if (!open_ || active_ == kNone) {
    return;
  }
  open_ = false;
  slots_[static_cast<std::size_t>(active_)].close();
}

void ChartPanelHost::handle_mouse() {
  // chart_view_handle_mouse (0x2ac8e)
  ui::Mouse& mouse = gui_.mouse();
  const int mx = mouse.x();
  const int my = mouse.y();
  const bool button = (mouse.buttons() & ui::kButtonLeft) != 0;
  if (!open_ && hovered_) {
    set_hovered(false);
  }
  if (open_) {
    if (dragging_) {
      if (!button) {
        if (hooks_.cursor) {
          hooks_.cursor(ChartCursor::kDefault);
        }
        locked_ = false;
        dragging_ = false;
      } else {
        drag(mx, my);
      }
      last_mouse_.reset();
      return;
    }
    const ui::Rect r = *active_rect();
    const bool inside = mx >= r.x0 && my >= r.y0 && mx <= r.x1 && my <= r.y1;
    if (!inside) {
      if (hovered_) {
        set_hovered(false);
      }
    } else if (hovered_) {
      if (button && my < r.y0 + kDragStrip) {
        if (hooks_.cursor) {
          hooks_.cursor(ChartCursor::kDrag);
        }
        drag_dx_ = mx - r.x0;
        drag_dy_ = my - r.y0;
        locked_ = true;
        dragging_ = true;
      }
    } else {
      set_hovered(true);
    }
  }
  if (hovered_) {
    last_mouse_.reset();
    return;
  }
  if (!last_mouse_ || (*last_mouse_)[0] != mx || (*last_mouse_)[1] != my) {
    if (hooks_.mouse_move) {
      hooks_.mouse_move(mx, my);
    }
    last_mouse_ = std::array<int, 2>{mx, my};
  }
}

void ChartPanelHost::drag(int mx, int my) {
  // chart_view_drag_panel (0x2b0d6): the panel follows the pointer, kept
  // within the view.
  ChartPanelSlot& s = slots_[static_cast<std::size_t>(active_)];
  const int x = std::clamp(mx - drag_dx_, bounds_.x0, bounds_.x1 - s.width + 1);
  const int y = std::clamp(my - drag_dy_, bounds_.y0, bounds_.y1 - s.height + 1);
  if (x != s.position->x || y != s.position->y) {
    close_panel();
    s.position->x = x;
    s.position->y = y;
    open_panel();
  }
}

void ChartPanelHost::event(const ui::Event& event) {
  if (open_) {
    slots_[static_cast<std::size_t>(active_)].event(event);
  }
}

void ChartPanelHost::process() {
  if (open_) {
    slots_[static_cast<std::size_t>(active_)].process();
  }
}

void ChartPanelHost::update() {
  if (open_) {
    const ChartPanelSlot& s = slots_[static_cast<std::size_t>(active_)];
    if (s.update) {
      s.update();
    }
  }
}

void ChartPanelHost::redraw() {
  if (open_) {
    slots_[static_cast<std::size_t>(active_)].redraw();
  }
}

// --- ChartZoomBox ---------------------------------------------------------------

void ChartZoomBox::grow(const ChartView& view, std::int32_t max_zoom) {
  // chart_zoom_box_grow (0x2bdc7)
  const std::int32_t zoom = view.zoom_int();
  const std::int32_t box = (value_ != 0 ? value_ : zoom) * 2;
  if (box / zoom < 0x41 && box < max_zoom) {
    value_ = box;
  }
}

bool ChartZoomBox::shrink(ChartView& view) {
  // chart_zoom_box_shrink (0x2be2c)
  if (value_ == 0) {
    const std::int32_t zoom = view.zoom();
    view.set_zoom(zoom >> 1);
    shown_box_ = -1;
    return zoom != view.zoom();
  }
  if (view.zoom_int() < value_ >> 1) {
    value_ >>= 1;
  }
  return false;
}

void ChartZoomBox::track(Image8& screen, const ChartView& view, int x, int y) {
  if (!at_ || (*at_)[0] != x || (*at_)[1] != y || shown_box_ != value_) {
    free_magnifier(screen);
    draw_magnifier(screen, view, x, y);
    at_ = std::array<int, 2>{x, y};
    shown_box_ = value_;
  }
}

void ChartZoomBox::reset(Image8& screen) {
  free_magnifier(screen);
  at_.reset();
}

void ChartZoomBox::draw_magnifier(Image8& screen, const ChartView& view, int mx, int my) {
  // chart_view_draw_magnifier (0x2b838): the box of the area the click
  // will show, filled with the screen around the pointer at twice its size
  // and framed in darkened map colours.
  const std::int32_t k = value_ / view.zoom_int();
  const std::int32_t w = (view.x1() - view.x0() + 1) / k;
  const std::int32_t h = (view.y1() - view.y0() + 1) / k;
  const std::int32_t bx0 = mx - w / 2;
  const std::int32_t bx1 = bx0 + w - 1;
  const std::int32_t by0 = my - h / 2;
  const std::int32_t by1 = by0 + h - 1;
  const std::int32_t x0 = std::max(bx0, view.x0());
  const std::int32_t y0 = std::max(by0, view.y0());
  const std::int32_t x1 = std::min(bx1, view.x1());
  const std::int32_t y1 = std::min(by1, view.y1());
  if (x1 < x0 || y1 < y0) {
    return;
  }
  saved_ = Image8(x1 - x0 + 1, y1 - y0 + 1);
  saved_x0_ = x0;
  saved_y0_ = y0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      saved_.set(x - x0, y - y0, screen.at(x, y));
    }
  }
  const std::int32_t sx = std::max(mx - w / 4, view.x0());
  const std::int32_t sy = std::max(my - h / 4, view.y0());
  const std::int32_t cw = x1 - x0 + 1;
  const std::int32_t ch = y1 - y0 + 1;
  std::int32_t dx = x0;
  std::int32_t dy = y0;
  if ((cw & 1) != 0 && x1 != bx1) {
    ++dx;
  }
  if ((ch & 1) != 0 && y1 != by1) {
    ++dy;
  }
  for (std::int32_t j = 0; j < ch / 2; ++j) {
    for (std::int32_t i = 0; i < cw / 2; ++i) {
      const std::uint8_t c = pixel_at(saved_, sx + i - x0, sy + j - y0);
      for (int k2 = 0; k2 < 4; ++k2) {
        const int px = dx + 2 * i + (k2 & 1);
        const int py = dy + 2 * j + (k2 >> 1);
        if (screen.contains(px, py)) {
          screen.set(px, py, c);
        }
      }
    }
  }
  const auto frame_pixel = [&](int x, int y) {
    const std::uint8_t c = pixel_at(saved_, x, y);
    return static_cast<std::uint8_t>((static_cast<int>(c) - 0x80) / 5 + 0xd0);
  };
  const Viewport vp = Viewport::full(screen);
  const auto frame_line = [&](int ax, int ay, int bx, int by) {
    ui::draw_line(vp, ax, ay, bx, by, [&](int x, int y) { screen.set(x, y, frame_pixel(x - x0, y - y0)); });
  };
  for (int inset = 0; inset < 2; ++inset) {
    const int l = x0 + inset;
    const int t = y0 + inset;
    const int r = x1 - inset;
    const int b = y1 - inset;
    frame_line(l, t, r, t);
    frame_line(r, t, r, b);
    frame_line(l, b, r, b);
    frame_line(l, t, l, b);
  }
}

void ChartZoomBox::free_magnifier(Image8& screen) {
  // chart_view_free_magnifier (0x2bd51)
  if (saved_.width() > 0) {
    screen.blit(saved_, saved_x0_, saved_y0_);
    saved_ = Image8();
  }
}

// --- ChartRubberBand ------------------------------------------------------------

void ChartRubberBand::draw(const Viewport& vp, int x0, int y0, int x1, int y1) {
  // chart_course_line_draw (0x2c3a5): an inverted line.
  Image8& screen = *vp.bitmap;
  ui::draw_line(vp, x0, y0, x1, y1, [&](int x, int y) {
    const int sx = vp.x0 + x;
    const int sy = vp.y0 + y;
    screen.set(sx, sy, static_cast<std::uint8_t>(screen.at(sx, sy) ^ 0xff));
  });
  drawn_ = true;
  vp_ = vp;
  x0_ = x0;
  y0_ = y0;
  x1_ = x1;
  y1_ = y1;
}

void ChartRubberBand::erase() {
  // chart_course_line_erase (0x2c36f)
  if (drawn_) {
    draw(vp_, x0_, y0_, x1_, y1_);
    drawn_ = false;
  }
}

// --- Edge scrolling -------------------------------------------------------------

ChartEdges chart_edge_scroll_edges(int x, int y, bool over_panel) {
  const int margin = over_panel ? 1 : kChartEdgeScrollMargin;
  return ChartEdges{
      .left = x < margin,
      .right = x > kScreenWidth - 1 - margin,
      .top = y < margin,
      .bottom = y > kScreenHeight - 1 - margin,
  };
}

void chart_edge_scroll(ChartView& view, ui::Mouse& mouse, bool over_panel, bool& dirty) {
  // chart_view_edge_scroll (0x2aa58): the pointer on a screen edge scrolls
  // 50 / zoom chart pixels and is moved back 30 pixels.
  int x = mouse.x();
  int y = mouse.y();
  const ChartEdges e = chart_edge_scroll_edges(x, y, over_panel);
  if (!e.any()) {
    return;
  }
  const unsigned edges = (e.left ? 1u : 0u) | (e.right ? 2u : 0u) | (e.top ? 4u : 0u) | (e.bottom ? 8u : 0u);
  const std::int32_t step = fx::div(0x320000, view.zoom());
  std::int32_t cx = view.center_x();
  std::int32_t cy = view.center_y();
  const std::int32_t old_x = cx;
  const std::int32_t old_y = cy;
  if ((edges & 1) != 0) {
    cx -= step;
  }
  if ((edges & 2) != 0) {
    cx += step;
  }
  if ((edges & 4) != 0) {
    cy -= step;
  }
  if ((edges & 8) != 0) {
    cy += step;
  }
  view.set_center(cx, cy);
  dirty = dirty || view.center_x() != old_x || view.center_y() != old_y;
  if (dirty) {
    x += (edges & 1) != 0 ? 0x1e : 0;
    x -= (edges & 2) != 0 ? 0x1e : 0;
    y += (edges & 4) != 0 ? 0x1e : 0;
    y -= (edges & 8) != 0 ? 0x1e : 0;
    mouse.update(x, y, mouse.buttons(), 0);
  }
}

// --- The info popup ---------------------------------------------------------------

std::array<int, 2> chart_place_info_popup(int x, int y, int w, int h, std::optional<ui::Rect> avoid) {
  std::int32_t radius = std::max(w, h) * 4 / 5;
  // The starting angles of the quadrants (0x219fc).
  constexpr std::array<std::int32_t, 4> kStart{0x8ca, 0x546, 0xc4e, 0x1c2};
  const std::size_t quadrant = (x < 0x140 ? 1u : 0u) | (y > 0xe5 ? 2u : 0u);
  std::int32_t angle = kStart[quadrant];
  for (int pass = 0, rounds = 0; rounds < 2; ++pass) {
    for (int tries = 0; tries <= 0xb4; ++tries) {
      const fx::Point2 off = fx::point_rotate_scale({0, -radius}, {0, 0}, Angle10(angle), fx::kOne, fx::kOne);
      const int bx = x + off.x - w / 2;
      const int by = y + off.y - h / 2;
      const int bx1 = bx + w - 1;
      const int by1 = by + h - 1;
      const bool on_screen = bx >= 0 && bx1 < 0x280 && by >= 0 && by1 < 0x1cb;
      const bool clear = !avoid || bx1 < avoid->x0 || by1 < avoid->y0 || avoid->x1 < bx || avoid->y1 < by;
      angle = Angle10(angle + 0x14).normalized().tenths();
      if (on_screen && clear) {
        return {bx, by};
      }
    }
    // The 32-bit radius wraps after many rounds, as the original's.
    radius = fx::wadd(radius, radius / 10);
    if (pass >= 0x97) {
      avoid.reset();
      pass = 0;
      ++rounds;
    }
  }
  return {0, 0};
}

void ChartInfoPopup::show(int x, int y, const std::vector<std::string>& lines, std::optional<ui::Rect> avoid) {
  // chart_show_info_popup (0x21a0c) and chart_view_open_popup (0x2b088)
  if (dialog_) {
    for (std::size_t i = 0; i < lines_.size(); ++i) {
      const std::string text = i < lines.size() ? lines[i] : std::string();
      if (lines_[i]->text != text) {
        lines_[i]->text = text;
        lines_[i]->dirty = true;
      }
    }
    return;
  }
  std::int32_t w = 0;
  for (const std::string& l : lines) {
    w = std::max(w, font_.text_width(l));
  }
  w += 0x16;
  const std::int32_t h = static_cast<std::int32_t>(lines.size()) * 0xf + 0x1e;
  const std::array<int, 2> at = chart_place_info_popup(x, y, w, h, avoid);
  dialog_ = std::make_unique<ui::Dialog>(gui_, at[0], at[1], w, h, ui::kDialogFramed, &gui_.screen());
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    lines_[i] = &dialog_->emplace<ui::Label>(0xb, 0xf + 0xf * static_cast<int>(i),
                                             i < lines.size() ? lines[i] : std::string(), ui::FontSlot::kCustom,
                                             true, &font_);
  }
  dialog_->open();
  dialog_->process();
}

bool ChartInfoPopup::close() {
  // chart_view_close_popup (0x2b002)
  if (!dialog_) {
    return false;
  }
  dialog_->close();
  dialog_.reset();
  lines_ = {};
  return true;
}

std::vector<std::string> ChartInfoPopup::text() const {
  std::vector<std::string> lines;
  if (dialog_) {
    for (const ui::Label* l : lines_) {
      if (!l->text.empty()) {
        lines.push_back(l->text);
      }
    }
  }
  return lines;
}

std::array<int, 2> ChartInfoPopup::position() const {
  return dialog_ ? std::array<int, 2>{dialog_->x(), dialog_->y()} : std::array<int, 2>{0, 0};
}

void ChartInfoPopup::repaint() {
  if (dialog_) {
    dialog_->invalidate();
    dialog_->redraw_controls();
  }
}

}  // namespace sh

#include "game/stations/nav_panel.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "assets/game_exe.h"
#include "game/mission.h"
#include "gfx/sprite.h"
#include "sim/geometry.h"
#include "ui/draw.h"
#include "ui/skin.h"

namespace sh {

namespace {

// The templates of `gauges_panel_open`'s callers: compass, telegraph,
// helm, speed, depth, engage button, pitch bubble (0 none), test depth
// position and colour, sprites, digit colours.
struct TemplateAddresses {
  std::uint32_t compass, telegraph, rudder, speed, depth, engage, pitch;
  int text_x, text_y;
  std::uint8_t text_colour;
  const char* sprites;
  const GlyphColourTable& (*digit_colours)();
};

// The counters' tables (see `nav_digit_colours`); the gauges station's is
// the GUI's identity table 0x11dde0.
const GlyphColourTable& gauges_digit_colours() {
  static const GlyphColourTable t = ui::GuiResources::identity_table();
  return t;
}
// bridge_screen (0x1773c); tbt_screen (0x77941) fills its own the same way.
const GlyphColourTable& bridge_digit_colours() {
  static const GlyphColourTable t = patched_colours({{0xc0, 0xa0},
                                                     {0xc2, 0xa1},
                                                     {0xc4, 0xa2},
                                                     {0xc6, 0xa3},
                                                     {0xc8, 0xa4},
                                                     {0xca, 0xa3},
                                                     {0xe0, 0xa0},
                                                     {0xe1, 0xd0},
                                                     {0xe3, 0xd2},
                                                     {0xe5, 0xd5},
                                                     {0xe7, 0xd7},
                                                     {0xe9, 0xd9},
                                                     {0xeb, 0xdb},
                                                     {0xed, 0xdd},
                                                     {0xef, 0xdd},
                                                     {0xff, 0xff}});
  return t;
}
// scope_screen (0x6d0a3)
const GlyphColourTable& scope_digit_colours() {
  static const GlyphColourTable t = patched_colours({{0xc0, 0xdf},
                                                     {0xc2, 0xe0},
                                                     {0xc4, 0xe1},
                                                     {0xc6, 0xe2},
                                                     {0xc8, 0xe3},
                                                     {0xca, 0xe4},
                                                     {0xe1, 0xed},
                                                     {0xe3, 0xef},
                                                     {0xe5, 0xf1},
                                                     {0xe7, 0xf3},
                                                     {0xe9, 0xf5},
                                                     {0xeb, 0xf9},
                                                     {0xed, 0xfd},
                                                     {0xef, 0xfd},
                                                     {0xff, 0xff}});
  return t;
}
// chart_screen (0x20979): 0xff is left 0, so the glyphs' transparent
// pixels are drawn black.
const GlyphColourTable& chart_digit_colours() {
  static const GlyphColourTable t = patched_colours({{0xc0, 0x74},
                                                     {0xc2, 0x73},
                                                     {0xc4, 0x72},
                                                     {0xc6, 0x71},
                                                     {0xc8, 0x70},
                                                     {0xca, 0x71},
                                                     {0xe1, 0x53},
                                                     {0xe3, 0x55},
                                                     {0xe5, 0x57},
                                                     {0xe7, 0x58},
                                                     {0xe9, 0x59},
                                                     {0xeb, 0x5a},
                                                     {0xed, 0x5b},
                                                     {0xef, 0x5c}});
  return t;
}

constexpr TemplateAddresses kGauges{0x108618, 0x108720, 0x108750, 0x1086c8,    0x108670,
                                    0x1085f0, 0x108774, 0x7d,     0x10e,       0xcc,
                                    "GAUGES.SHP", gauges_digit_colours};
constexpr TemplateAddresses kBridge{0x103178, 0x103280, 0x1032b0, 0x103228,     0x1031d0,
                                    0x103150, 0,        0x5b,     0x142,        0xa2,
                                    "BRDGNAV.SHP", bridge_digit_colours};
constexpr TemplateAddresses kChart{0x103580, 0x103688, 0x1036b8, 0x103630,  0x1035d8,
                                   0x1036dc, 0,        -1,       -1,        0xff,
                                   "CHART.SHP", chart_digit_colours};
constexpr TemplateAddresses kScope{0x10d070, 0x10d178, 0x10d1a8, 0x10d120,       0x10d0c8,
                                   0x10d048, 0x10d1cc, 0x60,     0x58,           0xe0,
                                   "SCOPENAV.SHP", scope_digit_colours};
constexpr TemplateAddresses kTbt{0x10edec, 0x10eef4, 0x10ef24, 0x10ee9c,     0x10ee44,
                                 0x10edc4, 0,        0x5b,     0x17b,        0xa2,
                                 "TBTNAV.SHP", bridge_digit_colours};

constexpr std::int32_t kSfxButtonClick = 2;
// The cursor of the dials (`control_hover_cursor`: CURSORS.SHP "SET").
constexpr std::size_t kSetCursor = 6;
// The deepest depth the depth dial shows.
constexpr std::int32_t kDepthDialMax = 600;

// `angle_add` (0x5447c) on raw tenths.
std::int32_t angle_add(std::int32_t a, std::int32_t b) { return fx::angle_add(Angle10(a), Angle10(b)).tenths(); }

std::int32_t angle_sub(std::int32_t a, std::int32_t b) { return fx::angle_sub(Angle10(a), Angle10(b)).tenths(); }

ui::Rect read_rect(const ExeImage& exe, std::uint32_t a) {
  return {exe.i32(a), exe.i32(a + 4), exe.i32(a + 8), exe.i32(a + 12)};
}

// `int_to_padded_str`: `value` with leading zeros to `digits` characters.
std::string padded(std::int32_t value, int digits) {
  std::string s = std::to_string(value);
  if (static_cast<int>(s.size()) < digits) {
    s.insert(0, static_cast<std::size_t>(digits) - s.size(), '0');
  }
  return s;
}

int rect_w(const ui::Rect& r) { return r.x1 - r.x0 + 1; }
int rect_h(const ui::Rect& r) { return r.y1 - r.y0 + 1; }

// Composes a dial: `draw` paints into a scratch copy of `background` under
// `r`, which then goes to `out` at `r` (the original's scratch bitmap,
// `viewport_blit` in and out).
template <class F>
void compose(const Viewport& out, const Image8& background, const ui::Rect& r, F&& draw) {
  Image8 scratch(rect_w(r), rect_h(r), 0);
  for (int y = 0; y < scratch.height(); ++y) {
    for (int x = 0; x < scratch.width(); ++x) {
      if (background.contains(r.x0 + x, r.y0 + y)) {
        scratch.set(x, y, background.at(r.x0 + x, r.y0 + y));
      }
    }
  }
  draw(Viewport::full(scratch));
  if (!out.valid()) {
    return;
  }
  const ClipRect clip = out.clip();
  for (int y = 0; y < scratch.height(); ++y) {
    for (int x = 0; x < scratch.width(); ++x) {
      const int bx = out.x0 + r.x0 + x;
      const int by = out.y0 + r.y0 + y;
      if (clip.contains(bx, by)) {
        out.bitmap->set(bx, by, scratch.at(x, y));
      }
    }
  }
}

void draw_rotated(const Viewport& vp, const Shp& sprites, std::int32_t frame, int x, int y, std::int32_t angle) {
  Image8 scratch;
  sprite_draw_transformed(vp, sprites.frame(static_cast<std::size_t>(frame)), x, y, scratch, Angle10(angle),
                          Fixed16::from_int(1), Fixed16::from_int(1), 0);
}

// `draw_padded_number` (0x4b109).
void draw_number(const Viewport& vp, int x, int y, std::int32_t value, int digits, const Font& font,
                 const GlyphColourTable& colours) {
  gui_draw_text(vp, x, y, font, padded(value, digits), &colours);
}

NavTemplate read_template(const ExeImage& exe, const TemplateAddresses& t) {
  NavTemplate n;
  n.compass = CompassDial::read(exe, t.compass);
  n.telegraph = TelegraphDial::read(exe, t.telegraph);
  n.rudder = RudderDial::read(exe, t.rudder);
  n.speed = NeedleDial::read(exe, t.speed);
  n.depth = NeedleDial::read(exe, t.depth);
  // gauges_panel_open: no cap on either dial.
  n.speed.cap_frame = -1;
  n.depth.cap_frame = -1;
  n.engage = EngageButton::read(exe, t.engage);
  if (t.pitch != 0) {
    n.pitch = PitchDial::read(exe, t.pitch);
  }
  if (t.text_x != -1 && t.text_y != -1) {
    n.test_depth_at = std::array<int, 2>{t.text_x, t.text_y};
  }
  n.test_depth_colour = t.text_colour;
  n.sprites = t.sprites;
  n.digit_colours = t.digit_colours();
  return n;
}

}  // namespace

// --- Templates --------------------------------------------------------------

NeedleDial NeedleDial::read(const ExeImage& exe, std::uint32_t a) {
  NeedleDial d;
  d.cx = exe.i32(a);
  d.cy = exe.i32(a + 4);
  d.digits_x = exe.i32(a + 8);
  d.digits_y = exe.i32(a + 0xc);
  d.arc_start = exe.i32(a + 0x10);
  d.arc_end = exe.i32(a + 0x14);
  d.max = exe.i32(a + 0x18);
  d.digits = exe.i32(a + 0x1c);
  d.rect = read_rect(exe, a + 0x20);
  d.needle_frame = exe.i32(a + 0x34);
  d.marker_frame = exe.i32(a + 0x38);
  d.cap_frame = exe.i32(a + 0x54);
  return d;
}

CompassDial CompassDial::read(const ExeImage& exe, std::uint32_t a) {
  CompassDial d;
  d.cx = exe.i32(a);
  d.cy = exe.i32(a + 4);
  d.digits_x = exe.i32(a + 8);
  d.digits_y = exe.i32(a + 0xc);
  d.lamp_x = exe.i32(a + 0x10);
  d.lamp_y = exe.i32(a + 0x14);
  d.digits = exe.i32(a + 0x18);
  d.rect = read_rect(exe, a + 0x1c);
  d.card_frame = exe.i32(a + 0x30);
  d.ordered_frame = exe.i32(a + 0x34);
  d.lamp_on = exe.i32(a + 0x38);
  d.lamp_off = exe.i32(a + 0x3c);
  return d;
}

TelegraphDial TelegraphDial::read(const ExeImage& exe, std::uint32_t a) {
  TelegraphDial d;
  d.x = exe.i32(a);
  d.y = exe.i32(a + 4);
  d.rect = read_rect(exe, a + 8);
  d.base_frame = exe.i32(a + 0x1c);
  d.mask_frame = exe.i32(a + 0x20);
  return d;
}

RudderDial RudderDial::read(const ExeImage& exe, std::uint32_t a) {
  RudderDial d;
  d.rect = read_rect(exe, a);
  for (std::uint32_t i = 0; i < 3; ++i) {
    d.from[i] = exe.u8(a + 0x10 + 2 * i);
    d.to[i] = exe.u8(a + 0x11 + 2 * i);
  }
  return d;
}

EngageButton EngageButton::read(const ExeImage& exe, std::uint32_t a) {
  EngageButton b;
  b.rect = read_rect(exe, a);
  b.frame = exe.i32(a + 0x14);
  b.pressed_frame = exe.i32(a + 0x18);
  b.repeat = exe.i32(a + 0x1c);
  return b;
}

PitchDial PitchDial::read(const ExeImage& exe, std::uint32_t a) {
  PitchDial d;
  d.x = exe.i32(a);
  d.y = exe.i32(a + 4);
  d.rect = read_rect(exe, a + 0xc);
  d.glass_frame = exe.i32(a + 0x20);
  d.bubble_frame = exe.i32(a + 0x24);
  return d;
}

NavTemplate nav_template(const ExeImage& exe, NavPanelSite site) {
  switch (site) {
    case NavPanelSite::kBridge:
      return read_template(exe, kBridge);
    case NavPanelSite::kScope:
      return read_template(exe, kScope);
    case NavPanelSite::kTbt:
      return read_template(exe, kTbt);
    case NavPanelSite::kChart:
      break;
  }
  return read_template(exe, kChart);
}

NavTemplate gauges_nav_template(const ExeImage& exe) { return read_template(exe, kGauges); }

const GlyphColourTable& nav_digit_colours(NavPanelSite site) {
  switch (site) {
    case NavPanelSite::kBridge:
    case NavPanelSite::kTbt:
      return bridge_digit_colours();
    case NavPanelSite::kScope:
      return scope_digit_colours();
    case NavPanelSite::kChart:
      break;
  }
  return chart_digit_colours();
}

// --- Dial arithmetic --------------------------------------------------------

std::int32_t needle_angle(const NeedleDial& dial, std::int32_t value) {
  // needle_dial_draw (0x49866)
  const std::int32_t span = angle_add(angle_sub(dial.arc_end * 10, dial.arc_start * 10), 10);
  const std::int32_t per_unit = span * 100 / dial.max;
  return angle_add(value * per_unit / 100, dial.arc_start * 10);
}

std::int32_t needle_value_at(const NeedleDial& dial, std::int32_t angle) {
  // needle_dial_proc (0x4978c)
  const std::int32_t span = angle_add(angle_sub(dial.arc_end * 10, dial.arc_start * 10), 10);
  std::int32_t v = angle_sub(angle, dial.arc_start * 10) * 100 / (span * 100 / dial.max);
  if (v > dial.max) {
    v = angle < 1800 ? dial.max : 0;
  }
  return v;
}

std::int32_t dial_angle(int x, int y, int cx, int cy) {
  return angle_add(fx::heading_from_delta(y - cy - 1, x - cx - 1).tenths(), 900);
}

std::int32_t telegraph_frame(const TelegraphDial& dial, std::int32_t order) {
  // telegraph_dial_draw (0x4a468)
  return dial.base_frame + (order == 10 ? 1 : order);
}

std::array<std::array<int, 2>, 15> rudder_zones() {
  // int32 {x, width}[15] at 0x109330.
  constexpr std::uint32_t kRudderZones = 0x109330;
  const ExeImage& exe = game_exe();
  std::array<std::array<int, 2>, 15> zones{};
  for (std::uint32_t i = 0; i < zones.size(); ++i) {
    zones[i] = {exe.i32(kRudderZones + 8 * i), exe.i32(kRudderZones + 8 * i + 4)};
  }
  return zones;
}

std::optional<std::int32_t> rudder_step_at(int x) {
  // rudder_dial_proc (0x4a666)
  const auto zones = rudder_zones();
  for (std::size_t i = 0; i < zones.size(); ++i) {
    if (x >= zones[i][0] && x < zones[i][0] + zones[i][1]) {
      return static_cast<std::int32_t>(i) - 7;
    }
  }
  return std::nullopt;
}

std::optional<BubblePlace> pitch_bubble(std::int32_t tenths) {
  // 0x4a985 with the table 0x1093a8: int32 {x, y}[16] by whole degree.
  constexpr std::uint32_t kBubblePlaces = 0x1093a8;
  std::int32_t degrees = (tenths + 5) / 10;
  bool down = false;
  if (degrees > 15) {
    degrees = 360 - degrees;
    down = true;
  }
  if (degrees > 15) {
    return std::nullopt;
  }
  const ExeImage& exe = game_exe();
  const std::uint32_t e = kBubblePlaces + 8 * static_cast<std::uint32_t>(degrees);
  const std::int32_t x = exe.i32(e);
  return BubblePlace{down ? -x : x, exe.i32(e + 4), degrees * 30};
}

void draw_needle_dial(const Viewport& out, const Image8& background, const NeedleDial& dial, const Shp& sprites,
                      const Font& digits_font, const GlyphColourTable& colours, std::int32_t value,
                      std::optional<std::int32_t> ordered, std::int32_t digit_value) {
  // needle_dial_draw (0x49866)
  compose(out, background, dial.rect, [&](const Viewport& vp) {
    if (dial.digits != 0) {
      const std::int32_t v = dial.digits < 0 ? digit_value / 10 : digit_value;
      draw_number(vp, dial.digits_x - dial.rect.x0, dial.digits_y - dial.rect.y0, v, std::abs(dial.digits),
                  digits_font, colours);
    }
    const int px = dial.cx - dial.rect.x0;
    const int py = dial.cy - dial.rect.y0;
    if (ordered) {
      draw_rotated(vp, sprites, dial.marker_frame, px, py, needle_angle(dial, *ordered));
    }
    draw_rotated(vp, sprites, dial.needle_frame, px, py, needle_angle(dial, value));
    if (dial.cap_frame != -1) {
      draw_sprite_clipped(vp, sprites.frame(static_cast<std::size_t>(dial.cap_frame)), px, py);
    }
  });
}

// --- The panel --------------------------------------------------------------

// The dial controls (an area of the control vtable 0x101afc whose message
// proc is the dial's).
class NavPanel::DialControl : public ui::Control {
 public:
  using Handler = std::function<bool(ui::Dialog&, ui::Msg, const ui::Event&, bool pressed)>;
  DialControl(ui::Rect rect, bool set_cursor, StationEnv& env, Handler handler)
      : rect_(rect), set_cursor_(set_cursor), env_(env), handler_(std::move(handler)) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event& event) override {
    if (msg == ui::Msg::kRegister) {
      ui::add_control_hotspot(dialog, *this, rect_, 0, 0, ui::kFocusNone);
    } else if (msg == ui::Msg::kNone || static_cast<int>(msg) > static_cast<int>(ui::Msg::kPaint)) {
      return false;
    }
    return handler_(dialog, msg, event, pressed);
  }
  void hover(bool on, int, const ui::Event&) override {
    // control_hover_cursor (0x4b839), or `gui_nop`.
    if (!set_cursor_) {
      return;
    }
    if (on) {
      env_.set_cursor(kSetCursor);
    } else {
      env_.default_cursor();
    }
  }

 private:
  ui::Rect rect_;
  bool set_cursor_;
  StationEnv& env_;
  Handler handler_;
};

NavPanel::NavPanel(StationEnv& env, NavTemplate layout, std::optional<NavPanelFrame> frame)
    : env_(env), layout_(std::move(layout)), frame_(std::move(frame)) {}

NavPanel::~NavPanel() = default;

void NavPanel::open() {
  // gauges_panel_open (0x39ed5)
  Image8& screen = env_.screen();
  sprites_ = Shp::load(env_.install, env_.paths.path(GameDir::kArt, layout_.sprites));
  const SpriteFrame& mask = sprites_.frame(static_cast<std::size_t>(layout_.telegraph.mask_frame));
  if (mask.width() > rect_w(layout_.telegraph.rect) || mask.height() > rect_h(layout_.telegraph.rect)) {
    throw DataError("the telegraph click map is larger than the telegraph");  // fatal 100
  }
  telegraph_mask_ = Image8(mask.width(), mask.height(), 0);
  draw_sprite_clipped(Viewport::full(telegraph_mask_), mask, 0, 0);
  const PlayerSub& player = env_.mission.player();
  if (layout_.test_depth_at) {
    // The test depth, in BEARING.FNT on the station picture (a stack table
    // of which only 0xe0 and 0xff are set: the glyphs use these two).
    GlyphColourTable table = ui::GuiResources::identity_table();
    table[0xe0] = layout_.test_depth_colour;
    table[0xff] = 0xff;
    gui_draw_text(Viewport::full(screen), (*layout_.test_depth_at)[0], (*layout_.test_depth_at)[1],
                  env_.assets.bearing, std::to_string(player.sub().test_depth_ft), &table);
  }
  ordered_depth_ = player.dive().ordered_depth_ft;
  shown_depth_ = std::min(player.depth_ft(), kDepthDialMax);

  const int dx = frame_ ? frame_->x : 0;
  const int dy = frame_ ? frame_->y : 0;
  const int dw = frame_ ? frame_->w : 0x27f;
  const int dh = frame_ ? frame_->h : 0x1df;
  dialog_ = std::make_unique<ui::Dialog>(env_.gui, dx, dy, dw, dh, frame_ ? frame_->flags : 0u, &screen);
  if (frame_ && frame_->add_controls) {
    frame_->add_controls(*dialog_);
  }
  if (frame_ && frame_->background != nullptr) {
    background_ = *frame_->background;
  } else {
    background_ = Image8(dw + 1, dh + 1, 0);
    for (int y = 0; y <= dh; ++y) {
      for (int x = 0; x <= dw; ++x) {
        if (screen.contains(dx + x, dy + y)) {
          background_.set(x, y, screen.at(dx + x, dy + y));
        }
      }
    }
  }

  const NavTemplate& t = layout_;
  // The compass (`compass_dial_proc` 0x49c1d).
  controls_[0] = &dialog_->emplace<DialControl>(
      t.compass.rect, true, env_, [this](ui::Dialog& d, ui::Msg msg, const ui::Event& e, bool pressed) {
        if (msg == ui::Msg::kPress) {
          if (pressed) {
            return false;
          }
          PlayerSub& p = env_.mission.player();
          p.stop_following_route();
          const std::int32_t course =
              dial_angle(e.x, e.y, layout_.compass.cx + d.x(), layout_.compass.cy + d.y());
          const bool changed = course != p.motion().ordered_heading.tenths();
          p.set_course(Angle10(course));
          if (!changed) {
            return false;
          }
        } else if (msg != ui::Msg::kPaint) {
          return false;
        }
        draw_compass(d.gfx());
        d.add_dirty(layout_.compass.rect);
        return true;
      });
  // The telegraph (`telegraph_dial_proc` 0x4a3e3).
  controls_[1] = &dialog_->emplace<DialControl>(
      t.telegraph.rect, true, env_, [this](ui::Dialog& d, ui::Msg msg, const ui::Event& e, bool pressed) {
        if (msg == ui::Msg::kPress) {
          if (pressed) {
            return false;
          }
          const int mx = e.x - d.x() - layout_.telegraph.rect.x0;
          const int my = e.y - d.y() - layout_.telegraph.rect.y0;
          // Outside the click map the original reads an error code; such a
          // click is ignored here.
          if (!telegraph_mask_.contains(mx, my) || telegraph_mask_.at(mx, my) == 0) {
            return false;
          }
          PlayerSub& p = env_.mission.player();
          const std::int32_t order = telegraph_mask_.at(mx, my) - 1;
          if (order == p.telegraph()) {
            return false;
          }
          p.set_telegraph(order);
        } else if (msg != ui::Msg::kPaint) {
          return false;
        }
        draw_telegraph(d.gfx());
        d.add_dirty(layout_.telegraph.rect);
        return true;
      });
  // The helm (`rudder_dial_proc` 0x4a666).
  controls_[2] = &dialog_->emplace<DialControl>(
      t.rudder.rect, true, env_, [this](ui::Dialog& d, ui::Msg msg, const ui::Event& e, bool) {
        if (msg == ui::Msg::kPress) {
          const std::optional<std::int32_t> step = rudder_step_at(e.x - layout_.rudder.rect.x0 - d.x());
          if (!step) {
            return false;
          }
          // Leaves course mode and the route whether or not the step changes.
          PlayerSub& p = env_.mission.player();
          p.stop_following_route();
          const bool changed = *step != p.helm();
          p.set_helm(*step);
          if (!changed) {
            return false;
          }
        } else if (msg != ui::Msg::kPaint) {
          return false;
        }
        draw_rudder(d.gfx());
        d.add_dirty(layout_.rudder.rect);
        return true;
      });
  // The speed dial: shown only (no ordered value).
  controls_[3] = &dialog_->emplace<DialControl>(t.speed.rect, false, env_,
                                                [this](ui::Dialog& d, ui::Msg msg, const ui::Event&, bool) {
                                                  if (msg != ui::Msg::kPaint) {
                                                    return false;
                                                  }
                                                  draw_speed(d.gfx());
                                                  d.add_dirty(layout_.speed.rect);
                                                  return true;
                                                });
  // The depth dial (`needle_dial_proc`): a click orders a depth.
  controls_[4] = &dialog_->emplace<DialControl>(
      t.depth.rect, true, env_, [this](ui::Dialog& d, ui::Msg msg, const ui::Event& e, bool pressed) {
        if (msg == ui::Msg::kPress) {
          if (pressed) {
            return false;
          }
          const std::int32_t v = needle_value_at(
              layout_.depth, dial_angle(e.x, e.y, layout_.depth.cx + d.x(), layout_.depth.cy + d.y()));
          if (v == ordered_depth_) {
            return false;
          }
          ordered_depth_ = v;
        } else if (msg != ui::Msg::kPaint) {
          return false;
        }
        draw_depth(d.gfx());
        d.add_dirty(layout_.depth.rect);
        return true;
      });
  if (t.engage) {
    // The course engage button (`cycle_button_proc` 0x49472).
    controls_[5] = &dialog_->emplace<DialControl>(
        t.engage->rect, true, env_, [this](ui::Dialog& d, ui::Msg msg, const ui::Event& e, bool pressed) {
          switch (msg) {
            case ui::Msg::kRegister:
              engage_count_ = 0;
              return false;
            case ui::Msg::kPress:
              d.set_repeat_interval((e.flags_high & 0x8000) != 0 ? 0 : static_cast<std::uint32_t>(layout_.engage->repeat));
              draw_engage(d.gfx(), true);
              if (!pressed) {
                env_.sound.play_sfx(kSfxButtonClick, 0x1e);
                ++engage_count_;
                engage();
              }
              return true;
            case ui::Msg::kRelease:
            case ui::Msg::kPaint:
              engage_count_ = 0;
              draw_engage(d.gfx(), false);
              return true;
            default:
              return false;
          }
        });
  }
  if (t.pitch) {
    controls_[6] = &dialog_->emplace<DialControl>(t.pitch->rect, false, env_,
                                                  [this](ui::Dialog& d, ui::Msg msg, const ui::Event&, bool) {
                                                    if (msg != ui::Msg::kPaint) {
                                                      return false;
                                                    }
                                                    draw_pitch(d.gfx());
                                                    d.add_dirty(layout_.pitch->rect);
                                                    return true;
                                                  });
  }
  const MotionState& m = player.motion();
  last_heading_ = m.heading.tenths();
  last_course_ = m.ordered_heading.tenths();
  last_course_mode_ = m.steering;
  last_telegraph_ = player.telegraph();
  last_helm_ = player.helm();
  last_speed_ = m.speed_tenths;
  last_log_ = m.log_miles;
  last_ordered_ = ordered_depth_;
  last_depth_ = player.depth_ft();
  last_pitch_ = m.pitch.tenths();
  dialog_->open();
}

void NavPanel::event(const ui::Event& event) { dialog_->handle_event(event); }

void NavPanel::process() { dialog_->process(); }

void NavPanel::close() {
  dialog_->close();
  dialog_.reset();
  controls_.fill(nullptr);
}

void NavPanel::update(bool full_redraw) {
  // gauges_panel_update (0x3a456)
  PlayerSub& p = env_.mission.player();
  const MotionState& m = p.motion();
  if (full_redraw) {
    dialog_->invalidate();
  }
  if (last_heading_ != m.heading.tenths()) {
    controls_[0]->dirty = true;
    last_heading_ = m.heading.tenths();
  }
  if (last_course_ != m.ordered_heading.tenths()) {
    controls_[0]->dirty = true;
    last_course_ = m.ordered_heading.tenths();
    if (m.steering && !p.following_route()) {
      env_.sound.play_speech("HELM" + padded(last_course_ / 10, 4) + ".RAW");
    }
  }
  if (last_course_mode_ != m.steering) {
    controls_[0]->dirty = true;
    last_course_mode_ = m.steering;
  }
  if (last_telegraph_ != p.telegraph()) {
    controls_[1]->dirty = true;
    last_telegraph_ = p.telegraph();
  }
  if (last_helm_ != p.helm()) {
    controls_[2]->dirty = true;
    last_helm_ = p.helm();
  }
  if (last_speed_ != m.speed_tenths || last_log_ != m.log_miles) {
    last_speed_ = m.speed_tenths;
    controls_[3]->dirty = true;
    last_log_ = m.log_miles;
  }
  if (last_ordered_ != ordered_depth_) {
    // A depth ordered on the dial goes to the boat (`player_set_ordered_depth`).
    controls_[4]->dirty = true;
    p.set_ordered_depth(ordered_depth_);
    ordered_depth_ = p.dive().ordered_depth_ft;
    last_ordered_ = ordered_depth_;
  }
  if (last_depth_ != p.depth_ft()) {
    controls_[4]->dirty = true;
    last_depth_ = p.depth_ft();
    shown_depth_ = std::min(p.depth_ft(), kDepthDialMax);
  }
  if (ordered_depth_ != p.dive().ordered_depth_ft) {
    ordered_depth_ = p.dive().ordered_depth_ft;
  }
  if (shown_depth_ != p.depth_ft()) {
    shown_depth_ = std::min(p.depth_ft(), kDepthDialMax);
  }
  if (last_pitch_ != m.pitch.tenths()) {
    if (controls_[6] != nullptr) {
      controls_[6]->dirty = true;
    }
    last_pitch_ = m.pitch.tenths();
  }
}

void NavPanel::engage() {
  // gauges_compass_engage (0x3a6ea): back on the plotted route, or course
  // mode on the ordered course.
  PlayerSub& p = env_.mission.player();
  if (p.route().count != 0) {
    p.resume_route();
  } else {
    p.set_course(p.motion().ordered_heading);
  }
}

void NavPanel::draw_compass(const Viewport& out) {
  // compass_dial_draw (0x49cbe)
  const CompassDial& c = layout_.compass;
  const MotionState& m = env_.mission.player().motion();
  compose(out, background_, c.rect, [&](const Viewport& vp) {
    const std::int32_t heading = m.heading.tenths();
    draw_number(vp, c.digits_x - c.rect.x0, c.digits_y - c.rect.y0, c.digits < 0 ? heading / 10 : heading,
                std::abs(c.digits), env_.assets.digits, layout_.digit_colours);
    const std::int32_t lamp = m.steering ? c.lamp_on : c.lamp_off;
    if (lamp != -1) {
      draw_sprite_clipped(vp, sprites_.frame(static_cast<std::size_t>(lamp)), c.lamp_x - c.rect.x0,
                          c.lamp_y - c.rect.y0);
    }
    draw_rotated(vp, sprites_, c.ordered_frame, c.cx - c.rect.x0, c.cy - c.rect.y0, m.ordered_heading.tenths());
    draw_rotated(vp, sprites_, c.card_frame, c.cx - c.rect.x0, c.cy - c.rect.y0, heading);
  });
}

void NavPanel::draw_telegraph(const Viewport& out) {
  const TelegraphDial& t = layout_.telegraph;
  const std::int32_t frame = telegraph_frame(t, env_.mission.player().telegraph());
  compose(out, background_, t.rect, [&](const Viewport& vp) {
    draw_sprite_clipped(vp, sprites_.frame(static_cast<std::size_t>(frame)), t.x - t.rect.x0, t.y - t.rect.y0);
  });
}

void NavPanel::draw_rudder(const Viewport& out) {
  // rudder_dial_draw (0x4a708): the selected zone's three colours swapped.
  const RudderDial& r = layout_.rudder;
  const std::int32_t helm = std::clamp(env_.mission.player().helm(), -7, 7);
  const auto zone = rudder_zones()[static_cast<std::size_t>(helm + 7)];
  compose(out, background_, r.rect, [&](const Viewport& vp) {
    for (int x = zone[0]; x < zone[0] + zone[1]; ++x) {
      for (int y = 0; y < rect_h(r.rect); ++y) {
        if (!vp.bitmap->contains(x, y)) {
          continue;
        }
        const std::uint8_t c = vp.bitmap->at(x, y);
        for (std::size_t k = 0; k < 3; ++k) {
          if (c == r.from[k]) {
            vp.bitmap->set(x, y, r.to[k]);
            break;
          }
        }
      }
    }
  });
}

void NavPanel::draw_speed(const Viewport& out) {
  const MotionState& m = env_.mission.player().motion();
  draw_needle_dial(out, background_, layout_.speed, sprites_, env_.assets.digits, layout_.digit_colours, m.speed_tenths,
                   std::nullopt, m.log_miles);
}

void NavPanel::draw_depth(const Viewport& out) {
  // The ordered depth's needle shows only when one is ordered.
  draw_needle_dial(out, background_, layout_.depth, sprites_, env_.assets.digits, layout_.digit_colours, shown_depth_,
                   ordered_depth_ >= 0 ? std::optional<std::int32_t>(ordered_depth_) : std::nullopt, shown_depth_);
}

void NavPanel::draw_engage(const Viewport& out, bool pressed) {
  // cycle_button_draw (0x4951e): onto the station picture itself.
  const EngageButton& b = *layout_.engage;
  draw_sprite_clipped(out, sprites_.frame(static_cast<std::size_t>(pressed ? b.pressed_frame : b.frame)), b.rect.x0,
                      b.rect.y0);
}

void NavPanel::draw_pitch(const Viewport& out) {
  // 0x4a985: the bubble and the glass over it, within 15 degrees.
  const PitchDial& d = *layout_.pitch;
  const std::optional<BubblePlace> place = pitch_bubble(env_.mission.player().motion().pitch.normalized().tenths());
  compose(out, background_, d.rect, [&](const Viewport& vp) {
    if (place) {
      draw_rotated(vp, sprites_, d.bubble_frame, place->dx + d.x - d.rect.x0, place->dy + d.y - d.rect.y0,
                   place->angle);
      draw_sprite_clipped(vp, sprites_.frame(static_cast<std::size_t>(d.glass_frame)), 0, 0);
    }
  });
}

std::unique_ptr<NavPanel> make_nav_panel(StationEnv& env, NavPanelSite site, std::optional<NavPanelFrame> frame) {
  if (site == NavPanelSite::kChart && !frame) {
    frame = NavPanelFrame{};
  }
  return std::make_unique<NavPanel>(env, nav_template(env.exe, site), std::move(frame));
}

}  // namespace sh

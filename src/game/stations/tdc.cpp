#include "game/stations/tdc.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>

#include "assets/game_exe.h"
#include "assets/pcx.h"
#include "core/error.h"
#include "core/fixed_math.h"
#include "core/install.h"
#include "game/mission.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "sim/geometry.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The definitions of the panel controls (the static tables the panel
// openers receive: 0x10f59c.. on the TDC station, 0x10d1fc.. on the scope,
// 0x10ef48.. on the TBT). Coordinates are relative to the panel dialogs,
// which cover the screen from row `y` down. Frames index the panel sprites
// (TBTTDC.SHP, or SCOPETDC.SHP on the scope).

struct Box {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

// A rotating knob (the def of `knob_dial_proc` / `knob_dial_draw`): centre,
// digits position and count (negative: the value / 10), the redrawn box,
// knob, cap and underlay frames.
struct KnobDef {
  int cx = 0;
  int cy = 0;
  int digits_x = 0;
  int digits_y = 0;
  int digits = 0;
  Box box;
  std::int32_t knob_frame = 0;
  std::int32_t cap_frame = -1;
  std::int32_t under_frame = 0;
};

// A digit counter (`digit_counter_proc`): click box, text position, digits.
struct CounterDef {
  Box box;
  int text_x = 0;
  int text_y = 0;
  int digits = 0;
};

// A sprite button: click box, first frame (push buttons: + state; track
// button: `frame` on, `frame2` off).
struct ButtonDef {
  Box box;
  std::int32_t frame = 0;
  std::int32_t frame2 = 0;
};

// A needle dial (`needle_dial_proc`, and the run-time dial of
// `tdc_draw_run_timer`): centre, digits, the arc in degrees and the value
// at its end, the redrawn box, needle, marker and cap frames.
struct NeedleDef {
  int cx = 0;
  int cy = 0;
  int digits_x = 0;
  int digits_y = 0;
  int arc_start = 0;
  int arc_end = 0;
  int max = 0;
  int digits = 0;
  Box box;
  std::int32_t needle_frame = 0;
  std::int32_t marker_frame = 0;
  std::int32_t cap_frame = -1;
};

struct SiteLayout {
  int y = 0;  // the panel dialogs' top row
  std::array<Box, 6> bow_tubes;
  std::array<Box, 4> stern_tubes;
  ButtonDef mode;
  KnobDef bearing;
  KnobDef course;
  CounterDef range;
  CounterDef speed;
  ButtonDef track;
  Box quality;
  KnobDef gyro_bow;
  KnobDef gyro_stern;
  KnobDef spread;
  NeedleDef depth;
  ButtonDef torpedo_speed;
  ButtonDef torpedo_type;
  NeedleDef run_timer;
};

// Reads the control definitions of a site from SH.EXE: the static tables
// `tdc_tube_panel_open`, `tdc_solution_panel_open`, `tdc_gyro_panel_open`
// and `tdc_torpedo_panel_open` receive, at fixed offsets from the first
// one (int32 fields; the sprite and value pointers they also hold are set
// at run time here). Throws `DataError` when a box is off the screen.
class LayoutReader {
 public:
  LayoutReader(const ExeImage& exe, std::uint32_t base) : exe_(exe), base_(base) {}

  Box box(std::uint32_t offset) const {
    const Box b{at(offset), at(offset + 4), at(offset + 8), at(offset + 12)};
    if (b.x0 < 0 || b.y0 < 0 || b.x1 < b.x0 || b.y1 < b.y0 || b.x1 >= kScreenWidth || b.y1 >= kScreenHeight) {
      throw DataError(std::format("SH.EXE: TDC panel box at {:#x} off the screen", base_ + offset));
    }
    return b;
  }
  // {box, sprites*, frame, frame2}
  ButtonDef button(std::uint32_t offset) const { return {box(offset), at(offset + 20), at(offset + 24)}; }
  // {cx, cy, digits x, y, digits, box, sprites*, knob, cap, underlay, ...}
  KnobDef knob(std::uint32_t offset) const {
    return {at(offset), at(offset + 4), at(offset + 8), at(offset + 12), at(offset + 16), box(offset + 20),
            at(offset + 40), at(offset + 44), at(offset + 48)};
  }
  // {box, text x, y, 0, digits, ...}
  CounterDef counter(std::uint32_t offset) const {
    return {box(offset), at(offset + 16), at(offset + 20), at(offset + 28)};
  }
  // {cx, cy, digits x, y, arc start, end, max, digits, box, sprites*,
  // needle, marker, ..., cap (+0x54)}
  NeedleDef needle(std::uint32_t offset) const {
    return {at(offset),      at(offset + 4),  at(offset + 8),  at(offset + 12), at(offset + 16),
            at(offset + 20), at(offset + 24), at(offset + 28), box(offset + 32), at(offset + 52),
            at(offset + 56), at(offset + 0x54)};
  }

 private:
  int at(std::uint32_t offset) const { return exe_.i32(base_ + offset); }

  const ExeImage& exe_;
  std::uint32_t base_;
};

// Returns the layout whose tables start at `base` (the tubes, 10 x 0x28
// bytes), its dialogs from row `y`.
SiteLayout read_layout(const ExeImage& exe, std::uint32_t base, int y) {
  const LayoutReader r(exe, base);
  SiteLayout l;
  l.y = y;
  for (std::uint32_t i = 0; i < 6; ++i) {
    l.bow_tubes[i] = r.box(0x28 * i);
  }
  for (std::uint32_t i = 0; i < 4; ++i) {
    l.stern_tubes[i] = r.box(0x28 * (6 + i));
  }
  l.mode = r.button(0x190);
  l.bearing = r.knob(0x1ac);
  l.course = r.knob(0x1f8);
  l.range = r.counter(0x244);
  l.speed = r.counter(0x270);
  l.track = r.button(0x29c);
  l.quality = r.box(0x2c4);
  l.gyro_bow = r.knob(0x2e0);
  l.gyro_stern = r.knob(0x32c);
  l.spread = r.knob(0x378);
  l.depth = r.needle(0x3c4);
  l.torpedo_speed = r.button(0x41c);
  l.torpedo_type = r.button(0x438);
  l.run_timer = r.needle(0x454);
  return l;
}

// tdc_screen (0x79f00) with the defs 0x10f59c.. 0x10f9f0, scope_load_overlays
// (0x6d4d9) mode 1 with 0x10d1fc.. 0x10d650, tbt_load_overlays (0x77d2d)
// mode 1 with 0x10ef48.. 0x10f39c and its dialogs from row 0x101.
SiteLayout layout_of(TdcPanelSite site) {
  const ExeImage& exe = game_exe();
  switch (site) {
    case TdcPanelSite::kScope:
      return read_layout(exe, 0x10d1fc, 0);
    case TdcPanelSite::kTbt:
      return read_layout(exe, 0x10ef48, 0x101);
    case TdcPanelSite::kTdc:
      break;
  }
  return read_layout(exe, 0x10f59c, 0);
}

// The glyph colour tables of the digits (BSS tables patched by
// `tdc_screen`, `scope_screen` and `tbt_screen`).
GlyphColourTable colours_of(TdcPanelSite site) {
  if (site == TdcPanelSite::kScope) {
    return patched_colours({{0xc0, 0xdf},
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
  }
  GlyphColourTable t = patched_colours({{0xc0, 0xa0},
                                        {0xc2, 0xa1},
                                        {0xc4, 0xa2},
                                        {0xc6, 0xa3},
                                        {0xc8, 0xa4},
                                        {0xca, 0xa3},
                                        {0xe1, 0xd0},
                                        {0xe3, 0xd2},
                                        {0xe5, 0xd5},
                                        {0xe7, 0xd7},
                                        {0xe9, 0xd9},
                                        {0xeb, 0xdb},
                                        {0xed, 0xdd},
                                        {0xef, 0xdd},
                                        {0xff, 0xff}});
  if (site == TdcPanelSite::kTbt) {
    t[0xe0] = 0xa0;
  }
  return t;
}

// Returns `value` as `width` digits (`int_to_padded_str`, 0x5908d):
// negative values print as 0, at most 10 digits.
std::string padded_number(std::int32_t value, int width) {
  width = std::min(width, 10);
  value = std::max(value, 0);
  std::string s(static_cast<std::size_t>(std::max(width, 0)), '0');
  std::int32_t power = 1;
  for (int i = width - 1; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = static_cast<char>('0' + value / power % 10);
    power *= 10;
  }
  return s;
}

// Draws `value` as `width` digits at (x, y) of `vp` (`draw_padded_number`,
// 0x4b109).
void draw_padded_number(const Viewport& vp, int x, int y, std::int32_t value, int width, const Font& font,
                        const GlyphColourTable& colours) {
  for (const char c : padded_number(value, width)) {
    x += std::max(font_draw_char(vp, x, y, font, static_cast<std::uint8_t>(c), &colours), 0);
  }
}

// What the panel controls draw with and act on.
struct TdcPanelContext {
  AttackServices& services;
  TdcPanelSite site;
  SiteLayout layout;
  Shp sprites;      // TBTTDC.SHP / SCOPETDC.SHP
  Font digits;      // DIGITS.FNT (0x11a260)
  Font big_digits;  // DIGITS3.FNT
  GlyphColourTable colours{};
  // The screen as the panels' sprites left it (the back buffer 0x11a574
  // the composited dials copy their box from).
  Image8 background;
  Image8 scratch;
  // The spread dial's value (0x129b78: spread x 3600 / 400).
  std::int32_t spread_dial = 0;

  Mission& mission() { return services.mission(); }
  Tdc& tdc() { return services.mission().player_combat().tdc; }

  // Returns a buffer of `box` (dialog coordinates) filled from the
  // background (the `viewport_blit` that starts each dial redraw).
  Image8 box_buffer(const Box& b, int dialog_y) const {
    Image8 buf(b.x1 - b.x0 + 1, b.y1 - b.y0 + 1);
    for (int y = 0; y < buf.height(); ++y) {
      for (int x = 0; x < buf.width(); ++x) {
        const int sx = b.x0 + x;
        const int sy = dialog_y + b.y0 + y;
        buf.set(x, y, background.contains(sx, sy) ? background.at(sx, sy) : 0);
      }
    }
    return buf;
  }
  // Draws `frame` rotated by `angle` with its hotspot at (x, y) of `vp`.
  void draw_rotated(const Viewport& vp, std::int32_t frame, int x, int y, std::int32_t angle) {
    if (frame < 0 || static_cast<std::size_t>(frame) >= sprites.frame_count()) {
      return;
    }
    sprite_draw_transformed(vp, sprites.frame(static_cast<std::size_t>(frame)), x, y, scratch, Angle10(angle),
                            Fixed16::from_int(1), Fixed16::from_int(1), 0);
  }
  void draw_frame(const Viewport& vp, std::int32_t frame, int x, int y) const {
    if (frame < 0 || static_cast<std::size_t>(frame) >= sprites.frame_count()) {
      return;
    }
    draw_sprite_clipped(vp, sprites.frame(static_cast<std::size_t>(frame)), x, y);
  }
  // Copies a dial buffer to the screen at its box and marks it dirty.
  void present(ui::Dialog& dialog, const Image8& buf, const Box& b) {
    services.screen().blit(buf, dialog.x() + b.x0, dialog.y() + b.y0);
    dialog.add_dirty({b.x0, b.y0, b.x1, b.y1});
  }
  void hover_cursor(bool on, ui::CursorId id) {
    if (on) {
      services.set_cursor(id);
    } else {
      services.set_default_cursor();
    }
  }
  void click_sound() { services.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume); }
};

// The base of the panel controls: one hotspot over `box`, no focus.
class PanelControl : public ui::Control {
 public:
  PanelControl(TdcPanelContext& ctx, Box box, ui::CursorId cursor) : ctx_(ctx), box_(box), cursor_(cursor) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int sub, const ui::Event& event) override {
    if (msg == ui::Msg::kRegister) {
      ui::add_control_hotspot(dialog, *this, {box_.x0, box_.y0, box_.x1, box_.y1}, 0, 0, ui::kFocusNone);
      return false;
    }
    return message(dialog, msg, sub, event);
  }
  bool is_disabled(int) const override { return disabled_ && disabled_(); }
  void hover(bool on, int, const ui::Event&) override {
    if (has_cursor_) {
      ctx_.hover_cursor(on, cursor_);
    }
  }

  // Disables the control while `fn` returns true (the manual-only dials).
  void set_disabled_when(std::function<bool()> fn) { disabled_ = std::move(fn); }
  void without_cursor() { has_cursor_ = false; }

 protected:
  virtual bool message(ui::Dialog& dialog, ui::Msg msg, int sub, const ui::Event& event) = 0;

  TdcPanelContext& ctx_;
  Box box_;

 private:
  ui::CursorId cursor_;
  bool has_cursor_ = true;
  std::function<bool()> disabled_;
};

// A tube button (`tdc_tube_button_handler`, 0x7cf57): the press fires the
// tube; lit when loaded and undamaged.
class TubeButton final : public PanelControl {
 public:
  TubeButton(TdcPanelContext& ctx, Box box, std::size_t tube, std::int32_t ready_frame)
      : PanelControl(ctx, box, ui::CursorId::kFire), tube_(tube), ready_frame_(ready_frame) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg == ui::Msg::kPress && !pressed) {
      // tdc_fire_tube_N -> fire_tube
      Mission& m = ctx_.mission();
      SimContext sim = m.sim().context();
      m.player_combat().fire_tube(sim, m.player(), tube_);
    }
    if (msg != ui::Msg::kPress && msg != ui::Msg::kRelease && msg != ui::Msg::kPaint) {
      return false;
    }
    const TorpedoTubes& tubes = ctx_.mission().player_combat().tubes;
    const bool ready = tubes.content(tube_) > 0 && tubes.damage(tube_).flags == 0;
    const std::int32_t frame = ready ? ready_frame_ : ready_frame_ + 1;
    ctx_.draw_frame(dialog.gfx(), frame, box_.x0, box_.y0);
    add_sprite_dirty(dialog, frame);
    return true;
  }

 private:
  void add_sprite_dirty(ui::Dialog& dialog, std::int32_t frame) {
    if (static_cast<std::size_t>(frame) < ctx_.sprites.frame_count()) {
      const SpriteFrame& f = ctx_.sprites.frame(static_cast<std::size_t>(frame));
      dialog.add_dirty({box_.x0, box_.y0, box_.x0 + f.width() - 1, box_.y0 + f.height() - 1});
    }
  }

  std::size_t tube_;
  std::int32_t ready_frame_;
};

// A sprite push button (`push_button_proc`): the press clicks and runs
// `action(true)`; it shows frame `base + action(false)`.
class PushButton final : public PanelControl {
 public:
  PushButton(TdcPanelContext& ctx, const ButtonDef& def, std::function<std::int32_t(bool)> action)
      : PanelControl(ctx, def.box, ui::CursorId::kSet), base_(def.frame), action_(std::move(action)) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg == ui::Msg::kPress) {
      if (!pressed) {
        ctx_.click_sound();
        action_(true);
      }
    } else if (msg != ui::Msg::kPaint) {
      return false;
    }
    const std::int32_t frame = base_ + action_(false);
    ctx_.draw_frame(dialog.gfx(), frame, box_.x0, box_.y0);
    if (static_cast<std::size_t>(frame) < ctx_.sprites.frame_count()) {
      const SpriteFrame& f = ctx_.sprites.frame(static_cast<std::size_t>(frame));
      dialog.add_dirty({box_.x0, box_.y0, box_.x0 + f.width() - 1, box_.y0 + f.height() - 1});
    }
    return true;
  }

 private:
  std::int32_t base_;
  std::function<std::int32_t(bool)> action_;
};

// The solution track button (`tdc_track_button_handler`, 0x7d0b9).
class TrackButton final : public PanelControl {
 public:
  TrackButton(TdcPanelContext& ctx, const ButtonDef& def)
      : PanelControl(ctx, def.box, ui::CursorId::kSet), on_(def.frame), off_(def.frame2) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg == ui::Msg::kPress) {
      if (pressed) {
        return false;
      }
      ctx_.tdc().toggle_track();
    } else if (msg != ui::Msg::kRelease && msg != ui::Msg::kPaint) {
      return false;
    }
    const std::int32_t frame = ctx_.tdc().tracking() ? on_ : off_;
    ctx_.draw_frame(dialog.gfx(), frame, box_.x0, box_.y0);
    if (static_cast<std::size_t>(frame) < ctx_.sprites.frame_count()) {
      const SpriteFrame& f = ctx_.sprites.frame(static_cast<std::size_t>(frame));
      dialog.add_dirty({box_.x0, box_.y0, box_.x0 + f.width() - 1, box_.y0 + f.height() - 1});
    }
    return true;
  }

 private:
  std::int32_t on_;
  std::int32_t off_;
};

// Returns the angle of the event around (cx, cy) of `dialog`, clockwise
// from up (`knob_dial_proc` / `needle_dial_proc`: heading_from_delta of the
// offset from the centre, plus 90 deg).
std::int32_t pointer_angle(const ui::Dialog& dialog, int cx, int cy, const ui::Event& event) {
  const std::int32_t a =
      fx::heading_from_delta(event.y - (cy + dialog.y()) - 1, event.x - (cx + dialog.x()) - 1).tenths();
  return fx::angle_add(Angle10(a), Angle10(900)).tenths();
}

// A knob dial (`knob_dial_proc`, 0x4a015 / `knob_dial_draw`, 0x4a0ac): a
// press sets the value to the pointer's angle and runs the change action.
class KnobDial final : public PanelControl {
 public:
  using Getter = std::function<std::int32_t()>;
  KnobDial(TdcPanelContext& ctx, const KnobDef& def, Getter value, std::function<void(std::int32_t)> set,
           Getter underlay = {})
      : PanelControl(ctx, def.box, ui::CursorId::kSet),
        def_(def),
        value_(std::move(value)),
        set_(std::move(set)),
        underlay_(std::move(underlay)) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event& event) override {
    if (msg == ui::Msg::kPress) {
      if (pressed) {
        return false;
      }
      const std::int32_t a = pointer_angle(dialog, def_.cx, def_.cy, event);
      if (a != value_()) {
        set_(a);
        draw(dialog);
      }
      return true;
    }
    if (msg == ui::Msg::kPaint) {
      draw(dialog);
      return true;
    }
    return false;
  }

 private:
  void draw(ui::Dialog& dialog) {
    const Box& b = def_.box;
    Image8 buf = ctx_.box_buffer(b, dialog.y());
    const Viewport vp = Viewport::full(buf);
    const int x = def_.cx - b.x0;
    const int y = def_.cy - b.y0;
    if (underlay_) {
      ctx_.draw_rotated(vp, def_.under_frame, x, y, underlay_());
    }
    if (def_.digits != 0) {
      const std::int32_t v = def_.digits < 0 ? value_() / 10 : value_();
      draw_padded_number(vp, def_.digits_x - b.x0, def_.digits_y - b.y0, v, std::abs(def_.digits), ctx_.digits,
                         ctx_.colours);
    }
    ctx_.draw_rotated(vp, def_.knob_frame, x, y, value_());
    if (def_.cap_frame != -1) {
      ctx_.draw_frame(vp, def_.cap_frame, x, y);
    }
    ctx_.present(dialog, buf, b);
  }

  KnobDef def_;
  Getter value_;
  std::function<void(std::int32_t)> set_;
  Getter underlay_;
};

// A digit counter (`digit_counter_proc`, 0x4ac23): a click on a digit
// counts it up (9 -> 0).
class DigitCounter final : public PanelControl {
 public:
  DigitCounter(TdcPanelContext& ctx, const CounterDef& def, std::function<std::int32_t()> value,
               std::function<void(std::int32_t)> set)
      : PanelControl(ctx, def.box, ui::CursorId::kUp), def_(def), value_(std::move(value)), set_(std::move(set)) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event& event) override {
    if (msg == ui::Msg::kPress) {
      if (pressed) {
        return false;
      }
      const int cell = (box_.x1 - box_.x0 + 1) / def_.digits;
      int index = 0;
      for (int edge = cell; index < def_.digits && edge <= event.x - box_.x0 - dialog.x(); edge += cell) {
        ++index;
      }
      std::int32_t power = 1;
      for (int i = 0; i < def_.digits - 1 - index; ++i) {
        power *= 10;
      }
      std::int32_t value = value_();
      std::int32_t digit = value / power;
      if (digit > 9) {
        digit %= 10;
      }
      std::int32_t next = digit + 1;
      if (next > 9) {
        next = 0;
      }
      value += (next - digit) * power;
      ctx_.click_sound();
      set_(value);
    } else if (msg != ui::Msg::kPaint) {
      return false;
    }
    draw_padded_number(dialog.gfx(), def_.text_x, def_.text_y, value_(), def_.digits, ctx_.big_digits, ctx_.colours);
    dialog.add_dirty({box_.x0, box_.y0, box_.x1, box_.y1});
    return true;
  }

 private:
  CounterDef def_;
  std::function<std::int32_t()> value_;
  std::function<void(std::int32_t)> set_;
};

// The solution quality tape (`linear_tape_proc` 0x4ae08 -> `linear_tape_draw`, 0x4ae45):
// a 0..100 scale, 3 px a unit, centred on the value.
class QualityTape final : public PanelControl {
 public:
  QualityTape(TdcPanelContext& ctx, const Box& box) : PanelControl(ctx, box, ui::CursorId::kSet) { without_cursor(); }

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg != ui::Msg::kPaint) {
      return false;
    }
    Image8 buf = ctx_.box_buffer(box_, dialog.y());
    const Viewport vp = Viewport::full(buf);
    const int w = buf.width();
    const int h = buf.height();
    const std::int32_t value = ctx_.tdc().quality();
    // The label colour: the identity GUI table with entry 0xe0 set to the
    // def's colour (0).
    GlyphColourTable colours{};
    for (std::size_t i = 0; i < colours.size(); ++i) {
      colours[i] = static_cast<std::uint8_t>(i);
    }
    colours[0xe0] = kColour;
    const Font& font = ctx_.services.bearing_font();
    std::int32_t unit = value / 10 * 10 - 30;
    for (int x = w / 2 - 90 - (value % 10) * 3; x < w + 30; x += 3, ++unit) {
      if (unit < 0 || unit > 100) {
        continue;
      }
      if (unit % 10 == 0) {
        const std::string text = std::to_string(unit);
        const int half = font.text_width(text) / 2;
        ui::draw_line(vp, x, h - 3, x, h - 8, kColour);
        int pen = x - half + 1;
        for (const char c : text) {
          pen += std::max(font_draw_char(vp, pen, h - 17, font, static_cast<std::uint8_t>(c), &colours), 0);
        }
      } else if (unit % 10 == 5) {
        ui::draw_line(vp, x, h - 3, x, h - 7, kColour);
      } else {
        ui::draw_line(vp, x, h - 3, x, h - 5, kColour);
      }
    }
    ctx_.present(dialog, buf, box_);
    return true;
  }

 private:
  static constexpr std::uint8_t kColour = 0;
};

// A needle dial (`needle_dial_proc`, 0x4978c / `needle_dial_draw`,
// 0x49866): the torpedo depth; a press sets the value at the pointer's
// angle along the arc.
class NeedleDial final : public PanelControl {
 public:
  NeedleDial(TdcPanelContext& ctx, const NeedleDef& def, std::function<std::int32_t()> value,
             std::function<void(std::int32_t)> set)
      : PanelControl(ctx, def.box, ui::CursorId::kSet), def_(def), value_(std::move(value)), set_(std::move(set)) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event& event) override {
    if (msg == ui::Msg::kPress) {
      if (pressed) {
        return false;
      }
      const std::int32_t a = pointer_angle(dialog, def_.cx, def_.cy, event);
      const std::int32_t arc =
          fx::angle_add(fx::angle_sub(Angle10(def_.arc_end * 10), Angle10(def_.arc_start * 10)), Angle10(10)).tenths();
      std::int32_t v = fx::angle_sub(Angle10(a), Angle10(def_.arc_start * 10)).tenths() * 100 / (arc * 100 / def_.max);
      if (v > def_.max) {
        v = a < 1800 ? def_.max : 0;
      }
      if (v != value_()) {
        set_(v);
        draw(dialog);
      }
      return true;
    }
    if (msg == ui::Msg::kPaint) {
      draw(dialog);
      return true;
    }
    return false;
  }

 private:
  void draw(ui::Dialog& dialog) {
    const Box& b = def_.box;
    Image8 buf = ctx_.box_buffer(b, dialog.y());
    const Viewport vp = Viewport::full(buf);
    if (def_.digits != 0) {
      const std::int32_t v = def_.digits < 0 ? value_() / 10 : value_();
      draw_padded_number(vp, def_.digits_x - b.x0, def_.digits_y - b.y0, v, std::abs(def_.digits), ctx_.digits,
                         ctx_.colours);
    }
    const std::int32_t arc =
        fx::angle_add(fx::angle_sub(Angle10(def_.arc_end * 10), Angle10(def_.arc_start * 10)), Angle10(10)).tenths();
    const std::int32_t scale = arc * 100 / def_.max;
    const int x = def_.cx - b.x0;
    const int y = def_.cy - b.y0;
    // The ordered-value marker, then the needle: both the depth here.
    const std::int32_t angle = fx::angle_add(Angle10(value_() * scale / 100), Angle10(def_.arc_start * 10)).tenths();
    ctx_.draw_rotated(vp, def_.marker_frame, x, y, angle);
    ctx_.draw_rotated(vp, def_.needle_frame, x, y, angle);
    if (def_.cap_frame != -1) {
      ctx_.draw_frame(vp, def_.cap_frame, x, y);
    }
    ctx_.present(dialog, buf, b);
  }

  NeedleDef def_;
  std::function<std::int32_t()> value_;
  std::function<void(std::int32_t)> set_;
};

// The torpedo run-time dial (`tdc_run_timer_handler` /
// `tdc_draw_run_timer`, 0x7d21f): the shortest running time as mm:ss and
// one needle per tracked torpedo.
class RunTimer final : public PanelControl {
 public:
  RunTimer(TdcPanelContext& ctx, const NeedleDef& def) : PanelControl(ctx, def.box, ui::CursorId::kSet), def_(def) {}

 protected:
  bool message(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg != ui::Msg::kPaint) {
      return false;
    }
    const Tdc& tdc = ctx_.tdc();
    const auto& timers = tdc.run_timers();
    const auto& tracked = tdc.tracked_torpedoes();
    const Box& b = def_.box;
    Image8 buf = ctx_.box_buffer(b, dialog.y());
    const Viewport vp = Viewport::full(buf);
    std::int32_t shortest = 0x7fffffff;
    for (std::size_t i = 0; i < Tdc::kTrackedTorpedoes; ++i) {
      if (tracked[i] != nullptr && timers[i] >= 0 && timers[i] < shortest) {
        shortest = timers[i];
      }
    }
    if (shortest == 0x7fffffff) {
      shortest = 0;
    }
    draw_padded_number(vp, def_.digits_x - b.x0, def_.digits_y - b.y0, shortest + shortest / 60 * 40, def_.digits,
                       ctx_.digits, ctx_.colours);
    const std::int32_t arc =
        fx::angle_add(fx::angle_sub(Angle10(def_.arc_end * 10), Angle10(def_.arc_start * 10)), Angle10(10)).tenths();
    const std::int32_t scale = arc * 100 / def_.max;
    for (std::size_t i = 0; i < Tdc::kTrackedTorpedoes; ++i) {
      if (tracked[i] == nullptr) {
        continue;
      }
      const std::int32_t t = std::min(timers[i], def_.max);
      ctx_.draw_rotated(vp, def_.needle_frame, def_.cx - b.x0, def_.cy - b.y0,
                        fx::angle_add(Angle10(t * scale / 100), Angle10(def_.arc_start * 10)).tenths());
    }
    if (def_.cap_frame != -1) {
      ctx_.draw_frame(vp, def_.cap_frame, def_.cx - b.x0, def_.cy - b.y0);
    }
    ctx_.present(dialog, buf, b);
    return true;
  }

 private:
  NeedleDef def_;
};

// The type label offsets from the type button of `tdc_torpedo_panel_open`:
// int32[3] x at 0x10fa48, int32[3] y at 0x10fa54.
constexpr std::uint32_t kTypeLabelDx = 0x10fa48;
constexpr std::uint32_t kTypeLabelDy = 0x10fa54;

}  // namespace

struct TdcPanels::Impl {
  explicit Impl(AttackServices& services, TdcPanelSite site)
      : ctx{services, site, layout_of(site), {}, {}, {}, colours_of(site), {}, {}, 0} {}

  TdcPanelContext ctx;
  std::optional<Shp> background;  // SCOPTDC<n>.SHP / TBTTDC<n>.SHP
  std::unique_ptr<ui::Dialog> tube;
  std::unique_ptr<ui::Dialog> solution;
  std::unique_ptr<ui::Dialog> gyro;
  std::unique_ptr<ui::Dialog> torpedo;
  // The values the updates compare to dirty the controls (0x129998..).
  std::array<std::int32_t, TorpedoTubes::kMaxTubes> tube_content{};
  std::array<std::uint16_t, TorpedoTubes::kMaxTubes> tube_damage{};
  std::vector<ui::Control*> tube_buttons;
  ui::Control* mode_button = nullptr;
  Tdc::Mode mode = Tdc::Mode::kOff;
  struct Watched {
    ui::Control* control = nullptr;
    std::function<std::int32_t()> value;
    std::int32_t last = 0;
  };
  std::vector<Watched> watched;  // the solution, gyro and torpedo values
  ui::Control* run_timer = nullptr;

  std::unique_ptr<ui::Dialog> make_dialog() {
    Image8& screen = ctx.services.screen();
    const int y = ctx.layout.y;
    return std::make_unique<ui::Dialog>(ctx.services.gui(), 0, y, kScreenWidth, kScreenHeight - y, 0u, &screen);
  }
  void watch(ui::Control& c, std::function<std::int32_t()> value) {
    const std::int32_t v = value();
    watched.push_back({&c, std::move(value), v});
  }
  bool manual() { return ctx.tdc().mode() == Tdc::Mode::kManual; }
  // tdc_mark_target's placement (0x7c788): the target at the dialled
  // bearing and range.
  void place_target() {
    Mission& m = ctx.mission();
    PlayerCombat& pc = m.player_combat();
    SimContext sim = m.sim().context();
    pc.tdc.mark_target(pc.own_boat(m.player(), sim), pc.tdc.bearing(), m.sim().vehicles(), pc.sensing);
  }

  void open_tube_panel();
  void open_solution_panel();
  void open_gyro_panel();
  void open_torpedo_panel();
};

void TdcPanels::Impl::open_tube_panel() {
  // tdc_tube_panel_open (0x7a15d)
  tube = make_dialog();
  tube_buttons.clear();
  const TorpedoTubes& tubes = ctx.mission().player_combat().tubes;
  std::int32_t frame = 0;
  std::size_t index = 0;
  for (std::int32_t i = 0; i < tubes.bow_count() && i < 6; ++i, ++index, frame += 2) {
    tube_buttons.push_back(
        &tube->emplace<TubeButton>(ctx, ctx.layout.bow_tubes[static_cast<std::size_t>(i)], index, frame));
  }
  for (std::int32_t i = 0; i < tubes.stern_count() && i < 4; ++i, ++index, frame += 2) {
    tube_buttons.push_back(
        &tube->emplace<TubeButton>(ctx, ctx.layout.stern_tubes[static_cast<std::size_t>(i)], index, frame));
  }
  for (std::size_t t = 0; t < index; ++t) {
    tube_content[t] = tubes.content(t);
    tube_damage[t] = tubes.damage(t).flags;
  }
  // tdc_mode_button (0x7cde6): auto -> manual -> auto.
  mode_button = &tube->emplace<PushButton>(ctx, ctx.layout.mode, [this](bool press) {
    Tdc& tdc = ctx.tdc();
    if (press) {
      std::int32_t m = static_cast<std::int32_t>(tdc.mode()) - 1;
      if (m < 1) {
        m = 2;
      }
      tdc.set_mode(static_cast<Tdc::Mode>(m));
    }
    return static_cast<std::int32_t>(tdc.mode());
  });
  mode = ctx.tdc().mode();
  tube->open();
}

void TdcPanels::Impl::open_solution_panel() {
  // tdc_solution_panel_open (0x7a565)
  solution = make_dialog();
  const SiteLayout& l = ctx.layout;
  Tdc& tdc = ctx.tdc();
  auto manual_off = [this] { return !manual(); };
  auto& bearing = solution->emplace<KnobDial>(
      ctx, l.bearing, [&tdc] { return tdc.bearing().tenths(); },
      [this, &tdc](std::int32_t v) {
        tdc.set_bearing(Angle10(v));
        place_target();
      });
  auto& course = solution->emplace<KnobDial>(
      ctx, l.course, [&tdc] { return tdc.course().tenths(); }, [&tdc](std::int32_t v) { tdc.set_course(Angle10(v)); },
      [this] { return ctx.mission().player().heading().tenths(); });
  auto& range = solution->emplace<DigitCounter>(
      ctx, l.range, [&tdc] { return tdc.range_yd(); },
      [this, &tdc](std::int32_t v) {
        tdc.set_range_yd(v);
        place_target();
      });
  auto& speed = solution->emplace<DigitCounter>(
      ctx, l.speed, [&tdc] { return tdc.speed_tenths(); }, [&tdc](std::int32_t v) { tdc.set_speed_tenths(v); });
  auto& track = solution->emplace<TrackButton>(ctx, l.track);
  auto& quality = solution->emplace<QualityTape>(ctx, l.quality);
  for (PanelControl* c :
       {static_cast<PanelControl*>(&bearing), static_cast<PanelControl*>(&course), static_cast<PanelControl*>(&range),
        static_cast<PanelControl*>(&speed), static_cast<PanelControl*>(&track)}) {
    c->set_disabled_when(manual_off);
  }
  watch(bearing, [&tdc] { return tdc.bearing().tenths(); });
  watch(course, [this, &tdc] { return tdc.course().tenths() * 4000 + ctx.mission().player().heading().tenths(); });
  watch(range, [&tdc] { return tdc.range_yd(); });
  watch(speed, [&tdc] { return tdc.speed_tenths(); });
  watch(track, [&tdc] { return tdc.tracking() ? 1 : 0; });
  watch(quality, [&tdc] { return tdc.quality(); });
  solution->open();
}

void TdcPanels::Impl::open_gyro_panel() {
  // tdc_gyro_panel_open (0x7a9cf)
  gyro = make_dialog();
  const SiteLayout& l = ctx.layout;
  Tdc& tdc = ctx.tdc();
  auto manual_off = [this] { return !manual(); };
  auto& bow = gyro->emplace<KnobDial>(
      ctx, l.gyro_bow, [&tdc] { return tdc.gyro(Tdc::kBow).tenths(); },
      [&tdc](std::int32_t v) { tdc.set_gyro(Tdc::kBow, Angle10(v)); });
  auto& stern = gyro->emplace<KnobDial>(
      ctx, l.gyro_stern, [&tdc] { return tdc.gyro(Tdc::kStern).tenths(); },
      [&tdc](std::int32_t v) { tdc.set_gyro(Tdc::kStern, Angle10(v)); });
  ctx.spread_dial = tdc.spread() * 3600 / 400;
  auto& spread = gyro->emplace<KnobDial>(
      ctx, l.spread, [this] { return ctx.spread_dial; }, [this](std::int32_t v) { ctx.spread_dial = v; });
  bow.set_disabled_when(manual_off);
  stern.set_disabled_when(manual_off);
  watch(bow, [&tdc] { return tdc.gyro(Tdc::kBow).tenths(); });
  watch(stern, [&tdc] { return tdc.gyro(Tdc::kStern).tenths(); });
  watch(spread, [this] { return ctx.spread_dial; });
  gyro->open();
}

void TdcPanels::Impl::open_torpedo_panel() {
  // tdc_torpedo_panel_open (0x7acda)
  torpedo = make_dialog();
  const SiteLayout& l = ctx.layout;
  Tdc& tdc = ctx.tdc();
  PlayerCombat& pc = ctx.mission().player_combat();
  auto& depth = torpedo->emplace<NeedleDial>(
      ctx, l.depth, [&tdc] { return tdc.depth_ft(); }, [&tdc](std::int32_t v) { tdc.set_depth_ft(v); });
  // tdc_toggle_torpedo_speed (0x7ce07) / tdc_cycle_torpedo_type (0x7ce18).
  auto& speed = torpedo->emplace<PushButton>(ctx, l.torpedo_speed, [&tdc](bool press) {
    if (press) {
      tdc.toggle_speed();
    }
    return tdc.speed_setting();
  });
  auto& type = torpedo->emplace<PushButton>(ctx, l.torpedo_type, [&tdc, &pc](bool press) {
    if (press) {
      tdc.cycle_type(pc.torpedo_types);
    }
    return tdc.type_index();
  });
  run_timer = &torpedo->emplace<RunTimer>(ctx, l.run_timer);
  watch(depth, [&tdc] { return tdc.depth_ft(); });
  watch(speed, [&tdc] { return tdc.speed_setting(); });
  watch(type, [&tdc] { return tdc.type_index(); });
  torpedo->open();
  // The labels of the available torpedo types beside the type button.
  for (std::size_t k = 0; k < 3; ++k) {
    if (pc.torpedo_types[k].available) {
      ctx.draw_frame(torpedo->gfx(), l.torpedo_type.frame + 3 + static_cast<std::int32_t>(k),
                     l.torpedo_type.box.x0 - game_exe().i32(kTypeLabelDx + 4 * static_cast<std::uint32_t>(k)),
                     l.torpedo_type.box.y0 + game_exe().i32(kTypeLabelDy + 4 * static_cast<std::uint32_t>(k)));
    }
  }
}

TdcPanels::TdcPanels(AttackServices& services, TdcPanelSite site) : impl_(std::make_unique<Impl>(services, site)) {}

TdcPanels::~TdcPanels() = default;

void TdcPanels::open() {
  Impl& p = *impl_;
  TdcPanelContext& ctx = p.ctx;
  const Install& install = ctx.services.install();
  ctx.digits = Font::load(install, "ART/DIGITS.FNT");
  ctx.big_digits = Font::load(install, "ART/DIGITS3.FNT");
  Image8& screen = ctx.services.screen();
  const auto sub = static_cast<std::size_t>(ctx.services.tdc_panel);
  switch (ctx.site) {
    case TdcPanelSite::kTdc:
      ctx.sprites = Shp::load(install, "ART/TBTTDC.SHP");
      break;
    case TdcPanelSite::kScope: {
      // scope_load_overlays: SCOPTDC<n> frame 0 on the left, 1 on the right.
      ctx.sprites = Shp::load(install, "ART/SCOPETDC.SHP");
      static constexpr std::array<std::string_view, 3> kFiles = {"ART/SCOPTDC0.SHP", "ART/SCOPTDC1.SHP",
                                                                 "ART/SCOPTDC2.SHP"};
      p.background = Shp::load(install, kFiles.at(sub));
      const Viewport vp = Viewport::full(screen);
      draw_sprite_clipped(vp, p.background->frame(0), 0, 0);
      draw_sprite_clipped(vp, p.background->frame(1), 0x1d7, 0);
      break;
    }
    case TdcPanelSite::kTbt: {
      // tbt_load_overlays: TBTTDC<n> under the view from row 0x101.
      ctx.sprites = Shp::load(install, "ART/TBTTDC.SHP");
      static constexpr std::array<std::string_view, 3> kFiles = {"ART/TBTTDC0.SHP", "ART/TBTTDC1.SHP",
                                                                 "ART/TBTTDC2.SHP"};
      p.background = Shp::load(install, kFiles.at(sub));
      draw_sprite_clipped(Viewport::full(screen), p.background->frame(0), 0, 0x101);
      break;
    }
  }
  ctx.background = screen;
  p.watched.clear();
  p.open_tube_panel();
  if (ctx.site == TdcPanelSite::kTdc) {
    p.open_solution_panel();
    p.open_gyro_panel();
    p.open_torpedo_panel();
    return;
  }
  switch (ctx.services.tdc_panel) {
    case TdcSubPanel::kSolution:
      p.open_solution_panel();
      break;
    case TdcSubPanel::kGyro:
      p.open_gyro_panel();
      break;
    case TdcSubPanel::kTorpedo:
      p.open_torpedo_panel();
      break;
  }
}

void TdcPanels::event(const ui::Event& event) {
  for (auto* d : {impl_->tube.get(), impl_->solution.get(), impl_->gyro.get(), impl_->torpedo.get()}) {
    if (d != nullptr) {
      d->handle_event(event);
    }
  }
}

void TdcPanels::process() {
  for (auto* d : {impl_->tube.get(), impl_->solution.get(), impl_->gyro.get(), impl_->torpedo.get()}) {
    if (d != nullptr) {
      d->process();
    }
  }
}

void TdcPanels::update() {
  // tdc_tube_panel_update (0x7a459) and the other panels' updates.
  Impl& p = *impl_;
  TdcPanelContext& ctx = p.ctx;
  if (ctx.services.redraw_requested()) {
    for (auto* d : {p.tube.get(), p.solution.get(), p.gyro.get(), p.torpedo.get()}) {
      if (d != nullptr) {
        d->invalidate();
      }
    }
  }
  if (p.tube) {
    const TorpedoTubes& tubes = ctx.mission().player_combat().tubes;
    for (std::size_t t = 0; t < p.tube_buttons.size(); ++t) {
      if (p.tube_content[t] != tubes.content(t) || p.tube_damage[t] != tubes.damage(t).flags) {
        p.tube_content[t] = tubes.content(t);
        p.tube_damage[t] = tubes.damage(t).flags;
        p.tube_buttons[t]->dirty = true;
      }
    }
    if (p.mode != ctx.tdc().mode()) {
      p.mode = ctx.tdc().mode();
      p.mode_button->dirty = true;
    }
  }
  if (p.gyro) {
    // tdc_gyro_panel_update: the spread dial back to the spread.
    ctx.tdc().set_spread(ctx.spread_dial * 400 / 3600);
  }
  for (Impl::Watched& w : p.watched) {
    const std::int32_t v = w.value();
    if (v != w.last) {
      w.last = v;
      w.control->dirty = true;
    }
  }
  if (p.torpedo) {
    p.run_timer->dirty = true;
  }
}

void TdcPanels::close() {
  for (auto* d : {&impl_->tube, &impl_->solution, &impl_->gyro, &impl_->torpedo}) {
    if (*d != nullptr) {
      (*d)->close();
      d->reset();
    }
  }
  impl_->tube_buttons.clear();
  impl_->watched.clear();
  impl_->mode_button = nullptr;
  impl_->run_timer = nullptr;
}

TdcStation::TdcStation(AttackServices& services) : services_(services) {}

TdcStation::~TdcStation() = default;

void TdcStation::open() {
  // tdc_screen (0x79f00)
  const Install& install = services_.install();
  const PcxImage pcx = load_pcx(install, "ART/TDC.PCX");
  services_.palette() = pcx.palette;
  if (services_.host().rig_for_red()) {
    services_.palette() = load_pcx(install, "ART/TDC.PAL").palette;
  }
  services_.screen().blit(pcx.image, 0, 0);
  CommandBarMenu menu;
  menu.items.push_back({2, 3, [this] { services_.go_to(StationId::kConn); }});  // tdc_goto_conn
  services_.host().open_command_bar(menu);
  panels_ = std::make_unique<TdcPanels>(services_, TdcPanelSite::kTdc);
  panels_->open();
  services_.host().fade_out();
}

void TdcStation::event(const ui::Event& event) { panels_->event(event); }

void TdcStation::process() { panels_->process(); }

void TdcStation::update() {
  panels_->update();
  if (first_frame()) {
    services_.host().fade_in();
  }
}

void TdcStation::close() {
  panels_->close();
  panels_.reset();
}

}  // namespace sh

#include "game/stations/deckgun.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "assets/game_exe.h"
#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "game/mission.h"
#include "game/world_view_adapter.h"
#include "gfx/sprite.h"
#include "sim/ballistic.h"
#include "sim/player_combat.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// DECKGUN.SHP frames.
constexpr std::size_t kReticleFrame = 0;    // the sight over the view
constexpr std::size_t kZoomPlateFrame = 5;  // the plate sliding over the 1/2/4/8 labels (1..4)
constexpr std::size_t kZoomKnob = 6;        // 6/7 up/down
constexpr std::size_t kReadyLampOff = 8;    // 8/9
constexpr std::size_t kFireButton = 10;     // 10/11 up/down
constexpr std::size_t kAftLabel = 12;       // 12 AFT, 13 FWD
constexpr std::size_t kBreechFrame = 14;    // the gun's breech under the flash
constexpr std::size_t kBreechTopFrame = 15;
constexpr std::size_t kTowerFrame = 16;    // the conning tower seen by the forward gun
constexpr std::size_t kManualSwitch = 17;  // 17 crew, 18 manual

// GLOBAL.SHP: the target marker, colour 1 remapped.
constexpr std::size_t kTargetMarkerFrame = 37;
constexpr std::uint8_t kTargetMarkerColour = 0xe5;

// The view: the 201 x 200 sight blitted at (0x15e, 0x4f).
constexpr int kViewX = 0x15e;
constexpr int kViewY = 0x4f;
constexpr int kViewRows = 200;
// The zoom knob's button.
constexpr ui::Rect kZoomKnobArea{0x100, 0xac, 0x131, 0xf0};
// The marker row in the view and the muzzle flash's breech area
// (0x105348: screen (0xb2, 0x46)-(0x110, 0xaa)).
constexpr int kMarkerY = 0xaa;
constexpr int kBreechX0 = 0xb2;
constexpr int kBreechY0 = 0x46;
constexpr int kBreechX1 = 0x110;
constexpr int kBreechY1 = 0xaa;
// The zoom plate (0x10535c) at screen (0x11c, 0xc9).
constexpr int kZoomPlateX = 0x11c;
constexpr int kZoomPlateY = 0xc9;
// The magnification labels by integer zoom (0x105180), +1 = frame.
constexpr std::uint32_t kZoomLabel = 0x105180;  // int32[9]

constexpr std::uint8_t kTapeColour = 0xa0;
// The muzzle velocity in ft/s (0x834).
constexpr std::int32_t kVelocity = 2100;
// Elevation limit 45 deg.
constexpr std::int32_t kMaxElevation = 0x1c2;

// The station's glyph table 0x11bd50 as `deckgun_screen` patches it (the
// rest of the BSS table stays 0).
GlyphColourTable station_colours() {
  return patched_colours({{0xc0, 0xa0},
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
}

// Draws `text` glyph after glyph (`font_draw_string`, 0xae766).
void draw_string(const Viewport& vp, int x, int y, const Font& font, const std::string& text,
                 const GlyphColourTable& colours) {
  for (const char c : text) {
    x += std::max(font_draw_char(vp, x, y, font, static_cast<std::uint8_t>(c), &colours), 0);
  }
}

// Returns `value` as `digits` zero-padded digits (`int_to_padded_str`).
std::string padded(std::int32_t value, std::int32_t digits) {
  digits = std::min(digits, 10);
  value = std::max(value, 0);
  std::string s(static_cast<std::size_t>(digits), '0');
  std::int32_t div = 1;
  for (std::int32_t i = digits - 1; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = static_cast<char>('0' + value / div % 10);
    div *= 10;
  }
  return s;
}

// Returns the w x h copy of `background` at (x0, y0).
Image8 crop(const Image8& background, int x0, int y0, int w, int h) {
  Image8 out(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      out.set(x, y, background.contains(x0 + x, y0 + y) ? background.at(x0 + x, y0 + y) : 0);
    }
  }
  return out;
}

// linear_tape_draw (0x4ae45): a 0..100 scale over (x0, y0)-(x1, y1), 3 px
// per unit centred on `value`, labels every 10 drawn through the identity
// glyph table with glyph 0xe0 in `colour`.
void draw_linear_tape(Image8& screen, const Image8& background, const Font& font, std::int32_t value, int x0, int y0,
                      int x1, int y1, std::uint8_t colour) {
  const int w = x1 - x0 + 1;
  const int h = y1 - y0;  // the height less one
  Image8 tape = crop(background, x0, y0, w, h + 1);
  const Viewport vp = Viewport::full(tape);
  GlyphColourTable identity{};
  for (std::size_t i = 0; i < identity.size(); ++i) {
    identity[i] = static_cast<std::uint8_t>(i);
  }
  identity[0xe0] = colour;
  std::int32_t unit = value / 10 * 10 - 30;
  for (int x = w / 2 - 90 - (value - value / 10 * 10) * 3; x < w + 30; x += 3, ++unit) {
    if (unit < 0 || unit > 100) {
      continue;
    }
    if (unit % 10 == 0) {
      const std::string text = std::to_string(unit);
      ui::draw_line(vp, x, h - 2, x, h - 7, colour);
      draw_string(vp, x - font.text_width(text) / 2 + 1, h - 16, font, text, identity);
    } else if (unit % 10 == 5) {
      ui::draw_line(vp, x, h - 2, x, h - 6, colour);
    } else {
      ui::draw_line(vp, x, h - 2, x, h - 4, colour);
    }
  }
  screen.blit(tape, x0, y0);
}

// draw_elevation_tape (0x4b39f): the vertical elevation scale, 8 px per
// degree centred on `elevation` (tenths), a tick every half degree (longer
// on the whole degrees) and a label every 5 deg up to 45.
void draw_elevation_tape(Image8& screen, const Image8& background, const Font& font, std::int32_t elevation, int x0,
                         int y0, int x1, int y1, std::uint8_t colour, const GlyphColourTable& colours) {
  const int w = x1 - x0 + 1;
  const int h = y1 - y0 + 1;
  Image8 tape = crop(background, x0, y0, w, h);
  const Viewport vp = Viewport::full(tape);
  const std::int32_t base = elevation / 50 * 50;
  const std::int32_t offset = (elevation - base) * 4 / 5;
  std::int32_t next_label = fx::angle_sub(Angle10(base), Angle10(350)).tenths();
  std::int32_t label = next_label;
  const int right = w - 1;
  const int x = right - 2;
  int y = h / 2 + 0x117 + offset;
  int text_y = y - 3;
  for (int count = 0; y > -(h + 40); ++count, y -= 4, text_y -= 4) {
    const int r = count % 10;
    if (r == 0) {
      label = next_label;
      const std::string text = std::to_string(label / 10);
      next_label = fx::angle_add(Angle10(next_label), Angle10(50)).tenths();
      if (label <= kMaxElevation) {
        ui::draw_line(vp, x, y, right - 7, y, colour);
        draw_string(vp, right - 7 - font.text_width(text), text_y, font, text, colours);
      }
    } else if (r % 2 == 0) {
      if (label < kMaxElevation) {
        ui::draw_line(vp, x, y, right - 5, y, colour);
      }
    } else if (label < kMaxElevation) {
      ui::draw_line(vp, x, y, right - 3, y, colour);
    }
  }
  screen.blit(tape, x0, y0);
}

// deckgun_clamp_train (0x3786b): `train` + `delta`, kept 7 deg off the
// tower, without wrapping the sum before the clamp.
std::int32_t clamp_train_step(std::int32_t mount, std::int32_t train, std::int32_t delta) {
  if (mount == 0) {
    train = fx::angle_add(Angle10(train), Angle10(1800)).tenths();
  }
  std::int32_t t = std::clamp(delta + train, 70, 3530);
  if (mount == 0) {
    t = fx::angle_add(Angle10(t), Angle10(1800)).tenths();
  }
  return t;
}

// A sprite drawn from the station's sprites at a fixed place.
struct SpritePlace {
  const Shp* sprites = nullptr;
  int x = 0;
  int y = 0;

  ui::Rect draw(ui::Dialog& d, std::size_t frame) const {
    if (frame >= sprites->frame_count()) {
      return {x, y, x, y};
    }
    const SpriteFrame& f = sprites->frame(frame);
    draw_sprite_clipped(d.gfx(), f, x, y);
    return {x, y, x + f.width() - 1, y + f.height() - 1};
  }
};

// cycle_button_proc (0x49472): a sprite button whose action gets the click
// count; the pressed frame shows while held.
class SpriteButton : public ui::Control {
 public:
  SpriteButton(ui::Rect area, std::uint32_t key, SpritePlace place, std::size_t frame, std::size_t pressed_frame,
               std::function<void()> action, std::function<void()> click_sound, std::function<void(bool)> on_hover)
      : area_(area),
        key_(key),
        place_(place),
        frame_(frame),
        pressed_frame_(pressed_frame),
        action_(std::move(action)),
        click_sound_(std::move(click_sound)),
        on_hover_(std::move(on_hover)) {}

  bool handle(ui::Dialog& d, ui::Msg msg, int, const ui::Event&) override {
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(d, *this, area_, 0, key_, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        d.set_repeat_interval(0);  // the definitions' repeat interval is 0
        d.add_dirty(place_.draw(d, pressed_frame_));
        if (!pressed) {
          click_sound_();
          action_();
        }
        return true;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        d.add_dirty(place_.draw(d, frame_));
        return true;
      default:
        return false;
    }
  }
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int, const ui::Event&) override { on_hover_(on); }

  bool disabled = false;

 private:
  ui::Rect area_;
  std::uint32_t key_;
  SpritePlace place_;
  std::size_t frame_;
  std::size_t pressed_frame_;
  std::function<void()> action_;
  std::function<void()> click_sound_;
  std::function<void(bool)> on_hover_;
};

// push_button_proc (0x49613): a two-state switch drawn as frame + state.
class SpriteSwitch : public ui::Control {
 public:
  SpriteSwitch(ui::Rect area, SpritePlace place, std::size_t frame, std::function<bool(bool toggle)> state,
               std::function<void()> click_sound, std::function<void(bool)> on_hover)
      : area_(area),
        place_(place),
        frame_(frame),
        state_(std::move(state)),
        click_sound_(std::move(click_sound)),
        on_hover_(std::move(on_hover)) {}

  bool handle(ui::Dialog& d, ui::Msg msg, int, const ui::Event&) override {
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(d, *this, area_, 0, 0, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        if (!pressed) {
          click_sound_();
          state_(true);
        }
        [[fallthrough]];
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        d.add_dirty(place_.draw(d, frame_ + (state_(false) ? 1 : 0)));
        return true;
      default:
        return false;
    }
  }
  void hover(bool on, int, const ui::Event&) override { on_hover_(on); }

 private:
  ui::Rect area_;
  SpritePlace place_;
  std::size_t frame_;
  std::function<bool(bool)> state_;
  std::function<void()> click_sound_;
  std::function<void(bool)> on_hover_;
};

// A control redrawn on paint only (`deckgun_draw_sprite_control`,
// `digit_counter_proc`'s display, the quality tape).
class Display : public ui::Control {
 public:
  explicit Display(std::function<ui::Rect(ui::Dialog&)> paint) : paint_(std::move(paint)) {}
  bool handle(ui::Dialog& d, ui::Msg msg, int, const ui::Event&) override {
    if (msg != ui::Msg::kPaint) {
      return false;
    }
    d.add_dirty(paint_(d));
    return true;
  }

 private:
  std::function<ui::Rect(ui::Dialog&)> paint_;
};

}  // namespace

// The controls whose state the update changes.
struct DeckGunStation::Controls {
  ui::Area* train_left = nullptr;
  ui::Area* train_right = nullptr;
  ui::Area* lock = nullptr;
  ui::Area* elevate_up = nullptr;
  ui::Area* elevate_down = nullptr;
  SpriteButton* fire = nullptr;
  Display* range = nullptr;
  Display* ammo = nullptr;
  Display* quality = nullptr;
  Display* ready = nullptr;
  Display* gun_label = nullptr;
  SpriteSwitch* manual = nullptr;
};

DeckGunStation::DeckGunStation(AttackServices& services)
    : services_(services),
      dialog_(std::make_unique<ui::Dialog>(services.gui(), 0, 0, kScreenWidth, kScreenHeight, ui::kDialogNoNavigation,
                                           &services.screen())),
      controls_(std::make_unique<Controls>()),
      colours_(station_colours()) {
  // deckgun_init_dialogs (0x36054): dialog 0x11c228, in its control order.
  ui::Dialog& d = *dialog_;
  Controls& c = *controls_;
  auto cursor = [this](ui::CursorId id) {
    return [this, id](bool on) {
      if (on) {
        services_.set_cursor(id);
      } else {
        services_.set_default_cursor();
      }
    };
  };
  auto click = [this] { services_.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume); };
  const SpritePlace at{&sprites_, 0, 0};
  c.train_left = &add_train_area(
      d, 0, 0x121, 0x57, 0x1ca, 0x332c, [this](std::int32_t s) { train(s, -1); },
      cursor(ui::CursorId::kRotateLeftFast));
  c.train_right = &add_train_area(
      d, 0x23b, 0x121, 0x27f, 0x1ca, 0x342e, [this](std::int32_t s) { train(s, 1); },
      cursor(ui::CursorId::kRotateRightFast));
  d.emplace<SpriteButton>(
      kZoomKnobArea, 0xff5a, SpritePlace{&sprites_, 0x100, 0xac}, kZoomKnob, kZoomKnob + 1,
      [this] { cycle_zoom(); }, click, cursor(ui::CursorId::kZoom));
  c.lock = &add_hotkey(d, 0x3920, [this] { toggle_lock(); });
  // The range (5 digits) and rounds (3 digits) counters (digit_counter_proc).
  auto counter = [this](int x0, int y0, int x1, int y1, std::function<std::int32_t()> value, std::int32_t digits) {
    return [this, x0, y0, x1, y1, value = std::move(value), digits](ui::Dialog& dlg) {
      const Viewport vp = dlg.gfx();
      services_.screen().blit(crop(background_, x0, y0, x1 - x0 + 1, y1 - y0 + 1), x0, y0);
      draw_string(vp, x0, y0, digits_, padded(value(), digits), colours_);
      return ui::Rect{x0, y0, x1, y1};
    };
  };
  DeckGun& gun = services_.mission().player_combat().deck_gun;
  c.range = &d.emplace<Display>(counter(0x17c, 0x1a0, 0x1bc, 0x1b2, [&gun] { return gun.range_yd(); }, 5));
  c.ammo = &d.emplace<Display>(counter(0x1c4, 0x168, 0x1ea, 0x17a, [&gun] { return gun.ammo(); }, 3));
  c.quality = &d.emplace<Display>([this, &gun](ui::Dialog&) {
    draw_linear_tape(services_.screen(), background_, services_.bearing_font(), gun.quality(), 0x1db, 0x19f, 0x225,
                     0x1b2, 0);
    return ui::Rect{0x1db, 0x19f, 0x225, 0x1b2};
  });
  c.ready = &d.emplace<Display>([this, at](ui::Dialog& dlg) {
    return SpritePlace{at.sprites, 0x1f8, 0x15d}.draw(dlg, ready_ ? kReadyLampOff + 1 : kReadyLampOff);
  });
  c.fire = &d.emplace<SpriteButton>(
      ui::Rect{0x124, 0x184, 0x156, 0x1b7}, 0xff46, SpritePlace{&sprites_, 0x124, 0x184}, kFireButton, kFireButton + 1,
      [this] { fire(); }, click, cursor(ui::CursorId::kFire));
  c.elevate_up = &add_train_area(
      d, 0x247, 0x5b, 0x27f, 0xb5, 0, [this](std::int32_t s) { elevate(s, 1); }, cursor(ui::CursorId::kRaiseGun));
  c.elevate_down = &add_train_area(
      d, 0x247, 0xb6, 0x27f, 0x111, 0, [this](std::int32_t s) { elevate(s, -1); }, cursor(ui::CursorId::kLowerGun));
  c.gun_label = &d.emplace<Display>([this, at](ui::Dialog& dlg) {
    return SpritePlace{at.sprites, 0x1b7, 0x155}.draw(dlg, label_gun_ != 0 ? kAftLabel : kAftLabel + 1);
  });
  add_hotkey(d, 0xff47, [this] { select_gun(services_.mission().player_combat().deck_gun.selected() == 0 ? 1 : 0); });
  c.manual = &d.emplace<SpriteSwitch>(
      ui::Rect{0x17e, 0x159, 0x1b6, 0x18b}, SpritePlace{&sprites_, 0x17e, 0x159}, kManualSwitch,
      [this](bool toggle) {
        if (toggle) {
          toggle_manual();
        }
        return services_.mission().player_combat().deck_gun.manual();
      },
      click, cursor(ui::CursorId::kSet));
}

DeckGunStation::~DeckGunStation() = default;

void DeckGunStation::open_command_bar() {
  // The menus 0x11c1e0 (one gun), 0x11c210 (forward selected), 0x11c1f8
  // (aft selected): the bridge, then the other gun.
  CommandBarMenu menu;
  menu.items.push_back({14, 15, [this] { services_.go_to(StationId::kBridge); }});
  if (services_.mission().player_class().deck_gun_count != 1) {
    if (services_.mission().player_combat().deck_gun.selected() == 0) {
      menu.items.push_back({32, 33, [this] { pending_gun_ = 1; }});
    } else {
      menu.items.push_back({30, 31, [this] { pending_gun_ = 0; }});
    }
  }
  services_.host().open_command_bar(menu);
}

void DeckGunStation::open() {
  // deckgun_screen (0x36cd9)
  PcxImage pcx = load_pcx(services_.install(), "ART/DECKGUN.PCX");
  background_ = std::move(pcx.image);
  services_.screen().blit(background_, 0, 0);
  services_.palette() = pcx.palette;
  pcx_palette_ = pcx.palette;
  services_.shade_palette(&pcx_palette_);
  sprites_ = Shp::load(services_.install(), "ART/DECKGUN.SHP");
  digits_ = Font::load(services_.install(), "ART/DIGITS3.FNT");
  dialog_->open();
  if (services_.features.drag_rotate) {
    // The sight's rows across the screen (and beside it on a wide
    // window), but the zoom knob; the train areas.
    constexpr int kAny = std::numeric_limits<int>::max();
    drag_ = std::make_unique<ViewDrag>(drag_hooks());
    drag_->set_regions({{-kAny, kViewY, kAny, kViewY + kViewRows - 1},
                        {0, 0x121, 0x57, 0x1ca},
                        {0x23b, 0x121, 0x27f, 0x1ca}});
    drag_->set_holes({kZoomKnobArea});
    services_.gui().mouse().set_band_rows(std::pair{kViewY, kViewY + kViewRows - 1});
  }
  last_tick_ = services_.gui().ticks();
  fire_ticks_ = 0;
  zoom_ticks_ = 0;
  view_ticks_ = 0;
  zoom_offset_ = 0;
  zoom_speed_ = 0;
  fire_frame_ = -1;
  range_readout_ = 0;
  const DeckGun& gun = services_.mission().player_combat().deck_gun;
  label_gun_ = gun.selected();
  open_command_bar();
  drawn_train_ = -1;
  drawn_range_ = -1;
  drawn_ammo_ = -1;
  drawn_quality_ = -1;
  zoom_plate_ = true;
  drawn_elevation_ = -1;
  drawn_gun_ = -1;
  drawn_manual_ = -2;
  ready_ = gun.mount(gun.selected()).ready;
  last_reload_ = gun.mount(gun.selected()).reload;
  services_.host().fade_out();
}

void DeckGunStation::event(const ui::Event& event) {
  if (drag_ && drag_->event(event, services_.gui().ticks())) {
    return;
  }
  dialog_->handle_event(event);
}

void DeckGunStation::process() { dialog_->process(); }

void DeckGunStation::close() {
  // deckgun_close (0x36f34)
  if (drag_) {
    drag_.reset();
    services_.gui().mouse().set_band_rows(std::nullopt);
  }
  dialog_->close();
}

void DeckGunStation::advance_timers() {
  const auto d = static_cast<std::int32_t>(services_.elapsed(last_tick_));
  view_ticks_ += d;
  zoom_ticks_ += d;
  fire_ticks_ += d;
}

void DeckGunStation::train(std::int32_t step, int sign) {
  // deckgun_train_left / deckgun_train_right
  DeckGun& gun = services_.mission().player_combat().deck_gun;
  const std::int32_t n = std::clamp(step, 1, 50);
  DeckGun::Mount& m = gun.mount(gun.selected());
  m.train = Angle10(clamp_train_step(gun.selected(), m.train.tenths(), sign < 0 ? -n : n));
  gun.toggle_bearing_follow(true);
}

void DeckGunStation::elevate(std::int32_t step, int sign) {
  // deckgun_elevate_up / deckgun_elevate_down
  DeckGun& gun = services_.mission().player_combat().deck_gun;
  const std::int32_t n = std::clamp(step, 1, 3);
  DeckGun::Mount& m = gun.mount(gun.selected());
  const std::int32_t offset = m.elevation_offset.tenths();
  m.elevation_offset = Angle10(sign > 0 ? fx::angle_add(Angle10(offset), Angle10(n)).tenths()
                                        : fx::angle_sub(Angle10(offset), Angle10(n)).tenths());
  if (fx::angle_add(m.elevation_offset, m.elevation).tenths() > kMaxElevation) {
    m.elevation_offset = fx::angle_sub(Angle10(sign > 0 ? kMaxElevation : 0), m.elevation);
  }
}

ViewDrag::Hooks DeckGunStation::drag_hooks() {
  // Not in the original: the train and the elevation offset from their
  // values at the press, within the tower stops (`clamp_train_step`) and
  // the elevation limits of `elevate`, the bearing follow stopped.
  struct Start {
    std::int32_t mount = 0;
    std::int32_t train = 0;
    std::int32_t offset = 0;  // signed tenths
  };
  auto start = std::make_shared<Start>();
  ViewDrag::Hooks h;
  h.tenths_per_px = [this] { return view_tenths_per_pixel(services_.zoom); };
  h.capture = [this, start] {
    DeckGun& gun = services_.mission().player_combat().deck_gun;
    const DeckGun::Mount& m = gun.mount(gun.selected());
    start->mount = gun.selected();
    start->train = m.train.normalized().tenths();
    start->offset = fx::angle_diff(m.elevation_offset, Angle10(0));
  };
  h.turn = [this, start](double across, double up) {
    DeckGun& gun = services_.mission().player_combat().deck_gun;
    if (gun.selected() != start->mount) {
      return;
    }
    DeckGun::Mount& m = gun.mount(start->mount);
    const auto a = static_cast<std::int32_t>(std::lround(across));
    m.train = Angle10(clamp_train_step(start->mount, start->train, a));
    gun.toggle_bearing_follow(true);
    const std::int32_t elevation = fx::angle_diff(m.elevation, Angle10(0));
    const std::int32_t offset = std::clamp(start->offset + static_cast<std::int32_t>(std::lround(up)), -elevation,
                                           kMaxElevation - elevation);
    m.elevation_offset = Angle10(offset).normalized();
  };
  h.release_dialog = [this] { release_dialog_press(*dialog_); };
  h.cursor = [this](bool on) {
    on ? services_.set_cursor(ui::CursorId::kMove) : services_.set_default_cursor();
  };
  h.up_tenths_per_px = 0.5;
  h.inertia = false;
  // The crew's fire disables the aiming controls, the drag with them.
  h.enabled = [this] { return services_.mission().player_combat().deck_gun.manual(); };
  return h;
}

void DeckGunStation::cycle_zoom() {
  // deckgun_cycle_zoom (0x379cd): the view goes black with the sight until
  // it is rendered again 10 ticks later.
  services_.zoom = next_magnification(services_.zoom);
  zoom_plate_ = true;
  zoom_speed_ = 10;
  zoom_ticks_ = 100;
  view_ticks_ = -5;
  const Viewport view = services_.world_window(0, 0, 200, 199);
  ui::fill_rect(view, 0, 0, 200, 199, 0);
  draw_sprite_clipped(view, sprites_.frame(kReticleFrame), 0, 0);
  services_.blit_to_screen(view, kViewX, kViewY);
}

void DeckGunStation::fire() {
  // deckgun_fire_selected (0x37a84)
  Mission& mission = services_.mission();
  PlayerCombat& combat = mission.player_combat();
  DeckGun& gun = combat.deck_gun;
  const std::int32_t m = gun.selected();
  if (!gun.mount(m).ready) {
    return;
  }
  SimContext ctx = mission.sim().context();
  gun.fire(ctx, mission.player(), m, combat.crew);
  // deckgun_start_fire_anim (0x37278)
  fire_ticks_ = 10;
  fire_frame_ = 0;
  last_reload_ = gun.mount(m).reload;
}

void DeckGunStation::toggle_lock() {
  Mission& mission = services_.mission();
  PlayerCombat& combat = mission.player_combat();
  const SimContext ctx = mission.sim().context();
  combat.deck_gun.toggle_target_lock(combat.own_boat(mission.player(), ctx), mission.sim().vehicles(), combat.sensing);
}

void DeckGunStation::toggle_manual() {
  DeckGun& gun = services_.mission().player_combat().deck_gun;
  gun.set_manual(!gun.manual());
}

void DeckGunStation::select_gun(std::int32_t mount) {
  if (services_.mission().player_class().deck_gun_count == 1) {
    return;
  }
  services_.mission().player_combat().deck_gun.select(mount);
  services_.host().close_command_bar();
  open_command_bar();
}

void DeckGunStation::draw_tower(const Viewport& view) {
  // 0x373c6: at 1x the forward gun sees the conning tower over the boat's
  // centre at sea level.
  if (services_.zoom.raw() > 0x10000) {
    return;
  }
  const ViewCamera& cam = services_.world().camera();
  const fx::Vec3 p = services_.mission().player().position();
  const fx::Vec3 v = fx::transform(cam.view, {p.x - cam.x, cam.y, p.z - cam.z});
  if (v.z <= 0) {
    return;
  }
  const int x = v.x * 0x398 / v.z + cam.half_width;
  const int y = v.y * 0x1cc / v.z + kHorizonRow;
  draw_sprite_clipped(view, sprites_.frame(kTowerFrame), x, y);
}

void DeckGunStation::draw_target_marker(const Viewport& view) {
  // deckgun_draw_target_marker (0x382a4): in manual aim, the target's
  // marker blinks until locked.
  const DeckGun& gun = services_.mission().player_combat().deck_gun;
  if (!gun.manual() || gun.target() == nullptr || (!gun.locked() && gun.blink())) {
    return;
  }
  const fx::Vec3 p = gun.target()->position();
  const ViewEye eye = deck_gun_eye(services_.mission().player(), gun.mount(gun.selected()), services_.zoom);
  const std::optional<int> x = services_.world().project_x(eye, p.x, p.z);
  const Shp& global = services_.global_sprites();
  if (!x || global.frame_count() <= kTargetMarkerFrame) {
    return;
  }
  ColourRemap remap = colours_;
  remap[1] = kTargetMarkerColour;
  draw_sprite_remapped(view, global.frame(kTargetMarkerFrame), *x, kMarkerY, remap);
}

void DeckGunStation::render_view() {
  const Mission& mission = services_.mission();
  const DeckGun& gun = mission.player_combat().deck_gun;
  services_.render_world(deck_gun_eye(mission.player(), gun.mount(gun.selected()), services_.zoom));
  const Viewport view = services_.world_window(0, 0, 200, 199);
  draw_tower(view);
  draw_target_marker(view);
  draw_sprite_clipped(view, sprites_.frame(kReticleFrame), 0, 0);
  // The range of the elevation, centred at the top of the sight with glyph
  // 0xe0 in colour 0xfe.
  GlyphColourTable table = colours_;
  table[0xe0] = 0xfe;
  const std::string text = std::to_string(range_readout_);
  const Font& font = services_.bearing_font();
  draw_string(view, 100 - font.text_width(text) / 2, 3, font, text, table);
  services_.blit_to_screen(view, kViewX, kViewY);
}

void DeckGunStation::draw_elevation_dial() {
  // deckgun_draw_elevation_dial (0x374d5)
  const DeckGun& gun = services_.mission().player_combat().deck_gun;
  const DeckGun::Mount& m = gun.mount(gun.selected());
  const std::int32_t e = fx::angle_add(m.elevation, m.elevation_offset).tenths();
  if (e == drawn_elevation_) {
    return;
  }
  drawn_elevation_ = e;
  range_readout_ = ballistic_range_for_elevation(kVelocity, Angle10(e)) / 3;
  draw_elevation_tape(services_.screen(), background_, services_.bearing_font(), e, 0x249, 0x92, 0x25c, 0xd8,
                      kTapeColour, colours_);
}

void DeckGunStation::draw_fire_animation() {
  // deckgun_draw_fire_anim (0x3728d): GUNEXPL.SHP over the breech, a frame
  // every 10 ticks, then one clean redraw.
  if (fire_frame_ == -1 || fire_ticks_ < 10) {
    return;
  }
  fire_ticks_ = 0;
  const Viewport vp{&services_.screen(), kBreechX0, kBreechY0, kBreechX1, kBreechY1};
  draw_sprite_clipped(vp, sprites_.frame(kBreechFrame), 0, 0);
  const Shp& flash = services_.world().assets().effect(EffectSprite::kGunExplosion);
  if (fire_frame_ >= 0 && static_cast<std::size_t>(fire_frame_) < flash.frame_count()) {
    draw_sprite_clipped(vp, flash.frame(static_cast<std::size_t>(fire_frame_)), 0x2f, 0x32);
  }
  draw_sprite_clipped(vp, sprites_.frame(kBreechTopFrame), 0, 0);
  if (fire_frame_ == -2) {
    fire_frame_ = -1;
    return;
  }
  ++fire_frame_;
  if (static_cast<std::size_t>(fire_frame_) >= flash.frame_count()) {
    fire_frame_ = -2;
  }
}

void DeckGunStation::draw_zoom_indicator() {
  // deckgun_draw_zoom_indicator (0x37562): the new label slides in under
  // the plate, a step every 5 ticks.
  if (!zoom_plate_) {
    return;
  }
  const std::int32_t z = std::clamp(services_.zoom.floor_int(), 0, 8);
  const std::size_t label = static_cast<std::size_t>(game_exe().i32(kZoomLabel + 4 * static_cast<std::uint32_t>(z)) + 1);
  const SpriteFrame& f = sprites_.frame(label);
  const int w = f.width();
  if (zoom_speed_ != 0) {
    if (zoom_ticks_ < 5) {
      return;
    }
    zoom_ticks_ = 0;
    if (zoom_offset_ > w) {
      zoom_speed_ = -(w - 5);
    }
    zoom_offset_ = std::max(zoom_offset_ + zoom_speed_, 0);
  }
  const Viewport vp{&services_.screen(), kZoomPlateX, kZoomPlateY, kZoomPlateX + w - 1, kZoomPlateY + f.height() - 1};
  draw_sprite_clipped(vp, f, zoom_offset_ >> 1, 0);
  draw_sprite_clipped(vp, sprites_.frame(kZoomPlateFrame), zoom_offset_ - w, 0);
  if (zoom_offset_ == 0) {
    zoom_plate_ = false;
  }
}

void DeckGunStation::update() {
  // deckgun_update (0x36fa4)
  Mission& mission = services_.mission();
  if (mission.player().position().y + mission.player().sub().bridge_height_ft * kUnitsPerFoot <= 0) {
    services_.go_to(StationId::kConn);
    return;
  }
  // A bar button's gun switch rebuilds the bar, so not from inside it.
  if (pending_gun_) {
    const std::int32_t mount = *pending_gun_;
    pending_gun_.reset();
    select_gun(mount);
  }
  advance_timers();
  if (drag_) {
    drag_->update(services_.gui().ticks());
  }
  const bool shade = services_.advance_world();
  if (services_.redraw_requested()) {
    dialog_->invalidate();
  }
  PlayerCombat& combat = mission.player_combat();
  DeckGun& gun = combat.deck_gun;
  {
    const SimContext ctx = mission.sim().context();
    gun.pick_target_on_bearing(combat.own_boat(mission.player(), ctx), mission.sim().vehicles(), combat.sensing);
  }
  draw_elevation_dial();
  // The crew's shots of the selected gun start the muzzle flash too
  // (deckgun_fire calls deckgun_start_fire_anim).
  const DeckGun::Mount& selected = gun.mount(gun.selected());
  if (selected.reload > last_reload_) {
    fire_ticks_ = 10;
    fire_frame_ = 0;
  }
  last_reload_ = selected.reload;
  draw_fire_animation();
  if (first_frame() || view_ticks_ > 5 || drawn_train_ != selected.train.tenths()) {
    view_ticks_ = 0;
    render_view();
  }
  if (drawn_train_ != selected.train.tenths()) {
    // deckgun_draw_train_dial (0x37485)
    drawn_train_ = selected.train.tenths();
    draw_bearing_tape(services_.screen(), background_, services_.bearing_font(), selected.train, 0x19e, 10, 0x1e6, 0x19,
                      kTapeColour, colours_);
  }
  draw_zoom_indicator();
  Controls& c = *controls_;
  if (drawn_range_ != gun.range_yd()) {
    c.range->dirty = true;
    drawn_range_ = gun.range_yd();
  }
  if (drawn_ammo_ != gun.ammo()) {
    c.ammo->dirty = true;
    drawn_ammo_ = gun.ammo();
  }
  if (drawn_quality_ != gun.quality()) {
    c.quality->dirty = true;
    drawn_quality_ = gun.quality();
  }
  if (ready_ != selected.ready) {
    ready_ = selected.ready;
    c.ready->dirty = true;
  }
  if (drawn_gun_ != gun.selected()) {
    c.gun_label->dirty = true;
    drawn_gun_ = gun.selected();
    label_gun_ = gun.selected();
  }
  const std::int32_t manual = gun.manual() ? 1 : 0;
  if (drawn_manual_ != manual) {
    // The crew's fire disables the aiming controls.
    drawn_manual_ = manual;
    const bool off = manual == 0;
    c.train_left->disabled = off;
    c.train_right->disabled = off;
    c.lock->disabled = off;
    c.fire->disabled = off;
    c.elevate_up->disabled = off;
    c.elevate_down->disabled = off;
    c.manual->dirty = true;
  }
  if (first_frame()) {
    services_.host().fade_in();
  }
  if (shade) {
    services_.shade_palette(&pcx_palette_);
  }
}

}  // namespace sh

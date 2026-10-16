#include "game/stations/radar.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "core/install.h"
#include "game/mission.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "sim/sensor_environment.h"
#include "ui/controls.h"
#include "ui/dial_scrub.h"
#include "ui/dialog.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The option bits of the radar fit (0x11a85c).
constexpr std::uint32_t kSjMask = 0x30000;
constexpr std::uint32_t kSjImproved = 0x10000;  // levels above 1: the improved SJ or the ST
constexpr std::uint32_t kSdFitted = 0x40000;
constexpr std::uint32_t kPpiFitted = 0x80000;

// RADAR.SHP frames.
constexpr std::size_t kFramePpi = 0;
constexpr std::size_t kFrameSjAScope = 1;
constexpr std::size_t kFrameSdAScope = 2;
constexpr std::int32_t kFrameSjRange = 3;  // + scale 0..4
constexpr std::int32_t kFrameSdRange = 8;  // + scale 0..4
constexpr std::size_t kFrameSmallBlip = 13;
constexpr std::size_t kFrameBigBlip = 14;
constexpr std::size_t kFrameNeedle = 15;
constexpr std::int32_t kFrameModeSwitch = 16;  // + 0 PPI, 1 A-scope, 2 off
constexpr std::int32_t kFrameHold = 19;        // + hold
constexpr std::int32_t kFrameSdPower = 21;     // + off
constexpr std::size_t kFramePpiLabel = 23;
constexpr std::size_t kFrameSjLabel = 24;  // 25: ST

// A screen rectangle.
struct Box {
  int x0, y0, x1, y1;
};

// Draws `value` zero-padded to `digits` at (x, y) of `vp` through `colours`
// (`draw_padded_number` with `int_to_padded_str`).
void draw_padded_number(const Viewport& vp, int x, int y, std::int32_t value, int digits, const Font& font,
                        const GlyphColourTable& colours) {
  digits = std::min(digits, 10);
  if (value < 0) {
    value = 0;
  }
  std::string text(static_cast<std::size_t>(digits), '0');
  std::int32_t place = 1;
  for (int i = digits - 1; i >= 0; --i) {
    text[static_cast<std::size_t>(i)] = static_cast<char>('0' + value / place % 10);
    place *= 10;
  }
  for (const char c : text) {
    x += std::max(font_draw_char(vp, x, y, font, static_cast<std::uint8_t>(c), &colours), 0);
  }
}

}  // namespace

fx::Point2 ppi_blip_offset(const RadarBlip& blip, std::int32_t radius, std::int32_t range_yd) {
  // sj_radar_draw_ppi (0x657db)
  const std::int32_t r = blip.range_yd * radius / range_yd;
  return fx::point_rotate_scale({0, -r}, {0, 0}, blip.bearing);
}

std::int32_t ascope_blip_column(const RadarBlip& blip, std::int32_t range_yd) {
  // radar_draw_ascope (0x65c51)
  return blip.range_yd * 0xb4 / range_yd;
}

struct RadarStation::Impl {
  explicit Impl(AttackServices& s) : services(s) {}

  AttackServices& services;
  Shp sprites;                     // RADAR.SHP (0x12556c)
  std::optional<Font> digit_font;  // DIGITS3.FNT (0x125570)
  Image8 background;               // the screen as opened, under the knob
  Image8 scratch;
  // 0x1255dc: identity but 0xff (0, BSS); entries 0x10..0x17 are the PPI
  // blip shades.
  GlyphColourTable colours{};
  std::optional<ui::Dialog> sj_dialog;  // 0x1256e0, while the SJ is fitted
  std::optional<ui::Dialog> sd_dialog;  // 0x1259e4, while the SD is fitted
  std::optional<Box> sj_view;           // 0x125554
  std::optional<Box> sd_view;           // 0x125598
  // Controls whose dirty and disabled flags the updates set.
  ui::Area* range_readout = nullptr;    // 0x125894
  ui::Area* bearing_counter = nullptr;  // 0x1257ec
  ui::Area* knob = nullptr;             // 0x125824
  ui::Area* ppi_area = nullptr;         // 0x1258cc
  ui::Area* mode_switch = nullptr;      // 0x1259ac
  ui::Area* sd_power = nullptr;         // 0x125af0
  ui::Area* sd_readout = nullptr;       // 0x125b28
  // Display state.
  std::int32_t bearing_deg = 0;        // 0x125574
  std::int32_t bearing_tenths = 0;     // 0x125578
  std::int32_t shown_nearest = -1;     // 0x12558c
  std::int32_t shown_damage = -1;      // 0x125590
  std::int32_t shown_mode = -1;        // 0x125594
  std::int32_t sd_shown_nearest = -1;  // 0x1255b8
  std::int32_t sd_shown_damage = -1;   // 0x1255bc
  std::int32_t sd_shown_on = -1;       // 0x1255c0
  // Extended `precise_dials` (not in the original): the scrubber of the
  // train knob and the bearing counter (null when off), their indices and
  // the ghosts' colours.
  std::unique_ptr<ui::DialScrubber> scrub;
  std::array<std::size_t, 2> scrub_index{};
  std::uint8_t ghost = 0;
  std::uint8_t ghost_back = 0;

  Mission& mission() { return services.mission(); }
  RadarSets& radar() { return mission().sim().sensors().radar(); }
  std::uint32_t options() { return mission().sim().conditions().options; }
  std::uint8_t sj_damage() {
    return mission().player().systems()[static_cast<std::size_t>(PlayerSystem::kSjRadar)].flags;
  }
  std::uint8_t sd_damage() {
    return mission().player().systems()[static_cast<std::size_t>(PlayerSystem::kSdRadar)].flags;
  }
  Viewport screen_vp(const Box& b) { return {&services.screen(), b.x0, b.y0, b.x1, b.y1}; }

  void open();
  void init_sj_display();
  void init_sd_display();
  void build_sj_dialog();
  void build_sd_dialog();
  void sj_display_update();
  void sd_display_update();
  void sj_redraw();
  void draw_sj_ascope();
  void draw_sj_ppi();
  void draw_sd();
  void draw_ascope(const Viewport& vp, const std::vector<RadarBlip>& blips, int baseline, int width, std::int32_t range,
                   std::uint8_t base);
  void draw_blips(const Viewport& vp, std::size_t frame, std::uint8_t base, std::int32_t limit, std::int32_t radius,
                  std::int32_t range, fx::Point2 centre);
  // Adds a sprite push button (`push_button_proc` with its definition):
  // `state(pressed)` acts on a press and returns the frame offset.
  ui::Area& add_push_button(ui::Dialog& d, Box box, std::int32_t first_frame, std::function<int(bool)> state);
  // `pending` (extended `precise_dials`) returns the pending value the
  // counter shows instead, if any.
  ui::Area& add_digit_counter(ui::Dialog& d, Box box, std::function<std::int32_t&()> value, int digits,
                              std::function<void()> on_change,
                              std::function<std::optional<std::int32_t>()> pending = {});
  void draw_knob();
  // Extended `precise_dials`: registers the train knob and the bearing
  // counter, which take a pending bearing while the beam is held.
  void add_scrub_targets();
  // Returns the pending bearing (tenths) of the knob or the counter, if any.
  std::optional<std::int32_t> pending_bearing() const;
  void hover(ui::Area& a, ui::CursorId cursor);
  std::uint8_t sj_base() { return (options() & kSjMask) > kSjImproved ? 0x18 : 0x10; }
};

void RadarStation::Impl::hover(ui::Area& a, ui::CursorId cursor) {
  a.on_hover = [this, cursor](bool on, int, const ui::Event&) {
    if (on) {
      services.set_cursor(cursor);
    } else {
      services.set_default_cursor();
    }
  };
}

ui::Area& RadarStation::Impl::add_push_button(ui::Dialog& d, Box box, std::int32_t first_frame,
                                              std::function<int(bool)> state) {
  // push_button_proc (0x49613): a press acts with a click; the press and
  // the paint draw the state's frame at the corner (push_button_draw).
  auto on_message = [this, box, first_frame, state = std::move(state)](ui::Dialog& dlg, ui::Area& self, ui::Msg msg,
                                                                       int, const ui::Event&) {
    if (msg != ui::Msg::kPress && msg != ui::Msg::kPaint) {
      return 0;
    }
    if (msg == ui::Msg::kPress) {
      if (self.pressed) {
        return 0;
      }
      services.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume);
      state(true);
    }
    const auto frame = static_cast<std::size_t>(first_frame + state(false));
    draw_sprite_clipped(dlg.gfx(), sprites.frame(frame), box.x0, box.y0);
    return -1;
  };
  ui::Area& a = d.emplace<ui::Area>(box.x0, box.y0, box.x1, box.y1, std::move(on_message), ui::Area::ActivateFn{});
  hover(a, ui::CursorId::kSet);
  return a;
}

ui::Area& RadarStation::Impl::add_digit_counter(ui::Dialog& d, Box box, std::function<std::int32_t&()> value,
                                                int digits, std::function<void()> on_change,
                                                std::function<std::optional<std::int32_t>()> pending) {
  // digit_counter_proc (0x4ac23): a click on a digit increments it (9 -> 0).
  auto on_message = [this, box, value = std::move(value), digits, on_change = std::move(on_change),
                     pending = std::move(pending)](ui::Dialog& dlg, ui::Area& self, ui::Msg msg, int,
                                                   const ui::Event& event) {
    if (msg != ui::Msg::kPress && msg != ui::Msg::kPaint) {
      return 0;
    }
    if (msg == ui::Msg::kPress) {
      if (self.pressed) {
        return 0;
      }
      const int step = (box.x1 - box.x0 + 1) / digits;
      int index = 0;
      for (int edge = step; index < digits && edge <= event.x - box.x0 - dlg.x(); edge += step) {
        ++index;
      }
      std::int32_t place = 1;
      for (int i = 0; i < digits - 1 - index; ++i) {
        place *= 10;
      }
      std::int32_t& v = value();
      std::int32_t digit = v / place;
      if (digit > 9) {
        digit %= 10;
      }
      std::int32_t next = digit + 1;
      v -= digit * place;
      if (next > 9) {
        next = 0;
      }
      v += next * place;
      services.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume);
      if (on_change) {
        on_change();
      }
    }
    if (const std::optional<std::int32_t> v = pending ? pending() : std::nullopt) {
      // Not in the original: the pending value, dark digits on amber cells.
      draw_padded_number(dlg.gfx(), box.x0, box.y0, *v, digits, *digit_font,
                         ui::ghost_digit_colours(colours, ghost, ghost_back));
      return -1;
    }
    // digit_counter_draw (0x4ad1e)
    draw_padded_number(dlg.gfx(), box.x0, box.y0, value(), digits, *digit_font, colours);
    return -1;
  };
  ui::Area& a = d.emplace<ui::Area>(box.x0, box.y0, box.x1, box.y1, std::move(on_message), ui::Area::ActivateFn{});
  hover(a, ui::CursorId::kUp);
  return a;
}

void RadarStation::Impl::draw_knob() {
  // knob_dial_draw (0x4a0ac): the needle turned by the bearing over the
  // knob's background.
  constexpr Box kKnob{0xfe, 0x12b, 0x12a, 0x157};
  Image8& screen = services.screen();
  for (int y = kKnob.y0; y <= kKnob.y1; ++y) {
    for (int x = kKnob.x0; x <= kKnob.x1; ++x) {
      screen.set(x, y, background.at(x, y));
    }
  }
  sprite_draw_transformed(screen_vp(kKnob), sprites.frame(kFrameNeedle), 0x114 - kKnob.x0, 0x141 - kKnob.y0, scratch,
                          Angle10(bearing_tenths), Fixed16::from_int(1), Fixed16::from_int(1), 0);
  if (const std::optional<std::int32_t> v = pending_bearing()) {
    // Not in the original: the pending bearing's ghost needle, over the
    // needle's span at the knob's rim.
    ui::draw_ghost_needle(screen_vp(kKnob), 0x114 - kKnob.x0, 0x141 - kKnob.y0, *v, 12, 21, ghost);
  }
}

void RadarStation::Impl::add_scrub_targets() {
  // Not in the original: the bearing of the held beam by tenths, 1 degree
  // steps, on the knob or the counter (extended `precise_dials`). Both are
  // disabled while the beam sweeps.
  scrub = std::make_unique<ui::DialScrubber>();
  ghost = ui::ghost_colour(services.palette());
  ghost_back = ui::nearest_colour(services.palette(), 16, 16, 16);
  const auto add = [this](ui::Area& a) {
    return scrub->add({
        .rect = {a.x0, a.y0, a.x1, a.y1},
        .scale = {.units_per_px = 2.5, .min = 0, .max = 3599, .wrap = true, .snap = 10},
        .current = [this] { return Angle10(bearing_tenths).normalized().tenths(); },
        .commit =
            [this](std::int32_t v) {
              // As the knob's press does (sj_radar_set_bearing).
              bearing_tenths = v;
              bearing_deg = v / 10;
              radar().sj().beam = v << 16;
            },
        .tap =
            [this, &a](const ui::Event& e) {
              a.handle(*sj_dialog, ui::Msg::kPress, 0, e);
              a.dirty = true;
            },
        .changed =
            [this] {
              knob->dirty = true;
              bearing_counter->dirty = true;
            },
        .enabled = [&a] { return !a.disabled; },
    });
  };
  scrub_index = {add(*knob), add(*bearing_counter)};
}

std::optional<std::int32_t> RadarStation::Impl::pending_bearing() const {
  if (scrub) {
    for (const std::size_t i : scrub_index) {
      if (const std::optional<std::int32_t> v = scrub->pending(i)) {
        return v;
      }
    }
  }
  return std::nullopt;
}

void RadarStation::Impl::build_sj_dialog() {
  // radar_static_init (0x64798): the SJ dialog 0x1256e0.
  ui::Dialog& d = sj_dialog.emplace(services.gui(), 0, 0, kScreenWidth, kScreenHeight, 0, &services.screen());
  // The range knob (def 0x10ccf4): sj_radar_cycle_range.
  add_push_button(d, {0x8b, 0x131, 0xc8, 0x170}, kFrameSjRange, [this](bool pressed) {
    std::int32_t& scale = radar().sj().scale;
    if (pressed && ++scale > 4) {
      scale = 0;
    }
    return scale;
  });
  // The bearing counter (def 0x10cd10): sj_radar_set_bearing_deg.
  bearing_counter = &add_digit_counter(
      d, {0x101, 0x114, 0x127, 0x127}, [this]() -> std::int32_t& { return bearing_deg; }, 3,
      [this] {
        while (bearing_deg > 359) {
          bearing_deg -= 360;
        }
        radar().sj().beam = bearing_deg * 0xa0000;
      },
      [this]() -> std::optional<std::int32_t> {
        const std::optional<std::int32_t> v = pending_bearing();
        return v ? std::optional<std::int32_t>(*v / 10) : std::nullopt;
      });
  // The train knob (def 0x10cd68, knob_dial_proc): sj_radar_set_bearing.
  auto knob_message = [this](ui::Dialog& dlg, ui::Area& self, ui::Msg msg, int, const ui::Event& event) {
    if (msg == ui::Msg::kPress) {
      if (self.pressed) {
        return 0;
      }
      const std::int32_t a =
          fx::angle_add(fx::heading_from_delta(event.y - (0x141 + dlg.y()) - 1, event.x - (0x114 + dlg.x()) - 1),
                        Angle10(900))
              .tenths();
      if (a != bearing_tenths) {
        bearing_tenths = a;
        draw_knob();
      }
      radar().sj().beam = bearing_tenths << 16;
      return 0;
    }
    if (msg == ui::Msg::kPaint) {
      draw_knob();
      return -1;
    }
    return 0;
  };
  knob = &d.emplace<ui::Area>(0xfe, 0x12b, 0x12a, 0x157, std::move(knob_message), ui::Area::ActivateFn{});
  hover(*knob, ui::CursorId::kTurn);
  // The hold switch (def 0x10cdb4): sj_radar_toggle_hold.
  add_push_button(d, {0x36, 0x125, 0x54, 0x147}, kFrameHold, [this](bool pressed) {
    bool& hold = radar().sj().hold;
    if (pressed) {
      hold = !hold;
    }
    return hold ? 1 : 0;
  });
  // The nearest echo (def 0x10cd3c), display only.
  range_readout = &add_digit_counter(d, {0x89, 0x17f, 0xc9, 0x192},
                                     [this]() -> std::int32_t& { return radar().sj().nearest_yd; }, 5, {});
  range_readout->disabled = true;
  // The PPI face (sj_radar_ppi_click): trains the held beam on the click.
  auto ppi_message = [this](ui::Dialog&, ui::Area& self, ui::Msg msg, int, const ui::Event& event) {
    if ((msg == ui::Msg::kPress || msg == ui::Msg::kRepeat) && !self.pressed) {
      bearing_tenths = fx::angle_add(fx::heading_from_delta(event.y - 0x9a, event.x - 0xab), Angle10(900)).tenths();
      radar().sj().beam = bearing_tenths << 16;
    }
    return 0;
  };
  ppi_area = &d.emplace<ui::Area>(sj_view->x0, sj_view->y0, sj_view->x1, sj_view->y1, std::move(ppi_message),
                                  ui::Area::ActivateFn{});
  hover(*ppi_area, ui::CursorId::kSet);
  // The PPI / A-SCOPE / OFF positions of the power switch.
  auto power = [this](bool on, bool ppi) {
    SimContext ctx = mission().sim().context();
    radar().set_sj_power(ctx, mission().player(), on, ppi, false, false);
  };
  ui::Area& ppi = add_click_area(d, 0x10, 0x118, 0x2e, 299, 0, [power] { power(true, true); });
  ppi.disabled = (options() & kPpiFitted) == 0;
  hover(ppi, ui::CursorId::kPpi);
  hover(add_click_area(d, 0x10, 0x13e, 0x2e, 0x151, 0, [power] { power(true, false); }), ui::CursorId::kAScope);
  hover(add_click_area(d, 0x10, 300, 0x2e, 0x13d, 0, [power] { power(false, false); }), ui::CursorId::kOff);
  // The switch itself (def 0x10cdd0, sj_radar_mode_index), key only.
  mode_switch = &add_push_button(d, {0x10, 0x125, 0x2e, 0x147}, kFrameModeSwitch, [this](bool) {
    switch (radar().sj().mode) {
      case SjMode::kOff:
        return 2;
      case SjMode::kAScope:
        return 1;
      case SjMode::kPpi:
        return 0;
    }
    return 0;
  });
  mode_switch->x0 = mode_switch->y0 = mode_switch->x1 = mode_switch->y1 = -1;
}

void RadarStation::Impl::build_sd_dialog() {
  // radar_static_init (0x64798): the SD dialog 0x1259e4.
  ui::Dialog& d = sd_dialog.emplace(services.gui(), 0, 0, kScreenWidth, kScreenHeight, 0, &services.screen());
  // The range knob (def 0x10cdec): sd_radar_cycle_range.
  add_push_button(d, {0x1b6, 0x131, 0x1f3, 0x170}, kFrameSdRange, [this](bool pressed) {
    std::int32_t& scale = radar().sd().scale;
    if (pressed && ++scale > 4) {
      scale = 0;
    }
    return scale;
  });
  // The power switch (def 0x10ce08): sd_radar_button_power.
  sd_power = &add_push_button(d, {0x247, 0x125, 0x265, 0x149}, kFrameSdPower, [this](bool pressed) {
    if (pressed) {
      SimContext ctx = mission().sim().context();
      radar().set_sd_power(ctx, mission().player(), !radar().sd().on, false);
    }
    return radar().sd().on ? 0 : 1;
  });
  // The nearest echo (def 0x10ce24), display only.
  sd_readout = &add_digit_counter(d, {0x1b5, 0x17f, 0x1f5, 0x192},
                                  [this]() -> std::int32_t& { return radar().sd().nearest_yd; }, 5, {});
  sd_readout->disabled = true;
}

void RadarStation::Impl::open() {
  // radar_screen (0x652b2)
  const Install& install = services.install();
  PcxImage pcx = load_pcx(install, "ART/RADAR.PCX");
  services.screen().blit(pcx.image, 0, 0);
  services.palette() = pcx.palette;
  if (services.host().rig_for_red()) {
    services.palette() = load_pcx(install, "ART/RADAR.PAL").palette;
  }
  CommandBarMenu menu;
  menu.items.push_back({2, 3, [this] { services.go_to(StationId::kConn); }});  // radar_goto_conn
  services.host().open_command_bar(menu);
  sprites = Shp::load(install, "ART/RADAR.SHP");
  digit_font.emplace(Font::load(install, "ART/DIGITS3.FNT"));
  for (std::size_t i = 0; i < 0xff; ++i) {
    colours[i] = static_cast<std::uint8_t>(i);
  }
  colours[0xff] = 0;
  shown_nearest = shown_damage = shown_mode = -1;
  sd_shown_nearest = sd_shown_damage = sd_shown_on = -1;
  init_sj_display();
  init_sd_display();
  services.host().fade_out();
}

void RadarStation::Impl::init_sj_display() {
  // radar_init_sj_display (0x65427)
  sj_view.reset();
  Image8& screen = services.screen();
  const Viewport full = Viewport::full(screen);
  if ((options() & kSjMask) == 0) {
    draw_sprite_clipped(full, Shp::load(services.install(), "ART/RADAR1.SHP").frame(0), 0, 0);
    background = screen;
    return;
  }
  const SpriteFrame& face = sprites.frame(kFramePpi);
  sj_view = Box{kSjScopeCentre.x + face.x0, kSjScopeCentre.y + face.y0, kSjScopeCentre.x + face.x1,
                kSjScopeCentre.y + face.y1};
  bearing_tenths = fx::trunc(radar().sj().beam);
  bearing_deg = bearing_tenths / 10;
  shown_nearest = -1;
  if ((options() & kPpiFitted) != 0) {
    draw_sprite_clipped(full, sprites.frame(kFramePpiLabel), 0x17, 0x119);
  }
  draw_sprite_clipped(full, sprites.frame(kFrameSjLabel + ((options() & kSjMask) == kSjMask ? 1 : 0)), 0x9f, 0x19b);
  background = screen;
  build_sj_dialog();
  if (services.features.precise_dials) {
    add_scrub_targets();
  }
  sj_dialog->open();
  sj_redraw();
}

void RadarStation::Impl::init_sd_display() {
  // radar_init_sd_display (0x65e1e)
  sd_view.reset();
  Image8& screen = services.screen();
  if ((options() & kSdFitted) == 0) {
    // RADAR1.SHP mirrored over the right half.
    const Shp cover = Shp::load(services.install(), "ART/RADAR1.SHP");
    sprite_draw_transformed(Viewport::full(screen), cover.frame(0), 0x27f, 0, scratch, Angle10(0),
                            Fixed16::from_raw(-0x10000), Fixed16::from_int(1), 0);
    background = screen;
    return;
  }
  const SpriteFrame& face = sprites.frame(kFrameSdAScope);
  sd_view = Box{kSdScopeCentre.x + face.x0, kSdScopeCentre.y + face.y0, kSdScopeCentre.x + face.x1,
                kSdScopeCentre.y + face.y1};
  sd_shown_nearest = -1;
  build_sd_dialog();
  sd_dialog->open();
  draw_sd();
}

void RadarStation::Impl::draw_ascope(const Viewport& vp, const std::vector<RadarBlip>& blips, int baseline, int width,
                                     std::int32_t range, std::uint8_t base) {
  // radar_draw_ascope (0x65c51): the noise grass, then a spike per blip
  // seen for 4 sweeps within 1.25 x the range.
  WatcomRandom& random = services.draw_random();
  const std::int32_t limit = range + range / 4;
  for (int x = 0; x <= width; ++x) {
    const std::int32_t length = random.random_int(5) + 3;
    const std::int32_t top = baseline - random.random_int(length);
    ui::draw_line(vp, x, top, x, top + length, static_cast<std::uint8_t>(base + 2));
  }
  for (const RadarBlip& blip : blips) {
    if (blip.age < 4 || blip.range_yd > limit) {
      continue;
    }
    const std::int32_t column = ascope_blip_column(blip, range);
    // The height is strength x 70, rounded in 16.16.
    const std::int32_t scaled = fx::mul(blip.strength, 0x460000);
    std::int32_t peak = fx::trunc(scaled);
    peak += random.random_int(peak / 4);
    const std::int32_t width_shift = blip.strength / 0x8000;
    const std::int32_t n = static_cast<std::int32_t>((2u << (static_cast<std::uint32_t>(width_shift) & 31)) + 1);
    const std::int32_t half = n / 2;
    for (std::int32_t k = 0; k < n; ++k) {
      std::int32_t c = peak;
      if (k < half) {
        c = peak / (half + 1 - k);
      } else if (k > half) {
        c = peak / (k - half + 1);
      }
      c += random.random_int(c / 2);
      const auto colour = static_cast<std::uint8_t>(random.random_int(2) + base + 1);
      const int x = column - half + k;
      ui::draw_line(vp, x, baseline, x, baseline - c, colour);
    }
  }
}

void RadarStation::Impl::draw_sj_ascope() {
  // sj_radar_draw_ascope (0x65b97)
  const Box& b = *sj_view;
  const Viewport vp = screen_vp(b);
  const std::uint8_t base = sj_base();
  ui::fill_rect(vp, 0, 0, b.x1 - b.x0, b.y1 - b.y0, static_cast<std::uint8_t>(base + 7));
  const RadarSets::Sj& sj = radar().sj();
  if (sj.mode != SjMode::kOff && (sj_damage() & 3) == 0) {
    draw_ascope(vp, sj.blips, kAScopeBaseline - b.y0, b.x1 - b.x0 + 1, radar_display_range(sj.scale), base);
  }
  draw_sprite_clipped(vp, sprites.frame(kFrameSjAScope), kSjScopeCentre.x - b.x0, kSjScopeCentre.y - b.y0);
}

void RadarStation::Impl::draw_blips(const Viewport& vp, std::size_t frame, std::uint8_t base, std::int32_t limit,
                                    std::int32_t radius, std::int32_t range, fx::Point2 centre) {
  // The blip passes of sj_radar_draw_ppi: shades 0x10..0x17 remapped
  // brighter for the newer echoes, the sprite turned at random and scaled
  // by the strength.
  for (const RadarBlip& blip : radar().sj().blips) {
    if (blip.range_yd > limit) {
      continue;
    }
    const fx::Point2 p = ppi_blip_offset(blip, radius, range);
    for (int k = 0; k < 8; ++k) {
      int v = std::max(8 - blip.age, 0) + base + k;
      if (v >= base + 8) {
        v = base + 7;
      }
      colours[static_cast<std::size_t>(0x10 + k)] = static_cast<std::uint8_t>(v);
    }
    ColourRemap remap{};
    std::copy(colours.begin(), colours.end(), remap.begin());
    sprite_draw_transformed(vp, sprites.frame(frame), centre.x + p.x, centre.y + p.y, scratch, Angle10(blip.random),
                            Fixed16::from_raw(blip.strength), Fixed16::from_raw(blip.strength), kSpriteRemap, &remap);
  }
}

void RadarStation::Impl::draw_sj_ppi() {
  // sj_radar_draw_ppi (0x657db)
  const Box& b = *sj_view;
  const Viewport vp = screen_vp(b);
  const std::uint8_t base = sj_base();
  ui::fill_rect(vp, 0, 0, b.x1 - b.x0, b.y1 - b.y0, static_cast<std::uint8_t>(base + 7));
  const fx::Point2 centre{kSjScopeCentre.x - b.x0, kSjScopeCentre.y - b.y0};
  const RadarSets::Sj& sj = radar().sj();
  if (sj.mode != SjMode::kOff && (sj_damage() & 3) == 0) {
    const std::int32_t radius = (b.x1 - b.x0 + 1) / 2;
    const std::int32_t range = radar_display_range(sj.scale);
    const std::int32_t limit = range + range / 4;
    draw_blips(vp, kFrameBigBlip, base, limit, radius, range, centre);
    // The beam: seven lines fading behind it, three while held.
    std::int32_t angle = fx::trunc(sj.beam);
    for (int i = 0; i < 7; ++i) {
      if (sj.hold && i > 2) {
        continue;
      }
      const fx::Point2 tip = fx::point_rotate_scale({0, -radius}, {0, 0}, Angle10(angle));
      ui::draw_line(vp, centre.x, centre.y, centre.x + tip.x, centre.y + tip.y, static_cast<std::uint8_t>(base + i));
      for (angle -= 5; angle < 0; angle += 3600) {
      }
    }
    draw_blips(vp, kFrameSmallBlip, base, limit, radius, range, centre);
  }
  draw_sprite_clipped(vp, sprites.frame(kFramePpi), centre.x, centre.y);
}

void RadarStation::Impl::sj_redraw() {
  // sj_radar_redraw (0x65798): off, the blank face of the fitted type.
  if (!sj_view) {
    return;
  }
  SjMode mode = radar().sj().mode;
  if (mode == SjMode::kOff) {
    mode = (options() & kPpiFitted) != 0 ? SjMode::kPpi : SjMode::kAScope;
  }
  if (mode == SjMode::kAScope) {
    draw_sj_ascope();
  } else {
    draw_sj_ppi();
  }
}

void RadarStation::Impl::draw_sd() {
  // sd_radar_draw (0x65fee)
  if (!sd_view) {
    return;
  }
  const Box& b = *sd_view;
  const Viewport vp = screen_vp(b);
  ui::fill_rect(vp, 0, 0, b.x1 - b.x0, b.y1 - b.y0, 0x17);
  const RadarSets::Sd& sd = radar().sd();
  if (sd.on && (sd_damage() & 3) == 0) {
    draw_ascope(vp, sd.blips, kAScopeBaseline - b.y0, b.x1 - b.x0 + 1, radar_display_range(sd.scale), 0x10);
  }
  draw_sprite_clipped(vp, sprites.frame(kFrameSdAScope), kSdScopeCentre.x - b.x0, kSdScopeCentre.y - b.y0);
}

void RadarStation::Impl::sj_display_update() {
  // sj_radar_display_update (0x65615)
  if (!sj_view) {
    return;
  }
  const RadarSets::Sj& sj = radar().sj();
  ppi_area->disabled = true;
  knob->disabled = true;
  bearing_counter->disabled = true;
  bool redraw = false;
  if (sj.nearest_yd != shown_nearest) {
    range_readout->dirty = true;
    shown_nearest = sj.nearest_yd;
  }
  if (sj.mode != SjMode::kOff && (sj_damage() & 3) == 0) {
    redraw = true;
  }
  if (sj_damage() != shown_damage) {
    redraw = true;
    shown_damage = sj_damage();
  }
  if (static_cast<std::int32_t>(sj.mode) != shown_mode) {
    redraw = true;
    shown_mode = static_cast<std::int32_t>(sj.mode);
    mode_switch->dirty = true;
  }
  if (sj.hold && sj.mode != SjMode::kOff) {
    knob->disabled = false;
    bearing_counter->disabled = false;
    if (sj.mode == SjMode::kPpi) {
      ppi_area->disabled = false;
    }
  }
  const std::int32_t tenths = fx::trunc(sj.beam);
  if (tenths / 10 != bearing_deg || tenths != bearing_tenths) {
    bearing_deg = tenths / 10;
    knob->dirty = true;
    bearing_counter->dirty = true;
    bearing_tenths = tenths;
  }
  if (redraw) {
    sj_redraw();
  }
  if (services.redraw_requested()) {
    sj_dialog->invalidate();
  }
}

void RadarStation::Impl::sd_display_update() {
  // sd_radar_display_update (0x65f5c)
  if (!sd_view) {
    return;
  }
  const RadarSets::Sd& sd = radar().sd();
  bool redraw = false;
  if (sd.nearest_yd != sd_shown_nearest) {
    sd_readout->dirty = true;
    sd_shown_nearest = sd.nearest_yd;
  }
  if (sd.on && (sd_damage() & 3) == 0) {
    redraw = true;
  }
  if (sd_damage() != sd_shown_damage) {
    redraw = true;
    sd_shown_damage = sd_damage();
  }
  if (static_cast<std::int32_t>(sd.on) != sd_shown_on) {
    redraw = true;
    sd_shown_on = sd.on;
    sd_power->dirty = true;
  }
  if (redraw) {
    draw_sd();
  }
  if (services.redraw_requested()) {
    sd_dialog->invalidate();
  }
}

RadarStation::RadarStation(AttackServices& services) : services_(services) {}

RadarStation::~RadarStation() = default;

void RadarStation::open() {
  impl_ = std::make_unique<Impl>(services_);
  impl_->open();
}

void RadarStation::event(const ui::Event& event) {
  if (impl_->scrub && impl_->scrub->event(event, services_.gui().ticks())) {
    return;
  }
  if (impl_->sj_view) {
    impl_->sj_dialog->handle_event(event);
  }
  if (impl_->sd_view) {
    impl_->sd_dialog->handle_event(event);
  }
}

void RadarStation::process() {
  if (impl_->scrub) {
    impl_->scrub->process(services_.gui().ticks());
  }
  if (impl_->sj_view) {
    impl_->sj_dialog->process();
  }
  if (impl_->sd_view) {
    impl_->sd_dialog->process();
  }
}

void RadarStation::update() {
  impl_->sj_display_update();
  impl_->sd_display_update();
  if (first_frame()) {
    services_.host().fade_in();
  }
}

bool RadarStation::capturing() const { return impl_ && impl_->scrub && impl_->scrub->capturing(); }

void RadarStation::close() {
  // sj_radar_close_display, sd_radar_close_display
  if (impl_->sj_view) {
    impl_->sj_dialog->close();
  }
  if (impl_->sd_view) {
    impl_->sd_dialog->close();
  }
  impl_.reset();
}

}  // namespace sh

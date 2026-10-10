#include "game/stations/attack_services.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "core/fixed_math.h"
#include "core/install.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "gfx/sprite.h"
#include "sim/contacts.h"
#include "sim/player_combat.h"
#include "sim/sensors.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The target marker of GLOBAL.SHP: a triangle of colour 1 under its hotspot.
constexpr std::size_t kTargetMarkerFrame = 37;
// The recon target indicators: in view, and on the target.
constexpr std::size_t kReconInViewFrame = 38;
constexpr std::size_t kReconMarkerFrame = 39;

}  // namespace

void StationEnvHost::open_command_bar(const CommandBarMenu& menu) {
  BarConfig config;
  for (const CommandBarItem& item : menu.items) {
    config.buttons.push_back(
        {static_cast<std::uint8_t>(item.frame), static_cast<std::uint8_t>(item.pressed_frame), item.action});
  }
  config.on_escape = menu.on_cancel;
  config.time_compression = menu.time_compression;
  config.clock = menu.clock;
  config.hotkeys = menu.hotkeys;
  env_.bar.open(config);
}

void StationEnvHost::close_command_bar() { env_.bar.close(); }

void StationEnvHost::fade_out() { env_.dac.fade_out(0x100); }

void StationEnvHost::fade_in() { env_.dac.fade_in(env_.palette, 10); }

std::unique_ptr<StationPanel> StationEnvHost::make_nav_panel(PanelSite site) {
  return nav_panels ? nav_panels(site) : nullptr;
}

std::unique_ptr<StationPanel> StationEnvHost::make_id_book(PanelSite site) {
  return id_books ? id_books(site) : nullptr;
}

bool StationEnvHost::rig_for_red() const { return env_.ui.rig_for_red; }

void StationEnvHost::play_sfx(std::int32_t sfx) { env_.sound.play_sfx(sfx); }

void StationEnvHost::play_sfx_at(std::int32_t sfx, std::int32_t volume) { env_.sound.play_sfx(sfx, volume); }

bool StationEnvHost::full_redraw() const { return env_.ui.redraw; }

Fixed16 next_magnification(Fixed16 zoom) {
  // tbt_cycle_zoom / scope_toggle_magnification: doubles up to 8x.
  return zoom.raw() < 0x80000 ? Fixed16::from_raw(zoom.raw() << 1) : Fixed16::from_int(1);
}

AttackServices::AttackServices(const Install& install, Mission& mission, ui::Gui& gui, VgaPalette& palette,
                               StationRegistry& stations, AttackStationHost& host, WorldViewAdapter& world,
                               const Shp& cursors)
    : install_(install),
      mission_(mission),
      gui_(gui),
      palette_(palette),
      stations_(stations),
      host_(host),
      world_(world),
      cursors_(cursors),
      global_(Shp::load(install, "ART/GLOBAL.SHP")),
      bearing_font_(Font::load(install, "ART/BEARING.FNT")) {}

void AttackServices::set_cursor(ui::CursorId id) { gui_.mouse().set_cursor(cursors_, id); }

void AttackServices::set_default_cursor() { gui_.mouse().set_cursor(cursors_, gui_.dialog_cursor_frame()); }

std::uint32_t AttackServices::elapsed(std::uint32_t& counter) {
  const std::uint32_t now = gui_.ticks();
  const std::uint32_t d = now - counter;
  counter = now;
  return d;
}

bool AttackServices::advance_world() {
  const std::uint32_t now = gui_.ticks();
  if (!world_ticks_set_) {
    world_ticks_ = now;
    world_ticks_set_ = true;
  }
  const auto ticks = static_cast<std::int32_t>(now - world_ticks_);
  world_ticks_ = now;
  world_.advance(ticks, mission_.sim().conditions().sea_state);
  return world_.update_sky(mission_);
}

void AttackServices::shade_palette(const VgaPalette* overlay) {
  const Darkening dark = world_.shade_palette(palette_);
  if (overlay != nullptr) {
    dim_station_overlay(palette_, *overlay, dark);
  }
}

RenderList AttackServices::render_world(const ViewEye& eye) {
  RenderList list = world_.render(mission_, eye, mission_.sim().random());
  world_.lighting().cycle_lights(palette_);
  return list;
}

Viewport AttackServices::world_window(int x0, int top, int x1, int bottom) {
  const int b = bob();
  return {&world_.frame(), x0, b + top, x1, b + bottom};
}

void AttackServices::blit_to_screen(const Viewport& window, int x, int y) {
  Image8& screen = gui_.screen();
  const ClipRect r = window.clip();
  for (int sy = r.top; sy <= r.bottom; ++sy) {
    for (int sx = r.left; sx <= r.right; ++sx) {
      const int dx = x + sx - window.x0;
      const int dy = y + sy - window.y0;
      if (screen.contains(dx, dy)) {
        screen.set(dx, dy, window.bitmap->at(sx, sy));
      }
    }
  }
}

void AttackServices::draw_target_marker(const Viewport& window, const ViewEye& eye, int y, std::uint8_t colour) {
  // tdc_draw_target_marker (0x7c60e): the marker blinks while unlocked.
  const Tdc& tdc = mission_.player_combat().tdc;
  const Vehicle* target = tdc.target();
  if (target == nullptr || (!tdc.locked() && tdc.blink())) {
    return;
  }
  const fx::Vec3 p = target->position();
  const std::optional<int> x = world_.project_x(eye, p.x, p.z);
  if (!x || global_.frame_count() <= kTargetMarkerFrame) {
    return;
  }
  ColourRemap remap = gui_colour_table();
  remap[1] = colour;
  draw_sprite_remapped(window, global_.frame(kTargetMarkerFrame), *x, y, remap);
}

void AttackServices::draw_recon_marker(const Viewport& window, const ViewEye& eye, int x, int y,
                                       std::uint8_t colour) {
  // player_draw_recon_target_marker (0x60e17): GLOBAL.SHP frames 38 and 39
  // with their only colour, 1, remapped (table 0x124f6c).
  if (global_.frame_count() <= kReconMarkerFrame) {
    return;
  }
  ColourRemap remap = gui_colour_table();
  remap[1] = colour;
  const ContactReports& reports = mission_.sim().sensors().reports();
  if (!mission_.player_combat().tdc.blink() && reports.recon_in_view() != nullptr) {
    draw_sprite_remapped(window, global_.frame(kReconInViewFrame), x, y, remap);
  }
  if (const Vehicle* target = reports.recon_target()) {
    // The row is that of the world frame less the window's top (the swell
    // offset plus the station's rows).
    if (const std::optional<std::array<int, 2>> p = world_.project_point(eye, target->position())) {
      draw_sprite_remapped(window, global_.frame(kReconMarkerFrame), (*p)[0], (*p)[1] - window.y0, remap);
    }
  }
}

void AttackServices::keep_events(const std::function<void()>& fn) {
  std::vector<ui::Event> pending;
  while (const std::optional<ui::Event> e = gui_.events().get()) {
    pending.push_back(*e);
  }
  fn();
  for (const ui::Event& e : pending) {
    gui_.events().post(e);
  }
}

std::int32_t AttackServices::bob() const { return mission_.player().swell(); }

ui::Area& add_train_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::uint32_t key,
                         std::function<void(std::int32_t step)> action, std::function<void(bool on)> hover) {
  // The repeat counter (0x116270, 0x126638, 0x1282dc, 0x11bd40) belongs to
  // the area.
  auto counter = std::make_shared<std::int32_t>(0);
  auto on_message = [counter](ui::Dialog& d, ui::Area&, ui::Msg msg, int, const ui::Event& event) {
    switch (msg) {
      case ui::Msg::kRegister:
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        *counter = 0;
        break;
      case ui::Msg::kPress:
        d.set_repeat_interval(event.is_key() ? 0 : 10);
        break;
      default:
        break;
    }
    return 0;
  };
  auto on_activate = [counter, action = std::move(action)](ui::Dialog&, ui::Area&, int, const ui::Event& event) {
    action(event.is_key() ? 5 : (*counter)++);
    return 0;
  };
  ui::Area& a = dialog.emplace<ui::Area>(x0, y0, x1, y1, std::move(on_message), std::move(on_activate), key);
  if (hover) {
    a.on_hover = [hover = std::move(hover)](bool on, int, const ui::Event&) { hover(on); };
  }
  return a;
}

ui::Area& add_hotkey(ui::Dialog& dialog, std::uint32_t key, std::function<void()> action) {
  return add_click_area(dialog, -1, -1, -1, -1, key, std::move(action));
}

ui::Area& add_click_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::uint32_t key,
                         std::function<void()> action, std::function<void(bool on)> hover) {
  // hotkey_proc (0x493ad): the action runs on the press of an idle control.
  auto on_message = [action = std::move(action)](ui::Dialog&, ui::Area& self, ui::Msg msg, int, const ui::Event&) {
    if (msg == ui::Msg::kPress && !self.pressed) {
      action();
    }
    return 0;
  };
  ui::Area& a = dialog.emplace<ui::Area>(x0, y0, x1, y1, std::move(on_message), ui::Area::ActivateFn{}, key);
  if (hover) {
    a.on_hover = [hover = std::move(hover)](bool on, int, const ui::Event&) { hover(on); };
  }
  return a;
}

ui::Area& add_cycle_button(ui::Dialog& dialog, AttackStationHost& host, int x0, int y0, int x1, int y1,
                           std::uint32_t key, const Shp& sprites, std::size_t normal, std::size_t pressed,
                           std::uint32_t repeat, std::function<void(std::int32_t)> action,
                           std::function<void(bool on)> hover) {
  // cycle_button_proc (0x49472) and cycle_button_draw (0x4951e)
  auto count = std::make_shared<std::int32_t>(0);
  auto draw = [&sprites, x0, y0](ui::Dialog& d, std::size_t frame) {
    draw_sprite_clipped(d.gfx(), sprites.frame(frame), x0, y0);
  };
  auto on_message = [=, &host, action = std::move(action)](ui::Dialog& d, ui::Area& self, ui::Msg msg, int,
                                                           const ui::Event& event) {
    switch (msg) {
      case ui::Msg::kRegister:
        *count = 0;
        return 0;
      case ui::Msg::kPress:
        d.set_repeat_interval(event.is_key() ? 0 : repeat);
        draw(d, pressed);
        if (!self.pressed) {
          host.play_sfx_at(kButtonClickSfx, kButtonClickVolume);
          action(++*count);
        }
        return -1;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        *count = 0;
        draw(d, normal);
        return -1;
      default:
        return 0;
    }
  };
  ui::Area& a = dialog.emplace<ui::Area>(x0, y0, x1, y1, std::move(on_message), ui::Area::ActivateFn{}, key);
  if (hover) {
    a.on_hover = [hover = std::move(hover)](bool on, int, const ui::Event&) { hover(on); };
  }
  return a;
}

ui::Area& add_push_button(ui::Dialog& dialog, AttackServices& services, int x0, int y0, int x1, int y1,
                          const Shp& sprites, std::size_t base, std::function<void()> advance,
                          std::function<std::int32_t()> state) {
  // push_button_proc (0x49613) and push_button_draw (0x4969b)
  auto on_message = [=, &services, &sprites, advance = std::move(advance), state = std::move(state)](
                        ui::Dialog& d, ui::Area& self, ui::Msg msg, int, const ui::Event&) {
    if (msg == ui::Msg::kPress && !self.pressed) {
      services.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume);
      advance();
    } else if (msg != ui::Msg::kPress && msg != ui::Msg::kPaint) {
      return 0;
    }
    const std::size_t frame = base + static_cast<std::size_t>(state());
    draw_sprite_clipped(d.gfx(), sprites.frame(frame), x0, y0);
    return -1;
  };
  ui::Area& a = dialog.emplace<ui::Area>(x0, y0, x1, y1, std::move(on_message), ui::Area::ActivateFn{}, 0);
  a.on_hover = [&services](bool on, int, const ui::Event&) {
    on ? services.set_cursor(ui::CursorId::kSet) : services.set_default_cursor();
  };
  return a;
}

Angle10 train_bearing(Angle10 bearing, std::int32_t step, int sign) {
  // bridge_turn_left / bridge_turn_right: n x 10 / 4 tenths.
  const std::int32_t n = std::clamp(step, 1, 30);
  const std::int32_t delta = n * 10 / 4;
  const std::int32_t b = bearing.normalized().tenths();
  return Angle10(sign < 0 ? fx::angle_sub(Angle10(b), Angle10(delta)).tenths()
                          : fx::angle_add(Angle10(b), Angle10(delta)).tenths());
}

void draw_bearing_tape(Image8& screen, const Image8& background, const Font& font, Angle10 bearing, int x0, int y0,
                       int x1, int y1, std::uint8_t colour, const GlyphColourTable& colours) {
  // draw_bearing_tape (0x4b18d): the tape is drawn in a buffer the size of
  // the rectangle, then copied to the screen.
  const int w = x1 - x0 + 1;
  const int h = y1 - y0 + 1;
  Image8 tape(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      tape.set(x, y, background.contains(x0 + x, y0 + y) ? background.at(x0 + x, y0 + y) : 0);
    }
  }
  const Viewport vp = Viewport::full(tape);
  const std::int32_t b = bearing.normalized().tenths();
  const std::int32_t whole = b / 100 * 100;
  const std::int32_t offset = (b - whole) * 3 / 10;
  std::int32_t label = fx::angle_sub(Angle10(whole), Angle10(700)).tenths();
  std::int32_t count = 0;
  for (int x = w / 2 - 0xd3 - offset; x < w + 30; x += 3, ++count) {
    const std::int32_t r = count % 10;
    if (r == 0) {
      const std::string text = std::to_string(label / 10);
      label = fx::angle_add(Angle10(label), Angle10(100)).tenths();
      const int half = font.text_width(text) / 2;
      ui::draw_line(vp, x, h - 1, x, h - 4, colour);
      int pen = x - half + 1;
      for (const char c : text) {
        // font_draw_string: glyph after glyph through the colour table.
        const int adv = font_draw_char(vp, pen, h - 13, font, static_cast<std::uint8_t>(c), &colours);
        pen += std::max(adv, 0);
      }
    } else if (r == 5) {
      ui::draw_line(vp, x, h - 1, x, h - 5, colour);
    } else {
      ui::draw_line(vp, x, h - 1, x, h - 2, colour);
    }
  }
  screen.blit(tape, x0, y0);
}

GlyphColourTable patched_colours(std::initializer_list<std::pair<std::uint8_t, std::uint8_t>> entries) {
  GlyphColourTable t{};
  for (const auto& [index, value] : entries) {
    t[index] = value;
  }
  return t;
}

}  // namespace sh

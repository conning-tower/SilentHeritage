#include "game/stations/gauges.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "game/command_bar.h"
#include "game/stations/attack_services.h"
#include "gfx/sprite.h"
#include "sim/geometry.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The instrument dials of GAUGES (needle dial templates in SH.EXE).
constexpr std::uint32_t kFathometerDial = 0x1087a4;
constexpr std::uint32_t kFuelDial = 0x1087fc;
constexpr std::uint32_t kAirDial = 0x108854;
constexpr std::uint32_t kBuoyancyDial = 0x1088ac;

// GAUGES.SHP frames.
constexpr std::int32_t kBatteryNeedleFrame = 4;
constexpr std::int32_t kEngineSlotFrame = 0x15;     // + room
constexpr std::int32_t kChargeLampOff = 0x19;
constexpr std::int32_t kChargeLampOn = 0x1a;
constexpr std::int32_t kRunLampOff = 0x1b;
constexpr std::int32_t kRunLampOn = 0x1c;
constexpr std::int32_t kBatteryGlassFrame = 0x1f;
constexpr std::int32_t kOxygenBackFrame = 0x20;
constexpr std::int32_t kOxygenBarFrame = 0x21;
constexpr std::int32_t kFathometerCover = 0x22;
constexpr std::int32_t kEnginePageFrame = 0x23;     // + 1 for the bathythermograph
constexpr std::int32_t kTraceEndFrame = 0x25;
// The engine rooms' slots {x, lamp offset}: int32[4][2] at 0x108904.
constexpr std::uint32_t kEngineSlots = 0x108904;

// The battery dial (0x1ee, 8)-(0x272, 0x69), the oxygen bar (0x1b9, 0xf)-
// (0x1c9, 0x68) and the bathythermograph window at (0x2d, 9).
constexpr ui::Rect kBatteryRect{0x1ee, 8, 0x272, 0x69};
constexpr ui::Rect kOxygenRect{0x1b9, 0xf, 0x1c9, 0x68};
constexpr ui::Rect kTraceRect{0x2d, 9, 0x2d + 0x8a, 9 + 0x8f};
constexpr std::uint8_t kTraceColour = 0x4f;

// The bar menus (0x11c6bc, 0x11c6d4, 0x11c6ec): conn, and the other page.
constexpr std::uint8_t kConnFrame = 2;
constexpr std::uint8_t kTraceButtonFrame = 0x1a;
constexpr std::uint8_t kEnginesButtonFrame = 0x1c;

// The SOUND knob (not in the original): the RESUME knob of the compass
// (the course engage button's frames) between the fathometer and the fuel
// dial, over a label plate in the lettering of the RESUME plate.
constexpr int kKnobX = 99;  // the frame's top-left corner
constexpr int kKnobY = 413;
constexpr int kPlateX = 97;
constexpr int kPlateY = 445;
constexpr int kPlateHeight = 13;
constexpr int kPlateMargin = 6;  // left of the text; 5 right
constexpr int kPlateTextY = 3;
// The plate's colours in GAUGES.PCX: its ground, the letters and their
// three edge shades.
constexpr std::uint8_t kPlateColour = 0xf7;
constexpr std::array<std::uint8_t, 4> kLetterColours{0xc9, 0xde, 0xdc, 0xd5};
// "SOUND", 7 rows: '#' the letter colour, 'a'..'c' its shades.
constexpr std::array<std::string_view, 7> kSoundLabel{
    "a#b.a#b.#..#.#..#.##b",  //
    "#.a.#.#.#..#.##.#.#.#",  //
    "#...#.#.#..#.##.#.#.#",  //
    "b#c.#.#.#..#.#bb#.#.#",  //
    "..#.#.#.#..#.#.##.#.#",  //
    "a.#.#.#.#ab#.#.##.#.#",  //
    "c#a.b#a.b##b.#..#.##b",  //
};
// GAUGES.SHP pixels of the knob and its socket (the frames' other pixels
// are the brass around the RESUME knob).
bool knob_pixel(std::uint8_t c) { return c < 0x70 || c >= 0xc0; }

// The SOUND knob, as the RESUME knob (`cycle_button_proc` 0x49472) without
// its auto-repeat: `faces` are the knob up and pressed, put at (x, y) of the
// dialog; `on_press` runs at each press.
class SoundingKnob : public ui::Control {
 public:
  SoundingKnob(int x, int y, std::array<Image8, 2> faces, std::function<void()> on_press)
      : x_(x), y_(y), faces_(std::move(faces)), on_press_(std::move(on_press)) {}

  ui::Rect rect() const { return {x_, y_, x_ + faces_[0].width() - 1, y_ + faces_[0].height() - 1}; }

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(dialog, *this, rect(), 0, 0, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        dialog.set_repeat_interval(0);
        if (!pressed) {
          on_press_();
        }
        dialog.gfx().bitmap->blit(faces_[1], dialog.gfx().x0 + x_, dialog.gfx().y0 + y_);
        return true;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        dialog.gfx().bitmap->blit(faces_[0], dialog.gfx().x0 + x_, dialog.gfx().y0 + y_);
        return true;
      default:
        return false;
    }
  }

 private:
  int x_, y_;
  std::array<Image8, 2> faces_;
  std::function<void()> on_press_;
};

// Options word bits.
constexpr std::uint32_t kOptionFathometer = 0x1000;
constexpr std::uint32_t kOptionBathythermograph = 0x200000;

// `angle_add` (0x5447c) on raw tenths.
std::int32_t angle_add(std::int32_t a, std::int32_t b) { return fx::angle_add(Angle10(a), Angle10(b)).tenths(); }

// Copies `background` under `r` into a scratch, lets `draw` paint it, and
// puts it on `screen` (the composition of the original's dials).
template <class F>
void compose(Image8& screen, const Image8& background, const ui::Rect& r, F&& draw) {
  Image8 scratch(r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, 0);
  for (int y = 0; y < scratch.height(); ++y) {
    for (int x = 0; x < scratch.width(); ++x) {
      if (background.contains(r.x0 + x, r.y0 + y)) {
        scratch.set(x, y, background.at(r.x0 + x, r.y0 + y));
      }
    }
  }
  draw(Viewport::full(scratch));
  screen.blit(scratch, r.x0, r.y0);
}

}  // namespace

std::int32_t fathometer_feet(std::int32_t keel_clearance) {
  // The clearance / 16, truncated toward zero.
  return std::clamp(-keel_clearance / 16, 0, 700);
}

std::int32_t fuel_needle(std::int32_t fuel, std::int32_t capacity) { return (fuel << 9) / capacity; }

std::int32_t air_needle(std::int32_t air_pct) { return (air_pct << 9) / 100; }

std::int32_t buoyancy_needle(std::int32_t flood, const SubClassParams& sub) {
  const std::int32_t sunk = std::min((flood << 9) / ((sub.ballast_max - sub.ballast_neutral) * 800), 0x200);
  return 0x200 - sunk;
}

std::int32_t battery_needle_angle(std::int32_t charge) {
  // angle_sub(62 deg, 296 deg) + 1 deg over 600 units, from 296 deg.
  const std::int32_t span = angle_add(fx::angle_sub(Angle10(0x26c), Angle10(0xb90)).tenths(), 10);
  return angle_add(charge * (span * 100 / 600) / 100, 0xb90);
}

std::int32_t oxygen_bar_offset(std::int32_t air, std::int32_t air_max) { return 0x5a - air * 0x5a / air_max; }

std::vector<TracePoint> bathythermograph_trace(std::int32_t deepest_ft, std::int32_t layer_ft) {
  std::vector<TracePoint> points;
  std::int32_t temperature = 6500;  // hundredths of a degree
  std::int32_t row = 0;             // hundredths of a pixel
  for (std::int32_t d = 0; d < 600 && d <= deepest_ft; d += 2) {
    if (layer_ft - 60 < d && d < layer_ft + 30) {
      if (layer_ft - 40 < d && d < layer_ft + 10) {
        temperature -= (layer_ft - 30 < d && d < layer_ft) ? 300 : 20;
      } else {
        temperature -= 10;
      }
    } else {
      temperature -= 1;
    }
    const TracePoint p{temperature / 100 * 12 / 10 + 0x1f, row / 100 + 0xc};
    if (points.empty() || points.back() != p) {
      points.push_back(p);
    }
    row += 0x28;
  }
  return points;
}

void DiveRecorder::tick() {
  // The end of `player_update` (0x5bf89).
  const std::int32_t depth = player_.depth_ft();
  const std::int32_t draft = player_.sub().draft_ft;
  if (deepest_ < 1) {
    if (draft < depth) {
      deepest_ = depth;
    }
  } else if (deepest_ < depth) {
    deepest_ = depth;
  } else if (depth <= draft) {
    deepest_ = -deepest_;
  }
}

void GaugesStation::draw(std::int32_t frame, int x, int y) {
  const SpriteFrame& f = sprites_.frame(static_cast<std::size_t>(frame));
  draw_sprite_clipped(Viewport::full(env_.screen()), f, x, y);
  if (background_.width() > 0) {
    draw_sprite_clipped(Viewport::full(background_), f, x, y);
  }
}

void GaugesStation::draw_sounding_plate() {
  Image8& screen = env_.screen();
  const int width = static_cast<int>(kSoundLabel[0].size()) + 2 * kPlateMargin - 1;
  screen.fill_rect(kPlateX, kPlateY, width, kPlateHeight, kPlateColour);
  for (std::size_t row = 0; row < kSoundLabel.size(); ++row) {
    for (std::size_t col = 0; col < kSoundLabel[row].size(); ++col) {
      const char c = kSoundLabel[row][col];
      if (c != '.') {
        screen.set(kPlateX + kPlateMargin + static_cast<int>(col), kPlateY + kPlateTextY + static_cast<int>(row),
                   kLetterColours[c == '#' ? 0 : static_cast<std::size_t>(c - 'a' + 1)]);
      }
    }
  }
}

std::array<Image8, 2> GaugesStation::sounding_knob_faces() {
  const NavTemplate layout = gauges_nav_template(env_.exe);
  const std::array<std::int32_t, 2> frames{layout.engage->frame, layout.engage->pressed_frame};
  std::array<Image8, 2> faces;
  for (std::size_t i = 0; i < 2; ++i) {
    const SpriteFrame& f = sprites_.frame(static_cast<std::size_t>(frames[i]));
    Image8 knob(f.width(), f.height(), 0);
    draw_sprite_clipped(Viewport::full(knob), f, -f.x0, -f.y0);
    Image8& face = faces[i];
    face = Image8(f.width(), f.height(), 0);
    for (int y = 0; y < face.height(); ++y) {
      for (int x = 0; x < face.width(); ++x) {
        face.set(x, y, knob_pixel(knob.at(x, y)) ? knob.at(x, y) : env_.screen().at(kKnobX + x, kKnobY + y));
      }
    }
  }
  return faces;
}

void GaugesStation::open() {
  // gauges_screen (0x39d1c)
  ++env_.ui.pause;
  background_ = Image8();
  load_screen_pcx(env_, "GAUGES.PCX");
  if (env_.ui.rig_for_red) {
    load_screen_palette(env_, "GAUGES.PAL");
  }
  sprites_ = Shp::load(env_.install, env_.paths.path(GameDir::kArt, "GAUGES.SHP"));
  set_page(env_.ui.gauges_page, false);
  if (!soundings() && (env_.mission.sim().conditions().options & kOptionFathometer) == 0) {
    draw(kFathometerCover, 0, 0x152);
  }
  panel_ = std::make_unique<NavPanel>(env_, gauges_nav_template(env_.exe));
  sounding_button_ = {};
  if (soundings()) {
    draw_sounding_plate();
    auto faces = sounding_knob_faces();
    panel_->set_station_controls([this, faces = std::move(faces)](ui::Dialog& d) {
      auto& knob = d.emplace<SoundingKnob>(kKnobX - d.x(), kKnobY - d.y(), faces, [this] {
        env_.sound.play_sfx(kButtonClickSfx, kButtonClickVolume);
        env_.mission.sim().sensors().fathometer().request();
      });
      const ui::Rect r = knob.rect();
      sounding_button_ = {d.x() + r.x0, d.y() + r.y0, d.x() + r.x1, d.y() + r.y1};
    });
  }
  panel_->open();
  last_keel_ = last_fuel_ = last_air_ = last_flood_ = last_battery_ = last_oxygen_ = last_sounding_ = -1;
  background_ = env_.screen();
  env_.dac.fade_out(0x100);
  --env_.ui.pause;
}

void GaugesStation::close() {
  panel_->close();
  panel_.reset();
  background_ = Image8();
}

void GaugesStation::event(const ui::Event& event) { panel_->event(event); }

void GaugesStation::process() { panel_->process(); }

void GaugesStation::set_page(std::int32_t page, bool switching) {
  // gauges_set_page (0x3aebd)
  ++env_.ui.pause;
  if (switching) {
    env_.bar.close();
    env_.ui.gauges_page = page;
  }
  BarConfig config;
  config.buttons.push_back({kConnFrame, kConnFrame + 1, [this] { env_.bar.select_station(StationId::kConn); }});
  if ((env_.mission.sim().conditions().options & kOptionBathythermograph) == 0) {
    env_.ui.gauges_page = 0;
  } else if (env_.ui.gauges_page == 0) {
    // The page change waits for the station update: the original redraws
    // the bar from inside the bar's own button.
    config.buttons.push_back({kTraceButtonFrame, kTraceButtonFrame + 1, [this] { pending_page_ = 1; }});
  } else {
    config.buttons.push_back({kEnginesButtonFrame, kEnginesButtonFrame + 1, [this] { pending_page_ = 0; }});
  }
  env_.bar.open(config);
  if (switching) {
    env_.bar.process();
  }
  draw(kEnginePageFrame + (env_.ui.gauges_page != 0 ? 1 : 0), 0, 0);
  if (env_.ui.gauges_page == 0) {
    draw_engine_slots();
  } else {
    last_trace_ = -1;
  }
  --env_.ui.pause;
}

void GaugesStation::draw_engine_slots() {
  last_diesel_mode_ = -1;
  const auto& rooms = env_.mission.player().engines();
  for (std::size_t i = 0; i < 4; ++i) {
    if (rooms[i][1].fitted) {
      draw(kEngineSlotFrame + static_cast<std::int32_t>(i), env_.exe.i32(kEngineSlots + 8 * static_cast<std::uint32_t>(i)),
           0x1f);
      last_engines_[i] = -1;
    }
  }
}

void GaugesStation::update_engine_lights() {
  // The diesels' running (top) and charging (bottom) lamps, lit only in
  // diesel mode.
  if (env_.ui.gauges_page != 0) {
    return;
  }
  const PlayerSub& p = env_.mission.player();
  const std::int32_t diesel = p.diesel_mode() ? 1 : 0;
  for (std::size_t i = 0; i < 4; ++i) {
    const EnginePart& d = p.engines()[i][1];
    if (!d.fitted) {
      continue;
    }
    const std::int32_t word = d.flags;
    if (diesel == last_diesel_mode_ && word == last_engines_[i]) {
      continue;
    }
    last_engines_[i] = word;
    const auto slot = kEngineSlots + 8 * static_cast<std::uint32_t>(i);
    const int x = env_.exe.i32(slot) + env_.exe.i32(slot + 4);
    draw((word & EnginePart::kRunning) != 0 && diesel == 1 ? kRunLampOn : kRunLampOff, x, 0x2a);
    draw((word & EnginePart::kCharging) != 0 && diesel == 1 ? kChargeLampOn : kChargeLampOff, x, 0x67);
  }
  last_diesel_mode_ = diesel;
}

void GaugesStation::draw_bathythermograph() {
  if (env_.ui.gauges_page == 0) {
    return;
  }
  const std::int32_t deepest = std::abs(recorder_.deepest());
  if (deepest == last_trace_) {
    return;
  }
  last_trace_ = deepest;
  const std::vector<TracePoint> trace =
      bathythermograph_trace(recorder_.deepest(), env_.mission.sim().conditions().thermal_layer_ft);
  compose(env_.screen(), background_, kTraceRect, [&](const Viewport& vp) {
    for (std::size_t i = 0; i < trace.size(); ++i) {
      if (i == 0) {
        ui::put_pixel(vp, trace[i].x, trace[i].y, kTraceColour);
      } else {
        ui::draw_line(vp, trace[i - 1].x, trace[i - 1].y, trace[i].x, trace[i].y, kTraceColour);
      }
    }
    if (!trace.empty()) {
      draw_sprite_clipped(vp, sprites_.frame(kTraceEndFrame), trace.back().x, trace.back().y);
    }
  });
}

void GaugesStation::update_dials() {
  const PlayerSub& p = env_.mission.player();
  const std::uint32_t options = env_.mission.sim().conditions().options;
  Image8& screen = env_.screen();
  const Viewport out = Viewport::full(screen);
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  const auto dial = [&](std::uint32_t address, std::int32_t value, std::int32_t digits) {
    draw_needle_dial(out, background_, NeedleDial::read(env_.exe, address), sprites_, env_.assets.digits, identity,
                     value, std::nullopt, digits);
  };
  if (soundings()) {
    // The needle holds the last sounding; none before the first.
    const Fathometer& fathometer = env_.mission.sim().sensors().fathometer();
    if (fathometer.state().soundings != last_sounding_) {
      last_sounding_ = fathometer.state().soundings;
      if (const std::optional<std::int32_t> keel = fathometer.sounded_keel_clearance()) {
        const std::int32_t feet = fathometer_feet(*keel);
        dial(kFathometerDial, feet, feet);
      }
    }
  } else if ((options & kOptionFathometer) != 0 && p.motion().keel_clearance != last_keel_) {
    last_keel_ = p.motion().keel_clearance;
    const std::int32_t feet = fathometer_feet(last_keel_);
    dial(kFathometerDial, feet, feet);
  }
  if (p.fuel() != last_fuel_) {
    // gauges_update_fuel (0x3a798): a career's second needle shows the
    // fuel of the transit home (0x1260b0).
    last_fuel_ = p.fuel();
    const std::int32_t capacity = p.sub().fuel_capacity;
    std::optional<std::int32_t> home;
    if ((options & option::kModeCareer) != 0) {
      home = fuel_needle(env_.mission.transit_fuel(), capacity);
    }
    draw_needle_dial(out, background_, NeedleDial::read(env_.exe, kFuelDial), sprites_, env_.assets.digits, identity,
                     fuel_needle(p.fuel(), capacity), home, p.fuel());
  }
  if (p.battery() != last_battery_) {
    last_battery_ = p.battery();
    compose(screen, background_, kBatteryRect, [&](const Viewport& vp) {
      Image8 scratch;
      sprite_draw_transformed(vp, sprites_.frame(kBatteryNeedleFrame), 0x42, 0x47, scratch,
                              Angle10(battery_needle_angle(p.battery())), Fixed16::from_int(1),
                              Fixed16::from_int(1), 0);
      draw_sprite_clipped(vp, sprites_.frame(kBatteryGlassFrame), 0, 0);
    });
  }
  if (p.dive().air_pct != last_air_) {
    last_air_ = p.dive().air_pct;
    dial(kAirDial, air_needle(last_air_), 0);
  }
  if (p.dive().flood != last_flood_) {
    last_flood_ = p.dive().flood;
    dial(kBuoyancyDial, buoyancy_needle(last_flood_, p.sub()), 0);
  }
  if (p.air() != last_oxygen_) {
    // Drawn on the station picture itself.
    last_oxygen_ = p.air();
    const SpriteFrame& back = sprites_.frame(kOxygenBackFrame);
    const SpriteFrame& bar = sprites_.frame(kOxygenBarFrame);
    const int top = oxygen_bar_offset(p.air(), p.air_max());
    for (Image8* target : {&screen, &background_}) {
      const Viewport vp{target, kOxygenRect.x0, kOxygenRect.y0, kOxygenRect.x1, kOxygenRect.y1};
      draw_sprite_clipped(vp, back, 0, 0);
      draw_sprite_clipped(vp, bar, 0, top);
    }
  }
}

bool GaugesStation::soundings() const { return env_.mission.sim().conditions().rules.fathometer_soundings; }

void GaugesStation::update() {
  // gauges_station_update (0x39e8a)
  if (pending_page_) {
    set_page(*pending_page_, true);
    pending_page_.reset();
  }
  panel_->update(env_.ui.redraw);
  update_engine_lights();
  draw_bathythermograph();
  update_dials();
  if (first_frame()) {
    env_.dac.fade_in(env_.palette, 10);
  }
}

}  // namespace sh

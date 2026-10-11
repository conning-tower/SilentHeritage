#include "game/stations/chart.h"

#include <algorithm>
#include <climits>
#include <utility>

#include "assets/game_exe.h"
#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/patrol_contacts.h"
#include "game/saves_options.h"
#include "game/stations/chart_reports.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/depth_charge.h"
#include "sim/effects.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/torpedo.h"
#include "ui/draw.h"
#include "view/polygon.h"

namespace sh {

namespace {

// The view's screen rectangle (viewport 0x104e68).
// The texts of SH.EXE (`chart.cpp` strings).
namespace text {
constexpr std::uint32_t kMapControls = 0xf1adc;       // map_controls_init_dialogs
constexpr std::uint32_t kNavigation = 0xf1aeb;
constexpr std::uint32_t kPlotMapControls = 0xf1af6;   // the plotting panel's own copy
constexpr std::uint32_t kExit = 0xf1b05;
constexpr std::uint32_t kClear = 0xf1b0a;
constexpr std::uint32_t kZoomKeys = 0xf1b42;
constexpr std::uint32_t kTenYards = 0xf1b62;
constexpr std::uint32_t kStatuteMiles = 0xf1b6e;
// chart_format_waypoint_prompt (0x21ce1): `char*` templates, byte 1 the
// waypoint number.
constexpr std::uint32_t kWaypointPrompts = 0x103420;
// chart_own_sub_info (0x29eac)
constexpr std::uint32_t kLoc = 0xf29ac;
constexpr std::uint32_t kHeading = 0xf29dd;
constexpr std::uint32_t kSpeed = 0xf29fb;
constexpr std::uint32_t kKts = 0xf2a2d;
constexpr std::uint32_t kDepth = 0xf2a3c;
constexpr std::uint32_t kFeet = 0xf2a58;
}  // namespace text

constexpr int kViewX1 = 0x27f;
constexpr int kViewY1 = 0x1ca;
// The panels (dialogs 0x117b68, 0x117fe4, 0x118278).
constexpr int kMapPanelW = 0x129;
constexpr int kMapPanelH = 0x81;
constexpr int kPlotPanelH = 0x86;
constexpr int kToolsPanelH = 0x78;  // not in the original
constexpr int kNavPanelW = 0x10b;
constexpr int kNavPanelH = 0x167;

// CHART.SHP frames.
constexpr std::size_t kNavPicture = 0;
constexpr std::size_t kScaleBar = 0x12;
constexpr std::uint8_t kZoomIn = 0x13;
constexpr std::uint8_t kZoomOut = 0x15;
constexpr std::uint8_t kCentre = 0x17;
constexpr std::uint8_t kCentreFollow = 0x18;
constexpr std::uint8_t kFollow = 0x19;
constexpr std::uint8_t kPlot = 0x1b;
constexpr std::size_t kOwnSub = 0x1d;
constexpr std::size_t kLastKnown = 0x20;
constexpr std::size_t kSinking = 0x26;
constexpr std::size_t kPilotMarker = 0x22;     // pilot_chart_icon
constexpr std::size_t kAirGroupMarker = 0x23;  // airgroup_chart_icon
constexpr std::size_t kReportMarker = 0x25;    // intel_report_chart_draw / _icon
constexpr std::size_t kTdcMarker = 0x27;       // chart_draw_target_markers
constexpr std::size_t kGunMarker = 0x28;
constexpr std::size_t kPaletteFrame = 0x29;
constexpr std::size_t kDepthChargeFrame = 0x2a;  // dc_chart_draw
constexpr std::size_t kSplashFrame = 0x2b;       // splash_chart_draw: + the splash's ticks
constexpr std::size_t kDebrisFrame = 0x32;       // debris_chart_draw
constexpr std::size_t kBaseSprite = 0x33;      // chart_draw_contacts, zoomed out
// The colour of the group symbols and the last-known marker that the
// draws remap.
constexpr std::uint8_t kSymbolColour = 0x30;
// The colour of the target markers.
constexpr std::uint8_t kMarkerColour = 0xf0;
// The base flag (+0x63) of the bases with a sprite on the map.
constexpr std::uint32_t kBaseSpriteFlag = 0x40;
// GLOBAL.SHP frames.
constexpr std::uint8_t kMapControlsButton = 0;
constexpr std::uint8_t kConnButton = 2;
constexpr std::uint8_t kCompressionButton = 0x12;
constexpr std::uint8_t kNavigationButton = 0x16;
constexpr std::size_t kClockBox = 0x24;
// SHIPICON.SHP frames of the own boat.
constexpr std::size_t kSubSurfaced = 1;
constexpr std::size_t kSubSubmerged = 2;
constexpr std::size_t kRaftIcon = 0x17;   // pilot_chart_draw
constexpr std::size_t kBomberIcon = 0x1a;  // airgroup_chart_draw
// AIRCRAFT.DAT +0x70: the aircraft's SHIPICON.SHP frame.
constexpr std::size_t kAircraftIconField = 0x70;
constexpr std::int32_t kMaxChartPlanes = 15;
// chart_downed_pilot_report's sighting range: its string's address.
constexpr std::int32_t kPilotSightingRange = 0xf2034;

// Colours.
constexpr std::uint8_t kCourseColour = 0xf;
constexpr std::uint8_t kHeadingLine = 0x52;
constexpr std::uint8_t kSurfacedDot = 0x33;
constexpr std::uint8_t kSubmergedDot = 0xa0;
constexpr std::uint8_t kAircraftBox = 0x51;   // also the high-speed screws' bearing
constexpr std::uint8_t kSoundColour = 0xd;
constexpr std::uint8_t kTrackColour = 0x80;
constexpr std::uint8_t kShellDot = 0x31;     // shell_chart_draw, debris_chart_draw
constexpr std::uint8_t kTorpedoBox = 0;      // torpedo_chart_draw

// The weapons overlay (realistic charts off): depth charges from 64x, a
// splash for its first 7 ticks; the scale divisors of their sprites.
constexpr std::int32_t kDepthChargeMinZoom = 0x40;
constexpr std::int32_t kSplashTicks = 7;
constexpr std::int32_t kDepthChargeScale = 1050;
constexpr std::int32_t kDebrisScale = 600;
// ship_chart_draw's sonar wedge: 2000 x 48 world units long, 3.5 degrees
// either side of the beam.
constexpr std::int32_t kWedgeLength = 2000;
constexpr std::int32_t kWedgeHalfAngle = 0x23;

constexpr std::int32_t kSfxClick = 2;
// The view poll period (0x129bd8 >= 25 ticks), on game timer 6.
constexpr std::size_t kPollTimer = 6;
// The cursors.
constexpr ui::CursorId kDefaultCursor = ui::CursorId::kPointer;
constexpr ui::CursorId kDragCursor = ui::CursorId::kMove;
constexpr ui::CursorId kPlotCursor = ui::CursorId::kCross;

// The tint table 0x117844 (chart_screen): the sea colours 0x80..0x95 become
// 0xc, 0xc and 0xd one lighter; the last-known markers and the sonar
// wedges draw through it.
ColourRemap tint_table() {
  ColourRemap t{};
  for (std::size_t i = 0; i < t.size(); ++i) {
    t[i] = static_cast<std::uint8_t>(i);
  }
  std::fill(t.begin() + 0x80, t.begin() + 0x96, 0xc);
  t[0xc] = 0xd;
  t[0xd] = 0xe;
  return t;
}

std::string padded(std::int32_t value) {
  std::string s = std::to_string(value);
  return s.size() < 2 ? "0" + s : s;
}

// format_clock_hms (0x4b762)
std::string clock_text(const GameClock& c) {
  return padded(c.hour()) + ":" + padded(c.minute()) + ":" + padded(c.second());
}

}  // namespace

std::string chart_scale_value(std::int32_t zoom_int) {
  const std::int32_t yards = 176000 / zoom_int;
  return std::to_string(yards / (yards < 10000 ? 10 : 1760));
}

std::string_view chart_scale_legend(std::int32_t zoom_int) {
  return game_exe().c_string(176000 / zoom_int < 10000 ? text::kTenYards : text::kStatuteMiles);
}

std::string chart_format_latlon(const GeoAngle& angle) {
  std::string minutes = std::to_string(angle.minutes);
  if (minutes.size() < 2) {
    minutes.insert(0, "0");
  }
  return std::to_string(angle.whole_degrees) + "\x1a" + minutes + "\x1b" + angle.hemisphere;
}

// The sprite buttons of the panels: `cycle_button_proc` (0x49472) with the
// area's click action, and `sprite_button_proc` (0x4922c) for the time
// compression button.
class ChartStation::SpriteButton : public ui::Control {
 public:
  SpriteButton(ChartStation& chart, ui::Rect rect, const Shp& sprites, std::function<std::uint8_t()> frame,
               std::uint8_t pressed_frame, std::function<void()> action, bool compression = false)
      : chart_(chart), rect_(rect), sprites_(sprites), frame_(std::move(frame)), pressed_frame_(pressed_frame),
        action_(std::move(action)), compression_(compression) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    bool down = false;
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(dialog, *this, rect_, 0, 0, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        if (!pressed || compression_) {
          chart_.env_.sound.play_sfx(kSfxClick, 0x1e);
        }
        down = true;
        break;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        break;
      default:
        return false;
    }
    dialog.add_dirty(draw(dialog, down));
    return true;
  }

  int activate(ui::Dialog&, int, const ui::Event&) override {
    action_();
    return 0;
  }

 private:
  ui::Rect draw(ui::Dialog& dialog, bool down) {
    const std::uint8_t f = down ? pressed_frame_ : frame_();
    const SpriteFrame& frame = sprites_.frame(f);
    draw_sprite_clipped(dialog.gfx(), frame, rect_.x0, rect_.y0);
    const ui::Rect r{rect_.x0, rect_.y0, rect_.x0 + frame.width() - 1, rect_.y0 + frame.height() - 1};
    if (compression_) {
      // sprite_button_draw (0x49291): the effective compression, centred.
      const std::int32_t compression = chart_.env_.mission.sim().clock().effective_compression();
      const std::string text = std::to_string(compression);
      const Font& font = chart_.env_.assets.font1;
      const std::int32_t free = r.x1 - r.x0 + 1 - font.text_width(text) - 1;
      GlyphColourTable table = ui::GuiResources::identity_table();
      table[7] = 0;
      gui_draw_text(dialog.gfx(), rect_.x0 + (down ? -1 : 0) + 1 + free / 2, rect_.y0 + (down ? 1 : 0) + 5, font,
                    text, &table);
      chart_.shown_compression_ = compression;
    }
    return r;
  }

  ChartStation& chart_;
  ui::Rect rect_;
  const Shp& sprites_;
  std::function<std::uint8_t()> frame_;
  std::uint8_t pressed_frame_;
  std::function<void()> action_;
  bool compression_;
};

// The follow toggle (`push_button_proc` 0x49613, `push_button_draw`
// 0x4969b): frame 0x19 plus the follow state.
class ChartStation::FollowButton : public ui::Control {
 public:
  FollowButton(ChartStation& chart, ui::Rect rect) : chart_(chart), rect_(rect) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    if (msg == ui::Msg::kRegister) {
      ui::add_control_hotspot(dialog, *this, rect_, 0, 0, ui::kFocusNone);
      return false;
    }
    if (msg == ui::Msg::kPress) {
      if (!pressed) {
        chart_.env_.sound.play_sfx(kSfxClick, 0x1e);
        chart_.toggle_follow();
      }
    } else if (msg != ui::Msg::kPaint) {
      return false;
    }
    const bool follow = chart_.env_.options.chart_follow_sub != 0;
    const SpriteFrame& frame = chart_.chart_sprites_.frame(kFollow + (follow ? 1 : 0));
    draw_sprite_clipped(dialog.gfx(), frame, rect_.x0, rect_.y0);
    dialog.add_dirty({rect_.x0, rect_.y0, rect_.x0 + frame.width() - 1, rect_.y0 + frame.height() - 1});
    return true;
  }

 private:
  ChartStation& chart_;
  ui::Rect rect_;
};

ChartStation::ChartStation(StationEnv& env)
    : env_(env),
      grid_(env.mission.world()),
      view_(env.mission.world(), 0, 0, kViewX1, kViewY1),
      chart_sprites_(Shp::load(env.install, env.paths.path(GameDir::kArt, "CHART.SHP"))),
      ship_icons_(Shp::load(env.install, env.paths.path(GameDir::kArt, "SHIPICON.SHP"))),
      popup_(env.gui, env.assets.font1),
      panels_(env.gui, ui::Rect{0, 0, kViewX1, kViewY1},
              ChartPanelHost::Hooks{
                  .hover = [this](bool over) { panel_hover(over); },
                  .mouse_move = [this](int x, int y) { mouse_moved(x, y); },
                  .cursor = [this](ChartCursor c) {
                    set_cursor(c == ChartCursor::kDrag ? kDragCursor : kDefaultCursor);
                  }}) {
  // chart_screen's chart_view_add_panel calls, in `ChartPanel` order.
  const auto slot = [this](ChartPanelPosition* position, int w, int h, std::function<void()> open) {
    ChartPanelSlot s;
    s.position = position;
    s.width = w;
    s.height = h;
    s.open = std::move(open);
    s.close = [this] { close_panel_dialogs(); };
    s.event = [this](const ui::Event& e) {
      if (nav_panel_) {
        nav_panel_->event(e);
      } else if (panel_dialog_) {
        panel_dialog_->handle_event(e);
      }
    };
    s.process = [this] {
      if (nav_panel_) {
        nav_panel_->process();
      } else if (panel_dialog_) {
        panel_dialog_->process();
      }
    };
    s.redraw = [this] { repaint_panel_dialog(); };
    return s;
  };
  panels_.add(slot(&panel_pos_, kMapPanelW, kMapPanelH, [this] { open_map_panel(); }));
  ChartPanelSlot nav = slot(&nav_pos_, kNavPanelW, kNavPanelH, [this] { open_nav_panel(); });
  nav.update = [this] { nav_panel_->update(env_.ui.redraw); };
  panels_.add(std::move(nav));
  panels_.add(slot(&panel_pos_, kMapPanelW, kPlotPanelH, [this] { open_plot_panel(); }));
  if (env_.ui.features.chart_tools) {
    panels_.add(slot(&panel_pos_, kMapPanelW, kToolsPanelH, [this] { open_tools_panel(); }));
  }
  // chart_reset_view (0x20868)
  view_.set_zoom(0x10000);
  view_.set_center(0, 0);
  const fx::Vec3 sub = env_.mission.player().position();
  const std::array<std::int32_t, 2> c = view_.world_to_chart(sub.x, sub.z);
  view_.set_center(c[0], c[1]);
}

ChartStation::~ChartStation() {
  if (env_.panels == this) {
    env_.panels = nullptr;
  }
}

void ChartStation::build_palette() {
  // chart_screen (0x20979): the previous colours with CHART frame 0x29's,
  // CONN.PCX's 0..15, SHIPICON frame 0's 16..63 and the map's chart
  // colours 128..239 (`chart_view_init`).
  VgaPalette& pal = env_.palette;
  sprite_apply_palette(chart_sprites_.frame(kPaletteFrame), pal);
  VgaPalette conn = load_pcx(env_.install, env_.paths.path(GameDir::kArt, "CONN.PCX")).palette;
  std::copy_n(conn.begin(), 16 * 3, pal.begin());
  sprite_apply_palette(ship_icons_.frame(0), conn);
  std::copy(conn.begin() + 16 * 3, conn.begin() + 64 * 3, pal.begin() + 16 * 3);
  const World& world = env_.mission.world();
  std::ranges::copy(world.chart_dac, pal.begin() + 3 * kChartDacFirstIndex);
  inks_ = ChartInks::nearest(pal);
}

void ChartStation::open() {
  // chart_screen (0x20979) and chart_view_init (0x2a5c8)
  ++env_.ui.pause;
  env_.bar.open({{}, {}, false, false, true});
  Image8& screen = env_.screen();
  ui::gui_fill_rect(Viewport::full(screen), 0, 0, screen.width(), screen.height(), 0);
  nav_background_ = Image8(kNavPanelW, kNavPanelH, 0);
  draw_sprite_clipped(Viewport::full(nav_background_), chart_sprites_.frame(kNavPicture), 8, 0x28);
  build_palette();

  dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, 0, kViewX1 + 1, kViewY1 + 1, 0, &screen);
  dialog_->emplace<ui::Area>(
      0, 0, kViewX1, kViewY1,
      [this](ui::Dialog& d, ui::Area& a, ui::Msg msg, int, const ui::Event& e) {
        // chart_view_on_click (0x2b716)
        if (msg == ui::Msg::kPress) {
          std::int32_t x = e.x - (d.x() + a.x0);
          std::int32_t y = e.y - (d.y() + a.y0);
          if (!panels_.locked() && zoom_box_.is_open()) {
            // chart_zoom_box_center (0x212fb): zooms on the pointer, or on
            // the boat when following it.
            std::array<std::int32_t, 2> c{};
            if (env_.options.chart_follow_sub == 0) {
              c = view_.screen_to_chart(x, y);
            } else {
              const fx::Vec3 sub = env_.mission.player().position();
              c = view_.world_to_chart(sub.x, sub.z);
            }
            view_.set_zoom(zoom_box_.value() << 16);
            view_.set_center(c[0], c[1]);
            zoom_box_.free_magnifier(env_.screen());
            zoom_box_.close();
            panels_.deactivate();
          } else if (env_.ui.features.chart_tools) {
            tools_press(x, y);
          } else {
            click(x, y);
          }
        }
        return 0;
      },
      nullptr);
  dialog_->emplace<ui::Area>(-1, -1, -1, -1, nullptr,
                             [this](ui::Dialog&, ui::Area&, int, const ui::Event&) {
                               zoom_box_grow();
                               return 0;
                             },
                             0xff5a);
  dialog_->emplace<ui::Area>(-1, -1, -1, -1, nullptr,
                             [this](ui::Dialog&, ui::Area&, int, const ui::Event&) {
                               zoom_box_shrink();
                               return 0;
                             },
                             0xff58);
  dialog_->open();
  panels_.set_view_dialog(dialog_.get());
  zoom_box_.reset(screen);
  dirty_ = true;
  panels_.reset();
  zoom_box_.close();
  force_overlays_ = false;

  env_.dac.fade_out(0x100);
  env_.ui.timers[kPollTimer] = 100000;
  course_line_.forget();
  zoom_message_ = false;
  shown_compression_ = env_.mission.sim().clock().effective_compression();
  set_cursor(kDefaultCursor);
  saved_click_mode_ = click_mode_;
  click_mode_ = ChartClickMode::kSelectTarget;
  if (panel() == ChartPanel::kPlot) {
    saved_click_mode_ = click_mode_;
    click_mode_ = ChartClickMode::kPlot;
    set_cursor(kPlotCursor);
  }
  if (env_.ui.features.chart_tools) {
    pointer_.cancel();
    if (panel() == ChartPanel::kTools) {
      saved_click_mode_ = click_mode_;
      click_mode_ = ChartClickMode::kAnnotate;
      set_cursor(kPlotCursor);
    }
  }
  env_.panels = this;
  --env_.ui.pause;
}

void ChartStation::close() {
  // chart_station_close (0x20de5) and chart_view_close (0x2a7b2)
  zoom_box_.free_magnifier(env_.screen());
  panels_.close_panel();
  popup_.close();
  dialog_->close();
  dialog_.reset();
  if (env_.panels == this) {
    env_.panels = nullptr;
  }
}

void ChartStation::event(const ui::Event& event) {
  // chart_station_event (0x20e8f): ESC or a right click ends the plotting.
  if (!zoom_box_.is_open() && panel() == ChartPanel::kPlot &&
      ((event.is_key() && event.key == ui::kKeyEsc) || (event.flags & ui::kEventRightUp) != 0)) {
    set_cursor(kDefaultCursor);
    std::swap(click_mode_, saved_click_mode_);
    panels_.request(static_cast<int>(ChartPanel::kMap));
    env_.bar.set_escape_passthrough(false);
    return;
  }
  if (env_.ui.features.chart_tools && tools_event(event)) {
    return;
  }
  dialog_->handle_event(event);
  panels_.event(event);
}

void ChartStation::process() {
  // chart_view_process (0x2a843)
  dialog_->process();
  panels_.process();
}

void ChartStation::update() {
  // chart_station_update (0x20f26)
  if (click_mode_ == ChartClickMode::kPlot && env_.mission.player().route_index() != 0) {
    compact_waypoints();
  }
  view_update();
  // The zoom box holds the simulation.
  if (zoom_box_.is_open() && env_.ui.pause == 0) {
    env_.ui.pause = 1;
  }
  if (!zoom_box_.is_open() && env_.ui.pause != 0) {
    env_.ui.pause = 0;
  }
  if (click_mode_ == ChartClickMode::kSelectTarget) {
    // chart_refresh_hover (0x219cb)
    hover_info(env_.gui.mouse().x(), env_.gui.mouse().y());
  }
  const GameClock& clock = env_.mission.sim().clock();
  if (clock.effective_compression() != shown_compression_) {
    if (panel() == ChartPanel::kMap && compression_control_ != nullptr) {
      compression_control_->dirty = true;
    }
    shown_compression_ = clock.effective_compression();
  }
  const std::array<std::int32_t, 3> now{clock.hour(), clock.minute(), clock.second()};
  if (now != shown_clock_) {
    if (clock_control_ != nullptr) {
      clock_control_->dirty = true;
    }
    shown_clock_ = now;
  }
  if (first_frame()) {
    env_.dac.fade_in(env_.palette, 10);
  }
}

// --- The view ----------------------------------------------------------------

void ChartStation::view_update() {
  // chart_view_update (0x2a875)
  panels_.update();
  const int active = panels_.active();
  panels_.switch_panel();
  if (panels_.active() != active) {
    zoom_hold_ = false;
  }
  const bool dragging = panels_.locked();
  panels_.handle_mouse();
  if (dragging && !panels_.locked()) {
    // The drag's end clears the whole flag.
    zoom_hold_ = false;
  }
  // chart_view_edge_scroll (0x2aa58): not while the view is held or
  // follows the boat (0x118a98).
  if (!view_held() && env_.options.chart_follow_sub == 0) {
    chart_edge_scroll(view_, env_.gui.mouse(), panels_.hovered(), dirty_);
  }
  if (env_.ui.features.chart_tools) {
    tools_update();
  }
  if (dirty_) {
    redraw_map();
  } else if (!zoom_box_.is_open()) {
    // chart_view_poll_redraw (0x2aeb3): a forced redraw closes the popup.
    if (force_overlays_) {
      close_popup();
    }
    if (poll() || force_overlays_) {
      redraw_overlays();
      force_overlays_ = false;
    }
  }
  if (!panels_.locked() && zoom_box_.is_open()) {
    panels_.close_panel();
    close_popup();
    set_cursor(kDefaultCursor);
    ui::Mouse& mouse = env_.gui.mouse();
    int x = mouse.x();
    int y = mouse.y();
    course_line_erase();
    if (env_.options.chart_follow_sub != 0) {
      const fx::Vec3 sub = env_.mission.player().position();
      const ChartPoint p = view_.world_to_screen(sub.x, sub.z);
      x = p.x;
      y = p.y;
      mouse.update(x, y, mouse.buttons(), 0);
    }
    const int cx = std::clamp(x, view_.x0(), view_.x1());
    const int cy = std::clamp(y, view_.y0(), view_.y1());
    if (cx != x || cy != y) {
      mouse.update(cx, cy, mouse.buttons(), 0);
    }
    zoom_box_.track(env_.screen(), view_, cx, cy);
  }
}

bool ChartStation::poll() {
  // chart_view_poll (0x210be)
  std::int32_t& timer = env_.ui.timers[kPollTimer];
  if (timer < 0x19) {
    return false;
  }
  timer = 0;
  course_line_erase();
  if (env_.options.chart_follow_sub != 0) {
    follow_sub();
  }
  return true;
}

void ChartStation::follow_sub() {
  // chart_follow_sub (0x21679)
  if (view_held()) {
    return;
  }
  const std::int32_t cx = view_.center_x();
  const std::int32_t cy = view_.center_y();
  const fx::Vec3 sub = env_.mission.player().position();
  const std::array<std::int32_t, 2> c = view_.world_to_chart(sub.x, sub.z);
  view_.set_center(c[0], c[1]);
  dirty_ = view_.center_x() != cx || view_.center_y() != cy;
}

void ChartStation::redraw_map() {
  // chart_view_redraw_if_dirty (0x2abce): the map, a clean copy of it, the
  // overlays, the panel.
  dirty_ = false;
  Image8& screen = env_.screen();
  view_.draw(screen);
  if (env_.ui.features.chart_tools) {
    draw_annotations();
  }
  clean_map_ = Image8(view_.x1() - view_.x0() + 1, view_.y1() - view_.y0() + 1);
  for (int y = 0; y < clean_map_.height(); ++y) {
    for (int x = 0; x < clean_map_.width(); ++x) {
      clean_map_.set(x, y, screen.at(view_.x0() + x, view_.y0() + y));
    }
  }
  draw_contacts();
  if (env_.ui.features.chart_tools) {
    draw_tool_overlays();
  }
  overlays_in_panel_ = overlays_reach_panel();
  repaint_panel();
}

void ChartStation::redraw_overlays() {
  // chart_view_redraw_overlays (0x2b35c): the overlays drawn again over
  // the clean map. The original restores only their rectangles and
  // repaints the open panel only when an old or a new one meets it: a
  // repaint does not draw a held control (e.g. the telegraph while
  // pressed). Here the whole clean map is restored, so the panel's pixels
  // are put back when no overlay, old or new, reaches into it.
  Image8& screen = env_.screen();
  const std::optional<ui::Rect> panel = panel_on_map();
  Image8 kept;
  if (panel) {
    kept = Image8(panel->x1 - panel->x0 + 1, panel->y1 - panel->y0 + 1);
    for (int y = 0; y < kept.height(); ++y) {
      for (int x = 0; x < kept.width(); ++x) {
        kept.set(x, y, screen.at(panel->x0 + x, panel->y0 + y));
      }
    }
  }
  screen.blit(clean_map_, view_.x0(), view_.y0());
  course_line_.forget();
  draw_contacts();
  if (env_.ui.features.chart_tools) {
    draw_tool_overlays();
  }
  const bool reached = overlays_reach_panel();
  if (!panel || reached || overlays_in_panel_) {
    repaint_panel();
  } else {
    screen.blit(kept, panel->x0, panel->y0);
    popup_.repaint();
  }
  overlays_in_panel_ = reached;
}

std::optional<ui::Rect> ChartStation::panel_on_map() const {
  if (!panels_.is_open()) {
    return std::nullopt;
  }
  const std::optional<ui::Rect> r = panels_.active_rect();
  if (!r) {
    return std::nullopt;
  }
  const ui::Rect clipped{std::max(r->x0, view_.x0()), std::max(r->y0, view_.y0()), std::min(r->x1, view_.x1()),
                         std::min(r->y1, view_.y1())};
  if (clipped.x0 > clipped.x1 || clipped.y0 > clipped.y1) {
    return std::nullopt;
  }
  return clipped;
}

bool ChartStation::overlays_reach_panel() const {
  const std::optional<ui::Rect> panel = panel_on_map();
  if (!panel) {
    return false;
  }
  const Image8& screen = env_.screen();
  for (int y = panel->y0; y <= panel->y1; ++y) {
    for (int x = panel->x0; x <= panel->x1; ++x) {
      if (screen.at(x, y) != clean_map_.at(x - view_.x0(), y - view_.y0())) {
        return true;
      }
    }
  }
  return false;
}

void ChartStation::repaint_panel() {
  popup_.repaint();
  panels_.redraw();
}

void ChartStation::repaint_panel_dialog() {
  ui::Dialog* d = nav_panel_ ? nav_panel_->dialog() : panel_dialog_.get();
  if (d != nullptr) {
    d->invalidate();
    d->redraw_controls();
  }
  if (nav_panel_) {
    nav_panel_->update(true);
  }
}

// --- The overlays ------------------------------------------------------------

void ChartStation::draw_contacts() {
  // chart_draw_contacts (0x21df4): from 8x the vehicles draw themselves
  // (pass -1: the tracks and splashes; then the sound bearings, the
  // vehicles lowest first and the target markers); below, the bases' sprites, the boat's
  // marker, the vehicles' map icons (slot +0x40: bomber groups, pilots, air
  // reports) and the contact groups.
  course_line_.forget();
  panels_.force_mouse_move();
  const auto& vehicles = env_.mission.sim().vehicles();
  if (view_.zoom_int() <= 7) {
    draw_base_sprites();
    draw_own_sub(false);
    for (const auto& v : vehicles) {
      if (v->removed()) {
        continue;
      }
      switch (v->class_id()) {
        case VehicleClass::kBomberGroup:
          draw_air_group_icon(static_cast<const AirGroup&>(*v));
          break;
        case VehicleClass::kDownedPilot:
          draw_pilot_icon(static_cast<const DownedPilot&>(*v));
          break;
        case VehicleClass::kAirReport:
          draw_report(static_cast<const IntelReport&>(*v));
          break;
        default:
          break;
      }
    }
    draw_groups();
    return;
  }
  // Pass -1: the track histories of the boat, of the ships and submarines
  // on the chart and of the torpedoes shown, and the splashes.
  const bool limited = (env_.mission.sim().conditions().options & option::kLimitedVisibility) != 0;
  for (const auto& v : vehicles) {
    if (v->removed()) {
      continue;
    }
    switch (v->class_id()) {
      case VehicleClass::kPlayer:
        draw_track(env_.mission.player().motion());
        break;
      case VehicleClass::kShip: {
        const auto& ship = static_cast<const Ship&>(*v);
        const bool sinking = limited ? ship.sinking() && ship.ai().sighting.sighted : ship.sinking();
        if ((ship.sighted() || !limited) && !sinking) {
          draw_track(ship.motion());
        }
        break;
      }
      case VehicleClass::kEnemySub: {
        const auto& sub = static_cast<const EnemySub&>(*v);
        const bool sinking = limited ? sub.sinking() && sub.sighting().sighted : sub.sinking();
        if ((sub.sighted() || (!limited && sub.visual_height() > 0)) && !sinking) {
          draw_track(sub.motion());
        }
        break;
      }
      case VehicleClass::kTorpedo: {
        // torpedo_chart_draw (0x24820), pass -1.
        const auto& torpedo = static_cast<const TorpedoVehicle&>(*v);
        if (weapons_shown() || torpedo.sighted()) {
          draw_track(torpedo.motion());
        }
        break;
      }
      case VehicleClass::kSplash:
        draw_splash(static_cast<const Splash&>(*v));
        break;
      default:
        break;
    }
  }
  draw_sound_bearings();
  std::vector<Vehicle*> order;
  for (const auto& v : vehicles) {
    switch (v->class_id()) {
      case VehicleClass::kPlayer:
      case VehicleClass::kShip:
      case VehicleClass::kEnemySub:
      case VehicleClass::kAircraft:
      case VehicleClass::kBomberGroup:
      case VehicleClass::kDownedPilot:
      case VehicleClass::kAirReport:
      case VehicleClass::kTorpedo:
      case VehicleClass::kDepthCharge:
      case VehicleClass::kShell:
      case VehicleClass::kDebris:
        if (!v->removed()) {
          order.push_back(v.get());
        }
        break;
      default:
        break;
    }
  }
  std::stable_sort(order.begin(), order.end(),
                   [](const Vehicle* a, const Vehicle* b) { return a->position().y < b->position().y; });
  for (Vehicle* v : order) {
    switch (v->class_id()) {
      case VehicleClass::kPlayer:
        draw_own_sub(true);
        break;
      case VehicleClass::kShip:
        draw_ship(static_cast<Ship&>(*v));
        break;
      case VehicleClass::kEnemySub:
        draw_enemy_sub(static_cast<const EnemySub&>(*v));
        break;
      case VehicleClass::kAircraft:
        draw_aircraft(static_cast<const Aircraft&>(*v));
        break;
      case VehicleClass::kBomberGroup:
        draw_air_group(static_cast<const AirGroup&>(*v));
        break;
      case VehicleClass::kDownedPilot:
        draw_pilot(static_cast<const DownedPilot&>(*v));
        break;
      case VehicleClass::kAirReport:
        draw_report(static_cast<const IntelReport&>(*v));
        break;
      case VehicleClass::kTorpedo:
        draw_torpedo(static_cast<const TorpedoVehicle&>(*v));
        break;
      case VehicleClass::kDepthCharge:
        draw_depth_charge(static_cast<const DepthCharge&>(*v));
        break;
      case VehicleClass::kShell:
        draw_shell(static_cast<const Shell&>(*v));
        break;
      case VehicleClass::kDebris:
        draw_debris(static_cast<const Debris&>(*v));
        break;
      default:
        break;
    }
  }
  draw_target_markers();
}

ContactGroups* ChartStation::contact_groups() const {
  // The patrol hands its groups to the sensors (game/patrol.cpp); the chart
  // draws and reports them (the original's list 0x119248).
  return dynamic_cast<ContactGroups*>(env_.mission.sim().sensors().environment().contact_groups);
}

std::vector<ContactGroup*> ChartStation::chart_groups() const {
  std::vector<ContactGroup*> out;
  if (ContactGroups* groups = contact_groups()) {
    for (const std::unique_ptr<ContactGroup>& g : groups->groups()) {
      out.push_back(g.get());
    }
  } else if (ContactGroup* g = env_.mission.scenario_group()) {
    out.push_back(g);
  }
  return out;
}

bool ChartStation::group_member(const ContactGroup& g, const Vehicle& v) const {
  if (const ContactGroups* groups = contact_groups()) {
    return groups->group_of(v) == &g;
  }
  return std::ranges::find(g.vehicles, &v) != g.vehicles.end();
}

void ChartStation::draw_base_sprites() {
  // chart_draw_contacts, zoomed out: CHART.SHP frame 0x33 at the zoom's
  // scale on the bases flagged 0x40 (+0x63).
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  const Fixed16 s = Fixed16::from_raw(view_.zoom());
  Image8 scratch;
  for (const BaseRecord& b : env_.mission.bases()) {
    std::int32_t x = 0;
    std::int32_t y = 0;
    if ((b.flags & kBaseSpriteFlag) != 0 && view_.point_on_screen(b.station.x, b.station.z, x, y)) {
      sprite_draw_transformed(vp, chart_sprites_.frame(kBaseSprite), x, y, scratch, Angle10(0), s, s, 0);
    }
  }
}

void ChartStation::draw_groups() {
  // contacts_chart_draw (0x30eea): each group's slot +0x14 in list order.
  for (ContactGroup* g : chart_groups()) {
    draw_group(*g);
  }
}

void ChartStation::draw_group(ContactGroup& g) {
  // The kinds' chart draws (convoy_group_chart_draw 0x27a08,
  // contacts_patrol_chart_draw 0x288c6, contacts_anchored_chart_draw
  // 0x29019, contacts_sampans_chart_draw 0x29635): the contact refreshed
  // from the members while materialized, then its symbol.
  if (g.materialized) {
    std::vector<GroupMember> members;
    for (const auto& v : env_.mission.sim().vehicles()) {
      if (group_member(g, *v)) {
        members.push_back({v.get(), v->convoy_group_by_role(false) != 0});
      }
    }
    const GameClock& clock = env_.mission.sim().clock();
    const fx::Vec3 sub = env_.mission.player().position();
    refresh_group_contact(g, members, sub.x, sub.z, {clock.hour(), clock.minute(), clock.month(), clock.day()},
                          (env_.mission.sim().conditions().options & option::kLimitedVisibility) != 0);
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!g.report.valid || g.finished || !view_.point_on_screen(g.report.x, g.report.z, x, y)) {
    return;
  }
  const std::optional<GroupSymbol> symbol = group_symbol(g, env_.exe);
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  if (symbol->heading) {
    const fx::Point2 end = fx::point_rotate_scale({x, y - 0xf}, {x, y}, *symbol->heading, fx::kOne, fx::kOne);
    ui::draw_line(vp, x, y, end.x, end.y, symbol->colour);
  }
  ColourRemap remap{};
  for (std::size_t i = 0; i < remap.size(); ++i) {
    remap[i] = static_cast<std::uint8_t>(i);
  }
  remap[kSymbolColour] = symbol->colour;
  draw_sprite_remapped(vp, chart_sprites_.frame(symbol->frame), x, y, remap);
}

void ChartStation::draw_track(const MotionState& m) {
  // chart_draw_track_history (0x26ce0): the wake trail, newest leg (to the
  // live position) in colour 0x80, each older one 2 further.
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  std::uint8_t colour = kTrackColour;
  for (std::int32_t i = m.wake_count; i > 0; --i) {
    const fx::Vec3& from = m.wake[static_cast<std::size_t>(i - 1)];
    const fx::Vec3& to = i == m.wake_count ? m.position : m.wake[static_cast<std::size_t>(i)];
    const ChartPoint a = view_.world_to_screen(from.x, from.z);
    const ChartPoint b = view_.world_to_screen(to.x, to.z);
    ui::draw_line(vp, a.x, a.y, b.x, b.y, colour);
    colour = static_cast<std::uint8_t>(colour + 2);
  }
}

void ChartStation::draw_sound_bearings() {
  // sound_contacts_next (0x70591) over the vehicles' slot +0x60
  // (ship_get_sound_contact clears its ping flag), each drawn by
  // chart_draw_sound_bearing (0x26eca): 150 pixels from the boat along the
  // bearing, colour 0x51 for high-speed screws, else 0xd.
  const PlayerSub& player = env_.mission.player();
  std::int32_t sx = 0;
  std::int32_t sy = 0;
  const bool on_screen = view_.point_on_screen(player.position().x, player.position().z, sx, sy);
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  for (const auto& v : env_.mission.sim().vehicles()) {
    Angle10 bearing;
    ScrewSound screw = ScrewSound::kHighSpeed;
    if (auto* ship = dynamic_cast<Ship*>(v.get())) {
      ShipAi& ai = ship->ai();
      const bool heard = ai.heard.heard || ai.sonar.chart_echo;
      ai.sonar.chart_echo = false;
      if (!heard) {
        continue;
      }
      bearing = ai.heard.bearing;
      screw = ai.heard.screw;
    } else if (const auto* sub = dynamic_cast<const EnemySub*>(v.get())) {
      if (!sub->heard()) {
        continue;
      }
      bearing = sub->heard_bearing();
      const ShipClass* cls = sub->setup().ship_class;
      screw = cls != nullptr ? cls->screw_sound : ScrewSound::kHighSpeed;
    } else {
      continue;
    }
    if (!on_screen) {
      continue;
    }
    const fx::Point2 d =
        fx::point_rotate_scale({0, -150}, {0, 0}, fx::angle_add(bearing, player.heading()), fx::kOne, fx::kOne);
    ui::draw_line(vp, sx, sy, sx + d.x, sy + d.y, screw == ScrewSound::kHighSpeed ? kAircraftBox : kSoundColour);
  }
}

void ChartStation::draw_target_markers() {
  // chart_draw_target_markers (0x26b15): the TDC's locked target (frame
  // 0x27) and the deck gun's (0x28), coloured 0xf0 + quality / 10.
  const PlayerCombat& combat = env_.mission.player_combat();
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  const auto marker = [&](const Vehicle* target, bool locked, std::int32_t quality, std::size_t frame) {
    if (target == nullptr || !locked) {
      return;
    }
    quality = std::min(quality, 99);
    if (quality < 0) {
      return;
    }
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (!view_.point_on_screen(target->position().x, target->position().z, x, y)) {
      return;
    }
    ColourRemap remap{};
    for (std::size_t i = 0; i < remap.size(); ++i) {
      remap[i] = static_cast<std::uint8_t>(i);
    }
    remap[kMarkerColour] = static_cast<std::uint8_t>(kMarkerColour + quality / 10);
    draw_sprite_remapped(vp, chart_sprites_.frame(frame), x, y, remap);
  };
  marker(combat.tdc.target(), combat.tdc.locked(), combat.tdc.quality(), kTdcMarker);
  marker(combat.deck_gun.target(), combat.deck_gun.locked(), combat.deck_gun.quality(), kGunMarker);
}

void ChartStation::draw_own_sub(bool tactical) {
  // chart_draw_own_sub (0x22340), and player_chart_draw (0x2232a) from 8x.
  const PlayerSub& sub = env_.mission.player();
  if (sub.following_route() || click_mode_ == ChartClickMode::kPlot) {
    draw_course();
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(sub.position().x, sub.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  if (tactical) {
    const bool up = env_.bar.surfaced();
    ui::put_pixel(vp, x, y, up ? kSurfacedDot : kSubmergedDot);
    const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), 300 << 16));
    Image8 scratch;
    sprite_draw_transformed(vp, ship_icons_.frame(up ? kSubSurfaced : kSubSubmerged), x, y, scratch, sub.heading(), s,
                            s, 0);
    return;
  }
  const fx::Point2 end = fx::point_rotate_scale(
      {x, y - 0xf}, {x, y}, Angle10(sub.heading().tenths() + 1800).normalized(), fx::kOne, fx::kOne);
  ui::draw_line(vp, x, y, end.x, end.y, kHeadingLine);
  draw_sprite_clipped(vp, chart_sprites_.frame(kOwnSub), x, y);
}

void ChartStation::draw_ship(Ship& ship) {
  // ship_chart_draw (0x22445): the class icon at the ship's position (the
  // last seen one with Limited Visibility, marked with the age in
  // minutes), the sinking mark once seen sinking; under it the sonar
  // wedge of an escort that pinged.
  const bool limited = (env_.mission.sim().conditions().options & option::kLimitedVisibility) != 0;
  const ShipSightingRecord& record = ship.ai().sighting;
  draw_hull_contact(ship, ship.setup().ship_class, record, ship.sighted() || !limited, !limited,
                    limited ? ship.sinking() && record.sighted : ship.sinking(), &ship.ai().sonar);
}

void ChartStation::draw_enemy_sub(const EnemySub& sub) {
  // enemy_sub_chart_draw (0x233d2): as a ship, but without Limited
  // Visibility only while above the water (visual height above 0); a
  // submerged one shows at its last-known position.
  const bool limited = (env_.mission.sim().conditions().options & option::kLimitedVisibility) != 0;
  const bool visible = !limited && sub.visual_height() > 0;
  const ShipSightingRecord& record = sub.sighting();
  draw_hull_contact(sub, sub.setup().ship_class, record, sub.sighted() || visible, visible,
                    limited ? sub.sinking() && record.sighted : sub.sinking());
}

void ChartStation::draw_hull_contact(const Vehicle& v, const ShipClass* cls, const ShipSightingRecord& record,
                                     bool seen, bool true_position, bool sinking, ShipSonar* sonar) {
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  if (v.removed()) {
    return;
  }
  std::int32_t wx = 0;
  std::int32_t wz = 0;
  if (true_position) {
    wx = v.position().x;
    wz = v.position().z;
  } else {
    if (record.last_x < 0) {
      return;
    }
    wx = record.last_x;
    wz = record.last_z;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(wx, wz, x, y)) {
    return;
  }
  if (sonar != nullptr && sonar->chart_beam >= 0 && seen && !sinking) {
    if (weapons_shown()) {
      draw_sonar_wedge(wx, wz, x, y, fx::angle_add(Angle10(sonar->chart_beam), v.heading()));
    }
    sonar->chart_beam = -1;
  }
  Image8 scratch;
  if (seen && !sinking) {
    ui::put_pixel(vp, x, y, kSurfacedDot);
    const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), 300 << 16));
    const std::size_t icon = cls != nullptr ? cls->category : 0;
    if (icon < ship_icons_.frame_count()) {
      sprite_draw_transformed(vp, ship_icons_.frame(icon), x, y, scratch, v.heading(), s, s, 0);
    }
  } else if (sinking) {
    const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), 200 << 16));
    sprite_draw_transformed(vp, chart_sprites_.frame(kSinking), x, y, scratch, Angle10(0), s, s, 0);
  } else {
    // The last-known marker: colour 0x30 of the frame shows the age digit
    // (table 0x117844).
    ColourRemap remap = tint_table();
    remap[0x30] = static_cast<std::uint8_t>('0' + fx::trunc(record.age) / 60);
    draw_sprite_remapped(vp, chart_sprites_.frame(kLastKnown), x, y, remap);
  }
}

bool ChartStation::weapons_shown() const {
  return (env_.mission.sim().conditions().options & option::kRealisticChartsOff) != 0;
}

void ChartStation::draw_sonar_wedge(std::int32_t wx, std::int32_t wz, std::int32_t x, std::int32_t y,
                                    Angle10 bearing) {
  // ship_chart_draw (0x22445): the beam's two edges 2000 x 48 units long
  // from the ship (world z north, hence the mirrored angle), the triangle
  // tinted through table 0x117844 when both ends are drawable.
  const Angle10 a = fx::angle_sub(Angle10(3600), bearing);
  std::array<PolyVertex, 3> v{};
  v[0] = {x, y, 2 << 16, 0, 0};
  bool drawable = true;
  const std::array<Angle10, 2> edges{fx::angle_sub(a, Angle10(kWedgeHalfAngle)),
                                     fx::angle_add(a, Angle10(kWedgeHalfAngle))};
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const fx::Point2 p = fx::point_rotate_scale({0, kWedgeLength}, {0, 0}, edges[i], fx::kOne, fx::kOne);
    if (!view_.point_on_screen(p.x * kUnitsPerYard + wx, wz + p.y * kUnitsPerYard, v[i + 1].x, v[i + 1].y)) {
      drawable = false;
    }
  }
  if (drawable) {
    const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
    remap_polygon(vp, v, tint_table());
  }
}

void ChartStation::draw_torpedo(const TorpedoVehicle& torpedo) {
  // torpedo_chart_draw (0x24820), pass 0: with realistic charts off or
  // while the crew sees it, its collision box (corners 0, 1, 5, 4) in
  // colour 0.
  if (!weapons_shown() && !torpedo.sighted()) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(torpedo.position().x, torpedo.position().z, x, y)) {
    return;
  }
  const auto& corners = torpedo.collision().box.corners;
  std::array<ChartPoint, 4> p;
  const std::array<std::size_t, 4> order{0, 1, 5, 4};
  for (std::size_t i = 0; i < order.size(); ++i) {
    p[i] = view_.world_to_screen(corners[order[i]].x, corners[order[i]].z);
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  for (std::size_t i = 0; i < p.size(); ++i) {
    const ChartPoint& b = p[(i + 1) % p.size()];
    ui::draw_line(vp, p[i].x, p[i].y, b.x, b.y, kTorpedoBox);
  }
}

void ChartStation::draw_depth_charge(const DepthCharge& dc) {
  // dc_chart_draw (0x24aa1): from 64x, CHART.SHP frame 0x2a turned to its
  // flight heading at zoom / 1050.
  if (!weapons_shown() || view_.zoom_int() < kDepthChargeMinZoom) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(dc.position().x, dc.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), kDepthChargeScale << 16));
  Image8 scratch;
  sprite_draw_transformed(vp, chart_sprites_.frame(kDepthChargeFrame), x, y, scratch, dc.flight_heading(), s, s, 0);
}

void ChartStation::draw_shell(const Shell& shell) {
  // shell_chart_draw (0x24d73): a dot in colour 0x31.
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (weapons_shown() && view_.point_on_screen(shell.position().x, shell.position().z, x, y)) {
    ui::put_pixel(Viewport{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()}, x, y, kShellDot);
  }
}

void ChartStation::draw_splash(const Splash& splash) {
  // splash_chart_draw (0x24dee), pass -1: for its first 7 ticks, CHART.SHP
  // frame 0x2b + ticks at zoom x (size x 328 / 10000).
  if (!weapons_shown() || splash.ticks() >= kSplashTicks) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(splash.position().x, splash.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  const Fixed16 s = Fixed16::from_raw(fx::mul(view_.zoom(), splash.size() * 0x148 / 10000));
  Image8 scratch;
  sprite_draw_transformed(vp, chart_sprites_.frame(kSplashFrame + static_cast<std::size_t>(splash.ticks())), x, y,
                          scratch, Angle10(0), s, s, 0);
}

void ChartStation::draw_debris(const Debris& debris) {
  // debris_chart_draw (0x250b3): a dot in colour 0x31 under CHART.SHP
  // frame 0x32 at zoom / 600.
  if (!weapons_shown()) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(debris.position().x, debris.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  ui::put_pixel(vp, x, y, kShellDot);
  const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), kDebrisScale << 16));
  Image8 scratch;
  sprite_draw_transformed(vp, chart_sprites_.frame(kDebrisFrame), x, y, scratch, Angle10(0), s, s, 0);
}

void ChartStation::draw_plane_icon(std::int32_t x, std::int32_t y, std::size_t frame, Angle10 heading,
                                   std::int32_t scale_div, std::int32_t box_below_zoom) {
  // The aircraft and bomber-group marks: a dot, the SHIPICON.SHP plane
  // turned to the heading at zoom / `scale_div`, and when zoomed in less
  // than `box_below_zoom` a 7-pixel box in colour 0x51 around it.
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  ui::put_pixel(vp, x, y, kSurfacedDot);
  const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), scale_div << 16));
  Image8 scratch;
  if (frame < ship_icons_.frame_count()) {
    sprite_draw_transformed(vp, ship_icons_.frame(frame), x, y, scratch, heading, s, s, 0);
  }
  if (view_.zoom_int() < box_below_zoom) {
    ui::draw_line(vp, x - 3, y - 3, x + 3, y - 3, kAircraftBox);
    ui::draw_line(vp, x - 3, y - 3, x - 3, y + 3, kAircraftBox);
    ui::draw_line(vp, x + 3, y - 3, x + 3, y + 3, kAircraftBox);
    ui::draw_line(vp, x - 3, y + 3, x + 3, y + 3, kAircraftBox);
  }
}

void ChartStation::draw_aircraft(const Aircraft& a) {
  // aircraft_chart_draw (0x240d9): airborne and sighted, its AIRCRAFT.DAT
  // +0x70 icon at zoom / 1700.
  const AircraftType* type = a.setup().type;
  if (!a.airborne() || !a.sighted() || type == nullptr) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(a.position().x, a.position().z, x, y)) {
    return;
  }
  const std::size_t frame = static_cast<std::size_t>(type->raw[kAircraftIconField]) |
                            static_cast<std::size_t>(type->raw[kAircraftIconField + 1]) << 8;
  draw_plane_icon(x, y, frame, a.heading(), 1700, 0x100);
}

void ChartStation::draw_air_group(const AirGroup& g) {
  // airgroup_chart_draw (0x2447b): while spotted, up to 15 planes of the
  // formation (the view's draws from the group's seed, the game's generator
  // then reseeded with the value it drew first), SHIPICON frame 0x1a at
  // zoom / 900.
  if (!g.sighted()) {
    return;
  }
  WatcomRandom& random = env_.mission.sim().random();
  const auto saved = static_cast<std::uint32_t>(random.rand());
  random.srand(g.formation_seed());
  std::int32_t spread = 700;
  std::int32_t left = random.random_int(5) + 5;
  const fx::Vec3 p = g.position();
  for (std::int32_t i = 0; i < g.plane_count() && i < kMaxChartPlanes; ++i) {
    const std::int32_t wx = p.x + (random.random_int(700) + spread) * 16;
    const std::int32_t wz = (random.random_int(700) + spread) * 16 + p.z;
    if (--left == 0) {
      left = random.random_int(5) + 5;
      spread += random.random_int(600) + 400;
    }
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (view_.point_on_screen(wx, wz, x, y)) {
      draw_plane_icon(x, y, kBomberIcon, g.heading(), 900, 0x80);
    }
  }
  random.srand(saved);
}

void ChartStation::draw_pilot(const DownedPilot& pilot) {
  // pilot_chart_draw (0x2537d): the raft once found, SHIPICON frame 0x17
  // at zoom / 1000.
  if (!pilot.sighted()) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(pilot.position().x, pilot.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  ui::put_pixel(vp, x, y, kSurfacedDot);
  const Fixed16 s = Fixed16::from_raw(fx::div(view_.zoom(), 1000 << 16));
  Image8 scratch;
  sprite_draw_transformed(vp, ship_icons_.frame(kRaftIcon), x, y, scratch, pilot.heading(), s, s, 0);
}

void ChartStation::draw_pilot_icon(const DownedPilot& pilot) {
  // pilot_chart_icon (0x2564a): once radioed and while the report lasts,
  // the CHART.SHP marker 0x22 at the last-seen (else reported) position,
  // its colour 0x52 fading from 0xa8 with the hours since the report.
  if (!pilot.radioed() || pilot.report_hours() <= 0) {
    return;
  }
  std::int32_t wx = pilot.reported_x();
  std::int32_t wz = pilot.reported_z();
  if (pilot.last_seen_x() >= 0) {
    wx = pilot.last_seen_x();
    wz = pilot.last_seen_z();
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(wx, wz, x, y)) {
    return;
  }
  ColourRemap remap{};
  for (std::size_t i = 0; i < remap.size(); ++i) {
    remap[i] = static_cast<std::uint8_t>(i);
  }
  const std::int32_t fade = std::min((36 - pilot.report_hours()) * 8 / 36, 8);
  remap[kHeadingLine] = static_cast<std::uint8_t>(0xa8 - fade);
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  draw_sprite_remapped(vp, chart_sprites_.frame(kPilotMarker), x, y, remap);
}

void ChartStation::draw_report(const IntelReport& report) {
  // intel_report_chart_draw (0x252aa) / intel_report_chart_icon
  // (0x25f17): once radioed and active, the CHART.SHP marker 0x25 at the
  // target.
  if (report.pending() || !report.active()) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(report.position().x, report.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  draw_sprite_clipped(vp, chart_sprites_.frame(kReportMarker), x, y);
}

void ChartStation::draw_air_group_icon(const AirGroup& g) {
  // airgroup_chart_icon (0x262ed): in flight (launch delay over) and
  // spotted, the CHART.SHP marker 0x23 at the group.
  if (g.launch_delay_min() != 0 || !g.sighted()) {
    return;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_.point_on_screen(g.position().x, g.position().z, x, y)) {
    return;
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  draw_sprite_clipped(vp, chart_sprites_.frame(kAirGroupMarker), x, y);
}

void ChartStation::draw_course() {
  // chart_draw_course (0x270c1): the legs from the boat to the current
  // waypoint and on, clipped in world units to 1000 pixels around the
  // view, white with a marker at each waypoint.
  const PlayerSub& sub = env_.mission.player();
  const std::int32_t count = waypoint_count();
  const std::int32_t index = sub.route_index() + 1;
  if (count < 2) {
    return;
  }
  const std::array<std::int32_t, 2> hi = view_.screen_to_world(1000, 1000);
  const std::array<std::int32_t, 2> lo = view_.screen_to_world(-1000, -1000);
  const std::int32_t x_hi = hi[0];
  const std::int32_t z_lo = hi[1];
  const std::int32_t x_lo = lo[0];
  const std::int32_t z_hi = lo[1];
  const auto outcode = [&](std::int32_t x, std::int32_t z) {
    unsigned c = 0;
    if (z_hi < z) {
      c |= 1;
    }
    if (z < z_lo) {
      c |= 2;
    }
    if (x_hi < x) {
      c |= 4;
    }
    if (x < x_lo) {
      c |= 8;
    }
    return c;
  };
  struct Leg {
    std::int32_t x0, z0, x1, z1;
  };
  std::vector<Leg> legs;
  std::int32_t current = -1;
  for (std::int32_t i = index; i < count; ++i) {
    const geom::WorldXZ p = waypoint(i);
    geom::WorldXZ q = i == index ? geom::WorldXZ{sub.position().x, sub.position().z} : waypoint(i - 1);
    if (i == index) {
      current = static_cast<std::int32_t>(legs.size());
    }
    std::int32_t px = p.x, pz = p.z, qx = q.x, qz = q.z;
    bool accept = false;
    while (true) {
      const unsigned c1 = outcode(px, pz);
      const unsigned c2 = outcode(qx, qz);
      if (c1 == 0 && c2 == 0) {
        accept = true;
        break;
      }
      if ((c1 & c2) != 0) {
        break;
      }
      const unsigned c = c2 != 0 ? c2 : c1;
      const std::int64_t dx = static_cast<std::int64_t>(px) - qx;
      const std::int64_t dz = static_cast<std::int64_t>(pz) - qz;
      std::int32_t x = 0;
      std::int32_t z = 0;
      if ((c & 1) != 0) {
        z = z_hi;
        x = qx + static_cast<std::int32_t>(dx * (z_hi - qz) / dz);
      } else if ((c & 2) != 0) {
        z = z_lo;
        x = qx + static_cast<std::int32_t>(dx * (z_lo - qz) / dz);
      } else if ((c & 4) != 0) {
        x = x_hi;
        z = qz + static_cast<std::int32_t>(dz * (x_hi - qx) / dx);
      } else {
        x = x_lo;
        z = qz + static_cast<std::int32_t>(dz * (x_lo - qx) / dx);
      }
      if (c == c2) {
        qx = x;
        qz = z;
      } else {
        px = x;
        pz = z;
      }
    }
    if (accept) {
      legs.push_back({qx, qz, px, pz});
    } else if (i == index) {
      current = -1;
    }
  }
  const Viewport vp{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  const auto marker = [&](std::int32_t x, std::int32_t y) {
    ui::put_pixel(vp, x, y, kCourseColour);
    ui::put_pixel(vp, x - 1, y, 0);
    ui::put_pixel(vp, x + 1, y, 0);
    ui::draw_line(vp, x - 1, y - 1, x + 1, y - 1, 0);
    ui::draw_line(vp, x - 1, y + 1, x + 1, y + 1, 0);
  };
  for (std::size_t s = 0; s < legs.size(); ++s) {
    std::int32_t ax = 0, ay = 0, bx = 0, by = 0;
    view_.point_on_screen(legs[s].x0, legs[s].z0, ax, ay);
    view_.point_on_screen(legs[s].x1, legs[s].z1, bx, by);
    ui::draw_line(vp, ax, ay, bx, by, kCourseColour);
    if (static_cast<std::int32_t>(s) != current) {
      marker(ax, ay);
    }
    marker(bx, by);
  }
}

// --- The pointer -------------------------------------------------------------

void ChartStation::mouse_moved(std::int32_t x, std::int32_t y) {
  // chart_on_mouse_move (0x21377): the hover info, the rubber band to the
  // next waypoint while plotting, and the zoom box hint on the bar.
  if (click_mode_ == ChartClickMode::kSelectTarget) {
    hover_info(x, y);
  } else {
    set_cursor(kPlotCursor);
  }
  if (click_mode_ == ChartClickMode::kPlot && !zoom_box_.is_open()) {
    const PlayerSub& sub = env_.mission.player();
    const std::int32_t count = waypoint_count();
    geom::WorldXZ from{sub.position().x, sub.position().z};
    if (count != 0 && count > sub.route_index() + 1) {
      from = waypoint(count - 1);
    }
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (view_.point_on_screen(from.x, from.z, sx, sy)) {
      const std::array<std::int32_t, 2> w = view_.screen_to_world(x, y);
      const std::int32_t draft = sub.sub().draft_ft;
      const std::int32_t height = env_.mission.floor().height_at(w[0], w[1]);
      bool valid = !(draft * -16 == height || -height < draft * 16);
      if (valid && land_on_line(grid_, from, {w[0], w[1]}).land) {
        valid = false;
      }
      course_line_erase();
      if (valid) {
        course_line_draw(sx, sy, x, y);
      }
    }
  }
  const Viewport bar{&env_.screen(), 0, 0x1cb, 0x27f, 0x1df};
  if (zoom_box_.is_open() && !zoom_message_) {
    ui::fill_rect(bar, 0, 0, bar.x1 - bar.x0, bar.y1 - bar.y0, 0);
    const GlyphColourTable table = gui_colour_table();
    gui_draw_text(bar, 4, 7, env_.assets.msg, env_.exe.c_string(text::kZoomKeys), &table);
    zoom_message_ = true;
  }
  if (!zoom_box_.is_open() && zoom_message_) {
    ui::fill_rect(bar, 0, 0, bar.x1 - bar.x0, bar.y1 - bar.y0, 0);
    zoom_message_ = false;
  }
}

void ChartStation::course_line_draw(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
  course_line_.draw(Viewport{&env_.screen(), view_.x0(), view_.y0(), view_.x1(), view_.y1()}, x0, y0, x1, y1);
}

void ChartStation::course_line_erase() { course_line_.erase(); }

void ChartStation::set_cursor(ui::CursorId id) { env_.set_cursor(static_cast<std::size_t>(id)); }

// --- The info popup ----------------------------------------------------------

void ChartStation::hover_info(std::int32_t x, std::int32_t y) {
  // chart_hover_info (0x2158e): the boat, zoomed out the contact groups,
  // then the vehicles and ports under the pointer. A new object closes the
  // popup first, so that it opens again sized and placed for it.
  if (panels_.hovered() || zoom_box_.is_open()) {
    return;
  }
  const bool tactical = view_.zoom_int() > 7;
  std::vector<std::string> lines;
  const void* object = nullptr;
  if (std::optional<std::vector<std::string>> own = own_sub_info(x, y)) {
    object = &env_.mission.player();
    lines = std::move(*own);
  } else if (!tactical) {
    object = pick_group(x, y, lines);
  }
  if (object == nullptr) {
    object = pick_vehicle_or_port(tactical, x, y, lines);
  }
  if (object == nullptr) {
    close_popup();
  } else {
    if (object != popup_object_) {
      close_popup();
    }
    // chart_show_info_popup (0x21a0c): off the open panel.
    popup_.show(x, y, chart_popup_lines(lines), panels_.is_open() ? panels_.active_rect() : std::nullopt);
  }
  popup_object_ = object;
}

ui::Rect ChartStation::frame_box(std::size_t frame, std::int32_t x, std::int32_t y) const {
  // sprite_frame_bbox (0xac13d): {0, 0, 0, 0} for a frame with no pixel.
  const SpriteFrame& f = chart_sprites_.frame(frame);
  if (f.empty()) {
    return {0, 0, 0, 0};
  }
  const DecodedFrame d = f.decode();
  int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
  for (int py = 0; py < d.opaque.height(); ++py) {
    for (int px = 0; px < d.opaque.width(); ++px) {
      if (d.opaque.at(px, py) != 0) {
        x0 = std::min(x0, px);
        x1 = std::max(x1, px + 1);
        y0 = std::min(y0, py);
        y1 = std::max(y1, py);
      }
    }
  }
  if (x1 == INT32_MIN) {
    return {0, 0, 0, 0};
  }
  return {x + f.x0 + x0, y + f.y0 + y0, x + f.x0 + x1, y + f.y0 + y1};
}

ui::Rect ChartStation::hull_box(const Vehicle& v) const {
  ui::Rect r{INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
  const auto& corners = v.collision().box.corners;
  for (const std::size_t i : {0u, 1u, 4u, 5u}) {
    const ChartPoint p = view_.world_to_screen(corners[i].x, corners[i].z);
    r.x0 = std::min(r.x0, p.x);
    r.y0 = std::min(r.y0, p.y);
    r.x1 = std::max(r.x1, p.x);
    r.y1 = std::max(r.y1, p.y);
  }
  return {r.x0 - 4, r.y0 - 4, r.x1 + 4, r.y1 + 4};
}

const ContactGroup* ChartStation::pick_group(std::int32_t x, std::int32_t y, std::vector<std::string>& lines) {
  for (ContactGroup* g : chart_groups()) {
    // The kinds' slot +0x20: the symbol's box once drawn.
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (!g->report.valid || g->report.symbol_frame == 0 || !view_.point_on_screen(g->report.x, g->report.z, sx, sy)) {
      continue;
    }
    const ui::Rect r = frame_box(static_cast<std::size_t>(g->report.symbol_frame), sx, sy);
    if (r.x0 <= x && x <= r.x1 && r.y0 <= y && y <= r.y1) {
      lines = group_report_lines(*g, env_.exe, env_.mission.world());
      return g;
    }
  }
  return nullptr;
}

SolutionEstimate ChartStation::estimate(const Vehicle& v, std::int32_t range_yd, std::int32_t track_time) const {
  TargetSensing sensing;
  sensing.track_time = Fixed16::from_raw(track_time);
  const PlayerSub& sub = env_.mission.player();
  const PlayerCombat& combat = env_.mission.player_combat();
  const OwnBoat own{sub.motion(), static_cast<std::int32_t>(sub.sub().length_ft),
                    env_.mission.sim().conditions().options, combat.crew};
  return tdc_estimate_solution(target_state(v, sensing), range_yd, 0, 0, own);
}

const void* ChartStation::pick_vehicle_or_port(bool tactical, std::int32_t x, std::int32_t y,
                                               std::vector<std::string>& lines) {
  const std::uint32_t options = env_.mission.sim().conditions().options;
  const bool limited = (options & option::kLimitedVisibility) != 0;
  const auto inside = [&](const ui::Rect& r) { return r.x0 <= x && x <= r.x1 && r.y0 <= y && y <= r.y1; };
  const auto at = [&](std::int32_t wx, std::int32_t wz, std::size_t frame) -> std::optional<ui::Rect> {
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (!view_.point_on_screen(wx, wz, sx, sy)) {
      return std::nullopt;
    }
    return frame_box(frame, sx, sy);
  };
  // The hull contacts' slot +0x44 (ship_chart_rect 0x22b8a,
  // enemy_sub_chart_rect 0x2394e), zoomed in only.
  const auto hull_rect = [&](const Vehicle& v, const ShipSightingRecord& record, bool seen, bool true_position,
                             bool sinking) -> std::optional<ui::Rect> {
    if (seen && !sinking) {
      return hull_box(v);
    }
    std::int32_t wx = v.position().x;
    std::int32_t wz = v.position().z;
    if (!true_position) {
      if (record.last_x < 0) {
        return std::nullopt;
      }
      wx = record.last_x;
      wz = record.last_z;
    }
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (!view_.point_on_screen(wx, wz, sx, sy)) {
      return std::nullopt;
    }
    if (!sinking) {
      return frame_box(kLastKnown, sx, sy);
    }
    // The sinking mark's size (not half of it) scaled by zoom / 200.
    const SpriteFrame& f = chart_sprites_.frame(kSinking);
    const std::int32_t s = fx::div(view_.zoom(), 200 << 16);
    const std::int32_t hw = (f.width() * s + 0x8000) >> 16;
    const std::int32_t hh = (f.height() * s + 0x8000) >> 16;
    return ui::Rect{sx - hw, sy - hh, sx + hw, sy + hh};
  };
  for (const auto& v : env_.mission.sim().vehicles()) {
    if (v->removed()) {
      continue;
    }
    if (const auto* ship = dynamic_cast<const Ship*>(v.get()); ship != nullptr && tactical) {
      const ShipSightingRecord& record = ship->ai().sighting;
      const bool seen = ship->sighted() || !limited;
      const bool sinking = limited ? ship->sinking() && record.sighted : ship->sinking();
      const std::optional<ui::Rect> r = hull_rect(*ship, record, seen, !limited, sinking);
      if (r && inside(*r)) {
        HullContact c{ship->setup().ship_class, false, seen, sinking, record.hour, record.minute,
                      estimate(*ship, ship->ai().player_range_yd, record.track_time), options};
        lines = hull_contact_lines(c, env_.exe);
        return ship;
      }
    } else if (const auto* sub = dynamic_cast<const EnemySub*>(v.get()); sub != nullptr && tactical) {
      const ShipSightingRecord& record = sub->sighting();
      const bool visible = !limited && sub->visual_height() > 0;
      const bool seen = sub->sighted() || visible;
      const bool sinking = limited ? sub->sinking() && record.sighted : sub->sinking();
      const std::optional<ui::Rect> r = hull_rect(*sub, record, seen, visible, sinking);
      if (r && inside(*r)) {
        HullContact c{sub->setup().ship_class, true, seen, sinking, record.hour, record.minute,
                      estimate(*sub, sub->player_range_yd(), record.track_time), options};
        lines = hull_contact_lines(c, env_.exe);
        return sub;
      }
    } else if (const auto* pilot = dynamic_cast<const DownedPilot*>(v.get())) {
      // pilot_chart_rect (0x2593f) zoomed in: the raft once found;
      // chart_downed_pilot_report passes the address of its "Downed pilot"
      // string as the sighting range. Zoomed out: the marker at the radioed
      // position while the report lasts (the original's rect is
      // uninitialized once it has run out: none here).
      if (tactical) {
        if (pilot->sighted() && inside(hull_box(*pilot))) {
          lines = pilot_contact_lines(estimate(*pilot, kPilotSightingRange, pilot->track_time()), env_.exe);
          return pilot;
        }
      } else if (pilot->report_hours() > 0) {
        const std::optional<ui::Rect> r = at(pilot->reported_x(), pilot->reported_z(), kPilotMarker);
        if (r && inside(*r)) {
          lines = pilot_report_lines(*pilot, env_.exe, env_.mission.world());
          return pilot;
        }
      }
    } else if (const auto* group = dynamic_cast<const AirGroup*>(v.get()); group != nullptr && !tactical) {
      // airgroup_chart_rect (0x2659c): zoomed out, spotted.
      if (group->sighted()) {
        const std::optional<ui::Rect> r = at(group->position().x, group->position().z, kAirGroupMarker);
        if (r && inside(*r)) {
          lines = air_group_report_lines(*group, env_.exe, env_.mission.world());
          return group;
        }
      }
    } else if (const auto* report = dynamic_cast<const IntelReport*>(v.get())) {
      // intel_report_chart_rect (0x25fbb), at any zoom.
      if (!report->pending() && report->active()) {
        const std::optional<ui::Rect> r = at(report->position().x, report->position().z, kReportMarker);
        if (r && inside(*r)) {
          const auto& bases = env_.mission.bases();
          const auto b = static_cast<std::size_t>(report->base());
          lines = intel_report_lines(*report, b < bases.size() ? bases[b].name : std::string(), env_.exe,
                                     env_.mission.world());
          return report;
        }
      }
    }
  }
  if (tactical) {
    return nullptr;
  }
  // chart_port_rect (0x2208e): 4 x zoom pixels around the port's station.
  const auto& bases = env_.mission.bases();
  for (std::size_t i = 0; i < bases.size(); ++i) {
    std::int32_t sx = 0;
    std::int32_t sy = 0;
    if (!view_.point_on_screen(bases[i].station.x, bases[i].station.z, sx, sy)) {
      continue;
    }
    const std::int32_t half = static_cast<std::int32_t>((static_cast<std::uint32_t>(view_.zoom()) * 4 + 0x8000) >> 16);
    if (inside({sx - half, sy - half, sx + half, sy + half})) {
      // The owner table 0x116248 as bases_update_owners keeps it.
      ZoneData zone;
      zone.bases = bases;
      zone.owners.resize(bases.size());
      const GameClock& clock = env_.mission.sim().clock();
      zone.update_owners(clock.day(), clock.month(), clock.year());
      lines = port_info_lines(bases[i], zone.owners[i].side, env_.exe, env_.mission.world());
      return &bases[i];
    }
  }
  return nullptr;
}

std::optional<std::vector<std::string>> ChartStation::own_sub_info(std::int32_t x, std::int32_t y) const {
  const PlayerSub& sub = env_.mission.player();
  std::int32_t sx = 0;
  std::int32_t sy = 0;
  if (!view_.point_on_screen(sub.position().x, sub.position().z, sx, sy)) {
    return std::nullopt;
  }
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t x1 = 0;
  std::int32_t y1 = 0;
  if (view_.zoom_int() > 7) {
    // The hull box's corners 0, 1, 4 and 5, 4 pixels larger.
    const auto& corners = sub.collision().box.corners;
    bool first = true;
    for (const std::size_t i : {0u, 1u, 4u, 5u}) {
      const ChartPoint p = view_.world_to_screen(corners[i].x, corners[i].z);
      x0 = first ? p.x : std::min(x0, p.x);
      y0 = first ? p.y : std::min(y0, p.y);
      x1 = first ? p.x : std::max(x1, p.x);
      y1 = first ? p.y : std::max(y1, p.y);
      first = false;
    }
    x0 -= 4;
    y0 -= 4;
    x1 += 4;
    y1 += 4;
  } else {
    // The marker's box (`sprite_frame_bbox`).
    const SpriteFrame& f = chart_sprites_.frame(kOwnSub);
    x0 = sx + f.x0;
    y0 = sy + f.y0;
    x1 = sx + f.x1;
    y1 = sy + f.y1;
  }
  if (x < x0 || x > x1 || y < y0 || y > y1) {
    return std::nullopt;
  }
  // chart_own_sub_info (0x29eac)
  std::vector<std::string> lines;
  lines.push_back(env_.mission.player_name());
  const auto str = [this](std::uint32_t address) { return std::string(env_.exe.c_string(address)); };
  std::string loc = str(text::kLoc);
  const World& world = env_.mission.world();
  if (world.bounds) {
    loc += chart_format_latlon(world.latitude_at(sub.position().z)) + ", " +
           chart_format_latlon(world.longitude_at(sub.position().x));
  }
  lines.push_back(loc);
  std::string heading = std::to_string(sub.heading().tenths() / 10);
  heading.insert(0, heading.size() < 3 ? 3 - heading.size() : 0, '0');
  lines.push_back(str(text::kHeading) + heading);
  const std::int32_t tenths = fx::trunc(sub.motion().speed.raw() * 10);
  std::string whole = std::to_string(tenths / 10);
  whole.insert(0, whole.size() < 2 ? 2 - whole.size() : 0, '0');
  lines.push_back(str(text::kSpeed) + whole + "." + std::to_string(tenths - tenths / 10 * 10) + str(text::kKts));
  lines.push_back(str(text::kDepth) + std::to_string(sub.depth_ft()) + str(text::kFeet));
  return lines;
}

void ChartStation::close_popup() {
  if (popup_.close()) {
    force_overlays_ = true;
  }
}

// --- The zoom box --------------------------------------------------------------

void ChartStation::zoom_box_grow() {
  // chart_zoom_box_grow (0x2bdc7): doubles the box, up to 64 times the
  // view's zoom and below 2048x.
  if (!panels_.locked()) {
    zoom_box_.grow(view_, kChartMaxZoom);
    zoom_hold_ = zoom_hold_ || zoom_box_.is_open();
  }
}

void ChartStation::zoom_box_shrink() {
  // chart_zoom_box_shrink (0x2be2c): halves the box, or without one the
  // zoom.
  if (panels_.locked()) {
    return;
  }
  if (!zoom_box_.is_open()) {
    dirty_ = zoom_box_.shrink(view_);
  } else {
    zoom_box_.shrink(view_);
  }
  zoom_hold_ = zoom_hold_ || zoom_box_.is_open();
}

// --- Commands ----------------------------------------------------------------

void ChartStation::center_button() {
  // chart_button_center (0x21863)
  if (env_.options.chart_follow_sub == 0) {
    follow_sub();
  } else {
    toggle_follow();
  }
}

void ChartStation::toggle_follow() {
  // chart_toggle_follow_sub (0x21965): the centre button shows frame 0x18
  // while following.
  env_.options.chart_follow_sub = env_.options.chart_follow_sub != 0 ? 0 : kOptionOn;
  if (panel_dialog_) {
    panel_dialog_->invalidate();
  }
}

void ChartStation::start_plotting() {
  // chart_button_plot_course (0x21882)
  saved_click_mode_ = click_mode_;
  click_mode_ = ChartClickMode::kPlot;
  set_cursor(kPlotCursor);
  panels_.request(static_cast<int>(ChartPanel::kPlot));
  env_.bar.set_escape_passthrough(true);
  compact_waypoints();
}

void ChartStation::exit_plotting() {
  // chart_button_exit_plot (0x21c6f)
  panels_.request(static_cast<int>(ChartPanel::kMap));
  if (waypoint_count() == 0) {
    env_.mission.player().stop_following_route();
  }
  std::swap(click_mode_, saved_click_mode_);
  env_.bar.set_escape_passthrough(false);
}

void ChartStation::clear_course() {
  // chart_button_clear_course (0x21caa)
  PlayerSub& sub = env_.mission.player();
  sub.follow_route(Route{});
  sub.stop_following_route();
  waypoint_shown_ = 1;
  if (prompt_control_ != nullptr) {
    prompt_control_->dirty = true;
  }
  dirty_ = true;
}

std::int32_t ChartStation::waypoint_count() const {
  // The original's arrays hold the start point first (0x11ab2c[0]): the
  // boat's route here holds the waypoints after it.
  const std::int32_t count = env_.mission.player().route().count;
  return count == 0 ? 0 : count + 1;
}

geom::WorldXZ ChartStation::waypoint(std::int32_t i) const {
  const Route& r = env_.mission.player().route();
  const auto k = static_cast<std::size_t>(i - 1);
  return {r.x.at(k), r.z.at(k)};
}

void ChartStation::compact_waypoints() {
  // chart_compact_waypoints (0x218c1): drops the waypoints already reached.
  PlayerSub& sub = env_.mission.player();
  const Route& r = sub.route();
  const std::int32_t index = sub.route_index();
  if (r.count > 0 && index > 0) {
    Route kept;
    for (std::int32_t i = index; i < r.count; ++i) {
      kept.x[static_cast<std::size_t>(kept.count)] = r.x[static_cast<std::size_t>(i)];
      kept.z[static_cast<std::size_t>(kept.count)] = r.z[static_cast<std::size_t>(i)];
      ++kept.count;
    }
    const bool following = sub.following_route();
    sub.follow_route(kept);
    if (!following) {
      sub.stop_following_route();
    }
  }
  const std::int32_t count = waypoint_count();
  waypoint_shown_ = count > 0x18 ? count - 1 : count;
  if (count == 0) {
    waypoint_shown_ = 1;
  }
  if (prompt_control_ != nullptr) {
    prompt_control_->dirty = true;
  }
}

bool ChartStation::add_waypoint(std::int32_t x, std::int32_t y) {
  // chart_add_waypoint (0x211b1)
  PlayerSub& sub = env_.mission.player();
  const std::array<std::int32_t, 2> w = view_.screen_to_world(x, y);
  if (env_.mission.floor().height_at(w[0], w[1]) > sub.sub().draft_ft * -2) {
    return false;
  }
  Route route = sub.route();
  if (route.count == 0x17) {
    // The 24th point replaces the last one.
    --route.count;
  }
  const geom::WorldXZ from =
      route.count > 0 ? waypoint(route.count) : geom::WorldXZ{sub.position().x, sub.position().z};
  if (land_on_line(grid_, from, {w[0], w[1]}).land) {
    return false;
  }
  route.x[static_cast<std::size_t>(route.count)] = w[0];
  route.z[static_cast<std::size_t>(route.count)] = w[1];
  ++route.count;
  sub.follow_route(route);
  const std::int32_t count = waypoint_count();
  waypoint_shown_ = count > 0x18 ? count - 1 : count;
  if (prompt_control_ != nullptr) {
    prompt_control_->dirty = true;
  }
  force_overlays_ = true;
  return true;
}

void ChartStation::click(std::int32_t x, std::int32_t y) {
  // chart_on_click (0x210f0)
  switch (click_mode_) {
    case ChartClickMode::kSelectTarget:
      select_target(x, y);
      break;
    case ChartClickMode::kPlot:
      add_waypoint(x, y);
      break;
    case ChartClickMode::kAnnotate:
      tools_click(x, y);
      break;
    case ChartClickMode::kMoveSub: {
      const std::array<std::int32_t, 2> w = view_.screen_to_world(x, y);
      MotionState& m = env_.mission.player().motion();
      m.position.x = w[0];
      m.position.z = w[1];
      m.pos_x = static_cast<std::int64_t>(w[0]) << 16;
      m.pos_z = static_cast<std::int64_t>(w[1]) << 16;
      break;
    }
  }
}

void ChartStation::select_target(std::int32_t x, std::int32_t y) {
  // tdc_select_target_at_chart_xy (0x7c33b): the nearest visible surface
  // target (slot +0x54) within zoom / 8 pixels, not hidden by land.
  const std::int32_t half = std::max(view_.zoom_int() >> 3, 1);
  const bool limited = (env_.mission.sim().conditions().options & option::kLimitedVisibility) != 0;
  const fx::Vec3 sub = env_.mission.player().position();
  Vehicle* best = nullptr;
  std::uint32_t best_distance = 0;
  for (const std::unique_ptr<Vehicle>& v : env_.mission.sim().vehicles()) {
    if (!v->is_surface_target() || v->removed() || v->sinking() || (limited && !v->sighted())) {
      continue;
    }
    const fx::Vec3 p = v->position();
    if (land_on_line(grid_, {sub.x, sub.z}, {p.x, p.z}).land) {
      continue;
    }
    const ChartPoint c = view_.world_to_screen(p.x, p.z);
    if (c.x - half <= x && x <= c.x + half && c.y - half <= y && y <= c.y + half) {
      const std::uint32_t d = fx::approx_distance(x - c.x, y - c.y);
      if (best == nullptr || d < best_distance) {
        best = v.get();
        best_distance = d;
      }
    }
  }
  if (best != nullptr) {
    env_.mission.player_combat().tdc.lock_target(best);
  }
}

// --- The panels ----------------------------------------------------------------

void ChartStation::suspend_panels() {
  suspended_open_ = panels_.is_open();
  panels_.close_panel();
  close_popup();
}

void ChartStation::resume_panels() {
  if (suspended_open_ && env_.mission.end_code() == 0) {
    panels_.open_panel();
  }
}

void ChartStation::panel_hover(bool over) {
  // chart_on_panel_hover (0x212cf)
  if (over) {
    close_popup();
    course_line_erase();
    set_cursor(kDefaultCursor);
  }
}

void ChartStation::open_map_panel() {
  // chart_view_open_panel (0x2af33) with the MAP CONTROLS open callback.
  dirty_ = true;
  compression_control_ = nullptr;
  clock_control_ = nullptr;
  prompt_control_ = nullptr;
  panel_dialog_ = make_map_panel();
  panel_dialog_->open();
}

void ChartStation::open_plot_panel() {
  dirty_ = true;
  compression_control_ = nullptr;
  clock_control_ = nullptr;
  prompt_control_ = nullptr;
  panel_dialog_ = make_plot_panel();
  panel_dialog_->open();
}

void ChartStation::open_nav_panel() {
  // chart_nav_panel_open (0x20d7d): the shared nav panel in the chart's
  // frame (dialog 0x117fe4).
  dirty_ = true;
  compression_control_ = nullptr;
  clock_control_ = nullptr;
  prompt_control_ = nullptr;
  NavPanelFrame frame;
  frame.x = nav_pos_.x;
  frame.y = nav_pos_.y;
  frame.w = kNavPanelW;
  frame.h = kNavPanelH;
  frame.background = &nav_background_;
  frame.add_controls = [this](ui::Dialog& d) {
    d.emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
      // chart_draw_nav_panel_frame (0x216f9)
      ui::gui_fill_rect(gfx, 0x69, 0x145, 0x92, 0x17, 0);
      draw_sprite_clipped(gfx, chart_sprites_.frame(kNavPicture), 8, 0x28);
      draw_sprite_clipped(gfx, env_.assets.global.frame(kClockBox), 0xe, 0x148);
    });
    d.emplace<ui::TitleBar>(std::string(env_.exe.c_string(text::kNavigation)));
    d.emplace<ui::Line>(7, 0x13e, 0x104, 0x13e, 3);
    d.emplace<ui::Line>(7, 0x13f, 0x104, 0x13f, 3);
    d.emplace<ui::Line>(7, 0x140, 0x104, 0x140, 2);
    d.emplace<ui::Line>(7, 0x141, 0x104, 0x141, 2);
    d.emplace<SpriteButton>(*this, ui::Rect{0xb3, 0x146, 0xfa, 0x15a}, env_.assets.global,
                            [] { return kConnButton; }, kConnButton + 1,
                            [this] { env_.bar.select_station(StationId::kConn); });
    d.emplace<SpriteButton>(*this, ui::Rect{0x6a, 0x146, 0xb1, 0x15a}, env_.assets.global,
                            [] { return kMapControlsButton; }, kMapControlsButton + 1,
                            [this] { panels_.request(static_cast<int>(ChartPanel::kMap)); });
    auto& clock = d.emplace<ui::TextField>(0x10, 0x14a, 0x51, 0x158,
                                           [this] { return clock_text(env_.mission.sim().clock()); },
                                           ui::FontSlot::kCustom, TextAlign::kLeft);
    clock.use_colours = false;
    clock.fill = 0;
    clock.custom = &env_.assets.digits2;
    clock_control_ = &clock;
    d.emplace<ui::GroupBox>(0xc, 0x146, 0x54, 0x15b);
  };
  nav_panel_ = make_nav_panel(env_, NavPanelSite::kChart, std::move(frame));
  nav_panel_->open();
}

void ChartStation::close_panel_dialogs() {
  // chart_view_close_panel (0x2af5d) with the panels' close callback.
  if (nav_panel_) {
    nav_panel_->close();
    nav_panel_.reset();
  }
  if (panel_dialog_) {
    panel_dialog_->close();
    panel_dialog_.reset();
  }
  compression_control_ = nullptr;
  clock_control_ = nullptr;
  prompt_control_ = nullptr;
  force_overlays_ = true;
}

void ChartStation::draw_scale_bar(const Viewport& gfx, int y) const {
  if (env_.ui.features.chart_tools) {
    draw_chart_scale_bar(gfx, 0x12, y, chart_nautical_scale(view_.zoom_int()).length);
  } else {
    draw_sprite_clipped(gfx, chart_sprites_.frame(kScaleBar), 0x12, y);
  }
}

void ChartStation::add_scale_controls(ui::Dialog& d, int line_y, int text_y) {
  // The scale bar: two dark and two light lines, "0", the value and unit.
  d.emplace<ui::Line>(7, line_y, 0x120, line_y, 3);
  d.emplace<ui::Line>(7, line_y + 1, 0x120, line_y + 1, 3);
  d.emplace<ui::Line>(7, line_y + 2, 0x120, line_y + 2, 2);
  d.emplace<ui::Line>(7, line_y + 3, 0x120, line_y + 3, 2);
  const Font* font1 = &env_.assets.font1;
  d.emplace<ui::Label>(0x10, text_y, "0", ui::FontSlot::kCustom, false, font1);
  auto& value = d.emplace<ui::TextField>(0x104, text_y, 0x120, text_y + 10,
                                         [this] {
                                           if (env_.ui.features.chart_tools) {
                                             return std::to_string(chart_nautical_scale(view_.zoom_int()).value);
                                           }
                                           return chart_scale_value(view_.zoom_int());
                                         },
                                         ui::FontSlot::kCustom, TextAlign::kCentre);
  value.use_colours = false;
  value.fill = 6;
  value.custom = font1;
  auto& legend = d.emplace<ui::TextField>(0x3f, text_y, 0xe5, text_y + 10,
                                          [this] {
                                            if (env_.ui.features.chart_tools) {
                                              // Nautical miles (not in the original).
                                              return chart_nautical_scale(view_.zoom_int()).yards
                                                         ? std::string(env_.exe.c_string(text::kTenYards))
                                                         : std::string("NAUTICAL MILES");
                                            }
                                            return std::string(chart_scale_legend(view_.zoom_int()));
                                          },
                                          ui::FontSlot::kCustom, TextAlign::kCentre);
  legend.use_colours = false;
  legend.fill = 6;
  legend.custom = font1;
}

std::unique_ptr<ui::Dialog> ChartStation::make_map_panel() {
  // MAP CONTROLS (dialog 0x117b68, built by 0x1e47c).
  auto d = std::make_unique<ui::Dialog>(env_.gui, panel_pos_.x, panel_pos_.y, kMapPanelW, kMapPanelH, ui::kDialogFramed,
                                        &env_.screen());
  d->emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    // chart_draw_map_panel_frame (0x21747)
    ui::gui_fill_rect(gfx, 0xba, 0x2a, 0x5a, 0x17, 0);
    ui::gui_fill_rect(gfx, 0xd, 0x44, 0x110, 0x17, 0);
    draw_scale_bar(gfx, 0x6f);
    draw_sprite_clipped(gfx, env_.assets.global.frame(kClockBox), 0x1c, 0x2c);
  });
  d->emplace<ui::TitleBar>(std::string(env_.exe.c_string(text::kMapControls)));
  add_scale_controls(*d, 0x5e, 99);
  compression_control_ = &d->emplace<SpriteButton>(
      *this, ui::Rect{0x42, 0x45, 0x89, 0x59}, env_.assets.global, [] { return kCompressionButton; },
      kCompressionButton + 1, [this] { env_.bar.menu_time_compression(); }, true);
  d->emplace<SpriteButton>(*this, ui::Rect{0xd4, 0x45, 0x11b, 0x59}, env_.assets.global, [] { return kConnButton; },
                           kConnButton + 1, [this] { env_.bar.select_station(StationId::kConn); });
  d->emplace<SpriteButton>(*this, ui::Rect{0xbb, 0x2b, 0xd0, 0x3f}, chart_sprites_, [] { return kZoomIn; },
                           kZoomIn + 1, [this] { zoom_box_grow(); });
  d->emplace<SpriteButton>(*this, ui::Rect{0xd2, 0x2b, 0xe7, 0x3f}, chart_sprites_, [] { return kZoomOut; },
                           kZoomOut + 1, [this] { zoom_box_shrink(); });
  d->emplace<SpriteButton>(
      *this, ui::Rect{0xe9, 0x2b, 0x107, 0x3f}, chart_sprites_,
      [this] { return env_.options.chart_follow_sub != 0 ? kCentreFollow : kCentre; }, kCentreFollow,
      [this] { center_button(); });
  d->emplace<FollowButton>(*this, ui::Rect{0x108, 0x2b, 0x112, 0x3f});
  d->emplace<ui::GroupBox>(0x1a, 0x2a, 0x62, 0x3f);
  auto& clock = d->emplace<ui::TextField>(0x1e, 0x2e, 0x5f, 0x3c,
                                          [this] { return clock_text(env_.mission.sim().clock()); },
                                          ui::FontSlot::kCustom, TextAlign::kLeft);
  clock.use_colours = false;
  clock.fill = 0;
  clock.custom = &env_.assets.digits2;
  clock_control_ = &clock;
  d->emplace<SpriteButton>(*this, ui::Rect{0x8b, 0x45, 0xd2, 0x59}, env_.assets.global,
                           [] { return kNavigationButton; }, kNavigationButton + 1,
                           [this] { panels_.request(static_cast<int>(ChartPanel::kNavigation)); });
  d->emplace<SpriteButton>(*this, ui::Rect{0xe, 0x45, 0x41, 0x59}, chart_sprites_, [] { return kPlot; }, kPlot + 1,
                           [this] { start_plotting(); });
  if (env_.ui.features.chart_tools) {
    add_tools_button(*d);
  }
  d->emplace<ui::Area>(-1, -1, -1, -1,
                       [this](ui::Dialog&, ui::Area& a, ui::Msg msg, int, const ui::Event&) {
                         // chart_cheat_toggle_move_sub (0x21d95), /SS305 only.
                         if (msg == ui::Msg::kPress && !a.pressed && env_.ui.debug) {
                           if (click_mode_ == ChartClickMode::kMoveSub) {
                             set_cursor(kDefaultCursor);
                             click_mode_ = cheat_saved_mode_;
                           } else {
                             set_cursor(kPlotCursor);
                             cheat_saved_mode_ = click_mode_;
                             click_mode_ = ChartClickMode::kMoveSub;
                           }
                         }
                         return 0;
                       },
                       nullptr, 0x1400);
  return d;
}

std::unique_ptr<ui::Dialog> ChartStation::make_plot_panel() {
  // The waypoint plotting panel (dialog 0x118278).
  auto d = std::make_unique<ui::Dialog>(env_.gui, panel_pos_.x, panel_pos_.y, kMapPanelW, kPlotPanelH, ui::kDialogFramed,
                                        &env_.screen());
  d->emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    // chart_draw_plot_panel_frame (0x21c41)
    ui::gui_fill_rect(gfx, 0xba, 0x2a, 0x5a, 0x17, 0);
    draw_scale_bar(gfx, 0x5b);
  });
  d->emplace<ui::TitleBar>(std::string(env_.exe.c_string(text::kPlotMapControls)));
  add_scale_controls(*d, 0x4a, 0x4f);
  d->emplace<ui::Line>(7, 0x65, 0x120, 0x65, 3);
  d->emplace<ui::Line>(7, 0x66, 0x120, 0x66, 3);
  d->emplace<ui::Line>(7, 0x67, 0x120, 0x67, 2);
  d->emplace<ui::Line>(7, 0x68, 0x120, 0x68, 2);
  auto& prompt = d->emplace<ui::TextField>(
      8, 0x6e, 0x11f, 0x78,
      [this] {
        // chart_format_waypoint_prompt (0x21ce1)
        std::string prompt;
        for (const char c : env_.exe.string_pointer(text::kWaypointPrompts)) {
          prompt += c == '\x01' ? std::to_string(waypoint_shown_) : std::string(1, c);
        }
        return prompt;
      },
      ui::FontSlot::kCustom, TextAlign::kCentre);
  prompt.use_colours = true;
  prompt.fill = 6;
  prompt.custom = &env_.assets.font1;
  prompt_control_ = &prompt;
  d->emplace<SpriteButton>(*this, ui::Rect{0xbb, 0x2b, 0xd0, 0x3f}, chart_sprites_, [] { return kZoomIn; },
                           kZoomIn + 1, [this] { zoom_box_grow(); });
  d->emplace<SpriteButton>(*this, ui::Rect{0xd2, 0x2b, 0xe7, 0x3f}, chart_sprites_, [] { return kZoomOut; },
                           kZoomOut + 1, [this] { zoom_box_shrink(); });
  d->emplace<SpriteButton>(
      *this, ui::Rect{0xe9, 0x2b, 0x107, 0x3f}, chart_sprites_,
      [this] { return env_.options.chart_follow_sub != 0 ? kCentreFollow : kCentre; }, kCentreFollow,
      [this] { center_button(); });
  d->emplace<FollowButton>(*this, ui::Rect{0x108, 0x2b, 0x112, 0x3f});
  d->emplace<ui::Button>(0xc, 0x2d, 0x41, std::string(env_.exe.c_string(text::kExit)), [this](int) {
    exit_plotting();
    return ui::kResultAccept;
  });
  d->emplace<ui::Button>(0x51, 0x2d, 0x41, std::string(env_.exe.c_string(text::kClear)), [this](int) {
    clear_course();
    return ui::kResultAccept;
  });
  return d;
}

void ChartStation::add_zoom_buttons(ui::Dialog& d, int dx) {
  d.emplace<SpriteButton>(*this, ui::Rect{0xbb + dx, 0x2b, 0xd0 + dx, 0x3f}, chart_sprites_, [] { return kZoomIn; },
                          kZoomIn + 1, [this] { zoom_box_grow(); });
  d.emplace<SpriteButton>(*this, ui::Rect{0xd2 + dx, 0x2b, 0xe7 + dx, 0x3f}, chart_sprites_, [] { return kZoomOut; },
                          kZoomOut + 1, [this] { zoom_box_shrink(); });
  d.emplace<SpriteButton>(
      *this, ui::Rect{0xe9 + dx, 0x2b, 0x107 + dx, 0x3f}, chart_sprites_,
      [this] { return env_.options.chart_follow_sub != 0 ? kCentreFollow : kCentre; }, kCentreFollow,
      [this] { center_button(); });
  d.emplace<FollowButton>(*this, ui::Rect{0x108 + dx, 0x2b, 0x112 + dx, 0x3f});
}

void ChartStation::open_tools_panel() {
  dirty_ = true;
  compression_control_ = nullptr;
  clock_control_ = nullptr;
  prompt_control_ = nullptr;
  panel_dialog_ = make_tools_panel();
  panel_dialog_->open();
}

ChartStation::SavedPanels ChartStation::saved_panels() const {
  return {panel(), {panel_pos_.x, panel_pos_.y, nav_pos_.x, nav_pos_.y, panel_pos_.x, panel_pos_.y}};
}

void ChartStation::restore_panels(const SavedPanels& saved) {
  const ChartPanel shown = saved.panel == ChartPanel::kNone || saved.panel == ChartPanel::kNavigation
                               ? saved.panel
                               : ChartPanel::kMap;
  panels_.request(static_cast<int>(shown));
  panel_pos_ = {saved.xy[0], saved.xy[1]};
  nav_pos_ = {saved.xy[2], saved.xy[3]};
  dirty_ = true;
}

}  // namespace sh


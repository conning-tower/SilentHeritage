#include "game/stations/tbt.h"

#include <algorithm>
#include <array>

#include "assets/game_exe.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/stations/nav_panel.h"
#include "game/stations/panel_ends.h"
#include "gfx/sprite.h"
#include "sim/player_combat.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The view window (0x10ed44): 291 x 125 pixels of the world frame from 37
// rows under the swell offset, shown at (0xad, 0x65).
constexpr int kViewW = kTbtViewWidth;
constexpr int kViewTop = 0x25;
constexpr int kViewBottom = 0xa1;
constexpr int kViewX = 0xad;
constexpr int kViewY = 0x65;
// The target marker row and colour (tdc_draw_target_marker(0x5a, .., 0xe5)).
constexpr int kMarkerY = 0x5a;
constexpr std::uint8_t kMarkerColour = 0xe5;
// The recon target's in-view lamp
// (player_draw_recon_target_marker(0x11e, 0x3e, .., 0xe5)).
constexpr int kReconLampX = 0x11e;
constexpr int kReconLampY = 0x3e;
// The bearing dial over (0x123, 0x55)-(0x15a, 99) in colour 0xa0, labels
// through the table 0x1282f4 tbt_screen patches.
constexpr int kDialX0 = 0x123;
constexpr int kDialY0 = 0x55;
constexpr int kDialX1 = 0x15a;
constexpr int kDialY1 = 99;
constexpr std::uint8_t kDialColour = 0xa0;
// The lower panels' sprites are drawn at (0, 0x101).
constexpr int kLowerY = 0x101;
// The magnification plate: TBT.SHP frame 3 + table 0x10ecbc[zoom] in the
// window at (0x204, 0x7e), under the cover frame 7.
constexpr int kPlateX = 0x204;
constexpr int kPlateY = 0x7e;
constexpr std::size_t kPlateCover = 7;
constexpr std::uint32_t kPlateFrame = 0x10ecbc;  // int32[9]
// The TDC mode's sub-panel button (definition 0x10f444): TBTTDC.SHP
// frames 20 + state (0x10f474).
constexpr std::uint32_t kTdcButtonState = 0x10f474;  // int32[3]

// The sides of a wide screen (not in the original): the sky and the
// housing's bands continued from the border columns, the nav panel's edge
// gauges completed (the depth gauge centred at 75.2, 370; the speed gauge
// 487 columns right), and the plate of the bridge's panel (its strip over
// BRIDGE.PCX, from row 200) beyond. Down to the command bar; as wide as the
// widest screen's sides.
constexpr int kSideEndRows = 0x1cb;
constexpr int kSideEndWidth = (kMaxScreenWidth - kScreenWidth) / 2;
constexpr PanelEndsLayout kNavEnds{.band_rows = 293,
                                   .centre_x2 = 151,
                                   .centre_y = 370,
                                   .gauge_shift = 487,
                                   .mount_radius = 82,
                                   .lug_radius = 16,
                                   .bolt_dx2 = 131,
                                   .bolt_top = 306,
                                   .bolt_bottom = 437,
                                   .left_plate = {0, 206, 15, 255},
                                   .right_plate = {624, 242, 639, 255}};
// The TDC and recognition panels: no gauges to complete.
constexpr PanelEndsLayout kPlainEnds{.band_rows = 293,
                                     .left_plate = {0, 206, 15, 255},
                                     .right_plate = {624, 242, 639, 255}};

// The row of the bridge's screen its strip is drawn at.
constexpr int kBridgeStripTop = 200;

// Returns, for each colour of `from`, the nearest colour of `to` (not in the
// original).
std::array<std::uint8_t, 256> nearest_colours(const VgaPalette& from, const VgaPalette& to) {
  std::array<std::uint8_t, 256> out{};
  for (std::size_t i = 0; i < out.size(); ++i) {
    int best = -1;
    for (std::size_t j = 0; j < out.size(); ++j) {
      int d = 0;
      for (std::size_t k = 0; k < 3; ++k) {
        const int e = from[3 * i + k] - to[3 * j + k];
        d += e * e;
      }
      if (best < 0 || d < best) {
        best = d;
        out[i] = static_cast<std::uint8_t>(j);
      }
    }
  }
  return out;
}

bool bridge_above_water(const PlayerSub& sub) {
  // player_is_surfaced (0x607a8)
  return sub.sub().bridge_height_ft * kUnitsPerFoot + sub.position().y > 0;
}

}  // namespace

bool ZoomPlate::step(std::int32_t width) {
  if (speed != 0) {
    if (ticks < 5) {
      return false;
    }
    ticks = 0;
    if (offset > width) {
      speed = -(width - 5);
    }
    offset += speed;
    if (offset <= 0) {
      offset = 0;
    }
  }
  return true;
}

TbtStation::TbtStation(AttackServices& services) : s_(services) {}

TbtStation::~TbtStation() = default;

CommandBarMenu TbtStation::menu() {
  // The menus 0x1287f4 (nav), 0x12880c (TDC), 0x128824 (ID): the bridge
  // and the two other lower panels.
  CommandBarMenu m;
  const CommandBarItem bridge{0xe, 0xf, [this] { s_.go_to(StationId::kBridge); }};
  const CommandBarItem id{0x18, 0x19, [this] { set_mode(LowerPanel::kIdBook); }};
  const CommandBarItem tdc{0x10, 0x11, [this] { show_tdc_panel(); }};
  const CommandBarItem nav{0x16, 0x17, [this] { set_mode(LowerPanel::kNav); }};
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      m.items = {bridge, id, tdc};
      break;
    case LowerPanel::kTdc:
      m.items = {bridge, id, nav};
      break;
    case LowerPanel::kIdBook:
      m.items = {bridge, tdc, nav};
      break;
  }
  return m;
}

void TbtStation::open() {
  // tbt_screen (0x77941)
  pick_target();
  const Install& install = s_.install();
  background_ = load_pcx(install, "ART/TBT.PCX");
  s_.screen().blit(background_.image, 0, 0);
  s_.palette() = background_.palette;
  pcx_palette_ = background_.palette;
  s_.shade_palette(&pcx_palette_);
  tbt_ = Shp::load(install, "ART/TBT.SHP");

  // tbt_init_dialogs (0x76e28): the train buttons left and right of the
  // binoculars, the magnification knob, the mark button, Alt-F (frame
  // rate, not shown here) and 'T'.
  dialog_ = std::make_unique<ui::Dialog>(s_.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &s_.screen());
  auto hover_left = [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateLeftFast) : s_.set_default_cursor(); };
  auto hover_right = [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateRightFast) : s_.set_default_cursor(); };
  add_train_area(*dialog_, 0, 0x21, 0x57, 0x10b, 0x332c, [this](std::int32_t step) { train(step, -1); }, hover_left);
  add_train_area(
      *dialog_, 0x260, 0x21, 0x27f, 0x7a, 0x342e, [this](std::int32_t step) { train(step, 1); }, hover_right);
  add_train_area(*dialog_, 0x236, 0x7b, 0x27f, 0x10b, 0, [this](std::int32_t step) { train(step, 1); }, hover_right);
  add_cycle_button(
      *dialog_, s_.host(), 0x218, 0x40, 0x25e, 0x7a, 0xff5a, *tbt_, 1, 2, 0, [this](std::int32_t) { cycle_zoom(); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kZoom) : s_.set_default_cursor(); });
  add_cycle_button(
      *dialog_, s_.host(), 0x62, 4, 0x84, 0x22, 0x3920, *tbt_, 8, 9, 0, [this](std::int32_t) { mark_target(); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kMark) : s_.set_default_cursor(); });
  add_hotkey(*dialog_, 0xff54, [this] { show_tdc_panel(); });
  dialog_->open();
  if (s_.features.drag_rotate) {
    // The view and the train areas.
    drag_ = std::make_unique<ViewDrag>(view_bearing_hooks(s_, &s_.zoom, [this] { return dialog_.get(); }));
    drag_->set_regions({{kViewX, kViewY, kViewX + kViewW - 1, kViewY + kViewBottom - kViewTop},
                        {0, 0x21, 0x57, 0x10b},
                        {0x260, 0x21, 0x27f, 0x7a},
                        {0x236, 0x7b, 0x27f, 0x10b}});
  }
  load_overlays();

  render_mark_ = s_.gui().ticks();
  render_delay_ = 0;
  plate_mark_ = s_.gui().ticks();
  plate_ = ZoomPlate{};
  dial_bearing_ = -1;
  s_.host().fade_out();
}

void TbtStation::load_overlays() {
  // tbt_load_overlays (0x77d2d)
  side_ends_due_ = true;
  const Install& install = s_.install();
  const Viewport screen = Viewport::full(s_.screen());
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      s_.host().open_command_bar(menu());
      lower_ = Shp::load(install, "ART/TBTNAV.SHP");
      draw_sprite_clipped(screen, lower_->frame(0), 0, kLowerY);
      panel_ = s_.host().make_nav_panel(PanelSite::kTbt);
      if (panel_) {
        panel_->open();
      }
      break;
    case LowerPanel::kTdc:
      s_.host().open_command_bar(menu());
      tdc_ = std::make_unique<TdcPanels>(s_, TdcPanelSite::kTbt);
      tdc_->open();
      lower_ = Shp::load(install, "ART/TBTTDC.SHP");
      // The dialog 0x128ad0: 'N' back to the nav panel and the sub-panel
      // button.
      mode_dialog_ = std::make_unique<ui::Dialog>(s_.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &s_.screen());
      add_hotkey(*mode_dialog_, 0xff4e, [this] { set_mode(LowerPanel::kNav); });
      add_push_button(
          *mode_dialog_, s_, 0x1da, 0x130, 0x214, 0x16b, *lower_, 20, [this] { cycle_tdc_panel(); },
          [this] { return game_exe().i32(kTdcButtonState + 4 * static_cast<std::uint32_t>(s_.tdc_panel)); });
      mode_dialog_->open();
      break;
    case LowerPanel::kIdBook:
      s_.host().open_command_bar(menu());
      lower_ = Shp::load(install, "ART/TBTID.SHP");
      draw_sprite_clipped(screen, lower_->frame(0), 0, kLowerY);
      panel_ = s_.host().make_id_book(PanelSite::kTbt);
      if (panel_) {
        panel_->open();
      }
      break;
  }
}

void TbtStation::set_side_ends() {
  if (!s_.host().wide_screen()) {
    return;
  }
  const Image8& screen = s_.screen();
  Image8 panel(kScreenWidth, kSideEndRows);
  for (int y = 0; y < kSideEndRows; ++y) {
    for (int x = 0; x < kScreenWidth; ++x) {
      panel.set(x, y, screen.at(x, y));
    }
  }
  // The bridge's panel (its strip over BRIDGE.PCX), whose plain plate the
  // TBT's panel hides, in the TBT's colours.
  const PcxImage bridge = load_pcx(s_.install(), "ART/BRIDGE.PCX");
  const Shp strip = Shp::load(s_.install(), "ART/BRDGNAV.SHP");
  Image8 plate(kScreenWidth, kScreenHeight - kBridgeStripTop, 0);
  plate.blit(bridge.image, 0, -kBridgeStripTop);
  draw_sprite_clipped(Viewport::full(plate), strip.frame(0), 0, 0);
  const std::array<std::uint8_t, 256> colours = nearest_colours(bridge.palette, background_.palette);
  for (std::uint8_t& c : plate.pixels()) {
    c = colours[c];
  }
  const PanelEnds ends = make_panel_ends(panel, plate, kSideEndWidth, kSideEndWidth,
                                         s_.lower_panel == LowerPanel::kNav ? kNavEnds : kPlainEnds);
  SideFill fill{.kind = SideFill::Kind::kPicture,
                .left = Image8(kSideEndWidth, kScreenHeight, 0),
                .right = Image8(kSideEndWidth, kScreenHeight, 0)};
  fill.left.blit(ends.left, 0, 0);
  fill.right.blit(ends.right, 0, 0);
  s_.host().set_side_fill(std::move(fill));
}

void TbtStation::unload_overlays() {
  // tbt_unload_overlays (0x77ff5)
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
    case LowerPanel::kIdBook:
      if (panel_) {
        panel_->close();
        panel_.reset();
      }
      break;
    case LowerPanel::kTdc:
      tdc_->close();
      tdc_.reset();
      mode_dialog_->close();
      mode_dialog_.reset();
      break;
  }
  lower_.reset();
}

void TbtStation::set_mode(LowerPanel mode, std::optional<TdcSubPanel> tdc_panel) {
  pending_ = ModeChange{mode, tdc_panel};
}

void TbtStation::apply_mode_change() {
  // tbt_set_mode (0x78253)
  if (!pending_ || !dialog_) {
    return;
  }
  const ModeChange change = *pending_;
  pending_.reset();
  s_.keep_events([&] {
    unload_overlays();
    s_.host().close_command_bar();
    if (change.tdc_panel) {
      s_.tdc_panel = *change.tdc_panel;
    }
    s_.lower_panel = change.mode;
    load_overlays();
  });
}

void TbtStation::event(const ui::Event& event) {
  if (drag_ && drag_->event(event, s_.gui().ticks())) {
    return;
  }
  // tbt_panel_event (0x780dd), then the TBT's controls.
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
    case LowerPanel::kIdBook:
      if (panel_) {
        panel_->event(event);
      }
      break;
    case LowerPanel::kTdc:
      tdc_->event(event);
      mode_dialog_->handle_event(event);
      break;
  }
  dialog_->handle_event(event);
  apply_mode_change();
}

void TbtStation::process() {
  // tbt_panel_process (0x78195)
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
    case LowerPanel::kIdBook:
      if (panel_) {
        panel_->process();
      }
      break;
    case LowerPanel::kTdc:
      tdc_->process();
      mode_dialog_->process();
      break;
  }
  dialog_->process();
  apply_mode_change();
}

void TbtStation::update() {
  apply_mode_change();
  Mission& mission = s_.mission();
  if (!bridge_above_water(mission.player())) {
    s_.go_to(StationId::kConn);
    return;
  }
  const bool shade = s_.advance_world();
  if (s_.redraw_requested()) {
    dialog_->invalidate();
  }
  // tbt_panel_update (0x781ae)
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      if (panel_) {
        panel_->update(s_.redraw_requested());
      }
      break;
    case LowerPanel::kTdc:
      tdc_->update();
      break;
    case LowerPanel::kIdBook:
      if (first_frame()) {
        draw_sprite_clipped(Viewport::full(s_.screen()), lower_->frame(0), 0, kLowerY);
      }
      if (panel_) {
        panel_->update(s_.redraw_requested());
      }
      break;
  }
  if (drag_) {
    drag_->update(s_.gui().ticks());
  }
  pick_target();
  const std::int32_t bearing = mission.player_combat().view_bearing.normalized().tenths();
  if (first_frame() || s_.gui().ticks() - render_mark_ > 5 + render_delay_ || dial_bearing_ != bearing) {
    render_view();
    blit_view();
  }
  draw_bearing_dial();
  draw_zoom_indicator();
  if (first_frame()) {
    s_.host().fade_in();
  }
  if (side_ends_due_) {
    side_ends_due_ = false;
    set_side_ends();
  }
  if (shade) {
    s_.shade_palette(&pcx_palette_);
  }
}

void TbtStation::close() {
  // tbt_close (0x77b2f)
  pending_.reset();
  unload_overlays();
  drag_.reset();
  dialog_->close();
  dialog_.reset();
  tbt_.reset();
}

void TbtStation::render_view() {
  // tbt_update's view.
  render_mark_ = s_.gui().ticks();
  render_delay_ = 0;
  Mission& mission = s_.mission();
  const ViewEye eye = tbt_eye(mission.player(), mission.player_combat().view_bearing, s_.zoom);
  s_.render_world(eye);
  const Viewport window = s_.world_window(0, kViewTop, kViewW - 1, kViewBottom);
  s_.draw_target_marker(window, eye, kMarkerY, kMarkerColour);
  s_.draw_recon_marker(window, eye, kReconLampX, kReconLampY, kMarkerColour);
  draw_sprite_clipped(window, tbt_->frame(0), 0, 0);
}

void TbtStation::blit_view() {
  s_.blit_to_screen(s_.world_window(0, kViewTop, kViewW - 1, kViewBottom), kViewX, kViewY);
}

void TbtStation::draw_bearing_dial() {
  const std::int32_t bearing = s_.mission().player_combat().view_bearing.normalized().tenths();
  if (dial_bearing_ == bearing) {
    return;
  }
  dial_bearing_ = bearing;
  draw_bearing_tape(s_.screen(), background_.image, s_.bearing_font(), Angle10(bearing), kDialX0, kDialY0, kDialX1,
                    kDialY1, kDialColour, nav_digit_colours(NavPanelSite::kTbt));
}

void TbtStation::draw_zoom_indicator() {
  plate_.ticks += static_cast<std::int32_t>(s_.elapsed(plate_mark_));
  if (!plate_.active) {
    return;
  }
  const std::int32_t power = std::clamp(s_.zoom.floor_int(), 0, 8);
  const auto frame = static_cast<std::size_t>(game_exe().i32(kPlateFrame + 4 * static_cast<std::uint32_t>(power)) + 3);
  if (frame >= tbt_->frame_count()) {
    return;
  }
  const SpriteFrame& plate = tbt_->frame(frame);
  const int w = plate.width();
  const int h = plate.height();
  if (!plate_.step(w)) {
    return;
  }
  const Viewport window{&s_.screen(), kPlateX, kPlateY, kPlateX + w - 1, kPlateY + h - 1};
  draw_sprite_clipped(window, plate, plate_.offset >> 1, 0);
  draw_sprite_clipped(window, tbt_->frame(kPlateCover), plate_.offset - w, 0);
  if (plate_.offset == 0) {
    plate_.active = false;
  }
}

void TbtStation::cycle_zoom() {
  // tbt_cycle_zoom (0x78641): the next power, the plate slides, and the
  // binoculars black out under their reticle until the view is redrawn 10
  // ticks later.
  s_.zoom = next_magnification(s_.zoom);
  plate_.start();
  plate_mark_ = s_.gui().ticks();
  render_mark_ = s_.gui().ticks();
  render_delay_ = 5;
  const Viewport window = s_.world_window(0, kViewTop, kViewW - 1, kViewBottom);
  ui::fill_rect(window, 0, 0, kViewW - 1, kViewBottom - kViewTop, 0);
  draw_sprite_clipped(window, tbt_->frame(0), 0, 0);
  blit_view();
}

void TbtStation::train(std::int32_t step, int sign) {
  // tbt_train_left / tbt_train_right
  PlayerCombat& combat = s_.mission().player_combat();
  combat.view_bearing = train_bearing(combat.view_bearing, step, sign);
  combat.tdc.toggle_bearing_follow(true);
}

void TbtStation::mark_target() {
  // tdc_mark_target through the TBT's thunk 0x786d4.
  Mission& mission = s_.mission();
  PlayerCombat& combat = mission.player_combat();
  SimContext ctx = mission.sim().context();
  combat.tdc.mark_target(combat.own_boat(mission.player(), ctx), combat.view_bearing, mission.sim().vehicles(),
                         combat.sensing);
}

void TbtStation::pick_target() {
  // tdc_pick_target_on_bearing (0x7c4ac)
  Mission& mission = s_.mission();
  PlayerCombat& combat = mission.player_combat();
  SimContext ctx = mission.sim().context();
  combat.tdc.pick_target_on_bearing(combat.own_boat(mission.player(), ctx), combat.view_bearing,
                                    mission.sim().vehicles(), combat.sensing);
}

void TbtStation::show_tdc_panel() {
  if (s_.lower_panel == LowerPanel::kTdc) {
    cycle_tdc_panel();
    return;
  }
  set_mode(LowerPanel::kTdc);
}

void TbtStation::cycle_tdc_panel() {
  const auto next = static_cast<std::int32_t>(s_.tdc_panel) + 1;
  set_mode(LowerPanel::kTdc, static_cast<TdcSubPanel>(next > 2 ? 0 : next));
}

}  // namespace sh

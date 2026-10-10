#include "game/stations/scope.h"

#include <algorithm>
#include <array>

#include "assets/game_exe.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/stations/conn.h"
#include "gfx/sprite.h"
#include "sim/player_combat.h"
#include "ui/draw.h"
#include "ui/skin.h"

namespace sh {

namespace {

// The view window: 201 x 200 pixels of the world frame, shown at (0xdb,
// 0x77), or (0xdb, 0x56) over the recognition manual.
constexpr int kViewW = kScopeViewWidth;
constexpr int kViewH = 200;
constexpr int kViewX = 0xdb;
constexpr int kViewY = 0x77;
constexpr int kIdViewY = 0x56;
// The handles dialog 0x126890: 639 x 92 at y 0x183, or 0xd7 over the
// manual.
constexpr int kDialogY = 0x183;
constexpr int kIdDialogY = 0xd7;
constexpr int kDialogW = 0x27f;
constexpr int kDialogH = 0x5c;
// The bearing tape over (0xd3, 0xd)-(0x1ac, 0x1c) in colour 0xe0.
constexpr int kTapeX0 = 0xd3;
constexpr int kTapeY0 = 0xd;
constexpr int kTapeX1 = 0x1ac;
constexpr int kTapeY1 = 0x1c;
constexpr std::uint8_t kTapeColour = 0xe0;
// The target marker row and colour (tdc_draw_target_marker(0xaa, .., 0xd4)).
constexpr int kMarkerY = 0xaa;
constexpr std::uint8_t kMarkerColour = 0xd4;
// The recon target's in-view lamp
// (player_draw_recon_target_marker(0xc4, 0x64, .., 0xd4)).
constexpr int kReconLampX = 0xc4;
constexpr int kReconLampY = 0x64;
// The magnification lamp: SCOPE.SHP frame 1 + table 0x10cf18[zoom] at
// (0x208, 0) of the handles dialog.
constexpr int kLampX = 0x208;
constexpr std::uint32_t kLampFrame = 0x10cf18;  // int32[9]
// The TDC mode's sub-panel button (definition 0x10d6a8): SCOPETDC.SHP
// frames 20 + state, states 2, 1, 0 for the solution, gyro and torpedo
// panels (0x10d6c4).
constexpr std::uint32_t kTdcButtonState = 0x10d6c4;  // int32[3]

}  // namespace

ScopeStation::ScopeStation(AttackServices& services) : s_(services) {}

ScopeStation::~ScopeStation() = default;

CommandBarMenu ScopeStation::menu() const {
  // The menus 0x126848 (nav), 0x126860 (TDC), 0x126878 (ID): the conn and
  // the two other lower panels.
  CommandBarMenu m;
  auto self = const_cast<ScopeStation*>(this);
  const CommandBarItem conn{2, 3, [this] { s_.go_to(StationId::kConn); }};
  const CommandBarItem id{0x18, 0x19, [self] { self->set_mode(LowerPanel::kIdBook); }};
  const CommandBarItem tdc{0x10, 0x11, [self] { self->toggle_tdc_view(); }};
  const CommandBarItem nav{0x16, 0x17, [self] { self->set_mode(LowerPanel::kNav); }};
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      m.items = {conn, id, tdc};
      break;
    case LowerPanel::kTdc:
      m.items = {conn, id, nav};
      break;
    case LowerPanel::kIdBook:
      m.items = {conn, tdc, nav};
      break;
  }
  return m;
}

void ScopeStation::open() {
  // scope_screen (0x6d0a3)
  const Install& install = s_.install();
  background_ = load_pcx(install, "ART/SCOPE.PCX");
  s_.screen().blit(background_.image, 0, 0);
  s_.palette() = background_.palette;
  if (s_.host().rig_for_red()) {
    s_.palette() = load_pcx(install, "ART/SCOPE.PAL").palette;
  }
  s_.shade_palette(nullptr);
  scope_ = Shp::load(install, "ART/SCOPE.SHP");
  render_mark_ = s_.gui().ticks();
  render_delay_ = 0;
  view_x_ = kViewX;
  view_y_ = kViewY;
  dialog_y_ = kDialogY;
  open_main_dialog();
  load_overlays(false);
  tape_bearing_ = -1;
  s_.host().fade_out();
}

void ScopeStation::open_main_dialog() {
  // scope_init_dialogs (0x6c68c): the train handles, the magnification
  // knob, Alt-F (frame rate, not shown here), space (mark the target) and
  // 'T' (the TDC panels).
  dialog_ =
      std::make_unique<ui::Dialog>(s_.gui(), 0, dialog_y_, kDialogW, kDialogH, ui::kDialogNoNavigation, &s_.screen());
  add_train_area(
      *dialog_, 0, 0, 0x76, 0x40, 0x332c, [this](std::int32_t step) { train(step, -1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateLeftFast) : s_.set_default_cursor(); });
  add_train_area(
      *dialog_, 0x22a, 0, 0x27f, 0x40, 0x342e, [this](std::int32_t step) { train(step, 1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateRightFast) : s_.set_default_cursor(); });
  auto zoom = [this](ui::Dialog& d, ui::Area&, ui::Msg msg, int, const ui::Event&) {
    if (msg == ui::Msg::kPress) {
      toggle_magnification();
      draw_magnification(d);
    } else if (msg == ui::Msg::kPaint) {
      draw_magnification(d);
    }
    return 0;
  };
  ui::Area& knob = dialog_->emplace<ui::Area>(0x207, 0, 0x229, 0x40, zoom, ui::Area::ActivateFn{}, 0xff5a);
  knob.on_hover = [this](bool on, int, const ui::Event&) {
    on ? s_.set_cursor(ui::CursorId::kZoom) : s_.set_default_cursor();
  };
  add_hotkey(*dialog_, 0x3920, [this] { mark_target(); });
  add_hotkey(*dialog_, 0xff54, [this] { toggle_tdc_view(); });
  dialog_->open();
}

void ScopeStation::load_overlays(bool redraw) {
  // scope_load_overlays (0x6d4d9)
  const Install& install = s_.install();
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      s_.host().open_command_bar(menu());
      nav_ = Shp::load(install, "ART/SCOPENAV.SHP");
      draw_sprite_clipped(Viewport::full(s_.screen()), nav_->frame(0), 0, 0);
      draw_sprite_clipped(Viewport::full(s_.screen()), nav_->frame(1), 0x1d7, 0);
      panel_ = s_.host().make_nav_panel(PanelSite::kScope);
      if (panel_) {
        panel_->open();
      }
      break;
    case LowerPanel::kTdc:
      s_.host().open_command_bar(menu());
      tdc_ = std::make_unique<TdcPanels>(s_, TdcPanelSite::kScope);
      tdc_->open();
      tdc_buttons_ = Shp::load(install, "ART/SCOPETDC.SHP");
      // The dialog 0x126aec: 'N' back to the nav panel and the sub-panel
      // button.
      mode_dialog_ = std::make_unique<ui::Dialog>(s_.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &s_.screen());
      add_hotkey(*mode_dialog_, 0xff4e, [this] { set_mode(LowerPanel::kNav); });
      add_push_button(
          *mode_dialog_, s_, 0x201, 0xb9, 0x23b, 0xf3, *tdc_buttons_, 20, [this] { cycle_tdc_panel(); },
          [this] { return game_exe().i32(kTdcButtonState + 4 * static_cast<std::uint32_t>(s_.tdc_panel)); });
      mode_dialog_->open();
      break;
    case LowerPanel::kIdBook:
      background_ = load_pcx(install, "ART/SCOPEID.PCX");
      s_.screen().blit(background_.image, 0, 0);
      s_.host().open_command_bar(menu());
      panel_ = s_.host().make_id_book(PanelSite::kScope);
      if (panel_) {
        panel_->open();
      }
      view_y_ = kIdViewY;
      dialog_->close();
      dialog_y_ = kIdDialogY;
      open_main_dialog();
      if (redraw) {
        tape_bearing_ = -1;
        update_bearing_scale();
        render_view();
        blit_view();
      }
      break;
  }
}

void ScopeStation::leave_mode() {
  // scope_leave_mode (0x6d810)
  switch (s_.lower_panel) {
    case LowerPanel::kNav:
      if (panel_) {
        panel_->close();
        panel_.reset();
      }
      nav_.reset();
      break;
    case LowerPanel::kTdc:
      tdc_->close();
      tdc_.reset();
      mode_dialog_->close();
      mode_dialog_.reset();
      tdc_buttons_.reset();
      break;
    case LowerPanel::kIdBook:
      if (panel_) {
        panel_->close();
        panel_.reset();
      }
      background_ = load_pcx(s_.install(), "ART/SCOPE.PCX");
      s_.screen().blit(background_.image, 0, 0);
      tape_bearing_ = -1;
      update_bearing_scale();
      view_y_ = kViewY;
      dialog_->close();
      dialog_y_ = kDialogY;
      open_main_dialog();
      render_view();
      blit_view();
      break;
  }
}

void ScopeStation::set_mode(LowerPanel mode, std::optional<TdcSubPanel> tdc_panel) {
  pending_ = ModeChange{mode, tdc_panel};
}

void ScopeStation::apply_mode_change() {
  // scope_set_mode (0x6dad7)
  if (!pending_ || !dialog_) {
    return;
  }
  const ModeChange change = *pending_;
  pending_.reset();
  s_.keep_events([&] {
    leave_mode();
    s_.host().close_command_bar();
    s_.lower_panel = change.mode;
    if (change.tdc_panel) {
      s_.tdc_panel = *change.tdc_panel;
    }
    load_overlays(true);
  });
}

void ScopeStation::event(const ui::Event& event) {
  // scope_mode_event (0x6d97a), then the handles.
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

void ScopeStation::process() {
  // scope_mode_process (0x6da32)
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

void ScopeStation::update() {
  apply_mode_change();
  Mission& mission = s_.mission();
  if (!mission.player().periscope_raised()) {
    // scope_station_update (0x6d2d1): the conn shows the periscope going down.
    if (auto* conn = dynamic_cast<ConnStation*>(s_.stations().get(StationId::kConn)); conn != nullptr) {
      conn->start_periscope(2);
    }
    s_.go_to(StationId::kConn);
    return;
  }
  const bool shade = s_.advance_world();
  if (s_.redraw_requested()) {
    dialog_->invalidate();
  }
  // scope_update_mode (0x6da4b)
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
        s_.screen().blit(background_.image, 0, 0);
      }
      if (panel_) {
        panel_->update(s_.redraw_requested());
      }
      break;
  }
  pick_target();
  const std::int32_t bearing = mission.player_combat().view_bearing.normalized().tenths();
  if (first_frame() || s_.gui().ticks() - render_mark_ > 5 + render_delay_ || tape_bearing_ != bearing) {
    render_view();
    blit_view();
  }
  update_bearing_scale();
  if (first_frame()) {
    s_.host().fade_in();
  }
  if (shade) {
    s_.shade_palette(nullptr);
  }
}

void ScopeStation::close() {
  // scope_station_close (0x6d26e)
  pending_.reset();
  leave_mode();
  dialog_->close();
  dialog_.reset();
  scope_.reset();
}

void ScopeStation::render_view() {
  // scope_render_view (0x6d3ca)
  render_mark_ = s_.gui().ticks();
  render_delay_ = 0;
  Mission& mission = s_.mission();
  const ViewEye eye = scope_eye(mission.player(), mission.player_combat().view_bearing, s_.zoom);
  s_.render_world(eye);
  const Viewport window = s_.world_window(0, 0, kViewW - 1, kViewH - 1);
  s_.draw_target_marker(window, eye, kMarkerY, kMarkerColour);
  s_.draw_recon_marker(window, eye, kReconLampX, kReconLampY, kMarkerColour);
  draw_sprite_clipped(window, scope_->frame(0), 0, 0);
}

void ScopeStation::blit_view() { s_.blit_to_screen(s_.world_window(0, 0, kViewW - 1, kViewH - 1), view_x_, view_y_); }

void ScopeStation::update_bearing_scale() {
  // scope_update_bearing_scale (0x6d472): the labels through the identity
  // table 0x11dde0.
  const std::int32_t bearing = s_.mission().player_combat().view_bearing.normalized().tenths();
  if (tape_bearing_ == bearing) {
    return;
  }
  tape_bearing_ = bearing;
  draw_bearing_tape(s_.screen(), background_.image, s_.bearing_font(), Angle10(bearing), kTapeX0, kTapeY0, kTapeX1,
                    kTapeY1, kTapeColour, ui::GuiResources::identity_table());
}

void ScopeStation::toggle_magnification() {
  // scope_toggle_magnification (0x6dcbb): the next power, the click, and
  // the scope blacked out under its reticle until the view is redrawn 10
  // ticks later.
  s_.zoom = next_magnification(s_.zoom);
  s_.host().play_sfx_at(kButtonClickSfx, kButtonClickVolume);
  render_mark_ = s_.gui().ticks();
  render_delay_ = 5;
  const Viewport window = s_.world_window(0, 0, kViewW - 1, kViewH - 1);
  ui::fill_rect(window, 0, 0, kViewW - 1, kViewH - 1, 0);
  draw_sprite_clipped(window, scope_->frame(0), 0, 0);
  blit_view();
}

void ScopeStation::draw_magnification(ui::Dialog& dialog) {
  const std::int32_t power = std::clamp(s_.zoom.floor_int(), 0, 8);
  const auto frame = static_cast<std::size_t>(game_exe().i32(kLampFrame + 4 * static_cast<std::uint32_t>(power)) + 1);
  draw_sprite_clipped(dialog.gfx(), scope_->frame(frame), kLampX, 0);
}

void ScopeStation::train(std::int32_t step, int sign) {
  // scope_train_left / scope_train_right: also stops the bearing follow.
  PlayerCombat& combat = s_.mission().player_combat();
  combat.view_bearing = train_bearing(combat.view_bearing, step, sign);
  combat.tdc.toggle_bearing_follow(true);
}

void ScopeStation::mark_target() {
  Mission& mission = s_.mission();
  PlayerCombat& combat = mission.player_combat();
  SimContext ctx = mission.sim().context();
  combat.tdc.mark_target(combat.own_boat(mission.player(), ctx), combat.view_bearing, mission.sim().vehicles(),
                         combat.sensing);
}

void ScopeStation::pick_target() {
  // tdc_pick_target_on_bearing (0x7c4ac)
  Mission& mission = s_.mission();
  PlayerCombat& combat = mission.player_combat();
  SimContext ctx = mission.sim().context();
  combat.tdc.pick_target_on_bearing(combat.own_boat(mission.player(), ctx), combat.view_bearing,
                                    mission.sim().vehicles(), combat.sensing);
}

void ScopeStation::toggle_tdc_view() {
  if (s_.lower_panel == LowerPanel::kTdc) {
    cycle_tdc_panel();
    return;
  }
  set_mode(LowerPanel::kTdc);
}

void ScopeStation::cycle_tdc_panel() {
  const auto next = static_cast<std::int32_t>(s_.tdc_panel) + 1;
  set_mode(LowerPanel::kTdc, static_cast<TdcSubPanel>(next > 2 ? 0 : next));
}

}  // namespace sh

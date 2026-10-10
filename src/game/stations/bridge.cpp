#include "game/stations/bridge.h"

#include "core/fixed_math.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/stations/nav_panel.h"
#include "gfx/sprite.h"
#include "sim/player_combat.h"

namespace sh {

namespace {

// The tape over (0x111, 0x1b0)-(0x16c, 0x1c0) in colour 0xa0, its labels
// (BEARING.FNT glyph 0xe0) through the table 0x11627c that bridge_screen
// patches.
constexpr int kTapeX0 = 0x111;
constexpr int kTapeY0 = 0x1b0;
constexpr int kTapeX1 = 0x16c;
constexpr int kTapeY1 = 0x1c0;
constexpr std::uint8_t kTapeColour = 0xa0;
// The deck sprite's hotspot row in the view window.
constexpr int kDeckY = 0xcd;
// The view window: the whole width, 200 rows from the swell offset.
constexpr int kViewRows = 200;

bool bridge_above_water(const PlayerSub& sub) {
  // player_is_surfaced (0x607a8)
  return sub.sub().bridge_height_ft * kUnitsPerFoot + sub.position().y > 0;
}

}  // namespace

std::optional<DeckView> bridge_deck_view(Angle10 bearing) {
  // bridge_draw_deck_overlay (0x17b4c)
  std::int32_t b = bearing.normalized().tenths();
  int mode = 0;
  if (b < 450 || b > 3150) {
    mode = 1;
  }
  if (b < 2250 && b > 1350) {
    mode = 2;
  }
  if (mode == 0) {
    return std::nullopt;
  }
  const std::int32_t base = mode == 1 ? 0 : 1800;
  // The half field of view and the pixels per tenth of a degree of the
  // 640-wide view (half width 320 at the 460-px focal distance).
  constexpr std::int32_t kHalfWidth = kBridgeViewWidth / 2;
  const std::int32_t half_fov = fx::heading_from_delta((kHalfWidth << 16) >> 1, kNearZ << 16).tenths();
  const std::int32_t px_per_tenth = fx::int64_div(static_cast<std::int64_t>(kHalfWidth << 16) << 16, half_fov << 16);
  std::int32_t offset = fx::angle_sub(Angle10(base), fx::angle_sub(Angle10(b), Angle10(half_fov))).tenths();
  if (offset > 1800) {
    offset -= 3600;
  }
  const std::int64_t product = static_cast<std::int64_t>(offset) * 65536 * px_per_tenth + 0x8000;
  const auto x16 = static_cast<std::int32_t>(product >> 16);
  DeckView v;
  v.x = x16 < 0 ? -(-x16 >> 16) : x16 >> 16;
  if (mode == 2) {
    b = fx::angle_sub(Angle10(b), Angle10(1800)).tenths();
  }
  b = fx::angle_sub(Angle10(b), Angle10(0)).tenths();
  if (b < 1800) {
    v.mirror = true;
  } else {
    b = fx::angle_sub(Angle10(3600), Angle10(b)).tenths();
  }
  v.frame = std::min(b / 150, 8) + (mode == 1 ? 0 : 9);
  return v;
}

BridgeStation::BridgeStation(AttackServices& services) : s_(services) {}

BridgeStation::~BridgeStation() = default;

void BridgeStation::open() {
  // bridge_screen (0x1773c)
  const Install& install = s_.install();
  background_ = load_pcx(install, "ART/BRIDGE.PCX");
  s_.screen().blit(background_.image, 0, 0);
  s_.palette() = background_.palette;
  pcx_palette_ = background_.palette;
  s_.shade_palette(&pcx_palette_);
  deck_ = Shp::load(install, "ART/BRIDGE.SHP");

  // bridge_init_dialogs (0x1745c): the two halves of the view train left
  // and right (',' / '.'); Alt-F toggles the frame rate display.
  dialog_ = std::make_unique<ui::Dialog>(s_.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &s_.screen());
  add_train_area(
      *dialog_, 0, 0, 0x13f, 199, 0x332c, [this](std::int32_t step) { turn(step, -1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateLeftFast) : s_.set_default_cursor(); });
  add_train_area(
      *dialog_, 0x140, 0, 0x27f, 199, 0x342e, [this](std::int32_t step) { turn(step, 1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateRightFast) : s_.set_default_cursor(); });
  dialog_->open();

  // bridge_load_nav (0x17a29): the menu 0x11667c (conn, TBT, deck gun),
  // BRDGNAV.SHP under the view and the nav panel.
  CommandBarMenu menu;
  menu.items = {{2, 3, [this] { s_.go_to(StationId::kConn); }},
                {0xc, 0xd, [this] { s_.go_to(StationId::kTbt); }},
                {0x14, 0x15, [this] { s_.go_to(StationId::kDeckGun); }}};
  s_.host().open_command_bar(menu);
  nav_ = Shp::load(install, "ART/BRDGNAV.SHP");
  draw_sprite_clipped(Viewport::full(s_.screen()), nav_->frame(0), 0, 200);
  nav_panel_ = s_.host().make_nav_panel(PanelSite::kBridge);
  if (nav_panel_) {
    nav_panel_->open();
  }

  render_mark_ = s_.gui().ticks();
  tape_bearing_ = -1;
  s_.host().fade_out();
}

void BridgeStation::event(const ui::Event& event) {
  if (nav_panel_) {
    nav_panel_->event(event);
  }
  dialog_->handle_event(event);
}

void BridgeStation::process() {
  if (nav_panel_) {
    nav_panel_->process();
  }
  dialog_->process();
}

void BridgeStation::update() {
  Mission& mission = s_.mission();
  if (!bridge_above_water(mission.player())) {
    s_.go_to(StationId::kConn);
    return;
  }
  const bool shade = s_.advance_world();
  if (s_.redraw_requested()) {
    dialog_->invalidate();
  }
  if (nav_panel_) {
    nav_panel_->update(s_.redraw_requested());
  }
  const std::int32_t bearing = mission.player_combat().view_bearing.normalized().tenths();
  if (first_frame() || s_.gui().ticks() - render_mark_ > 5 || tape_bearing_ != bearing) {
    render_mark_ = s_.gui().ticks();
    render_view();
  }
  // bridge_draw_bearing_strip (0x17de7)
  if (tape_bearing_ != bearing) {
    tape_bearing_ = bearing;
    draw_bearing_tape(s_.screen(), background_.image, s_.bearing_font(), Angle10(bearing), kTapeX0, kTapeY0, kTapeX1,
                      kTapeY1, kTapeColour, nav_digit_colours(NavPanelSite::kBridge));
  }
  if (first_frame()) {
    s_.host().fade_in();
  }
  if (shade) {
    s_.shade_palette(&pcx_palette_);
  }
}

void BridgeStation::close() {
  // bridge_free_nav (0x17a99), then the dialog and the sprites.
  if (nav_panel_) {
    nav_panel_->close();
    nav_panel_.reset();
  }
  nav_.reset();
  dialog_->close();
  dialog_.reset();
  deck_.reset();
  scratch_ = Image8();
}

void BridgeStation::render_view() {
  // bridge_render_view (0x17ac6)
  Mission& mission = s_.mission();
  const Angle10 bearing = mission.player_combat().view_bearing;
  const ViewEye eye = bridge_eye(mission.player(), bearing);
  s_.render_world(eye);
  const Viewport window = s_.world_window(0, 0, kBridgeViewWidth - 1, kViewRows - 1);
  if (const std::optional<DeckView> deck = bridge_deck_view(bearing)) {
    const SpriteFrame& frame = deck_->frame(static_cast<std::size_t>(deck->frame));
    const ColourRemap remap = gui_colour_table();
    sprite_draw_transformed(window, frame, deck->x, kDeckY, scratch_, Angle10(0),
                            Fixed16::from_int(deck->mirror ? -1 : 1), Fixed16::from_int(1), kSpriteRemap, &remap);
  }
  s_.blit_to_screen(window, 0, 0);
}

void BridgeStation::turn(std::int32_t step, int sign) {
  // bridge_turn_left / bridge_turn_right: also stops the TDC bearing follow.
  PlayerCombat& combat = s_.mission().player_combat();
  combat.view_bearing = train_bearing(combat.view_bearing, step, sign);
  combat.tdc.toggle_bearing_follow(true);
}

}  // namespace sh

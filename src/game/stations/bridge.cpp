#include "game/stations/bridge.h"

#include <algorithm>
#include <array>

#include "core/fixed_math.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/stations/nav_panel.h"
#include "game/stations/panel_ends.h"
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
// The strip under the view (BRDGNAV.SHP frame 0) and the command bar's top
// row.
constexpr int kStripTop = 200;
constexpr int kBarTop = 0x1cb;

// The strip's extensions on a wide screen (not in the original): its bands,
// the mounts of the depth gauge (centre 75.5, 113) and of the speed gauge
// (562.5), and plain plate sections next to the borders, where the plate's
// shading is the border's.
constexpr PanelEndsLayout kStripEnds{.band_rows = 33,
                                     .centre_x2 = 151,
                                     .centre_y = 113,
                                     .gauge_shift = 487,
                                     .mount_radius = 82,
                                     .lug_radius = 16,
                                     .bolt_dx2 = 131,
                                     .bolt_top = 49,
                                     .bolt_bottom = 180,
                                     .left_plate = {0, 206, 15, 255},
                                     .right_plate = {624, 242, 639, 255}};

bool bridge_above_water(const PlayerSub& sub) {
  // player_is_surfaced (0x607a8)
  return sub.sub().bridge_height_ft * kUnitsPerFoot + sub.position().y > 0;
}

// The transition between the deck frames of the wide bridge.
constexpr DeckTransition kDeckTransition = DeckTransition::kWarpDither;

// Returns the 4x4 ordered dither level (0..15) of pixel (x, y).
int dither_level(int x, int y) {
  static constexpr std::array<std::array<std::uint8_t, 4>, 4> kBayer{
      {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}}};
  return kBayer[static_cast<std::size_t>(y & 3)][static_cast<std::size_t>(x & 3)];
}

// Draws the deck of `view` through `remap` at its column, on row kDeckY of
// `vp`: each of the two frames resampled to their outline in between
// (left and right of the hotspot and above it, apart), the second frame's
// pixels where the dither level is under `weight` (not in the original).
// Nothing is decoded when the outline is out of `vp`.
void draw_wide_deck(const Viewport& vp, const Shp& sprites, const WideDeckView& view, const ColourRemap& remap) {
  const SpriteFrame& fa = sprites.frame(static_cast<std::size_t>(view.frame));
  const SpriteFrame& fb = sprites.frame(static_cast<std::size_t>(view.next));
  const auto lerp = [&](int p, int q) {
    return view.warp ? (p * (kDeckLevels - view.weight) + q * view.weight) / kDeckLevels : p;
  };
  // The outline: columns left of the hotspot as drawn, from it rightwards,
  // rows above it and from it downwards.
  const auto side = [&](const SpriteFrame& f, bool left) { return left != view.mirror ? -f.x0 : f.x1 + 1; };
  const int left = lerp(side(fa, true), side(fb, true));
  const int right = lerp(side(fa, false), side(fb, false));
  const int top = lerp(-fa.y0, -fb.y0);
  const int bottom = lerp(fa.y1 + 1, fb.y1 + 1);
  // The outline's part inside the clip rectangle, from the hotspot.
  const ClipRect clip = vp.clip();
  const int dx0 = std::max(-left, clip.left - vp.x0 - view.x);
  const int dx1 = std::min(right - 1, clip.right - vp.x0 - view.x);
  const int dy0 = std::max(-top, clip.top - vp.y0 - kDeckY);
  const int dy1 = std::min(bottom - 1, clip.bottom - vp.y0 - kDeckY);
  if (dx0 > dx1 || dy0 > dy1) {
    return;
  }
  struct Source {
    DecodedFrame pixels;
    int left;   // columns left of the hotspot, as drawn (mirrored or not)
    int right;  // from the hotspot rightwards
    int top;    // rows above the hotspot
    int bottom;
    bool mirror;
  };
  const auto source = [&](const SpriteFrame& f) {
    return Source{f.decode(), side(f, true), side(f, false), -f.y0, f.y1 + 1, view.mirror};
  };
  const Source a = source(fa);
  const Source b = source(fb);
  // The pixel of `s` at (`dx`, `dy`) from the hotspot of the outline, each
  // side scaled to the frame's own; nothing when transparent.
  const auto sample = [&](const Source& s, int dx, int dy) -> std::optional<std::uint8_t> {
    int sx = dx < 0 ? -1 - (-1 - dx) * s.left / std::max(left, 1) : dx * s.right / std::max(right, 1);
    const int sy = dy < 0 ? -1 - (-1 - dy) * s.top / std::max(top, 1) : dy * s.bottom / std::max(bottom, 1);
    // To the frame's pixels: its column 0 is the leftmost as drawn.
    sx += s.left;
    if (s.mirror) {
      sx = s.left + s.right - 1 - sx;
    }
    const int py = sy + s.top;
    if (!s.pixels.image.contains(sx, py) || s.pixels.opaque.at(sx, py) == 0) {
      return std::nullopt;
    }
    return s.pixels.image.at(sx, py);
  };
  for (int dy = dy0; dy <= dy1; ++dy) {
    for (int dx = dx0; dx <= dx1; ++dx) {
      const int x = view.x + dx;
      const int y = kDeckY + dy;
      const bool second = dither_level(x, y) < view.weight;
      if (const std::optional<std::uint8_t> c = sample(second ? b : a, dx, dy)) {
        vp.bitmap->set(vp.x0 + x, vp.y0 + y, remap[*c]);
      }
    }
  }
}

// Returns the column of the deck's axis for look bearing `b` (tenths) off
// the axis at `base` (0 the bow, 1800 the stern), in a `width`-wide view:
// the original's half field of view of a quarter of the width at the near
// distance, so that the deck moves faster than the world.
std::int32_t deck_axis_column(std::int32_t b, std::int32_t base, int width) {
  // The half field of view and the pixels per tenth of a degree of the
  // 640-wide view (half width 320 at the 460-px focal distance; wider for
  // the wide screen).
  const std::int32_t half_width = width / 2;
  const std::int32_t half_fov = fx::heading_from_delta((half_width << 16) >> 1, kNearZ << 16).tenths();
  const std::int32_t px_per_tenth = fx::int64_div(static_cast<std::int64_t>(half_width << 16) << 16, half_fov << 16);
  std::int32_t offset = fx::angle_sub(Angle10(base), fx::angle_sub(Angle10(b), Angle10(half_fov))).tenths();
  if (offset > 1800) {
    offset -= 3600;
  }
  const std::int64_t product = static_cast<std::int64_t>(offset) * 65536 * px_per_tenth + 0x8000;
  const auto x16 = static_cast<std::int32_t>(product >> 16);
  return x16 < 0 ? -(-x16 >> 16) : x16 >> 16;
}

}  // namespace

std::optional<DeckView> bridge_deck_view(Angle10 bearing, int width) {
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
  DeckView v;
  v.x = deck_axis_column(b, mode == 1 ? 0 : 1800, width);
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

WideDeckView bridge_wide_deck_view(Angle10 bearing, int width, DeckTransition transition) {
  const std::int32_t b = bearing.normalized().tenths();
  const bool aft = b > 900 && b < 2700;
  // Degrees off the axis, and the side looked at as the original mirrors.
  const std::int32_t rel = fx::angle_sub(Angle10(b), Angle10(aft ? 1800 : 0)).tenths();
  const bool right = rel < 1800;
  const std::int32_t off = right ? rel : 3600 - rel;
  WideDeckView v;
  v.mirror = right;
  const std::int32_t base = aft ? 9 : 0;
  v.frame = base + std::min(off / 150, 8);
  v.next = base + std::min(off / 150 + 1, 8);
  v.weight = transition == DeckTransition::kStepped ? 0 : off % 150 * kDeckLevels / 150;
  if (transition == DeckTransition::kWarp) {
    v.weight = v.weight * 2 < kDeckLevels ? 0 : kDeckLevels;
  }
  v.warp = transition != DeckTransition::kStepped;
  // The original's moving deck, sliding on out of the view.
  v.x = deck_axis_column(b, aft ? 1800 : 0, width);
  return v;
}

BridgeStation::BridgeStation(AttackServices& services) : s_(services) {}

BridgeStation::~BridgeStation() = default;

void BridgeStation::open() {
  // bridge_screen (0x1773c)
  const Install& install = s_.install();
  background_ = load_pcx(install, "ART/BRIDGE.PCX");
  // The 640 columns of the original in the middle of the screen (wider for
  // the wide screen), the view across its whole width.
  const int width = s_.screen().width();
  origin_ = (width - kScreenWidth) / 2;
  if (origin_ > 0) {
    Image8 wide(width, background_.image.height(), 0);
    wide.blit(background_.image, origin_, 0);
    background_.image = std::move(wide);
  }
  s_.screen().blit(background_.image, 0, 0);
  s_.palette() = background_.palette;
  pcx_palette_ = background_.palette;
  s_.shade_palette(&pcx_palette_);
  deck_ = Shp::load(install, "ART/BRIDGE.SHP");

  // bridge_init_dialogs (0x1745c): the two halves of the view train left
  // and right (',' / '.'); Alt-F toggles the frame rate display.
  dialog_ = std::make_unique<ui::Dialog>(s_.gui(), 0, 0, width - 1, 0x1df, ui::kDialogNoNavigation, &s_.screen());
  add_train_area(
      *dialog_, 0, 0, width / 2 - 1, 199, 0x332c, [this](std::int32_t step) { turn(step, -1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateLeftFast) : s_.set_default_cursor(); });
  add_train_area(
      *dialog_, width / 2, 0, width - 1, 199, 0x342e, [this](std::int32_t step) { turn(step, 1); },
      [this](bool on) { on ? s_.set_cursor(ui::CursorId::kRotateRightFast) : s_.set_default_cursor(); });
  dialog_->open();
  if (s_.features.drag_rotate) {
    drag_ = std::make_unique<ViewDrag>(view_bearing_hooks(s_, nullptr, [this] { return dialog_.get(); }));
    drag_->set_regions({{0, 0, kBridgeViewWidth - 1, kViewRows - 1}});
  }

  // bridge_load_nav (0x17a29): the menu 0x11667c (conn, TBT, deck gun),
  // BRDGNAV.SHP under the view and the nav panel.
  CommandBarMenu menu;
  menu.items = {{2, 3, [this] { s_.go_to(StationId::kConn); }},
                {0xc, 0xd, [this] { s_.go_to(StationId::kTbt); }},
                {0x14, 0x15, [this] { s_.go_to(StationId::kDeckGun); }}};
  s_.host().open_command_bar(menu);
  nav_ = Shp::load(install, "ART/BRDGNAV.SHP");
  draw_sprite_clipped(Viewport::full(s_.screen()), nav_->frame(0), origin_, 200);
  if (origin_ > 0) {
    draw_strip_ends();
  }
  nav_panel_ = s_.host().make_nav_panel(PanelSite::kBridge);
  if (nav_panel_) {
    nav_panel_->open();
  }

  render_mark_ = s_.gui().ticks();
  tape_bearing_ = -1;
  s_.host().fade_out();
}

void BridgeStation::event(const ui::Event& event) {
  if (drag_ && drag_->event(event, s_.gui().ticks())) {
    return;
  }
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
  if (drag_) {
    drag_->update(s_.gui().ticks());
  }
  const std::int32_t bearing = mission.player_combat().view_bearing.normalized().tenths();
  if (first_frame() || s_.gui().ticks() - render_mark_ > 5 || tape_bearing_ != bearing) {
    render_mark_ = s_.gui().ticks();
    render_view();
  }
  // bridge_draw_bearing_strip (0x17de7)
  if (tape_bearing_ != bearing) {
    tape_bearing_ = bearing;
    draw_bearing_tape(s_.screen(), background_.image, s_.bearing_font(), Angle10(bearing), origin_ + kTapeX0,
                      kTapeY0, origin_ + kTapeX1, kTapeY1, kTapeColour, nav_digit_colours(NavPanelSite::kBridge));
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
  drag_.reset();
  dialog_->close();
  dialog_.reset();
  deck_.reset();
  scratch_ = Image8();
}

void BridgeStation::render_view() {
  // bridge_render_view (0x17ac6)
  Mission& mission = s_.mission();
  const Angle10 bearing = mission.player_combat().view_bearing;
  const int width = s_.screen().width();
  const ViewEye eye = bridge_eye(mission.player(), bearing, width);
  s_.render_world(eye);
  const Viewport window = s_.world_window(0, 0, width - 1, kViewRows - 1);
  const ColourRemap remap = gui_colour_table();
  if (width > kBridgeViewWidth) {
    // The wide bridge (not in the original): every frame, blended.
    draw_wide_deck(window, *deck_, bridge_wide_deck_view(bearing, width, kDeckTransition), remap);
  } else if (const std::optional<DeckView> deck = bridge_deck_view(bearing, width)) {
    const SpriteFrame& frame = deck_->frame(static_cast<std::size_t>(deck->frame));
    sprite_draw_transformed(window, frame, deck->x, kDeckY, scratch_, Angle10(0),
                            Fixed16::from_int(deck->mirror ? -1 : 1), Fixed16::from_int(1), kSpriteRemap, &remap);
  }
  s_.blit_to_screen(window, 0, 0);
}

void BridgeStation::draw_strip_ends() {
  Image8& screen = s_.screen();
  Image8 strip(kScreenWidth, kBarTop - kStripTop);
  for (int y = 0; y < strip.height(); ++y) {
    for (int x = 0; x < kScreenWidth; ++x) {
      strip.set(x, y, screen.at(origin_ + x, kStripTop + y));
    }
  }
  const PanelEnds ends =
      make_panel_ends(strip, strip, origin_, screen.width() - origin_ - kScreenWidth, kStripEnds);
  screen.blit(ends.left, 0, kStripTop);
  screen.blit(ends.right, origin_ + kScreenWidth, kStripTop);
}

void BridgeStation::turn(std::int32_t step, int sign) {
  // bridge_turn_left / bridge_turn_right: also stops the TDC bearing follow.
  PlayerCombat& combat = s_.mission().player_combat();
  combat.view_bearing = train_bearing(combat.view_bearing, step, sign);
  combat.tdc.toggle_bearing_follow(true);
}

}  // namespace sh

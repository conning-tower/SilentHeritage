#include "editor/chart_screen.h"

#include <algorithm>
#include <utility>

#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "editor/toolbox.h"
#include "game/stations/chart.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "sim/line_of_sight.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

// The panels' dialogs (sbchart_init_dialogs).
constexpr int kToolboxW = 297;
constexpr int kToolboxH = 338;
constexpr int kModePanelW = 297;
constexpr int kModePanelH = 134;
constexpr int kCityPanelH = 300;
constexpr std::array<ChartPanelPosition, 5> kPanelStart{{{0, 121}, {22, 121}, {22, 121}, {22, 121}, {22, 121}}};

// SBCHART.SHP frames.
constexpr std::size_t kScaleRuler = 0;
constexpr std::size_t kMemoryGauge = 1;
constexpr std::size_t kZoomIn = 2;
constexpr std::size_t kZoomOut = 4;
constexpr std::size_t kCentre = 6;
constexpr std::size_t kRemoveIcon = 8;
constexpr std::size_t kShipIcon = 10;
constexpr std::size_t kAircraftIcon = 12;
constexpr std::size_t kAirstrikeIcon = 14;
constexpr std::size_t kPhotoReconIcon = 16;
constexpr std::size_t kPlayerMark = 0x14;
constexpr std::size_t kSubMark = 0x15;
constexpr std::size_t kShipMark = 0x16;
constexpr std::size_t kAircraftMark = 0x17;
constexpr std::size_t kAirstrikeMark = 0x18;
constexpr std::size_t kPhotoReconMark = 0x19;
constexpr std::size_t kConvoyLeaderIcon = 0x1a;
constexpr std::size_t kConvoyIcon = 0x1b;
constexpr std::size_t kBaseIcon = 0x2b;
// SHIPICON.SHP frames of the player (surfaced, submerged).
constexpr std::size_t kPlayerSurfaced = 1;
constexpr std::size_t kPlayerSubmerged = 2;

// The SBCURSOR.SHP frames (`chart_view_init` arguments 1 and 2, 4 over an
// object).
constexpr std::size_t kCursorPointer = 1;
constexpr std::size_t kCursorDrag = 2;
constexpr std::size_t kCursorObject = 4;

// Colours.
constexpr std::uint8_t kCourseColour = 0xb;
constexpr std::uint8_t kSelectedCourse = 0x51;
constexpr std::uint8_t kSelectedPath = 0x53;
constexpr std::uint8_t kSightingCircle = 0x52;
constexpr std::uint8_t kRadarCircle = 0;
constexpr std::uint8_t kLeaderCross = 0x51;
constexpr std::uint8_t kBracketOutline = 0;
constexpr std::uint8_t kBracket = 0x3f;
constexpr std::uint8_t kStatusText = 9;

// The class icons scale with the zoom: zoom / 300.
constexpr std::int32_t kIconScaleDivisor = 300 << 16;
// The yards of the range circles to world units.
constexpr std::int32_t kUnitsPerYard = 0x30;
// BaseRecord flags: the base is drawn on the chart (+99 bit 6).
constexpr std::uint32_t kBaseOnChart = 0x40;
// BaseRecord flags: a city (bit 0) and a naval base (bit 1).
constexpr std::uint32_t kBaseCity = 0x01;
constexpr std::uint32_t kBaseNaval = 0x02;

// SE strings (sbchart).
namespace str {
constexpr std::uint32_t kZoomMessage = 0x920c2;   // "Z-key zooms in, X-key zooms out"
constexpr std::uint32_t kFeet = 0x920ee;          // " ft"
constexpr std::uint32_t kMenuHint = 0x9210b;      // "Press ESC/Right Mouse Button for Menu Bar"
constexpr std::uint32_t kSelectConvoy = 0x92141;  // "Select CONVOY "
constexpr std::uint32_t kSelect = 0x92168;        // "Select "
constexpr std::uint32_t kSelectCity = 0x92188;    // "Select "
constexpr std::uint32_t kToolbox = 0x91e5c;
constexpr std::uint32_t kPlotCourse = 0x91f8c;
constexpr std::uint32_t kSelectACity = 0x91fad;
constexpr std::uint32_t kRemoveObject = 0x91fcb;
constexpr std::uint32_t kDone = 0x91f9a;
constexpr std::uint32_t kClear = 0x91f9f;
constexpr std::uint32_t kExit = 0x91fa8;
constexpr std::uint32_t kCancel = 0x91fdb;
constexpr std::uint32_t kSelectButton = 0x91fbd;  // "SELECT" (SELECT A CITY)
constexpr std::uint32_t kCityCancel = 0x91fc4;    // "CANCEL" (SELECT A CITY)
constexpr std::uint32_t kCurrent = 0x91fe2;
constexpr std::uint32_t kPrevious = 0x91e66;  // glyph 0x1c
constexpr std::uint32_t kNext = 0x91e68;      // glyph 0x1d
constexpr std::uint32_t kAddSub = 0x9235e;
constexpr std::uint32_t kAddShip = 0x92366;
constexpr std::uint32_t kSelectPoint = 0x92393;
constexpr std::uint32_t kPickConvoy = 0x9227e;
constexpr std::uint32_t kPickConvoy2 = 0x92298;
constexpr std::uint32_t kSelectRadius = 0x9228a;
constexpr std::uint32_t kTitleOpen = 0x9247f;   // "*** "
constexpr std::uint32_t kTitleClose = 0x9249c;  // " ***"
constexpr std::uint32_t kPercent = 0x92c7d;
constexpr std::uint32_t kRemove = 0x92209;    // "REMOVE "
constexpr std::uint32_t kQuestion = 0x92229;  // "?"
constexpr std::uint32_t kWarning = 0x9226a;
constexpr std::uint32_t kRemoveLine2 = 0x9223e;  // "Are you sure you want to remove this object"
constexpr std::uint32_t kRemoveLine3 = 0x9222b;  // "from the scenario?"
constexpr std::uint32_t kLeaderWarning = 0x92c69;
constexpr std::uint32_t kLeaderLine1 = 0x92c53;   // "REMOVE CONVOY LEADER?"
constexpr std::uint32_t kLeaderLine2 = 0x92c29;   // "Convoy members and escorts still"
constexpr std::uint32_t kLeaderLine3 = 0x92c09;   // "will have their orders changed."
constexpr std::uint32_t kNoRemovable = 0x9243d;   // "No removable objects found"
constexpr std::uint32_t kCannotDelete = 0x92458;  // "CANNOT DELETE OBJECT"
constexpr std::uint32_t kError = 0x9246d;
constexpr std::uint32_t kEmpty = 0x923dd;
constexpr std::uint32_t kLoc = 0x922c9;    // "Loc: "
constexpr std::uint32_t kComma = 0x922e7;  // ", "
constexpr std::uint32_t kSpace = 0x9231a;  // " "
constexpr std::uint32_t kNavalBase = 0x92328;
constexpr std::uint32_t kPort = 0x9233f;
constexpr std::uint32_t kPrompts = 0x9e70c;    // 9 entries, by `Prompt`
constexpr std::uint32_t kOwners = 0x98260;     // "", "Japanese", "American"
constexpr std::uint32_t kCitySizes = 0x9826c;  // "Fishing" .. "Large"
}  // namespace str

constexpr std::size_t kOwnerCount = 3;
constexpr std::size_t kCitySizeCount = 4;

// `stricmp`: <0, 0, >0 comparing ASCII case-insensitively.
int stricmp(std::string_view a, std::string_view b) {
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
  for (std::size_t i = 0;; ++i) {
    const int ca = i < a.size() ? static_cast<unsigned char>(lower(a[i])) : 0;
    const int cb = i < b.size() ? static_cast<unsigned char>(lower(b[i])) : 0;
    if (ca != cb || ca == 0) {
      return ca - cb;
    }
  }
}

// Applies the colours `first`..`last` of `frame`'s own palette to `pal`.
void apply_frame_colours(const SpriteFrame& frame, VgaPalette& pal, int first, int last) {
  VgaPalette copy = pal;
  sprite_apply_palette(frame, copy);
  std::copy(copy.begin() + 3 * first, copy.begin() + 3 * (last + 1), pal.begin() + 3 * first);
}

// The inclusive rectangle of `frame` drawn with its hotspot at (x, y)
// (`sprite_frame_bbox`).
ui::Rect frame_box(const SpriteFrame& frame, int x, int y) {
  return {x + frame.x0, y + frame.y0, x + frame.x1, y + frame.y1};
}

}  // namespace

VgaPalette se_chart_palette(const Install& install, const World& world, bool started_by_game) {
  VgaPalette pal{};
  if (started_by_game) {
    pal = load_pcx(install, "ART/M8.PCX").palette;
  }
  const Shp sbchart = Shp::load(install, "ART/SBCHART.SHP");
  const Shp intro = Shp::load(install, "ART/SBINTRO.SHP");
  const Shp icons = Shp::load(install, "ART/SHIPICON.SHP");
  sprite_apply_palette(sbchart.frame(0), pal);
  std::ranges::copy(world.chart_dac, pal.begin() + 3 * kChartDacFirstIndex);
  sprite_apply_palette(intro.frame(0), pal);
  sprite_apply_palette(sbchart.frame(0), pal);
  apply_frame_colours(icons.frame(0), pal, 16, 63);
  return pal;
}

std::vector<WorldPoint> chart_circle_points(WorldPoint centre, std::int32_t radius) {
  // chart_build_circle_points (0x13015): the quadrant signs and the cosine /
  // sine swap of table 0x982b4.
  struct Quadrant {
    std::int32_t sx, sy;
    bool swap;
  };
  constexpr std::array<Quadrant, 4> kQuadrants{{{1, 1, false}, {-1, 1, true}, {-1, -1, false}, {1, -1, true}}};
  std::vector<WorldPoint> points;
  if (radius == 0) {
    return points;
  }
  std::array<std::int32_t, 15> c{};
  std::array<std::int32_t, 15> s{};
  for (std::size_t k = 0; k < c.size(); ++k) {
    const fx::SinCos sc = fx::angle_sincos(Angle10(static_cast<std::int32_t>(k) * 60));
    c[k] = fx::mul(radius, sc.cos);
    s[k] = fx::mul(radius, sc.sin);
  }
  for (const Quadrant& q : kQuadrants) {
    const auto& xs = q.swap ? s : c;
    const auto& ys = q.swap ? c : s;
    for (std::size_t k = 0; k < c.size(); ++k) {
      points.push_back({fx::wadd(centre.x, fx::imul(xs[k], q.sx)), fx::wadd(centre.y, fx::imul(ys[k], q.sy))});
    }
  }
  return points;
}

// --- Construction
// --------------------------------------------------------------

ChartScreen::ChartScreen(ChartScreenEnv env, Scenario& scenario, const EditorOptions& options, ChartHooks hooks,
                         bool started_by_game)
    : env_(env),
      scenario_(scenario),
      options_(options),
      hooks_(std::move(hooks)),
      started_by_game_(started_by_game),
      ship_icons_(Shp::load(env.install, "ART/SHIPICON.SHP")),
      popup_(env.gui, env.font1),
      panel_pos_(kPanelStart),
      panels_(env.gui, ui::Rect{0, 0, kViewX1, kViewY1},
              ChartPanelHost::Hooks{.hover = [this](bool over) { panel_hover(over); },
                                    .mouse_move = [this](int x, int y) { mouse_moved(x, y); },
                                    .cursor =
                                        [this](ChartCursor c) {
                                          env_.gui.mouse().set_cursor(
                                              env_.cursors, c == ChartCursor::kDrag ? kCursorDrag : kCursorPointer);
                                        }}) {
  build_view();
  add_panels();
  toolbox_ = std::make_unique<Toolbox>(*this, env_, scenario_, hooks_);
}

ChartScreen::~ChartScreen() = default;

void ChartScreen::build_view() {
  // sb_setup_world (0x45259): the view at scale and zoom 1, centred on the
  // chart.
  const World& world = scenario_.world().world();
  view_.emplace(world, 0, 0, kViewX1, kViewY1);
  view_->set_zoom(0x10000);
  const std::array<std::int32_t, 2> home = view_home_center(world);
  view_->set_center(home[0], home[1]);
}

Viewport ChartScreen::view_viewport() {
  return {&env_.gui.screen(), view_->x0(), view_->y0(), view_->x1(), view_->y1()};
}

void ChartScreen::set_cursor(std::size_t frame) {
  // chart_set_cursor (0x43c09)
  env_.gui.mouse().set_cursor(env_.cursors, frame);
  cursor_ = frame;
}

// --- Frame
// -----------------------------------------------------------------------

void ChartScreen::open() {
  // sbchart_open (0x3b668) and chart_view_init (0x113b4)
  Image8& screen = env_.gui.screen();
  ui::gui_fill_rect(Viewport::full(screen), 0, 0, screen.width(), screen.height(), 0);
  env_.palette = se_chart_palette(env_.install, scenario_.world().world(), started_by_game_);
  center_on_player();
  dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, 0, kViewX1 + 1, kViewY1 + 1, 0, &screen);
  dialog_->emplace<ui::Area>(
      0, 0, kViewX1, kViewY1,
      [this](ui::Dialog& d, ui::Area& a, ui::Msg msg, int, const ui::Event& e) {
        // chart_view_on_click (0x1251e)
        if (msg != ui::Msg::kPress) {
          return 0;
        }
        const std::int32_t x = e.x - (d.x() + a.x0);
        const std::int32_t y = e.y - (d.y() + a.y0);
        if (!panels_.locked() && zoom_box_.is_open()) {
          // sbchart_zoom_box_center_cb (0x3c313) with chart_view_map_to_screen.
          const std::array<std::int32_t, 2> c = view_->screen_to_chart(x, y);
          view_->set_zoom(zoom_box_.value() << 16);
          view_->set_center(c[0], c[1]);
          zoom_box_.free_magnifier(env_.gui.screen());
          zoom_box_.close();
          panels_.deactivate();
          panels_.open_panel();
        } else {
          click(x, y);
        }
        return 0;
      },
      nullptr);
  dialog_->emplace<ui::Area>(
      -1, -1, -1, -1, nullptr,
      [this](ui::Dialog&, ui::Area&, int, const ui::Event&) {
        zoom_in();
        return 0;
      },
      0xff5a);
  dialog_->emplace<ui::Area>(
      -1, -1, -1, -1, nullptr,
      [this](ui::Dialog&, ui::Area&, int, const ui::Event&) {
        zoom_out();
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
  view_suspended_ = false;
  toolbox_reopen_ = false;
  popup_owner_ = nullptr;
  objects_drawn_ = false;
  select_on_open_ = nullptr;
  last_requested_ = panels_.requested();
  course_line_.forget();
  set_click_mode(ClickMode::kSelect);
}

void ChartScreen::close() {
  // sbchart_close (0x3bf93) and chart_view_close (0x1159e)
  zoom_box_.free_magnifier(env_.gui.screen());
  dialog_->close();
  dialog_.reset();
  panels_.close_panel();
  popup_.close();
}

void ChartScreen::world_changed() {
  build_view();
  env_.palette = se_chart_palette(env_.install, scenario_.world().world(), started_by_game_);
  cities_.clear();
  dirty_ = true;
}

void ChartScreen::handle_event(const ui::Event& event) {
  // sbchart_handle_event (0x3c03a)
  if (!zoom_box_.is_open()) {
    const bool cancel = (event.is_key() && event.key == ui::kKeyEsc) || (event.flags & ui::kEventRightUp) != 0;
    if (panels_.requested() > 0 && cancel) {
      if (panel() == EditorPanel::kPick && !pick_exit_) {
        return;
      }
      if (panel() != EditorPanel::kSelectCity) {
        finish_mode();
      } else {
        cancel_city();
      }
      return;
    }
    if (hooks_.menu_event && hooks_.menu_event(event)) {
      set_cursor(cursor_);
      return;
    }
  }
  // chart_view_handle_event (0x11604)
  dialog_->handle_event(event);
  panels_.event(event);
}

void ChartScreen::process() {
  // sbchart_process (0x3c0eb) with chart_view_process (0x1162f)
  dialog_->process();
  panels_.process();
  if (toolbox_reopen_) {
    toolbox_reopen_ = false;
    close_toolbox();
    open_toolbox();
  }
}

void ChartScreen::update(bool first) {
  // sbchart_update (0x3c13f)
  retired_dialogs_.clear();
  view_update();
  hover_poll();
  const std::size_t count = scenario_.objects().size();
  if (count != object_count_) {
    object_count_ = count;
    if (prev_button_ != nullptr) {
      prev_button_->disabled = count < 2;
      prev_button_->dirty = true;
      next_button_->disabled = count < 2;
      next_button_->dirty = true;
    }
  }
  last_requested_ = panels_.requested();
  if (first) {
    if (hooks_.fade_in) {
      hooks_.fade_in(env_.palette);
    }
    set_cursor(kCursorPointer);
    objects_drawn_ = true;
    overlay_redraw_ = true;
  }
}

// --- The view
// ----------------------------------------------------------------------

void ChartScreen::view_update() {
  // chart_view_update (0x11661)
  panels_.update();
  const int active = panels_.active();
  panels_.switch_panel();
  if (panels_.active() != active) {
    // chart_view_switch_panel also clears the zoom box's hold (0xa473c).
    view_suspended_ = false;
  }
  const bool locked = panels_.locked();
  panels_.handle_mouse();
  if (locked && !panels_.locked()) {
    // The end of a drag clears it too.
    view_suspended_ = false;
  }
  if (!panels_.locked() && !view_suspended_) {
    chart_edge_scroll(*view_, env_.gui.mouse(), panels_.hovered(), dirty_);
  }
  if (dirty_) {
    redraw_map();
  } else if (!zoom_box_.is_open()) {
    // chart_view_poll_redraw (0x11c9f) with sbchart_view_poll (0x3c2ed).
    const bool requested = overlay_redraw_;
    overlay_redraw_ = false;
    if (requested && !force_overlays_) {
      course_line_.erase();
    }
    if (requested || force_overlays_) {
      redraw_overlays();
      force_overlays_ = false;
    }
  }
  if (!panels_.locked() && zoom_box_.is_open()) {
    panels_.close_panel();
    close_popup();
    env_.gui.mouse().set_cursor(env_.cursors, kCursorPointer);
    ui::Mouse& mouse = env_.gui.mouse();
    const int x = mouse.x();
    const int y = mouse.y();
    course_line_.erase();
    const int cx = std::clamp(x, view_->x0(), view_->x1());
    const int cy = std::clamp(y, view_->y0(), view_->y1());
    if (cx != x || cy != y) {
      mouse.update(cx, cy, mouse.buttons(), 0);
    }
    zoom_box_.track(env_.gui.screen(), *view_, cx, cy);
  }
}

void ChartScreen::redraw_map() {
  // chart_view_redraw_if_dirty (0x119ba): the map, a clean copy of it, the
  // overlays, the panel and the popup.
  dirty_ = false;
  Image8& screen = env_.gui.screen();
  view_->draw(screen);
  clean_map_ = Image8(view_->x1() - view_->x0() + 1, view_->y1() - view_->y0() + 1);
  for (int y = 0; y < clean_map_.height(); ++y) {
    for (int x = 0; x < clean_map_.width(); ++x) {
      clean_map_.set(x, y, screen.at(view_->x0() + x, view_->y0() + y));
    }
  }
  draw_contents();
  repaint_panels();
}

void ChartScreen::redraw_overlays() {
  // chart_view_redraw_overlays (0x12148): the overlays drawn again over
  // the clean map; the original restores only their rectangles.
  env_.gui.screen().blit(clean_map_, view_->x0(), view_->y0());
  course_line_.forget();
  draw_contents();
  repaint_panels();
}

void ChartScreen::repaint_panels() {
  popup_.repaint();
  panels_.redraw();
}

void ChartScreen::zoom_in() {
  // chart_zoom_box_grow (0x12bb3): up to `city_zoom_` (8 while picking a
  // city); a box holds the edge scroll.
  if (panels_.locked()) {
    return;
  }
  zoom_box_.grow(*view_, city_zoom_);
  if (zoom_box_.is_open()) {
    view_suspended_ = true;
  }
}

void ChartScreen::zoom_out() {
  // chart_zoom_box_shrink (0x12c18)
  if (panels_.locked()) {
    return;
  }
  if (!zoom_box_.is_open()) {
    dirty_ = zoom_box_.shrink(*view_);
  } else {
    zoom_box_.shrink(*view_);
  }
  if (zoom_box_.is_open()) {
    view_suspended_ = true;
  }
}

void ChartScreen::center_on_selection() {
  // sbchart_button_center_on_object (0x3e475)
  if (view_suspended_) {
    return;
  }
  const WorldPoint p = scenario_.selected()->position(scenario_.object_env());
  const std::int32_t cx = view_->center_x();
  const std::int32_t cy = view_->center_y();
  const ChartPoint s = view_->world_to_screen(p.x, p.y);
  const std::array<std::int32_t, 2> c = view_->screen_to_chart(s.x, s.y);
  view_->set_center(c[0], c[1]);
  dirty_ = view_->center_x() != cx || view_->center_y() != cy;
}

void ChartScreen::center_on_player() {
  // sb_center_on_player (0x451f0)
  PlayerSub& player = scenario_.player();
  scenario_.select(&player);
  const ChartPoint s = view_->world_to_screen(player.rec.x, player.rec.y);
  const std::array<std::int32_t, 2> c = view_->screen_to_chart(s.x, s.y);
  view_->set_center(c[0], c[1]);
  dirty_ = true;
}

WorldPoint ChartScreen::screen_to_world(std::int32_t x, std::int32_t y) const {
  const std::array<std::int32_t, 2> w = view_->screen_to_world(x, y);
  return {w[0], w[1]};
}

std::optional<std::array<std::int32_t, 2>> ChartScreen::world_to_screen(WorldPoint p) const {
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(p.x, p.y, x, y)) {
    return std::nullopt;
  }
  return std::array<std::int32_t, 2>{x, y};
}

// --- Drawing
// ------------------------------------------------------------------------

void ChartScreen::draw_contents() {
  // sbchart_draw_contents (0x40e41)
  const Viewport vp = view_viewport();
  if (view_->zoom_int() < kBaseZoom) {
    // The bases drawn on the chart, at the zoom's scale.
    const SpriteFrame& icon = env_.sbchart.frame(kBaseIcon);
    Image8 scratch;
    bool ready = false;
    for (const BaseRecord& b : scenario_.world().bases()) {
      if ((b.flags & kBaseOnChart) == 0) {
        continue;
      }
      std::int32_t x = 0;
      std::int32_t y = 0;
      if (view_->point_on_screen(b.station.x, b.station.z, x, y)) {
        sprite_draw_transformed(vp, icon, x, y, scratch, Angle10(0), Fixed16::from_raw(view_->zoom()),
                                Fixed16::from_raw(view_->zoom()), ready ? kSpriteScratchReady : 0u);
        ready = true;
      }
    }
  }
  if (!objects_drawn_) {
    return;
  }
  course_line_.forget();
  panels_.force_mouse_move();
  const auto& objects = scenario_.objects();
  const EditorObject* selected = scenario_.selected();
  // Pass -1 (courses, circles), then pass 0 (icons), both in reverse list
  // order.
  for (std::size_t i = objects.size(); i-- > 0;) {
    draw_object(*objects[i], true, objects[i].get() == selected);
  }
  std::optional<std::size_t> boxed;
  for (std::size_t i = objects.size(); i-- > 0;) {
    draw_object(*objects[i], false, objects[i].get() == selected);
    if ((click_mode_ == ClickMode::kSelect || click_mode_ == ClickMode::kRemove) && objects[i].get() == selected) {
      boxed = i;
    }
  }
  if (view_->zoom_int() >= kIconZoom && options_.convoy_icons != 0 && selected != nullptr) {
    // The convoy icons of the selection's convoy.
    std::int32_t group = selected->convoy_group(true);
    if (group == 0) {
      group = selected->convoy_group(false);
    }
    if (group == 0) {
      group = selected->escorted_group();
    }
    if (group != 0) {
      for (std::size_t i = objects.size(); i-- > 0;) {
        const EditorObject& o = *objects[i];
        const WorldPoint p = o.position(scenario_.object_env());
        if (o.convoy_group(true) == group) {
          draw_convoy_icon(true, p);
        } else if (o.convoy_group(false) == group || o.escorted_group() == group) {
          draw_convoy_icon(false, p);
        }
      }
    }
  }
  if (boxed) {
    if (const std::optional<ui::Rect> r = hit_rect(*objects[*boxed])) {
      draw_selection_box(*r);
    }
  }
}

void ChartScreen::draw_object(const EditorObject& object, bool pass_courses, bool selected) {
  // Slot +0x08 of each class.
  if (const auto* p = dynamic_cast<const PlayerSub*>(&object)) {
    draw_player(*p, pass_courses);
  } else if (const auto* v = dynamic_cast<const Vessel*>(&object)) {
    draw_vessel(*v, pass_courses, selected);
  } else if (const auto* a = dynamic_cast<const Aircraft*>(&object)) {
    draw_aircraft(*a, pass_courses, selected);
  } else if (const auto* m = dynamic_cast<const SpecialMission*>(&object)) {
    draw_mission(*m, pass_courses);
  }
}

std::optional<ui::Rect> ChartScreen::draw_unit_icon(WorldPoint p, std::optional<std::size_t> icon, std::size_t mark,
                                                    std::int32_t heading) {
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(p.x, p.y, x, y)) {
    return std::nullopt;
  }
  const Viewport vp = view_viewport();
  if (view_->zoom_int() >= kIconZoom) {
    if (!icon) {
      return std::nullopt;
    }
    // The class icon, turned to the heading and scaled by zoom / 300.
    const SpriteFrame& f = ship_icons_.frame(*icon);
    const std::int32_t scale = fx::div(view_->zoom(), kIconScaleDivisor);
    Image8 scratch;
    sprite_draw_transformed(vp, f, x, y, scratch, Angle10(heading * 10), Fixed16::from_raw(scale),
                            Fixed16::from_raw(scale), 0);
    const std::int32_t half = fx::trunc(fx::mul(std::max(f.width(), f.height()) << 16, scale));
    return ui::Rect{x - half, y - half, x + half, y + half};
  }
  const SpriteFrame& f = env_.sbchart.frame(mark);
  draw_sprite_clipped(vp, f, x, y);
  return frame_box(f, x, y);
}

std::optional<ui::Rect> ChartScreen::unit_hit_rect(WorldPoint p, std::optional<std::size_t> icon,
                                                   std::size_t mark) const {
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(p.x, p.y, x, y)) {
    return std::nullopt;
  }
  if (view_->zoom_int() >= kIconZoom) {
    if (!icon) {
      return std::nullopt;
    }
    const SpriteFrame& f = ship_icons_.frame(*icon);
    const std::int32_t scale = fx::div(view_->zoom(), kIconScaleDivisor);
    const std::int32_t half = std::max(fx::trunc(fx::mul(std::max(f.width(), f.height()) << 16, scale)), 5);
    return ui::Rect{x - half, y - half, x + half, y + half};
  }
  ui::Rect r = frame_box(env_.sbchart.frame(mark), x, y);
  r.y0 -= 2;
  r.y1 += 2;
  return r;
}

std::optional<ui::Rect> ChartScreen::draw_player(const PlayerSub& p, bool pass_courses) {
  // sbplayer_draw (0x41232)
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(p.rec.x, p.rec.y, x, y)) {
    return std::nullopt;
  }
  if (pass_courses && options_.range_circles != 0) {
    const std::vector<std::int32_t> radii = range_circle_radii();
    for (std::size_t i = 0; i < radii.size(); ++i) {
      std::vector<WorldPoint> pts = chart_circle_points({p.rec.x, p.rec.y}, radii[i]);
      std::vector<std::int32_t> xs, ys;
      for (const WorldPoint& q : pts) {
        xs.push_back(q.x);
        ys.push_back(q.y);
      }
      xs.push_back(-1);
      const std::uint8_t c = i == 0 ? kSightingCircle : kRadarCircle;
      draw_route(static_cast<std::int32_t>(pts.size()), xs.data(), ys.data(), c, c);
    }
    return std::nullopt;
  }
  // Original quirk (harmless): with the range circles off, the courses
  // pass draws the icon too; the icon pass draws it again.
  const std::optional<std::size_t> cls = scenario_.data().find_sub(scn_text(p.rec.sub_class));
  const std::int32_t draft = cls ? static_cast<std::int32_t>(scenario_.data().tables.subs[*cls].draft_ft) : 0;
  return draw_unit_icon({p.rec.x, p.rec.y}, p.rec.depth > draft ? kPlayerSubmerged : kPlayerSurfaced, kPlayerMark,
                        p.rec.heading);
}

std::optional<ui::Rect> ChartScreen::draw_vessel(const Vessel& v, bool pass_courses, bool selected) {
  // sbship_draw (0x418f1) / sbsub_draw (0x42235)
  const WorldPoint pos = v.position(scenario_.object_env());
  if (!world_to_screen(pos)) {
    return std::nullopt;
  }
  const bool ship = v.kind == Vessel::Kind::kShip;
  if (pass_courses) {
    const ShipOrders orders = v.orders();
    const bool route =
        ship ? (orders == ShipOrders::kEscort || orders == ShipOrders::kPatrol || orders == ShipOrders::kConvoyLeader ||
                orders == ShipOrders::kConvoyMember || orders == ShipOrders::kFollowPath)
             : orders == ShipOrders::kFollowPath;
    if (route) {
      const Waypoints& w = v.waypoints();
      draw_route(w.count, w.x.data(), w.y.data(), selected ? kSelectedCourse : kCourseColour,
                 selected ? 0 : kCourseColour);
    }
    return std::nullopt;
  }
  const std::optional<std::size_t> cls = scenario_.data().find_ship(v.class_name());
  const std::optional<std::size_t> icon =
      cls ? std::optional<std::size_t>(scenario_.data().tables.ships[*cls].category) : std::nullopt;
  const std::optional<ui::Rect> r =
      draw_unit_icon(pos, icon, ship ? kShipMark : kSubMark, ship ? v.ship.heading : v.sub.heading);
  if (ship && view_->zoom_int() >= kIconZoom && v.ship.orders == ShipOrders::kConvoyLeader) {
    // A cross on the leader of the selection's convoy.
    const EditorObject* sel = scenario_.selected();
    if (sel != nullptr &&
        (sel->convoy_group(false) == v.ship.convoy_group || sel->escorted_group() == v.ship.convoy_group)) {
      draw_leader_cross(pos);
    }
  }
  return r;
}

std::optional<ui::Rect> ChartScreen::draw_aircraft(const Aircraft& a, bool pass_courses, bool selected) {
  // sbair_draw (0x42a85): at the first path point on Follow Path.
  const bool path = a.rec.mission == AircraftMission::kFollowPath;
  const WorldPoint pos = path ? WorldPoint{a.rec.waypoints.x[0], a.rec.waypoints.y[0]} : WorldPoint{a.rec.x, a.rec.y};
  const std::uint8_t colour = selected ? kSelectedPath : kCourseColour;
  const std::uint8_t marker = selected ? 0 : kCourseColour;
  if (pass_courses) {
    if (path) {
      draw_route(a.rec.waypoints.count, a.rec.waypoints.x.data(), a.rec.waypoints.y.data(), colour, marker);
    } else if (a.rec.orbit_radius > 0) {
      std::vector<WorldPoint> pts = chart_circle_points(pos, a.rec.orbit_radius);
      std::vector<std::int32_t> xs, ys;
      for (const WorldPoint& q : pts) {
        xs.push_back(q.x);
        ys.push_back(q.y);
      }
      xs.push_back(-1);
      draw_route(static_cast<std::int32_t>(pts.size()), xs.data(), ys.data(), colour, marker);
    }
    return std::nullopt;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(pos.x, pos.y, x, y)) {
    return std::nullopt;
  }
  const SpriteFrame& f = env_.sbchart.frame(kAircraftMark);
  draw_sprite_clipped(view_viewport(), f, x, y);
  return frame_box(f, x, y);
}

std::optional<ui::Rect> ChartScreen::draw_mission(const SpecialMission& m, bool pass_courses) {
  // sbmission_draw (0x42e5b): the mark on the target base.
  if (pass_courses || m.base() == -1) {
    return std::nullopt;
  }
  const WorldPoint pos = m.position(scenario_.object_env());
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(pos.x, pos.y, x, y)) {
    return std::nullopt;
  }
  const SpriteFrame& f = env_.sbchart.frame(m.type() == ObjectType::kAirstrike ? kAirstrikeMark : kPhotoReconMark);
  draw_sprite_clipped(view_viewport(), f, x, y);
  return frame_box(f, x, y);
}

void ChartScreen::draw_route(std::int32_t n, const std::int32_t* xs, const std::int32_t* ys, std::uint8_t colour,
                             std::uint8_t marker) {
  // sbchart_draw_route (0x4360c): the legs clipped in world units to 1000
  // pixels around the view, a 3x3 marker at both ends of each leg when the
  // colours differ.
  if (n <= 1) {
    return;
  }
  const bool markers = colour != marker;
  const WorldPoint hi = screen_to_world(1000, 1000);
  const WorldPoint lo = screen_to_world(-1000, -1000);
  const std::int32_t x_max = hi.x;
  const std::int32_t y_min = hi.y;
  const std::int32_t x_min = lo.x;
  const std::int32_t y_max = lo.y;
  const auto outcode = [&](std::int32_t x, std::int32_t y) {
    unsigned c = 0;
    if (y_max < y) {
      c |= 1;
    }
    if (y < y_min) {
      c |= 2;
    }
    if (x_max < x) {
      c |= 4;
    }
    if (x < x_min) {
      c |= 8;
    }
    return c;
  };
  struct Leg {
    std::int32_t x0, y0, x1, y1;
  };
  std::vector<Leg> legs;
  const auto max_legs = static_cast<std::size_t>(n) * 3;
  for (std::int32_t i = 1; i <= n; ++i) {
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    if (i == n) {
      if (xs[n] != -1) {
        continue;
      }
      x1 = xs[0];
      y1 = ys[0];
    } else {
      x1 = xs[i];
      y1 = ys[i];
    }
    std::int32_t x0 = xs[i - 1];
    std::int32_t y0 = ys[i - 1];
    bool accept = false;
    // Exact clipping ends within four clips; the wrapping arithmetic of
    // points far off the map (a damaged file) may not, so the leg is then
    // dropped.
    for (int clips = 0; clips <= 8; ++clips) {
      const unsigned c1 = outcode(x1, y1);
      const unsigned c0 = outcode(x0, y0);
      if (c0 == 0 && c1 == 0) {
        accept = true;
        break;
      }
      if ((c0 & c1) != 0) {
        break;
      }
      const unsigned c = c0 != 0 ? c0 : c1;
      const std::int32_t dx = fx::wsub(x1, x0);
      const std::int32_t dy = fx::wsub(y1, y0);
      std::int32_t x = 0;
      std::int32_t y = 0;
      if ((c & 1) != 0) {
        y = y_max;
        x = fx::wadd(x0, fx::int64_div(static_cast<std::int64_t>(dx) * fx::wsub(y_max, y0), dy));
      } else if ((c & 2) != 0) {
        y = y_min;
        x = fx::wadd(x0, fx::int64_div(static_cast<std::int64_t>(dx) * fx::wsub(y_min, y0), dy));
      } else if ((c & 4) != 0) {
        x = x_max;
        y = fx::wadd(y0, fx::int64_div(static_cast<std::int64_t>(dy) * fx::wsub(x_max, x0), dx));
      } else {
        x = x_min;
        y = fx::wadd(y0, fx::int64_div(static_cast<std::int64_t>(dy) * fx::wsub(x_min, x0), dx));
      }
      if (c == c0) {
        x0 = x;
        y0 = y;
      } else {
        x1 = x;
        y1 = y;
      }
    }
    if (accept && legs.size() < max_legs) {
      legs.push_back({x0, y0, x1, y1});
    }
  }
  const Viewport vp = view_viewport();
  const auto mark = [&](std::int32_t x, std::int32_t y) {
    ui::put_pixel(vp, x, y, colour);
    ui::put_pixel(vp, x - 1, y, marker);
    ui::put_pixel(vp, x + 1, y, marker);
    ui::draw_line(vp, x - 1, y - 1, x + 1, y - 1, marker);
    ui::draw_line(vp, x - 1, y + 1, x + 1, y + 1, marker);
  };
  for (const Leg& l : legs) {
    std::int32_t ax = 0, ay = 0, bx = 0, by = 0;
    view_->point_on_screen(l.x0, l.y0, ax, ay);
    view_->point_on_screen(l.x1, l.y1, bx, by);
    ui::draw_line(vp, ax, ay, bx, by, colour);
    if (markers) {
      mark(ax, ay);
      mark(bx, by);
    }
  }
}

void ChartScreen::draw_selection_box(ui::Rect r) {
  // sbchart_draw_selection_box (0x432c5): corner brackets 4 pixels out, a
  // 0x3f line between two black ones.
  r.x0 -= 4;
  r.y0 -= 4;
  r.x1 += 4;
  r.y1 += 4;
  const Viewport vp = view_viewport();
  const int a = r.x0 + 1;
  const int b = r.y0 + 1;
  const int c = r.x1 - 2;
  const int d = r.y1 - 1;
  const int len = std::max((c - a + 1) / 4, 3);
  const int e = b - 1;
  const int f = a + len;
  const int g = b + 1;
  const int h = c - len;
  const int i = d - 1;
  const int j = d + 1;
  const auto line = [&](int x0, int y0, int x1, int y1, std::uint8_t colour) {
    ui::draw_line(vp, x0, y0, x1, y1, colour);
  };
  line(a, e, f, e, kBracketOutline);
  line(a, b, f, b, kBracket);
  line(a, g, f, g, kBracketOutline);
  line(c, e, h, e, kBracketOutline);
  line(c, b, h, b, kBracket);
  line(c, g, h, g, kBracketOutline);
  line(a, i, f, i, kBracketOutline);
  line(a, d, f, d, kBracket);
  line(a, j, f, j, kBracketOutline);
  line(c, i, h, i, kBracketOutline);
  line(c, d, h, d, kBracket);
  line(c, j, h, j, kBracketOutline);
  line(a - 1, e, a - 1, j, kBracketOutline);
  line(a, b, a, d, kBracket);
  line(a + 1, g, a + 1, i, kBracketOutline);
  line(c - 1, g, c - 1, i, kBracketOutline);
  line(c, b, c, d, kBracket);
  line(c + 1, e, c + 1, j, kBracketOutline);
  line(f + 1, e, f + 1, g, kBracketOutline);
  line(h - 1, e, h - 1, g, kBracketOutline);
  line(f + 1, i, f + 1, j, kBracketOutline);
  line(h - 1, i, h - 1, j, kBracketOutline);
}

void ChartScreen::draw_convoy_icon(bool leader, WorldPoint p) {
  // sbchart_draw_convoy_icon (0x4357c)
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (view_->point_on_screen(p.x, p.y, x, y)) {
    draw_sprite_clipped(view_viewport(), env_.sbchart.frame(leader ? kConvoyLeaderIcon : kConvoyIcon), x, y);
  }
}

void ChartScreen::draw_leader_cross(WorldPoint p) {
  // sbchart_draw_leader_cross (0x43b76)
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (view_->point_on_screen(p.x, p.y, x, y)) {
    const Viewport vp = view_viewport();
    ui::draw_line(vp, x, y - 2, x, y + 2, kLeaderCross);
    ui::draw_line(vp, x - 2, y, x + 2, y, kLeaderCross);
  }
}

// --- Objects
// -------------------------------------------------------------------------

std::optional<ui::Rect> ChartScreen::hit_rect(const EditorObject& object) const {
  // Slot +0x0c of each class.
  if (const auto* p = dynamic_cast<const PlayerSub*>(&object)) {
    // sbplayer_hit_rect (0x41515): the submerged icon's size.
    return unit_hit_rect({p->rec.x, p->rec.y}, kPlayerSubmerged, kPlayerMark);
  }
  if (const auto* v = dynamic_cast<const Vessel*>(&object)) {
    // sbship_hit_rect (0x41c14) / sbsub_hit_rect (0x424d4)
    const std::optional<std::size_t> cls = scenario_.data().find_ship(v->class_name());
    const std::optional<std::size_t> icon =
        cls ? std::optional<std::size_t>(scenario_.data().tables.ships[*cls].category) : std::nullopt;
    return unit_hit_rect(v->position(scenario_.object_env()), icon,
                         v->kind == Vessel::Kind::kShip ? kShipMark : kSubMark);
  }
  std::optional<WorldPoint> pos;
  std::size_t mark = kAircraftMark;
  if (const auto* a = dynamic_cast<const Aircraft*>(&object)) {
    // sbair_hit_rect (0x42b9a)
    pos = a->rec.mission == AircraftMission::kFollowPath ? WorldPoint{a->rec.waypoints.x[0], a->rec.waypoints.y[0]}
                                                         : WorldPoint{a->rec.x, a->rec.y};
  } else if (const auto* m = dynamic_cast<const SpecialMission*>(&object)) {
    // sbmission_hit_rect (0x42f1a)
    if (m->base() == -1) {
      return std::nullopt;
    }
    pos = m->position(scenario_.object_env());
    mark = m->type() == ObjectType::kAirstrike ? kAirstrikeMark : kPhotoReconMark;
  }
  if (!pos) {
    return std::nullopt;
  }
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(pos->x, pos->y, x, y)) {
    return std::nullopt;
  }
  return frame_box(env_.sbchart.frame(mark), x, y);
}

EditorObject* ChartScreen::pick_object_at(std::int32_t x, std::int32_t y) {
  // sbchart_pick_object_at (0x3dab1)
  hovered_ = nullptr;
  std::size_t cursor = kCursorPointer;
  const auto& objects = scenario_.objects();
  for (std::size_t i = 0; i < objects.size(); ++i) {
    EditorObject& o = *objects[i];
    const std::optional<ui::Rect> r = hit_rect(o);
    if (click_mode_ == ClickMode::kPickConvoy && o.convoy_group(true) == 0 && o.escorted_group() == 0 &&
        o.convoy_group(false) == 0) {
      continue;
    }
    if ((i == 0 && click_mode_ == ClickMode::kRemove) || !r) {
      continue;
    }
    if (x < r->x0 || x > r->x1 || y < r->y0 || y > r->y1) {
      continue;
    }
    if (&o == scenario_.selected() && click_mode_ == ClickMode::kSelect) {
      continue;
    }
    cursor = kCursorObject;
    hovered_ = &o;
    break;
  }
  set_cursor(cursor);
  return hovered_;
}

std::vector<std::int32_t> ChartScreen::range_circle_radii() const {
  // sbplayer_draw's pass -1: the circles' radii are yards x 0x30.
  std::vector<std::int32_t> radii;
  const EditorObject* sel = scenario_.selected();
  const std::int32_t sighting = sel != nullptr ? sel->visual_range() : 0;
  if (sighting <= 0) {
    return radii;
  }
  radii.push_back(sighting * kUnitsPerYard);
  const std::int32_t radar = player_radar_range_yd(scenario_.header.equipment);
  if (radar > 0) {
    radii.push_back(radar * kUnitsPerYard);
  }
  return radii;
}

void ChartScreen::select(EditorObject& object) {
  close_toolbox();
  close_popup();
  scenario_.select(&object);
  panels_.force_mouse_move();
  open_toolbox();
}

void ChartScreen::next_object() {
  // sbchart_toolbox_next_object (0x3eae2)
  const auto& objects = scenario_.objects();
  const std::size_t i = scenario_.index_of(*scenario_.selected());
  close_toolbox();
  scenario_.select(objects[(i + 1) % objects.size()].get());
  open_toolbox();
  overlay_redraw_ = true;
  panels_.force_mouse_move();
}

void ChartScreen::previous_object() {
  // sbchart_toolbox_prev_object (0x3eb48)
  const auto& objects = scenario_.objects();
  const std::size_t i = scenario_.index_of(*scenario_.selected());
  close_toolbox();
  scenario_.select(objects[i == 0 ? objects.size() - 1 : i - 1].get());
  open_toolbox();
  overlay_redraw_ = true;
  panels_.force_mouse_move();
}

std::int32_t ChartScreen::vessel_draft(const Vessel& vessel) const {
  // The original reads past SHIPS.DAT for an unknown class; here 0.
  const std::optional<std::size_t> cls = scenario_.data().find_ship(vessel.class_name());
  return cls ? static_cast<std::int32_t>(scenario_.data().tables.ships[*cls].draft_ft) : 0;
}

// --- Clicks and modes
// -------------------------------------------------------------------

void ChartScreen::set_click_mode(ClickMode mode) {
  hovered_ = nullptr;
  previous_mode_ = click_mode_;
  click_mode_ = mode;
}

void ChartScreen::restore_click_mode() {
  hovered_ = nullptr;
  std::swap(click_mode_, previous_mode_);
}

void ChartScreen::click(std::int32_t x, std::int32_t y) {
  // sbchart_on_click (0x3c8fc)
  switch (click_mode_) {
    case ClickMode::kSelect:
      pick_object_at(x, y);
      if (hovered_ == nullptr) {
        // The vtable `move_to` slot: the screen point to the world first.
        scenario_.move_object(*scenario_.selected(), screen_to_world(x, y));
      } else {
        EditorObject& o = *hovered_;
        hovered_ = nullptr;
        select(o);
      }
      overlay_redraw_ = true;
      panels_.force_mouse_move();
      break;
    case ClickMode::kAddShip:
    case ClickMode::kAddSub:
      add_vessel_at(x, y);
      break;
    case ClickMode::kRemove:
      pick_object_at(x, y);
      remove_hovered();
      break;
    case ClickMode::kCourse:
    case ClickMode::kSubCourse:
      add_course_point(x, y, false);
      break;
    case ClickMode::kAircraftPath:
      add_aircraft_point(x, y);
      break;
    case ClickMode::kEscortCourse:
      add_course_point(x, y, true);
      break;
    case ClickMode::kPickConvoy:
      pick_object_at(x, y);
      pick_convoy_hovered();
      break;
    case ClickMode::kPatrolCenter:
      set_patrol_area(x, y, false);
      break;
    case ClickMode::kPatrolRadius:
      set_patrol_area(x, y, true);
      break;
    case ClickMode::kPickCity:
      if (pick_city_at(x, y)) {
        finish_mode();
      }
      break;
  }
}

void ChartScreen::add_vessel_at(std::int32_t x, std::int32_t y) {
  // sbchart_add_ship_at (0x3cd5f)
  const WorldPoint p = screen_to_world(x, y);
  if (!scenario_.can_place_vessel(add_class_, p)) {
    return;
  }
  scenario_.add_vessel(add_class_, p,
                       click_mode_ == ClickMode::kAddSub ? Vessel::Kind::kSubmarine : Vessel::Kind::kShip);
  // Editor limit (ADD SHIP, one placement per use): the mode ends with the
  // first vessel placed.
  finish_mode();
  scenario_.modified = true;
}

void ChartScreen::add_course_point(std::int32_t x, std::int32_t y, bool looped) {
  // sbchart_add_course_waypoint (0x3d035): water deeper than the draft, no
  // land on the leg from the last point (nor back to the start when
  // looped), a click within 10 pixels of the start closes an open course.
  auto* v = dynamic_cast<Vessel*>(scenario_.selected());
  if (v == nullptr) {
    return;
  }
  Waypoints& course = v->waypoints();
  const WorldPoint p = screen_to_world(x, y);
  const std::int32_t draft = vessel_draft(*v);
  const std::int32_t height = scenario_.world().floor().height_at(p.x, p.y);
  if (draft * -16 == height || draft * 16 > -height || course.count == Scenario::kMaxCoursePoints) {
    return;
  }
  Scenario::start_course(course, v->position(scenario_.object_env()));
  const TerrainGrid& grid = scenario_.world().grid();
  const auto last = static_cast<std::size_t>(course.count - 1);
  if (land_on_line(grid, {course.x[last], course.y[last]}, {p.x, p.y}).land) {
    return;
  }
  if (looped && land_on_line(grid, {p.x, p.y}, {course.x[0], course.y[0]}).land) {
    return;
  }
  bool closes = false;
  if (!looped && course.count > 2) {
    const std::optional<std::array<std::int32_t, 2>> at = world_to_screen(p);
    const std::optional<std::array<std::int32_t, 2>> start = world_to_screen({course.x[0], course.y[0]});
    // chart_point_on_screen leaves its outputs set off screen too.
    const std::array<std::int32_t, 2> a = at.value_or(std::array<std::int32_t, 2>{});
    const std::array<std::int32_t, 2> s = start.value_or(std::array<std::int32_t, 2>{});
    closes = fx::abs(s[0] - a[0]) < kLoopClosePixels && fx::abs(s[1] - a[1]) < kLoopClosePixels;
  }
  if (Scenario::add_course_point(course, p, looped, closes) == CourseClick::kAdded) {
    anchor_ = p;
  }
  if (course.count == Scenario::kMaxCoursePoints) {
    finish_mode();
  }
  prompt_number_ = course.count + 1;
  set_plot_buttons_disabled(course.count < 2);
  if (panel_dialog_ != nullptr && panel() == EditorPanel::kPlotCourse) {
    panel_dialog_->invalidate();
  }
  overlay_redraw_ = true;
  panels_.force_mouse_move();
  scenario_.modified = true;
}

void ChartScreen::add_aircraft_point(std::int32_t x, std::int32_t y) {
  // sbchart_add_aircraft_waypoint (0x3d2a2)
  auto* a = dynamic_cast<Aircraft*>(scenario_.selected());
  if (a == nullptr || !Scenario::add_aircraft_point(*a, screen_to_world(x, y))) {
    return;
  }
  if (a->rec.waypoints.count == Scenario::kMaxCoursePoints) {
    finish_mode();
  }
  prompt_number_ = a->rec.waypoints.count + 1;
  set_plot_buttons_disabled(a->rec.waypoints.count < 2);
  if (panel_dialog_ != nullptr && panel() == EditorPanel::kPlotCourse) {
    panel_dialog_->invalidate();
  }
  overlay_redraw_ = true;
  panels_.force_mouse_move();
  scenario_.modified = true;
}

void ChartScreen::set_patrol_area(std::int32_t x, std::int32_t y, bool radius) {
  // sbchart_set_patrol_area (0x3d394)
  auto* a = dynamic_cast<Aircraft*>(scenario_.selected());
  if (a != nullptr) {
    const WorldPoint p = screen_to_world(x, y);
    if (!radius) {
      a->rec.x = p.x;
      a->rec.y = p.y;
    } else {
      a->rec.orbit_radius = static_cast<std::int32_t>(fx::approx_distance(a->rec.x - p.x, a->rec.y - p.y));
    }
  }
  finish_mode();
  scenario_.modified = true;
}

void ChartScreen::remove_hovered() {
  // sbchart_remove_hovered_object (0x3d3ee)
  if (hovered_ == nullptr) {
    return;
  }
  EditorObject& object = *hovered_;
  const SeExe& se = scenario_.data().se;
  const std::string line1 =
      std::string(se.str(str::kRemove)) + std::string(object.type_name(se)) + std::string(se.str(str::kQuestion));
  bool yes = true;
  if (hooks_.ask_yes_no) {
    with_panel_closed([&] {
      yes = hooks_.ask_yes_no(se.str(str::kWarning), {line1, se.str(str::kRemoveLine2), se.str(str::kRemoveLine3)});
    });
  }
  if (!yes || (scenario_.removal_releases_convoy(object) && !confirm_remove_leader())) {
    return;
  }
  hovered_ = nullptr;
  scenario_.remove(object);
  finish_mode();
  scenario_.modified = true;
  if (shown_panel() == EditorPanel::kToolbox) {
    // Removed before the REMOVE OBJECT panel replaced the toolbox (no frame
    // in between): the toolbox's panel showed the object, so it is built
    // again for the new selection.
    reopen_panel();
  }
}

void ChartScreen::pick_convoy_hovered() {
  // sbchart_pick_convoy_hovered (0x3d5d2); `finish_mode` applies the
  // membership (again, as it is idempotent) for a ship.
  if (hovered_ != nullptr && scenario_.pick_convoy(*scenario_.selected(), *hovered_)) {
    finish_mode();
    scenario_.modified = true;
  }
}

bool ChartScreen::finish_mode() {
  // sbchart_mode_finish (0x3d693)
  EditorObject* sel = scenario_.selected();
  EditorPanel next = EditorPanel::kToolbox;
  bool chained = false;
  switch (click_mode_) {
    case ClickMode::kAddShip:
    case ClickMode::kAddSub:
      sel->update_detection(scenario_.detection_env());
      overlay_redraw_ = true;
      break;
    case ClickMode::kRemove:
      overlay_redraw_ = true;
      break;
    case ClickMode::kCourse:
    case ClickMode::kSubCourse:
    case ClickMode::kEscortCourse: {
      auto* v = dynamic_cast<Vessel*>(sel);
      if (v == nullptr || v->waypoints().count < 2) {
        return false;
      }
      if (click_mode_ == ClickMode::kEscortCourse && v->orders() == ShipOrders::kEscort) {
        // An escort's screen goes on to PICK CONVOY.
        prompt_ = Prompt::kConvoyShip;
        pick_title_ = scenario_.data().se.str(str::kPickConvoy);
        pick_exit_ = true;
        restore_click_mode();
        next = EditorPanel::kPick;
        set_click_mode(ClickMode::kPickConvoy);
        chained = true;
      }
      break;
    }
    case ClickMode::kAircraftPath: {
      auto* a = dynamic_cast<Aircraft*>(sel);
      if (a == nullptr || a->rec.waypoints.count < 2) {
        return false;
      }
      break;
    }
    case ClickMode::kPickConvoy:
      if (auto* v = dynamic_cast<Vessel*>(sel); v != nullptr && v->type() == ObjectType::kShip) {
        scenario_.apply_convoy_membership(*v);
      }
      overlay_redraw_ = true;
      break;
    case ClickMode::kPatrolCenter:
      prompt_ = Prompt::kPatrolRadius;
      pick_title_ = scenario_.data().se.str(str::kSelectRadius);
      pick_exit_ = false;
      if (panel_dialog_ != nullptr) {
        panel_dialog_->invalidate();
      }
      restore_click_mode();
      next = EditorPanel::kPick;
      set_click_mode(ClickMode::kPatrolRadius);
      chained = true;
      overlay_redraw_ = true;
      break;
    case ClickMode::kPatrolRadius:
      overlay_redraw_ = true;
      if (auto* a = dynamic_cast<Aircraft*>(sel); a != nullptr && a->rec.mission == AircraftMission::kSearchOrbit) {
        // An escorting aircraft (mission 1) picks its convoy.
        prompt_ = Prompt::kConvoyShip;
        pick_title_ = scenario_.data().se.str(str::kPickConvoy2);
        pick_exit_ = true;
        if (panel_dialog_ != nullptr) {
          panel_dialog_->invalidate();
        }
        restore_click_mode();
        next = EditorPanel::kPick;
        set_click_mode(ClickMode::kPickConvoy);
        chained = true;
      }
      break;
    case ClickMode::kPickCity:
      city_zoom_ = 0x800;
      if (!cities_.empty()) {
        if (auto* m = dynamic_cast<SpecialMission*>(sel)) {
          const auto base = static_cast<std::int32_t>(cities_[static_cast<std::size_t>(city_selection_)].base);
          std::visit([base](auto& r) { r.base = base; }, m->rec);
        }
        overlay_redraw_ = true;
      }
      break;
    case ClickMode::kSelect:
      break;
  }
  if (!chained) {
    restore_click_mode();
  }
  panels_.force_mouse_move();
  panels_.request(static_cast<int>(next));
  close_popup();
  return true;
}

void ChartScreen::clear_course() {
  // sbchart_clear_course (0x3d950): a ship or sub course emptied with the
  // rubber band from the unit, an aircraft path back to its first point.
  if (click_mode_ == ClickMode::kAircraftPath) {
    if (auto* a = dynamic_cast<Aircraft*>(scenario_.selected())) {
      a->rec.waypoints.count = 1;
      prompt_number_ = 2;
    }
  } else if (click_mode_ == ClickMode::kCourse || click_mode_ == ClickMode::kSubCourse ||
             click_mode_ == ClickMode::kEscortCourse) {
    if (auto* v = dynamic_cast<Vessel*>(scenario_.selected())) {
      v->ship.waypoints.count = 0;
      v->sub.waypoints.count = 0;
      prompt_number_ = 1;
      anchor_ = v->position(scenario_.object_env());
    }
  } else {
    return;
  }
  set_plot_buttons_disabled(true);
  if (panel_dialog_ != nullptr) {
    panel_dialog_->invalidate();
  }
  overlay_redraw_ = true;
  panels_.force_mouse_move();
}

void ChartScreen::start_add_vessel(std::size_t ship_class) {
  // sbchart_tool_add_ship (0x3e51c) after SELECT SHIP TYPE.
  add_class_ = ship_class;
  const SeExe& se = scenario_.data().se;
  if (scenario_.data().tables.ships[ship_class].ship_type == ShipType::kSubmarine) {
    set_click_mode(ClickMode::kAddSub);
    pick_title_ = se.str(str::kAddSub);
  } else {
    set_click_mode(ClickMode::kAddShip);
    pick_title_ = se.str(str::kAddShip);
  }
  panels_.request(static_cast<int>(EditorPanel::kPick));
  prompt_ = Prompt::kLocation;
  pick_exit_ = true;
}

void ChartScreen::start_remove() {
  // sbchart_tool_remove_object (0x3e999)
  if (scenario_.objects().size() < 2) {
    const SeExe& se = scenario_.data().se;
    panels_.close_panel();
    close_popup();
    if (hooks_.message) {
      hooks_.message(se.str(str::kError), {se.str(str::kCannotDelete), se.str(str::kNoRemovable), se.str(str::kEmpty)});
    }
    panels_.open_panel();
    return;
  }
  set_click_mode(ClickMode::kRemove);
  panels_.request(static_cast<int>(EditorPanel::kRemove));
  prompt_ = Prompt::kObjectToRemove;
}

void ChartScreen::start_course(ClickMode mode) {
  // toolbox_ship_orders_button (0x3f57e): the course from the unit.
  auto* v = dynamic_cast<Vessel*>(scenario_.selected());
  if (v == nullptr) {
    return;
  }
  panels_.request(static_cast<int>(EditorPanel::kPlotCourse));
  prompt_ = Prompt::kWaypoint;
  v->waypoints().count = 0;
  anchor_ = v->position(scenario_.object_env());
  prompt_number_ = 1;
  set_click_mode(mode);
}

void ChartScreen::start_aircraft(Aircraft& aircraft) {
  // sbchart_tool_add_aircraft (0x3e5a4) after its object is made.
  select_on_open_ = &aircraft;
  if (aircraft.rec.mission == AircraftMission::kFollowPath) {
    panels_.request(static_cast<int>(EditorPanel::kPlotCourse));
    prompt_ = Prompt::kWaypoint;
    aircraft.rec.waypoints.count = 0;
    prompt_number_ = 1;
    set_click_mode(ClickMode::kAircraftPath);
  } else {
    panels_.request(static_cast<int>(EditorPanel::kPick));
    prompt_ = Prompt::kPatrolCenter;
    pick_title_ = scenario_.data().se.str(str::kSelectPoint);
    pick_exit_ = false;
    set_click_mode(ClickMode::kPatrolCenter);
  }
  scenario_.modified = true;
}

void ChartScreen::start_patrol_radius() {
  // toolbox_air_mission_button (0x404b5): straight to the radius.
  panels_.request(static_cast<int>(EditorPanel::kPick));
  prompt_ = Prompt::kPatrolRadius;
  pick_title_ = scenario_.data().se.str(str::kSelectRadius);
  pick_exit_ = false;
  set_click_mode(ClickMode::kPatrolRadius);
}

void ChartScreen::start_aircraft_path(Aircraft& aircraft) {
  // toolbox_air_mission_button (0x404b5): the path's first point is the
  // aircraft.
  panels_.request(static_cast<int>(EditorPanel::kPlotCourse));
  prompt_ = Prompt::kWaypoint;
  aircraft.rec.waypoints.x[0] = aircraft.rec.x;
  aircraft.rec.waypoints.y[0] = aircraft.rec.y;
  aircraft.rec.waypoints.count = 1;
  prompt_number_ = 2;
  set_click_mode(ClickMode::kAircraftPath);
}

void ChartScreen::start_pick_convoy() {
  set_click_mode(ClickMode::kPickConvoy);
  panels_.request(static_cast<int>(EditorPanel::kPick));
  prompt_ = Prompt::kConvoyShip;
  pick_title_ = scenario_.data().se.str(str::kPickConvoy);
  pick_exit_ = true;
}

void ChartScreen::start_pick_city(bool photo_recon, EditorObject* restore) {
  // sbchart_tool_add_airstrike / _photo_recon (0x3e78a / 0x3e895) and the
  // missions' Location buttons: SELECT A CITY, zoomed out below 8x.
  city_photo_recon_ = photo_recon;
  // SE keeps the last remembered object when `restore` is null (see
  // `cancel_city`).
  restore_selection_ = restore;
  city_zoom_ = kBaseZoom;
  set_click_mode(ClickMode::kPickCity);
  panels_.request(static_cast<int>(EditorPanel::kSelectCity));
  prompt_ = photo_recon ? Prompt::kCityToPhotograph : Prompt::kCityToBomb;
}

void ChartScreen::cancel_city() {
  // sbchart_city_button_cancel (0x40cf0)
  if (!finish_mode()) {
    return;
  }
  // SELECT A CITY cancel (docs/editor-design.md): the selected mission
  // is removed, also when the panel was opened from an existing one's
  // Location button (intended: the mission has no city then). Original
  // bugs fixed: SE unlinks it without freeing it, clears the modified flag
  // (unsaved changes could then be lost without a prompt) and selects the
  // object remembered by the last ADD AIRSTRIKE / PHOTO RECON, possibly
  // removed since (a dangling pointer). The removal sets the modified flag
  // here, and the selection goes back to the object selected before ADD
  // (the player after a Location button or when that object is gone).
  EditorObject* restore = restore_selection_;
  restore_selection_ = nullptr;
  scenario_.remove(*scenario_.selected());
  const auto& objects = scenario_.objects();
  const bool listed = std::ranges::any_of(objects, [&](const auto& o) { return o.get() == restore; });
  scenario_.select(listed ? restore : &scenario_.player());
  if (shown_panel() == EditorPanel::kToolbox) {
    // The toolbox finish_mode reopened showed the removed mission.
    reopen_panel();
  }
}

void ChartScreen::remove_current() {
  // sbchart_remove_button_current (0x40d5f)
  if (scenario_.selected()->type() != ObjectType::kPlayer) {
    hovered_ = scenario_.selected();
    remove_hovered();
  }
}

void ChartScreen::redraw_toolbox() {
  // sbchart_toolbox_redraw (0x3c28f)
  if (panel_dialog_ != nullptr && shown_panel() == EditorPanel::kToolbox) {
    panel_dialog_->redraw_controls();
    panel_dialog_->invalidate();
  }
}

void ChartScreen::reopen_panel() {
  panels_.close_panel();
  panels_.open_panel();
}

void ChartScreen::with_panel_closed(const std::function<void()>& modal) {
  panels_.close_panel();
  close_popup();
  modal();
  panels_.open_panel();
}

bool ChartScreen::confirm_remove_leader() {
  // sbchart_confirm_remove_leader (0x443c6)
  if (!hooks_.ask_yes_no) {
    return true;
  }
  const SeExe& se = scenario_.data().se;
  bool yes = true;
  with_panel_closed([&] {
    yes = hooks_.ask_yes_no(se.str(str::kLeaderWarning),
                            {se.str(str::kLeaderLine1), se.str(str::kLeaderLine2), se.str(str::kLeaderLine3)});
  });
  return yes;
}

void ChartScreen::mouse_moved(std::int32_t x, std::int32_t y) {
  // sbchart_on_mouse_move (0x3c371): the hover pick, the rubber band to the
  // pointer, then the status bar.
  bool band = false;
  bool check_water = false;
  WorldPoint from;
  std::int32_t draft = 0;
  bool over_city = false;
  switch (click_mode_) {
    case ClickMode::kSelect:
    case ClickMode::kRemove:
    case ClickMode::kPickConvoy:
      pick_object_at(x, y);
      break;
    case ClickMode::kCourse:
    case ClickMode::kSubCourse:
    case ClickMode::kEscortCourse:
      if (auto* v = dynamic_cast<Vessel*>(scenario_.selected())) {
        draft = vessel_draft(*v);
      }
      from = anchor_;
      band = true;
      check_water = true;
      break;
    case ClickMode::kAircraftPath:
      if (auto* a = dynamic_cast<Aircraft*>(scenario_.selected()); a != nullptr && a->rec.waypoints.count > 0) {
        const auto last = static_cast<std::size_t>(a->rec.waypoints.count - 1);
        from = {a->rec.waypoints.x[last], a->rec.waypoints.y[last]};
        band = true;
      }
      break;
    case ClickMode::kPatrolRadius:
      if (auto* a = dynamic_cast<Aircraft*>(scenario_.selected())) {
        from = {a->rec.x, a->rec.y};
        band = true;
      }
      break;
    case ClickMode::kPickCity:
      over_city = pick_city_at(x, y);
      break;
    default:
      break;
  }
  if (band && !zoom_box_.is_open()) {
    if (const std::optional<std::array<std::int32_t, 2>> s = world_to_screen(from)) {
      bool valid = true;
      if (check_water) {
        const WorldPoint w = screen_to_world(x, y);
        const std::int32_t height = scenario_.world().floor().height_at(w.x, w.y);
        if (draft * -16 == height || -height < draft * 16) {
          valid = false;
        }
        if (valid && land_on_line(scenario_.world().grid(), {from.x, from.y}, {w.x, w.y}).land) {
          valid = false;
        }
      }
      course_line_.erase();
      if (valid) {
        course_line_.draw(view_viewport(), (*s)[0], (*s)[1], x, y);
      }
    }
  }
  // The status bar.
  Image8& screen = env_.gui.screen();
  const Viewport bar{&screen, 0, kStatusY, kScreenWidth, kScreenHeight};
  ui::fill_rect(bar, 0, 0, bar.x1 - bar.x0, bar.y1 - bar.y0, 0);
  status_latitude_.clear();
  status_longitude_.clear();
  status_depth_.clear();
  status_hint_.clear();
  if (x > kViewX1 || x < 0 || y > kViewY1 || y < 0) {
    return;
  }
  GlyphColourTable table = gui_colour_table();
  table[7] = kStatusText;
  table[9] = kStatusText;
  const SeExe& se = scenario_.data().se;
  bool mode_hints = true;
  if (!zoom_box_.is_open()) {
    const WorldPoint w = screen_to_world(x, y);
    const World& world = scenario_.world().world();
    status_latitude_ = format_latlon(se, world.latitude_at(w.y));
    status_longitude_ = format_latlon(se, world.longitude_at(w.x));
    gui_draw_text(bar, 4, 5, env_.font1, status_latitude_, &table);
    gui_draw_text(bar, 0x37, 5, env_.font1, status_longitude_, &table);
    const std::int32_t depth = -scenario_.world().floor().height_at(w.x, w.y) / 16;
    if (depth > 0) {
      status_depth_ = std::to_string(depth) + std::string(se.str(str::kFeet));
      gui_draw_text(bar, 0x6e, 5, env_.font1, status_depth_, &table);
    }
  } else {
    status_depth_ = se.str(str::kZoomMessage);
    gui_draw_text(bar, 4, 7, env_.msg_font, status_depth_, &table);
    mode_hints = false;
  }
  if (mode_hints && click_mode_ == ClickMode::kSelect) {
    status_hint_ = se.str(str::kMenuHint);
  }
  if (mode_hints && click_mode_ == ClickMode::kPickConvoy && hovered_ != nullptr) {
    std::int32_t group = hovered_->convoy_group(true);
    if (group == 0) {
      group = hovered_->convoy_group(false);
    }
    if (group == 0) {
      group = hovered_->escorted_group();
    }
    if (group > 0 && group <= 8) {
      status_hint_ = std::string(se.str(str::kSelectConvoy)) +
                     std::string(se.table_str(se_addr::kConvoyLetters, static_cast<std::size_t>(group)));
    }
  }
  if (mode_hints && (click_mode_ == ClickMode::kSelect || click_mode_ == ClickMode::kRemove) && hovered_ != nullptr) {
    status_hint_ = std::string(se.str(str::kSelect)) + std::string(hovered_->type_name(se));
  }
  if (mode_hints && click_mode_ == ClickMode::kPickCity && over_city) {
    status_hint_ = std::string(se.str(str::kSelectCity)) +
                   scenario_.world().bases()[cities_[static_cast<std::size_t>(city_selection_)].base].name;
  }
  const int w = env_.msg_font.text_width(status_hint_);
  gui_draw_text(bar, bar.x1 - w - 1, 7, env_.msg_font, status_hint_, &table);
}

// --- Cities
// ----------------------------------------------------------------------------

void ChartScreen::build_city_list() {
  // sbchart_city_panel_open (0x3bccc): the cities held by Japan today, with
  // an air-raid weight for an airstrike, not excluded from photo recon for
  // a photo recon, sorted by name (selection sort, stricmp).
  cities_.clear();
  const std::vector<BaseRecord>& bases = scenario_.world().bases();
  const ZoneData* zone = scenario_.world().zone();
  for (std::size_t i = 0; i < bases.size(); ++i) {
    const BaseRecord& b = bases[i];
    const bool japanese = zone != nullptr && i < zone->owners.size() && zone->owners[i].side == BaseSide::kJapanese;
    if ((b.flags & kBaseCity) == 0 || !japanese) {
      continue;
    }
    if (city_photo_recon_ && b.has(BaseFlag::kNoRecon)) {
      continue;
    }
    if (!city_photo_recon_ && b.pick_weight == 0) {
      continue;
    }
    cities_.push_back({i, b.name});
  }
  for (std::size_t i = 0; i < cities_.size(); ++i) {
    std::size_t min = i;
    for (std::size_t k = i + 1; k < cities_.size(); ++k) {
      if (stricmp(cities_[k].name, cities_[min].name) < 0) {
        min = k;
      }
    }
    std::swap(cities_[i], cities_[min]);
  }
}

std::optional<ui::Rect> ChartScreen::city_hit_rect(std::size_t base) const {
  // sbchart_city_hit_rect (0x3e085): 5 x zoom pixels around the base.
  const BaseRecord& b = scenario_.world().bases()[base];
  std::int32_t x = 0;
  std::int32_t y = 0;
  if (!view_->point_on_screen(b.station.x, b.station.z, x, y)) {
    return std::nullopt;
  }
  const std::int32_t half = static_cast<std::int16_t>(
      static_cast<std::uint32_t>(fx::imul(fx::div(view_->zoom(), fx::kOne), 5) + 0x8000) >> 16);
  return ui::Rect{x - half, y - half, x + half, y + half};
}

bool ChartScreen::pick_city_at(std::int32_t x, std::int32_t y) {
  // sbchart_pick_city_at (0x3dbc4)
  std::size_t cursor = kCursorPointer;
  bool found = false;
  const std::size_t n = scenario_.world().bases().size();
  for (std::size_t i = 0; i < n && !found; ++i) {
    const std::optional<ui::Rect> r = city_hit_rect(i);
    if (!r || x < r->x0 || x > r->x1 || y < r->y0 || y > r->y1) {
      continue;
    }
    for (std::size_t k = 0; k < cities_.size(); ++k) {
      if (cities_[k].base == i) {
        cursor = kCursorObject;
        found = true;
        city_selection_ = static_cast<std::int32_t>(k);
        city_top_ = city_selection_;
        if (city_list_ != nullptr) {
          city_list_->selection = city_selection_;
          city_list_->top = city_top_;
          city_list_->dirty = true;
        }
      }
    }
  }
  set_cursor(cursor);
  return found;
}

DescribeText ChartScreen::city_info_text(std::size_t base) const {
  // sbchart_city_info_text (0x3e15e)
  DescribeText t;
  if (click_mode_ != ClickMode::kSelect && click_mode_ != ClickMode::kPickCity) {
    return t;
  }
  const SeExe& se = scenario_.data().se;
  const BaseRecord& b = scenario_.world().bases()[base];
  const World& world = scenario_.world().world();
  t[0] = b.name;
  t[1] = std::string(se.str(str::kLoc)) + format_latlon(se, world.latitude_at(b.station.z)) +
         std::string(se.str(str::kComma)) + format_latlon(se, world.longitude_at(b.station.x));
  const ZoneData* zone = scenario_.world().zone();
  const auto side =
      zone != nullptr && base < zone->owners.size() ? static_cast<std::size_t>(zone->owners[base].side) : 0;
  t[2] = side < kOwnerCount ? se.table_str(str::kOwners, side) : std::string_view();
  const auto size = static_cast<std::size_t>(b.anchored_size);
  t[3] = std::string(size < kCitySizeCount ? se.table_str(str::kCitySizes, size) : std::string_view()) +
         std::string(se.str(str::kSpace));
  if ((b.flags & kBaseCity) != 0) {
    t[3] += (b.flags & kBaseNaval) != 0 ? se.str(str::kNavalBase) : se.str(str::kPort);
  }
  return t;
}

// --- The hover popup
// ------------------------------------------------------------------------

void ChartScreen::hover_poll() {
  // sbchart_hover_poll (0x3dcb0)
  update_hover_popup(env_.gui.mouse().x(), env_.gui.mouse().y());
}

std::optional<std::pair<const void*, DescribeText>> ChartScreen::info_text_at(std::int32_t x, std::int32_t y) const {
  // sbchart_info_text_at (0x3df1d): the first object under the pointer in
  // mode 0 (or with an object hovered), else in modes 0 and 11 a city.
  ObjectEnv env = scenario_.object_env();
  env.select_mode = click_mode_ == ClickMode::kSelect;
  for (const auto& o : scenario_.objects()) {
    if (click_mode_ != ClickMode::kSelect && hovered_ == nullptr) {
      break;
    }
    const std::optional<ui::Rect> r = hit_rect(*o);
    if (r && x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1) {
      return std::pair<const void*, DescribeText>{o.get(), o->describe(env)};
    }
  }
  if (click_mode_ != ClickMode::kSelect && click_mode_ != ClickMode::kPickCity) {
    return std::nullopt;
  }
  const std::vector<BaseRecord>& bases = scenario_.world().bases();
  for (std::size_t i = 0; i < bases.size(); ++i) {
    const std::optional<ui::Rect> r = city_hit_rect(i);
    if (r && x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1) {
      return std::pair<const void*, DescribeText>{&bases[i], city_info_text(i)};
    }
  }
  return std::nullopt;
}

void ChartScreen::update_hover_popup(std::int32_t x, std::int32_t y) {
  // sbchart_update_hover_popup (0x3da61) with sbchart_place_info_popup
  // (0x3dce8): the lines up to the first empty one, clear of the active
  // panel.
  if (panels_.hovered() || zoom_box_.is_open()) {
    return;
  }
  const auto info = info_text_at(x, y);
  if (!info) {
    close_popup();
    popup_owner_ = nullptr;
    return;
  }
  if (info->first != popup_owner_) {
    close_popup();
  }
  std::vector<std::string> lines;
  for (const std::string& l : info->second) {
    if (l.empty()) {
      break;
    }
    lines.push_back(l);
  }
  popup_.show(x, y, lines, panels_.active_rect());
  popup_owner_ = info->first;
}

void ChartScreen::close_popup() {
  if (popup_.close()) {
    force_overlays_ = true;
  }
}

std::string ChartScreen::prompt_text() const {
  // sbchart_format_prompt (0x40d8d): glyphs 1..3 become the numbers
  // 0xaad6c[0..2]; only the first is ever set.
  const std::string_view p = scenario_.data().se.table_str(str::kPrompts, static_cast<std::size_t>(prompt_));
  std::string out;
  for (const char c : p) {
    if (c >= 1 && c <= 3) {
      out += c == 1 ? std::to_string(prompt_number_) : "0";
    } else {
      out += c;
    }
  }
  return out;
}

// --- The panels
// ----------------------------------------------------------------------------

void ChartScreen::add_panels() {
  // sbchart_open's chart_view_add_panel calls.
  const std::array<std::pair<int, int>, 5> sizes{{{kToolboxW, kToolboxH},
                                                  {kModePanelW, kModePanelH},
                                                  {kModePanelW, kModePanelH},
                                                  {kModePanelW, kCityPanelH},
                                                  {kModePanelW, kModePanelH}}};
  for (std::size_t i = 0; i < sizes.size(); ++i) {
    ChartPanelSlot s;
    s.position = &panel_pos_[i];
    s.width = sizes[i].first;
    s.height = sizes[i].second;
    const auto which = static_cast<EditorPanel>(i);
    s.open = [this, which] { open_panel_dialog(which); };
    s.close = [this] { close_panel_dialog(); };
    s.event = [this](const ui::Event& e) {
      if (panel_dialog_) {
        panel_dialog_->handle_event(e);
      }
    };
    s.process = [this] {
      if (panel_dialog_) {
        panel_dialog_->process();
      }
    };
    s.redraw = [this] { repaint_panel_dialog(); };
    if (which == EditorPanel::kToolbox) {
      s.update = [this] {
        // sbchart_toolbox_update (0x3c1d0): the gauge on a size change,
        // then the convoy of a leader.
        if (scenario_.header.required_memory != shown_memory_) {
          shown_memory_ = scenario_.header.required_memory;
          if (gauge_ != nullptr) {
            gauge_->dirty = true;
          }
        }
        toolbox_->update();
      };
    }
    panels_.add(std::move(s));
  }
}

void ChartScreen::open_panel_dialog(EditorPanel which) {
  dirty_ = true;
  if (which == EditorPanel::kToolbox) {
    open_toolbox();
    return;
  }
  if (select_on_open_ != nullptr) {
    scenario_.select(select_on_open_);
  }
  select_on_open_ = nullptr;
  if (which == EditorPanel::kPlotCourse && last_requested_ != panels_.requested()) {
    // sbchart_plot_panel_open (0x3bc4b): DONE and CLEAR disabled when the
    // panel was not already up.
    plot_buttons_disabled_ = true;
  }
  if (which == EditorPanel::kSelectCity) {
    // sbchart_city_panel_open (0x3bccc): the list, the current target
    // preselected, the zoom back to 1x from 8x.
    const bool reopened = last_requested_ == panels_.requested();
    const std::int32_t previous = city_selection_;
    const std::int32_t previous_top = city_top_;
    build_city_list();
    city_selection_ = 0;
    if (auto* m = dynamic_cast<SpecialMission*>(scenario_.selected())) {
      for (std::size_t k = 0; k < cities_.size(); ++k) {
        if (static_cast<std::int32_t>(cities_[k].base) == m->base()) {
          city_selection_ = static_cast<std::int32_t>(k);
        }
      }
    }
    city_top_ = city_selection_;
    if (reopened) {
      city_selection_ = previous;
      city_top_ = previous_top;
    } else if (view_->zoom_int() >= city_zoom_) {
      view_->set_zoom(0x10000);
    }
  }
  panel_dialog_ = make_mode_panel(which);
  panel_dialog_->open();
  if (which == EditorPanel::kSelectCity && city_list_ != nullptr) {
    // After `gui_dialog_open`, whose registration clears the list's top and
    // selection (DOSBox captures: the first city is highlighted).
    city_list_->selection = city_selection_;
    city_list_->top = city_top_;
    city_list_->dirty = true;
  }
}

void ChartScreen::retire_panel_dialog() {
  if (panel_dialog_) {
    panel_dialog_->close();
    retired_dialogs_.push_back(std::move(panel_dialog_));
  }
  toolbox_->closed();
  city_list_ = nullptr;
  done_button_ = nullptr;
  clear_button_ = nullptr;
}

void ChartScreen::set_plot_buttons_disabled(bool disabled) {
  // The flags are PLOT COURSE's buttons' own (0xad90c / 0xad948): an open
  // panel shows the change.
  plot_buttons_disabled_ = disabled;
  for (ui::Button* b : {done_button_, clear_button_}) {
    if (b != nullptr && b->disabled != disabled) {
      b->disabled = disabled;
      b->dirty = true;
    }
  }
}

void ChartScreen::close_panel_dialog() {
  retire_panel_dialog();
  prev_button_ = nullptr;
  next_button_ = nullptr;
  gauge_ = nullptr;
  if (dialog_) {
    // chart_view_close_panel: the map is restored under the panel at once
    // (a menu or a modal dialog may follow before the next frame); not
    // when the chart itself closes (chart_view_close).
    redraw_overlays();
  } else {
    force_overlays_ = true;
  }
}

void ChartScreen::repaint_panel_dialog() {
  // The panels' invalidate and redraw slots in one. Original bug (wrong
  // dialog invalidated): the redraw slots of SELECT A CITY and REMOVE
  // OBJECT (`sbchart_city_panel_redraw`, `sbchart_remove_panel_redraw`)
  // invalidate the pick panel's dialog, which is closed then, instead of
  // their own; their invalidate slots, which the map redraws use, are
  // right, so nothing shows and every panel is invalidated here.
  if (panel_dialog_) {
    panel_dialog_->invalidate();
    panel_dialog_->redraw_controls();
  }
}

void ChartScreen::open_toolbox() {
  // sbchart_toolbox_open (0x3b92c)
  object_count_ = scenario_.objects().size();
  if (select_on_open_ != nullptr) {
    scenario_.select(select_on_open_);
  }
  select_on_open_ = nullptr;
  shown_memory_ = scenario_.header.required_memory;
  panel_dialog_ = make_toolbox();
  panel_dialog_->open();
}

void ChartScreen::close_toolbox() {
  // sbchart_toolbox_close: the dialog only (the slot's open flag stays).
  if (panels_.active() == static_cast<int>(EditorPanel::kToolbox) && panels_.is_open() && panel_dialog_) {
    retire_panel_dialog();
    prev_button_ = nullptr;
    next_button_ = nullptr;
    gauge_ = nullptr;
  }
}

void ChartScreen::panel_hover(bool over) {
  // sbchart_on_panel_hover (0x3dc66)
  if (over) {
    set_cursor(kCursorPointer);
    close_popup();
    course_line_.erase();
    Image8& screen = env_.gui.screen();
    const Viewport bar{&screen, 0, kStatusY, kScreenWidth, kScreenHeight};
    ui::fill_rect(bar, 0, 0, bar.x1 - bar.x0, bar.y1 - bar.y0, 0);
  }
}

void ChartScreen::add_sprite_button(ui::Dialog& d, ui::Rect r, std::size_t up, std::size_t down,
                                    std::function<void()> action) {
  // An icon button (`sprite_button_repeat_msg` 0x24b8a): the down frame
  // while pressed.
  d.emplace<ui::Area>(
      r.x0, r.y0, r.x1, r.y1,
      [this, r, up, down](ui::Dialog& dlg, ui::Area&, ui::Msg msg, int, const ui::Event&) {
        std::size_t f = up;
        switch (msg) {
          case ui::Msg::kPress:
            f = down;
            break;
          case ui::Msg::kRelease:
          case ui::Msg::kPaint:
            break;
          default:
            return 0;
        }
        const SpriteFrame& frame = env_.sbchart.frame(f);
        draw_sprite_clipped(dlg.gfx(), frame, r.x0, r.y0);
        dlg.add_dirty(frame_box(frame, r.x0, r.y0));
        return -1;
      },
      [action = std::move(action)](ui::Dialog&, ui::Area&, int, const ui::Event&) {
        action();
        return 0;
      });
}

void ChartScreen::add_view_controls(ui::Dialog& d, int scale_y) {
  // The scale bar under the buttons: two dark and two light lines, "0",
  // the value and unit, and the zoom / centre icon buttons.
  d.emplace<ui::Line>(7, scale_y, 288, scale_y, 3);
  d.emplace<ui::Line>(7, scale_y + 1, 288, scale_y + 1, 3);
  d.emplace<ui::Line>(7, scale_y + 2, 288, scale_y + 2, 2);
  d.emplace<ui::Line>(7, scale_y + 3, 288, scale_y + 3, 2);
  const int text_y = scale_y + 5;
  d.emplace<ui::Label>(16, text_y, "0", ui::FontSlot::kCustom, true, &env_.font1);
  auto& value = d.emplace<ui::TextField>(
      260, text_y, 288, text_y + 10, [this] { return chart_scale_value(view_->zoom_int()); }, ui::FontSlot::kCustom,
      TextAlign::kCentre);
  value.fill = 6;
  value.custom = &env_.font1;
  auto& legend = d.emplace<ui::TextField>(
      63, text_y, 229, text_y + 10, [this] { return std::string(chart_scale_legend(view_->zoom_int())); },
      ui::FontSlot::kCustom, TextAlign::kCentre);
  legend.fill = 6;
  legend.custom = &env_.font1;
  add_sprite_button(d, {14, 44, 35, 64}, kZoomIn, kZoomIn + 1, [this] { zoom_in(); });
  add_sprite_button(d, {37, 44, 58, 64}, kZoomOut, kZoomOut + 1, [this] { zoom_out(); });
}

std::unique_ptr<ui::Dialog> ChartScreen::make_toolbox() {
  // TOOLBOX (dialog 0xaaf54).
  const SeExe& se = scenario_.data().se;
  const ChartPanelPosition& at = panel_pos_[0];
  auto d =
      std::make_unique<ui::Dialog>(env_.gui, at.x, at.y, kToolboxW, kToolboxH, ui::kDialogFramed, &env_.gui.screen());
  d->emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    // sbchart_toolbox_draw_background (0x3e329). Its highlight of a ship's
    // orders-dependent field (0xac0d4) is dead: the flag is only ever
    // cleared.
    ui::gui_fill_rect(gfx, 0xd, 0x2b, 0x4f, 0x17, 0);
    ui::gui_fill_rect(gfx, 0x24, 0x46, 0xe2, 0x17, 0);
    draw_sprite_clipped(gfx, env_.sbchart.frame(kScaleRuler), 0x12, 0x6f);
    draw_sprite_clipped(gfx, env_.sbchart.frame(kMemoryGauge), 0x77, 0x2c);
  });
  d->emplace<ui::TitleBar>(std::string(se.str(str::kToolbox)));
  add_view_controls(*d, 94);
  d->emplace<ui::Line>(7, 121, 288, 121, 3);
  d->emplace<ui::Line>(7, 122, 288, 122, 3);
  d->emplace<ui::Line>(7, 123, 288, 123, 2);
  d->emplace<ui::Line>(7, 124, 288, 124, 2);
  add_sprite_button(*d, {37, 71, 80, 91}, kRemoveIcon, kRemoveIcon + 1, [this] { start_remove(); });
  const auto tool = [this](ToolboxTool t) { return [this, t] { toolbox_->tool(t); }; };
  add_sprite_button(*d, {82, 71, 125, 91}, kShipIcon, kShipIcon + 1, tool(ToolboxTool::kAddShip));
  add_sprite_button(*d, {127, 71, 170, 91}, kAircraftIcon, kAircraftIcon + 1, tool(ToolboxTool::kAddAircraft));
  add_sprite_button(*d, {172, 71, 215, 91}, kAirstrikeIcon, kAirstrikeIcon + 1, tool(ToolboxTool::kAirstrike));
  add_sprite_button(*d, {217, 71, 260, 91}, kPhotoReconIcon, kPhotoReconIcon + 1, tool(ToolboxTool::kPhotoRecon));
  const bool single = scenario_.objects().size() < 2;
  prev_button_ = &d->emplace<ui::Button>(
      225, 43, 25, std::string(se.str(str::kPrevious)),
      [this](int) {
        previous_object();
        return 0;
      },
      0, 20);
  prev_button_->disabled = single;
  next_button_ = &d->emplace<ui::Button>(
      258, 43, 25, std::string(se.str(str::kNext)),
      [this](int) {
        next_object();
        return 0;
      },
      0, 20);
  next_button_->disabled = single;
  add_sprite_button(*d, {60, 44, 90, 64}, kCentre, kCentre + 1, [this] { center_on_selection(); });
  d->emplace<ui::GroupBox>(11, 128, 286, 146);
  gauge_ = &d->emplace<ui::Area>(
      119, 44, 193, 60,
      [this](ui::Dialog& dlg, ui::Area& a, ui::Msg msg, int, const ui::Event&) {
        // sbchart_draw_memory_gauge (0x4440c): the bar and "N%" of the
        // scenario size against the budget (101 % when just over).
        if (msg != ui::Msg::kPaint) {
          return 0;
        }
        const std::int32_t w = (a.x1 - a.x0) - 3;
        const std::int32_t size = scenario_.header.required_memory;
        const std::int32_t budget = scenario_.memory_budget();
        std::int32_t pct = budget != 0 ? static_cast<std::int32_t>(static_cast<std::int64_t>(size) * 100 / budget) : 0;
        if (pct == 100 && size > budget) {
          pct = 101;
        }
        const std::int32_t bar = std::max(std::min(w * pct / 100, w), 1);
        const Viewport& gfx = dlg.gfx();
        ui::gui_fill_rect(gfx, a.x0 + 2, a.y0 + 4, w, 9, 0xb);
        if (pct != 0) {
          ui::gui_fill_rect(gfx, a.x0 + 2, a.y0 + 4, bar, 9, pct <= 100 ? 2 : 0x4b);
        }
        const std::string text = std::to_string(pct) + std::string(scenario_.data().se.str(str::kPercent));
        // The text's colour table is a stack buffer with only its first and
        // last entries set; MENU_S's glyphs use colour 0 only.
        GlyphColourTable table;
        table.fill(0xff);
        table[0] = 9;
        const std::int32_t tw = env_.menu_font.text_width(text);
        gui_draw_text(gfx, a.x0 + 2 + (w - tw + 4) / 2, a.y0 + 5, env_.menu_font, text, &table);
        dlg.add_dirty({a.x0, a.y0, a.x1, a.y1});
        return -1;
      },
      nullptr);
  auto& title = d->emplace<ui::TextField>(
      13, 132, 283, 143,
      [this] {
        // sbchart_toolbox_title_text (0x3ea0d)
        const SeExe& s = scenario_.data().se;
        return std::string(s.str(str::kTitleOpen)) + std::string(scenario_.selected()->type_name(s)) +
               std::string(s.str(str::kTitleClose));
      },
      ui::FontSlot::kCustom, TextAlign::kCentre);
  title.fill = 6;
  title.custom = &env_.font1;
  toolbox_->add_controls(*d, *scenario_.selected());
  return d;
}

std::unique_ptr<ui::Dialog> ChartScreen::make_mode_panel(EditorPanel which) {
  // PLOT COURSE (0xad544), the pick panel (0xad954), SELECT A CITY
  // (0xadd28) and REMOVE OBJECT (0xae180): the background, the title, the
  // scale and the prompt, the zoom and centre buttons, their own buttons.
  const SeExe& se = scenario_.data().se;
  const ChartPanelPosition& at = panel_pos_[static_cast<std::size_t>(which)];
  const int h = which == EditorPanel::kSelectCity ? kCityPanelH : kModePanelH;
  auto d = std::make_unique<ui::Dialog>(env_.gui, at.x, at.y, kModePanelW, h, ui::kDialogFramed, &env_.gui.screen());
  d->emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    // sbchart_panel_draw_background (0x40cab)
    ui::gui_fill_rect(gfx, 0xd, 0x2b, 0x4f, 0x17, 0);
    draw_sprite_clipped(gfx, env_.sbchart.frame(kScaleRuler), 0x12, 0x5b);
  });
  std::string title;
  switch (which) {
    case EditorPanel::kPlotCourse:
      title = se.str(str::kPlotCourse);
      break;
    case EditorPanel::kPick:
      title = pick_title_;
      break;
    case EditorPanel::kSelectCity:
      title = se.str(str::kSelectACity);
      break;
    default:
      title = se.str(str::kRemoveObject);
      break;
  }
  d->emplace<ui::TitleBar>(title);
  add_view_controls(*d, 74);
  d->emplace<ui::Line>(7, 101, 288, 101, 3);
  d->emplace<ui::Line>(7, 102, 288, 102, 3);
  d->emplace<ui::Line>(7, 103, 288, 103, 2);
  d->emplace<ui::Line>(7, 104, 288, 104, 2);
  const int prompt_y = which == EditorPanel::kSelectCity ? 113 : 110;
  auto& prompt = d->emplace<ui::TextField>(
      8, prompt_y, 287, prompt_y + 10, [this] { return prompt_text(); }, ui::FontSlot::kCustom, TextAlign::kCentre);
  prompt.fill = 6;
  prompt.custom = &env_.font1;
  add_sprite_button(*d, {60, 44, 90, 64}, kCentre, kCentre + 1, [this] { center_on_selection(); });
  const auto done = [this](int) { return finish_mode() ? ui::kResultAccept : ui::kResultContinue; };
  switch (which) {
    case EditorPanel::kPlotCourse: {
      done_button_ = &d->emplace<ui::Button>(215, 45, 65, std::string(se.str(str::kDone)), done);
      done_button_->disabled = plot_buttons_disabled_;
      clear_button_ = &d->emplace<ui::Button>(141, 45, 65, std::string(se.str(str::kClear)), [this](int) {
        clear_course();
        return 0;
      });
      clear_button_->disabled = plot_buttons_disabled_;
      break;
    }
    case EditorPanel::kPick: {
      auto& exit = d->emplace<ui::Button>(215, 45, 65, std::string(se.str(str::kExit)), done);
      exit.disabled = !pick_exit_;
      break;
    }
    case EditorPanel::kRemove: {
      d->emplace<ui::Button>(200, 45, 80, std::string(se.str(str::kCancel)), done);
      auto& current = d->emplace<ui::Button>(100, 45, 90, std::string(se.str(str::kCurrent)), [this](int) {
        remove_current();
        return ui::kResultAccept;
      });
      current.disabled = scenario_.selected()->type() == ObjectType::kPlayer;
      break;
    }
    case EditorPanel::kSelectCity: {
      // The list (0xae0c0, `sbchart_city_list_on_select` on a double click
      // or Enter), SELECT and CANCEL.
      auto& list = d->emplace<ui::List>(
          10, 135, 250, [this] { return static_cast<int>(cities_.size()); },
          [this](int i) { return cities_[static_cast<std::size_t>(i)].name; });
      list.on_select = [this](int i) { city_selection_ = i; };
      list.on_activate = [this](int) { finish_mode(); };
      city_list_ = &list;
      d->emplace<ui::Button>(110, 45, 80, std::string(se.str(str::kSelectButton)), done);
      d->emplace<ui::Button>(200, 45, 80, std::string(se.str(str::kCityCancel)), [this](int) {
        cancel_city();
        return ui::kResultContinue;
      });
      break;
    }
    default:
      break;
  }
  return d;
}

}  // namespace sh::editor

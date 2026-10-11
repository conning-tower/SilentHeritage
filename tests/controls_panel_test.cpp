// The live state of the touch command panel (extended `panel_status`): the
// state read from the boat, the panel's progress and taps, its drawing,
// and the drawing without a state being today's.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/command_icons.h"
#include "app/panel_status.h"
#include "assets/dat_tables.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/shpath.h"
#include "game_data.h"
#include "platform/command_panel.h"
#include "platform/command_panel_view.h"
#include "sim/player_combat.h"

namespace sh {

namespace {

float cx(const PanelRect& r) { return r.x + r.w / 2; }
float cy(const PanelRect& r) { return r.y + r.h / 2; }

CommandPanel open_panel() {
  CommandPanel panel;
  panel.set_window(2208, 1840, 2.625f);
  const PanelRect b = panel.button_rect();
  panel.press(cx(b), cy(b));
  panel.release(cx(b), cy(b));
  REQUIRE(panel.is_open());
  return panel;
}

PanelStatus some_status() {
  PanelStatus s;
  s.depth_order = PanelCommand::kPeriscopeDepth;
  s.tubes[0] = {TubeState::kLoaded, 0, "Mk14"};
  s.tubes[1] = {TubeState::kReloading, 100, ""};
  s.tubes[2] = {TubeState::kEmpty, 0, ""};
  s.tubes[3] = {TubeState::kDamaged, 0, ""};
  return s;
}

// Icons of flat colours (no game data).
CommandIcons flat_icons() {
  CommandIcons icons;
  icons.button = RgbaImage(8, 4, 0xff806040);
  for (std::size_t i = 0; i < 2; ++i) {
    icons.compression[i] = RgbaImage(4, 4, 0xff406080);
    icons.compression_pressed[i] = RgbaImage(4, 4, 0xff204060);
  }
  for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
    icons.icons[i] = RgbaImage(4, 4, 0xffc00000);
    icons.pressed[i] = RgbaImage(4, 4, 0xff400000);
  }
  return icons;
}

// Draws `panel` into a software renderer; returns the pixels.
std::vector<std::uint32_t> render(const CommandPanel& panel) {
  SDL_Surface* window = SDL_CreateSurface(2208, 1840, SDL_PIXELFORMAT_XRGB8888);
  REQUIRE(window != nullptr);
  SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(window);
  REQUIRE(renderer != nullptr);
  {
    CommandPanelView view(renderer, flat_icons());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    view.draw(panel);
    SDL_RenderPresent(renderer);
  }
  const auto* p = static_cast<const std::uint32_t*>(window->pixels);
  std::vector<std::uint32_t> out(p, p + static_cast<std::size_t>(window->w) * static_cast<std::size_t>(window->h));
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(window);
  return out;
}

// Counts the pixels of `colour` (0xRRGGBB) within `r`.
int count(const std::vector<std::uint32_t>& pixels, const PanelRect& r, std::uint32_t colour) {
  int n = 0;
  for (int y = static_cast<int>(r.y); y < static_cast<int>(r.y + r.h); ++y) {
    for (int x = static_cast<int>(r.x); x < static_cast<int>(r.x + r.w); ++x) {
      n += (pixels[static_cast<std::size_t>(y) * 2208 + static_cast<std::size_t>(x)] & 0xffffff) == colour;
    }
  }
  return n;
}

constexpr std::uint32_t kGreen = 0x46c85a;  // the lamps and frames of kLampGreen

}  // namespace

TEST_CASE("controls: the panel reads the depth order in effect", "[controls][cmdpanel]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  PlayerSub& sub = mission->player();
  DiveState& dive = sub.dive();
  const SubClassParams& c = sub.sub();
  REQUIRE(c.periscope_depth_ft != c.radar_depth_ft);
  // Submerged below the draft, nothing ordered, planes level: none.
  dive.depth_ft = c.draft_ft + 100;
  dive.ordered_depth_ft = -1;
  dive.planes = 0;
  dive.mode = DiveState::Mode::kTrim;
  CHECK(depth_order_in_effect(sub) == std::nullopt);
  dive.ordered_depth_ft = c.periscope_depth_ft;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kPeriscopeDepth);
  dive.ordered_depth_ft = c.radar_depth_ft;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kRadarDepth);
  // A depth set on the dial is no plate's.
  dive.ordered_depth_ft = 123;
  CHECK(depth_order_in_effect(sub) == std::nullopt);
  dive.ordered_depth_ft = 200;
  dive.planes = -80;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kCrashDive);
  dive.ordered_depth_ft = -1;
  dive.planes = 60;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kSurface);
  dive.planes = 80;
  dive.mode = DiveState::Mode::kBlow;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kBlowBallast);
  // On the surface with no order: surfaced.
  dive.mode = DiveState::Mode::kSurface;
  dive.planes = 0;
  dive.depth_ft = c.draft_ft;
  CHECK(depth_order_in_effect(sub) == PanelCommand::kSurface);
  // Planes dived by hand: none.
  dive.depth_ft = c.draft_ft + 30;
  dive.planes = -60;
  CHECK(depth_order_in_effect(sub) == std::nullopt);
}

TEST_CASE("controls: the panel reads each tube's state", "[controls][cmdpanel]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  TorpedoTubes& tubes = mission->player_combat().tubes;
  REQUIRE(tubes.count() == 10);
  tubes.set_tube(0, 1);
  tubes.set_tube(1, -1);
  tubes.set_timer(1, (90 << 16) + 1);
  tubes.set_tube(2, 0);
  tubes.set_tube(3, 1);
  tubes.damage(3).flags = TubeDamage::kDamaged;
  tubes.set_tube(4, -1);
  tubes.set_timer(4, TorpedoTubes::kNoTimer);
  const PanelStatus s = command_panel_status(mission->player(), tubes);
  CHECK(s.tubes[0].state == TubeState::kLoaded);
  CHECK(s.tubes[0].label.empty());
  CHECK(s.tubes[1].state == TubeState::kReloading);
  CHECK(s.tubes[1].reload_seconds == 91);  // rounded up
  CHECK(s.tubes[2].state == TubeState::kEmpty);
  CHECK(s.tubes[3].state == TubeState::kDamaged);
  CHECK(s.tubes[4].state == TubeState::kReloading);
  CHECK(s.tubes[4].reload_seconds == 0);
}

TEST_CASE("controls: a boat's missing tubes are absent from the panel", "[controls][cmdpanel]") {
  SH_REQUIRE_INSTALL(install);
  // HIST01's BALAO has 10 tubes; an S-boat or a Gato-less loadout has
  // fewer: the panel's tubes past `count()` are absent.
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  const SubClass& balao = mission->player_class();
  SubClass small = balao;
  small.bow_tubes = 4;
  small.stern_tubes = 2;
  const TorpedoTubes tubes(small);
  const PanelStatus s = command_panel_status(mission->player(), tubes);
  for (std::size_t t = 0; t < kPanelTubeCount; ++t) {
    CHECK((s.tubes[t].state == TubeState::kAbsent) == (t >= 6));
  }
}

TEST_CASE("controls: the panel's status, progress and absent tubes", "[controls][cmdpanel]") {
  CommandPanel panel = open_panel();
  CHECK(!panel.status());
  CHECK(panel.set_status(some_status()));
  CHECK(!panel.set_status(some_status()));
  CHECK(panel.reload_progress(1) == 0);
  PanelStatus later = some_status();
  later.tubes[1].reload_seconds = 25;
  panel.set_status(later);
  CHECK(panel.reload_progress(1) == 0.75f);
  CHECK(panel.reload_progress(0) == 0);
  // A new reload starts from 0 again.
  later.tubes[1].state = TubeState::kLoaded;
  panel.set_status(later);
  later.tubes[1] = {TubeState::kReloading, 50, ""};
  panel.set_status(later);
  CHECK(panel.reload_progress(1) == 0);

  // An absent tube takes no tap; a present one gives its command.
  const auto rects = panel.icon_rects();
  const PanelRect t5 = rects[static_cast<std::size_t>(PanelCommand::kFireTube5)];
  panel.press(cx(t5), cy(t5));
  CHECK(!panel.release(cx(t5), cy(t5)).command);
  const PanelRect t1 = rects[static_cast<std::size_t>(PanelCommand::kFireTube1)];
  panel.press(cx(t1), cy(t1));
  CHECK(panel.release(cx(t1), cy(t1)).command == PanelCommand::kFireTube1);
  // Without a state, every tube takes taps as today.
  panel.set_status(std::nullopt);
  panel.press(cx(t5), cy(t5));
  CHECK(panel.release(cx(t5), cy(t5)).command == PanelCommand::kFireTube5);
}

TEST_CASE("controls: the panel draws the live state, and today's look without one", "[controls][cmdpanel]") {
  CommandPanel plain = open_panel();
  CommandPanel cleared = open_panel();
  cleared.set_status(some_status());
  cleared.set_status(std::nullopt);
  const std::vector<std::uint32_t> today = render(plain);
  CHECK(render(cleared) == today);
  CHECK(count(today, plain.panel_rect(), kGreen) == 0);

  CommandPanel live = open_panel();
  live.set_status(some_status());
  const std::vector<std::uint32_t> lit = render(live);
  const auto rects = live.icon_rects();
  // P lit: a green frame and lamp; R dark; tube 1 loaded in green; tube 5
  // absent: the plate's colour only.
  CHECK(count(lit, rects[static_cast<std::size_t>(PanelCommand::kPeriscopeDepth)], kGreen) > 100);
  CHECK(count(lit, rects[static_cast<std::size_t>(PanelCommand::kRadarDepth)], kGreen) == 0);
  CHECK(count(lit, rects[static_cast<std::size_t>(PanelCommand::kFireTube1)], kGreen) > 100);
  CHECK(count(lit, rects[static_cast<std::size_t>(PanelCommand::kFireTube3)], kGreen) == 0);
  const PanelRect t5 = rects[static_cast<std::size_t>(PanelCommand::kFireTube5)];
  CHECK(count(lit, t5, 0xc00000) == 0);
  CHECK(count(today, t5, 0xc00000) > 0);
}

TEST_CASE("controls: live panel window screenshots", "[.controls_panel_window][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const char* frame_path = std::getenv("SH_PANEL_FRAME");
  const char* out = std::getenv("SH_PANEL_OUT");
  REQUIRE(frame_path != nullptr);
  REQUIRE(out != nullptr);
  const CommandIcons icons = build_command_icons(CommandIconArt(install, ShPaths::load(install)));
  SDL_Surface* frame = SDL_LoadBMP(frame_path);
  REQUIRE(frame != nullptr);
  SDL_Surface* window = SDL_CreateSurface(2400, 1080, SDL_PIXELFORMAT_XRGB8888);
  SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(window);
  SDL_SetRenderLogicalPresentation(renderer, kScreenWidth, kScreenHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_Texture* picture = SDL_CreateTextureFromSurface(renderer, frame);
  SDL_SetTextureScaleMode(picture, SDL_SCALEMODE_LINEAR);
  {
    CommandPanel panel;
    panel.set_window(2400, 1080, 2.625f);
    const PanelRect b = panel.button_rect();
    panel.press(cx(b), cy(b));
    panel.release(cx(b), cy(b));
    PanelStatus s;
    s.depth_order = PanelCommand::kPeriscopeDepth;
    s.tubes = {TubeStatus{TubeState::kLoaded, 0, ""},     TubeStatus{TubeState::kLoaded, 0, ""},
               TubeStatus{TubeState::kReloading, 400, ""}, TubeStatus{TubeState::kLoaded, 0, ""},
               TubeStatus{TubeState::kEmpty, 0, ""},       TubeStatus{TubeState::kDamaged, 0, ""},
               TubeStatus{TubeState::kLoaded, 0, ""},     TubeStatus{TubeState::kReloading, 400, ""},
               TubeStatus{TubeState::kEmpty, 0, ""},       TubeStatus{TubeState::kLoaded, 0, ""}};
    panel.set_status(s);
    s.tubes[2].reload_seconds = 160;
    s.tubes[7].reload_seconds = 35;
    panel.set_status(s);
    CommandPanelView view(renderer, icons);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, picture, nullptr, nullptr);
    view.draw(panel);
    SDL_RenderPresent(renderer);
  }
  CHECK(SDL_SaveBMP(window, out));
  SDL_DestroyTexture(picture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(window);
  SDL_DestroySurface(frame);
}

}  // namespace sh

namespace sh {

TEST_CASE("controls: a hidden floating button takes no press, the panel opens above the bar", "[controls][cmdpanel]") {
  CommandPanel panel;
  panel.set_window(2400, 1080, 2.625f);
  panel.set_button_shown(false);
  const PanelRect b = panel.button_rect();
  CHECK(!panel.press(cx(b), cy(b)).consumed);
  panel.release(cx(b), cy(b));
  CHECK(!panel.is_open());
  // Opened above the bar's ORDERS button (bottom middle of the window).
  const PanelRect anchor{1100, 1030, 160, 50};
  panel.open_above(anchor);
  REQUIRE(panel.is_open());
  const PanelRect p = panel.panel_rect();
  CHECK(p.y + p.h <= anchor.y);
  CHECK(cx(p) == cx(anchor));
  // Its icons work; a tap outside closes it.
  const PanelRect t1 = panel.icon_rects()[static_cast<std::size_t>(PanelCommand::kFireTube1)];
  panel.press(cx(t1), cy(t1));
  CHECK(panel.release(cx(t1), cy(t1)).command == PanelCommand::kFireTube1);
  panel.press(10, 10);
  panel.release(10, 10);
  CHECK(!panel.is_open());
  // Shown again, the button opens it beside itself.
  panel.set_button_shown(true);
  panel.press(cx(b), cy(b));
  panel.release(cx(b), cy(b));
  REQUIRE(panel.is_open());
  CHECK(panel.panel_rect().x + panel.panel_rect().w <= b.x);
}

}  // namespace sh

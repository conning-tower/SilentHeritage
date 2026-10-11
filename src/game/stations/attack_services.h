// What the outside and attack stations (bridge, periscope, TBT, deck gun,
// TDC, radar) share: the calls they make into the mission screen hosting
// them, the state the original keeps in globals between them (the view
// magnification, the lower-panel mode of the scope and TBT, the TDC
// sub-panel), the in-mission sprites and fonts they draw with, and their
// common controls (the auto-repeating train areas, the key hotspots, the
// bearing tape).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "core/features.h"
#include "core/fixed.h"
#include "core/image.h"
#include "game/station.h"
#include "game/station_env.h"
#include "game/world_view_adapter.h"
#include "gfx/text.h"
#include "gfx/viewport.h"
#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/event.h"
#include "ui/mouse.h"

namespace sh {

class Install;
class Mission;

// One button of a station's command bar menu (12-byte records, e.g. the
// bridge's 0x10312c): its frames in the command bar sprites and its action.
struct CommandBarItem {
  std::int32_t frame = 0;
  std::int32_t pressed_frame = 0;
  std::function<void()> action;
};

// A station's command bar (`control_bar_open` definitions, e.g. 0x11667c).
struct CommandBarMenu {
  std::vector<CommandBarItem> items;
  // +8: run on Esc / a right click; empty: abort the mission (the default
  // `cmd_abort_mission`).
  std::function<void()> on_cancel;
  bool time_compression = true;  // +0xc
  bool clock = true;             // +0x10
  bool hotkeys = true;           // +0x14
};

// The screens that show the shared nav panel (`gauges_panel_open` with the
// station's control definitions: bridge 0x103150.. with BRDGNAV.SHP, scope
// 0x10d048.. with SCOPENAV.SHP, TBT 0x10edc4.. with TBTNAV.SHP) or the
// recognition manual (`idbook_show(0, 299, 0x260, 0xa0)` on the scope and
// TBT).
enum class PanelSite : std::uint8_t { kBridge, kScope, kTbt };

// A dialog panel of another module shown inside a station (the nav panel,
// the recognition manual), driven like the station itself.
class StationPanel {
 public:
  virtual ~StationPanel() = default;
  virtual void open() = 0;
  virtual void event(const ui::Event& event) = 0;
  virtual void process() = 0;
  // `full_redraw`: the screen was redrawn under the panel (0x11a898).
  virtual void update(bool full_redraw) = 0;
  virtual void close() = 0;
  // Whether a drag of the extended variant takes the events
  // (`Station::capturing`).
  virtual bool capturing() const { return false; }
};

// The calls the stations make into the mission screen.
class AttackStationHost {
 public:
  virtual ~AttackStationHost() = default;
  // Opens the in-game command bar with the station's menu (`control_bar_open`).
  virtual void open_command_bar(const CommandBarMenu& menu) = 0;
  // Closes it when a station changes its lower panel (`control_bar_close`).
  virtual void close_command_bar() = 0;
  // Shows the palette black (`palette_fade_out(0, 0x100)`, as a station's
  // screen is drawn), and fades it in (`palette_fade_in(0x11a274, 0, 10)`,
  // on the station's first update).
  virtual void fade_out() = 0;
  virtual void fade_in() = 0;
  // Returns the nav panel or the recognition manual shown at `site`, or
  // nullptr when the host has none.
  virtual std::unique_ptr<StationPanel> make_nav_panel(PanelSite site) = 0;
  virtual std::unique_ptr<StationPanel> make_id_book(PanelSite site) = 0;
  // Rig for red (0x11a8c0): the scope, TDC and radar load their `.PAL`.
  virtual bool rig_for_red() const = 0;
  // Plays sound effect `sfx` at full volume (`sound_play_sfx(sfx, -1, -1)`).
  virtual void play_sfx(std::int32_t sfx) = 0;
  // Plays sound effect `sfx` at `volume` 0..127 (`sound_play_sfx(sfx,
  // volume, -1)`; the button clicks are sfx 2 at 30).
  virtual void play_sfx_at(std::int32_t sfx, std::int32_t volume) {
    static_cast<void>(volume);
    play_sfx(sfx);
  }
  // The full redraw flag of the frame (0x11a898), set after a menu or a
  // modal dialog covered the station.
  virtual bool full_redraw() const { return false; }
};

// The host of the stations in the mission screen (`MissionScreen`): the
// command bar, the DAC fades, the UI flags and the sound of its
// `StationEnv`, which must outlive it. The nav panel and the recognition
// manual come from the factories when set.
class StationEnvHost final : public AttackStationHost {
 public:
  explicit StationEnvHost(StationEnv& env) : env_(env) {}

  void open_command_bar(const CommandBarMenu& menu) override;
  void close_command_bar() override;
  void fade_out() override;
  void fade_in() override;
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite site) override;
  std::unique_ptr<StationPanel> make_id_book(PanelSite site) override;
  bool rig_for_red() const override;
  void play_sfx(std::int32_t sfx) override;
  void play_sfx_at(std::int32_t sfx, std::int32_t volume) override;
  bool full_redraw() const override;

  std::function<std::unique_ptr<StationPanel>(PanelSite)> nav_panels;
  std::function<std::unique_ptr<StationPanel>(PanelSite)> id_books;

 private:
  StationEnv& env_;
};

// The lower panel of the scope and TBT (0x11bcd8).
enum class LowerPanel : std::int32_t { kNav = 0, kTdc = 1, kIdBook = 2 };

// The TDC panels (0x1282b8) of the scope's and TBT's TDC lower panel.
enum class TdcSubPanel : std::int32_t { kSolution = 0, kGyro = 1, kTorpedo = 2 };

// The magnifications of the scope, TBT and deck gun (0x11ac3b): 1x, 2x, 4x,
// 8x, then back to 1x.
Fixed16 next_magnification(Fixed16 zoom);

// The services and shared state of the outside and attack stations.
class AttackServices {
 public:
  // All references must outlive the services. Loads GLOBAL.SHP (the target
  // marker, `init_simulation`) and BEARING.FNT (the bearing tapes,
  // 0x11a25c); `cursors` is CURSORS.SHP.
  AttackServices(const Install& install, Mission& mission, ui::Gui& gui, VgaPalette& palette, StationRegistry& stations,
                 AttackStationHost& host, WorldViewAdapter& world, const Shp& cursors);

  const Install& install() const { return install_; }
  Mission& mission() { return mission_; }
  ui::Gui& gui() { return gui_; }
  Image8& screen() { return gui_.screen(); }
  // The screen palette 0x11a274.
  VgaPalette& palette() { return palette_; }
  StationRegistry& stations() { return stations_; }
  AttackStationHost& host() { return host_; }
  WorldViewAdapter& world() { return world_; }
  const Shp& cursors() const { return cursors_; }
  const Shp& global_sprites() const { return global_; }
  const Font& bearing_font() const { return bearing_font_; }

  // Selects the station the loop switches to (`0x11a860 = ...`).
  void go_to(StationId id) { stations_.select(id); }
  // Sets the mouse cursor (`mouse_set_cursor`).
  void set_cursor(ui::CursorId id);
  // The dialogs' default cursor (0x11dca8).
  void set_default_cursor();

  // Returns the 100 Hz ticks since the last call with the same counter, the
  // original's per-station timer counters 0x129bc0.. (counted by
  // `game_timer_tick`).
  std::uint32_t elapsed(std::uint32_t& counter);
  // Advances the 3D view's sea drift and light timers to the GUI clock and
  // updates the sky; returns true when the station palette must be shaded
  // again (`0x129c5c`). The outside stations call it in their update.
  bool advance_world();
  // Shades the world ranges of the screen palette
  // (`world_shade_screen_palette`); with `overlay`, the station PCX palette
  // of the bridge, deck gun and TBT, their palette hook then dims the
  // overlay colours 160-255 (`bridge_shade_palette_cb`).
  void shade_palette(const VgaPalette* overlay);
  // Renders the 3D view of `eye` into the world frame (`world_render_view`,
  // with the game's generator for the tracers) and steps the blinking light
  // colours of the screen palette (`world_set_camera`).
  RenderList render_world(const ViewEye& eye);
  // Returns the window of the world frame a station shows: columns `x0`..
  // `x1` of the 200 rows from the swell offset (+ `top`..`bottom` rows of
  // it), e.g. the scope's (0, 0)-(200, 199).
  Viewport world_window(int x0, int top, int x1, int bottom);
  // Copies `window` to the screen at (`x`, `y`).
  void blit_to_screen(const Viewport& window, int x, int y);
  // Draws the TDC target marker (`tdc_draw_target_marker`, 0x7c60e): when
  // the TDC has a target, locked or in the blink's visible half, GLOBAL.SHP
  // frame 37 in `colour` at the target's column of `eye`, row `y` of
  // `window`.
  void draw_target_marker(const Viewport& window, const ViewEye& eye, int y, std::uint8_t colour);
  // Draws the photo-recon indicators (`player_draw_recon_target_marker`,
  // 0x60e17) in `colour` into `window`: GLOBAL.SHP frame 38 at (`x`, `y`)
  // while the recon target is in view, in the TDC blink's visible half,
  // and frame 39 on the recon target, if any, seen from `eye`.
  void draw_recon_marker(const Viewport& window, const ViewEye& eye, int x, int y, std::uint8_t colour);
  // The swell offset of the view (0x124ef4, 30 on a calm sea).
  std::int32_t bob() const;
  // Runs `fn` (which opens dialogs, flushing the event queue) keeping the
  // pending events, as `hotkey_proc` re-posts the activation it drained.
  void keep_events(const std::function<void()>& fn);
  // Returns whether the screen must be redrawn in full this frame (the
  // host's 0x11a898).
  bool redraw_requested() const { return host_.full_redraw(); }

  // Shared state.
  Fixed16 zoom = Fixed16::from_int(1);             // 0x11ac3b
  LowerPanel lower_panel = LowerPanel::kNav;       // 0x11bcd8
  TdcSubPanel tdc_panel = TdcSubPanel::kSolution;  // 0x1282b8
  // The improvements beyond the original the stations run with (not in
  // the original; the mission's `MissionUiState::features`).
  Features features{};

 private:
  const Install& install_;
  Mission& mission_;
  ui::Gui& gui_;
  VgaPalette& palette_;
  StationRegistry& stations_;
  AttackStationHost& host_;
  WorldViewAdapter& world_;
  const Shp& cursors_;
  Shp global_;
  Font bearing_font_;
  std::uint32_t world_ticks_ = 0;
  bool world_ticks_set_ = false;
};

// The auto-repeating train area of the bridge, periscope and TBT (and the
// deck gun's repeat buttons): a pressed mouse button repeats every 10
// ticks with a step growing from 0 each repeat; a key repeats at once with
// step 5 (`bridge_look_button_event` / `_repeat`, `scope_train_button_*`,
// `tbt_repeat_button_*`, `deckgun_repeat_button_*`).
ui::Area& add_train_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::uint32_t key,
                         std::function<void(std::int32_t step)> action, std::function<void(bool on)> hover);

// A key-only hotspot that runs `action` once per press (`hotkey_proc`).
ui::Area& add_hotkey(ui::Dialog& dialog, std::uint32_t key, std::function<void()> action);

// A click area that runs `action` once per press (`hotkey_proc_simple`).
ui::Area& add_click_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::uint32_t key,
                         std::function<void()> action, std::function<void(bool on)> hover = {});

// The click of the sprite buttons (`sound_play_sfx(2, 0x1e, -1)`).
inline constexpr std::int32_t kButtonClickSfx = 2;
inline constexpr std::int32_t kButtonClickVolume = 0x1e;

// A sprite button counting its presses (`cycle_button_proc`, 0x49472, with
// a definition {x0, y0, x1, y1, sprites, normal, pressed, repeat, action}):
// frame `normal` of `sprites` at (x0, y0) of the dialog, `pressed` while
// held; a press plays the click and calls `action(n)`, n the presses
// since the release (auto-repeating every `repeat` ticks for the mouse).
ui::Area& add_cycle_button(ui::Dialog& dialog, AttackStationHost& host, int x0, int y0, int x1, int y1,
                           std::uint32_t key, const Shp& sprites, std::size_t normal, std::size_t pressed,
                           std::uint32_t repeat, std::function<void(std::int32_t)> action,
                           std::function<void(bool on)> hover);

// A sprite button showing a state (`push_button_proc`, 0x49613, with a
// definition {x0, y0, x1, y1, sprites, base, callback}): frame `base` +
// `state()` of `sprites` at (x0, y0); a press plays the click and calls
// `advance()` first. The hover shows the SET cursor (`control_hover_cursor`).
ui::Area& add_push_button(ui::Dialog& dialog, AttackServices& services, int x0, int y0, int x1, int y1,
                          const Shp& sprites, std::size_t base, std::function<void()> advance,
                          std::function<std::int32_t()> state);

// Returns `bearing` turned by `step` (clamped to 1..30) quarter degrees left
// (negative `sign`) or right (`bridge_turn_left/right`, `scope_train_*`).
Angle10 train_bearing(Angle10 bearing, std::int32_t step, int sign);

// Redraws the bearing tape of `bearing` (tenths) over (x0, y0)-(x1, y1) of
// `screen` (`draw_bearing_tape`, 0x4b18d): the background of `background`,
// a tick every 3 px per degree (5 px long at the 5's, labels from `font`
// through `colours` every 10), the current bearing at the centre column.
void draw_bearing_tape(Image8& screen, const Image8& background, const Font& font, Angle10 bearing, int x0, int y0,
                       int x1, int y1, std::uint8_t colour, const GlyphColourTable& colours);

// Returns a glyph colour table of zeros but the given entries (list 0xff
// to keep it transparent), the BSS tables the stations patch for their
// tapes and counters.
GlyphColourTable patched_colours(std::initializer_list<std::pair<std::uint8_t, std::uint8_t>> entries);

}  // namespace sh

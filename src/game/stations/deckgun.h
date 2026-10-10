// The deck gun station (deckgun.cpp, Alt-F4; vtable 0x102b8c): the gun
// sight's 3D view from the selected mount (DECKGUN.PCX, DECKGUN.SHP), the
// train and elevation dials, the target range, rounds left, solution
// quality and ready lamp, the muzzle flash, and the controls to train,
// elevate, zoom, fire, lock a target, switch between manual aim and the
// crew's fire, and select the forward or aft gun.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "core/image.h"
#include "game/station.h"
#include "game/stations/attack_services.h"
#include "gfx/text.h"
#include "ui/dialog.h"
#include "ui/event.h"

namespace sh {

class DeckGunStation final : public Station {
 public:
  // Builds the station's dialog (`deckgun_init_dialogs`, 0x36054);
  // `services` must outlive it.
  explicit DeckGunStation(AttackServices& services);
  ~DeckGunStation() override;

  // `deckgun_screen` (0x36cd9).
  void open() override;
  void event(const ui::Event& event) override;
  void process() override;
  // `deckgun_update` (0x36fa4): back to the conn once submerged.
  void update() override;
  // `deckgun_close` (0x36f34).
  void close() override;
  bool expensive() const override { return true; }
  bool outdoors() const override { return true; }
  bool shows_world() const override { return true; }

  // The control actions, also bound to the station's keys.
  // Trains the selected mount by `step` (1..50) tenths left (`sign` < 0) or
  // right, out of the tower's dead zone (`deckgun_train_left/right`).
  void train(std::int32_t step, int sign);
  // Raises (`sign` > 0) or lowers the selected mount's elevation offset by
  // `step` (1..3) tenths, the total kept within 0..45 deg
  // (`deckgun_elevate_up/down`).
  void elevate(std::int32_t step, int sign);
  // `deckgun_cycle_zoom` (0x379cd).
  void cycle_zoom();
  // `deckgun_fire_selected` (0x37a84).
  void fire();
  // `deckgun_toggle_target_lock` (0x384be).
  void toggle_lock();
  // `deckgun_toggle_manual` (0x37a60).
  void toggle_manual();
  // `deckgun_select_fwd_gun` / `_aft_gun` / `deckgun_toggle_gun`: no-ops on
  // a one-gun boat.
  void select_gun(std::int32_t mount);

 private:
  struct Controls;

  void open_command_bar();
  void render_view();
  void draw_tower(const Viewport& view);
  void draw_target_marker(const Viewport& view);
  void draw_fire_animation();
  void draw_zoom_indicator();
  void draw_elevation_dial();
  void advance_timers();

  AttackServices& services_;
  std::unique_ptr<ui::Dialog> dialog_;
  std::unique_ptr<Controls> controls_;
  Image8 background_;           // DECKGUN.PCX (screen 0x11a574)
  VgaPalette pcx_palette_{};    // 0x11be50, dimmed by the palette hook
  Shp sprites_;                 // DECKGUN.SHP (0x11bd44)
  Font digits_;                 // DIGITS3.FNT (0x11bd48)
  GlyphColourTable colours_{};  // 0x11bd50
  // The timer counters 0x129bd4 (view), 0x129bd8 (zoom plate), 0x129be8
  // (muzzle flash), in 100 Hz ticks.
  std::uint32_t last_tick_ = 0;
  std::int32_t view_ticks_ = 0;
  std::int32_t zoom_ticks_ = 0;
  std::int32_t fire_ticks_ = 0;
  // What is drawn (0x11c154.., -1: redraw).
  std::int32_t drawn_train_ = -1;      // 0x11c154
  std::int32_t drawn_elevation_ = -1;  // 0x11c158
  std::int32_t drawn_range_ = -1;      // 0x11c15c
  std::int32_t drawn_ammo_ = -1;       // 0x11c160
  std::int32_t drawn_quality_ = -1;    // 0x11c164
  bool ready_ = false;                 // 0x11c168
  std::int32_t drawn_gun_ = -1;        // 0x11c16c
  std::int32_t drawn_manual_ = -2;     // 0x11c170
  std::int32_t label_gun_ = 0;         // 0x11c188
  std::int32_t range_readout_ = 0;     // 0x11c1bc, yards for the elevation
  std::int32_t fire_frame_ = -1;       // 0x11c184
  // The gun picked on the command bar, selected at the next update.
  std::optional<std::int32_t> pending_gun_;
  bool zoom_plate_ = false;            // 0x11c150
  std::int32_t zoom_offset_ = 0;       // 0x11c17c
  std::int32_t zoom_speed_ = 0;        // 0x11c180
  std::int32_t last_reload_ = 0;       // detects the crew's shots
};

}  // namespace sh

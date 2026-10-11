// The periscope (F2, scope.cpp, vtable 0x102b44): the 201-wide view at
// the periscope's height through the reticle, trained with the handles or
// ',' / '.', magnified 1x to 8x, the bearing tape, and the lower panel the
// scope shares with the TBT (`0x11bcd8`): the nav panel (SCOPENAV.SHP), the
// TDC panels, or the recognition manual over SCOPEID.PCX.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "assets/pcx.h"
#include "assets/shp.h"
#include "game/station.h"
#include "game/stations/attack_services.h"
#include "game/stations/tdc.h"
#include "game/stations/view_drag.h"
#include "ui/dialog.h"

namespace sh {

class ScopeStation final : public Station {
 public:
  explicit ScopeStation(AttackServices& services);
  ~ScopeStation() override;

  // scope_screen (0x6d0a3)
  void open() override;
  // scope_station_event (0x6d2b1)
  void event(const ui::Event& event) override;
  // scope_station_process (0x6d2c2)
  void process() override;
  // scope_station_update (0x6d2d1): back to the conn once the periscope is
  // down; the view is redrawn on the first frame, when the bearing changed
  // or every 6 ticks.
  void update() override;
  // scope_station_close (0x6d26e)
  void close() override;
  bool expensive() const override { return true; }
  bool shows_world() const override { return true; }
  bool capturing() const override {
    return (drag_ && drag_->dragging()) || (panel_ && panel_->capturing()) || (tdc_ && tdc_->capturing());
  }
  bool rig_red_redraw() const override { return true; }

  // Switches the lower panel (`scope_set_mode`, 0x6dad7); a `tdc_panel`
  // also selects the TDC sub-panel. The switch closes the dialogs whose
  // controls ask for it, so it takes place once the current event, process
  // or update call returns.
  void set_mode(LowerPanel mode, std::optional<TdcSubPanel> tdc_panel = std::nullopt);

 private:
  struct ModeChange {
    LowerPanel mode;
    std::optional<TdcSubPanel> tdc_panel;
  };
  void apply_mode_change();
  void load_overlays(bool redraw);  // scope_load_overlays (0x6d4d9)
  void leave_mode();                // scope_leave_mode (0x6d810)
  void open_main_dialog();
  void render_view();  // scope_render_view (0x6d3ca)
  void blit_view();
  void update_bearing_scale();  // scope_update_bearing_scale (0x6d472)
  void toggle_magnification();  // scope_toggle_magnification (0x6dcbb)
  void draw_magnification(ui::Dialog& dialog);
  void train(std::int32_t step, int sign);
  void mark_target();      // tdc_mark_target through the scope's thunk 0x6dece
  void toggle_tdc_view();  // scope_toggle_tdc_view (0x6dfed)
  void cycle_tdc_panel();  // scope_cycle_tdc_panel (0x6ded3)
  void pick_target();
  CommandBarMenu menu() const;

  AttackServices& s_;
  PcxImage background_;                      // SCOPE.PCX or SCOPEID.PCX
  std::optional<Shp> scope_;                 // SCOPE.SHP 0x12661c: reticle, magnification lamps
  std::optional<Shp> nav_;                   // SCOPENAV.SHP 0x126620
  std::optional<Shp> tdc_buttons_;           // SCOPETDC.SHP: the TDC panel button
  std::unique_ptr<ui::Dialog> dialog_;       // 0x126890: handles and keys
  std::unique_ptr<ui::Dialog> mode_dialog_;  // 0x126aec: the TDC mode's keys and button
  std::unique_ptr<StationPanel> panel_;      // the nav panel or the ID book
  std::unique_ptr<TdcPanels> tdc_;
  // The drag of the view (extended `drag_rotate`), while open.
  std::unique_ptr<ViewDrag> drag_;
  int view_x_ = 0xdb;     // 0x12662c
  int view_y_ = 0x77;     // 0x126630
  int dialog_y_ = 0x183;  // 0x126894
  std::uint32_t render_mark_ = 0;
  // The ticks the next periodic render waits beyond the usual 6 (the zoom
  // click sets the counter to -5).
  std::uint32_t render_delay_ = 0;
  std::int32_t tape_bearing_ = -1;  // 0x12663c
  std::optional<ModeChange> pending_;
};

}  // namespace sh

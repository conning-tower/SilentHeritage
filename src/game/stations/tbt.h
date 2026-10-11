// The target bearing transmitter (F3, tbt.cpp, vtable 0x102ab4): the
// binoculars on the bridge, a 291 x 125 view from the bridge height along
// the look bearing it shares with the periscope, magnified 1x to 8x, with
// the bearing dial, the mark button and the lower panel the TBT shares with
// the periscope (`0x11bcd8`): nav (TBTNAV.SHP), TDC or recognition manual
// (TBTID.SHP). It works only while the bridge is above water.
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

// The slide of the TBT's magnification plate (`tbt_draw_zoom_indicator`,
// 0x7835a): the plate of the new power slides out from under its cover in
// steps of 10 every 5 ticks until past its width, then back in one step of
// width - 5.
struct ZoomPlate {
  bool active = true;       // 0x1282e4
  std::int32_t offset = 0;  // 0x1282ec
  std::int32_t speed = 0;   // 0x1282f0
  std::int32_t ticks = 0;   // 0x129bd8, 100 Hz ticks since the last step

  // Starts the slide (`tbt_cycle_zoom`).
  void start() {
    active = true;
    speed = 10;
    ticks = 100;
  }
  // Advances it for a plate `width` pixels wide; returns false while it
  // waits for its next step (nothing to draw).
  bool step(std::int32_t width);
};

class TbtStation final : public Station {
 public:
  explicit TbtStation(AttackServices& services);
  ~TbtStation() override;

  // tbt_screen (0x77941)
  void open() override;
  // tbt_event (0x77b7c)
  void event(const ui::Event& event) override;
  // tbt_process (0x77b8d)
  void process() override;
  // tbt_update (0x77b9c)
  void update() override;
  // tbt_close (0x77b2f)
  void close() override;
  bool expensive() const override { return true; }
  bool capturing() const override {
    return (drag_ && drag_->dragging()) || (panel_ && panel_->capturing()) || (tdc_ && tdc_->capturing());
  }
  bool outdoors() const override { return true; }
  bool shows_world() const override { return true; }

  // Switches the lower panel (`tbt_set_mode`, 0x78253), once the current
  // event, process or update call returns.
  void set_mode(LowerPanel mode, std::optional<TdcSubPanel> tdc_panel = std::nullopt);

 private:
  struct ModeChange {
    LowerPanel mode;
    std::optional<TdcSubPanel> tdc_panel;
  };
  void apply_mode_change();
  void load_overlays();    // tbt_load_overlays (0x77d2d)
  // Sets the side fill of a wide screen: the screen continued past its
  // borders (`make_panel_ends`; not in the original).
  void set_side_ends();
  void unload_overlays();  // tbt_unload_overlays (0x77ff5)
  void render_view();
  void blit_view();
  void draw_bearing_dial();    // tbt_draw_bearing_dial (0x78313)
  void draw_zoom_indicator();  // tbt_draw_zoom_indicator (0x7835a)
  void cycle_zoom();           // tbt_cycle_zoom (0x78641)
  void train(std::int32_t step, int sign);
  void mark_target();
  void pick_target();
  void show_tdc_panel();   // tbt_show_tdc_panel (0x78809)
  void cycle_tdc_panel();  // tbt_cycle_tdc_subpanel (0x786d9)
  CommandBarMenu menu();

  AttackServices& s_;
  PcxImage background_;                      // TBT.PCX
  VgaPalette pcx_palette_{};                 // 0x1284f4, for the overlay dimming
  std::optional<Shp> tbt_;                   // TBT.SHP 0x1282bc: reticle, buttons, plates
  std::optional<Shp> lower_;                 // TBTNAV / TBTTDC / TBTID.SHP 0x1282c0
  std::unique_ptr<ui::Dialog> dialog_;       // 0x12883c
  std::unique_ptr<ui::Dialog> mode_dialog_;  // 0x128ad0, the TDC mode's
  std::unique_ptr<StationPanel> panel_;
  std::unique_ptr<TdcPanels> tdc_;
  // The drag of the view (extended `drag_rotate`), while open.
  std::unique_ptr<ViewDrag> drag_;
  std::uint32_t render_mark_ = 0;
  std::uint32_t render_delay_ = 0;
  std::uint32_t plate_mark_ = 0;
  std::int32_t dial_bearing_ = -1;  // 0x1282e0
  ZoomPlate plate_;
  std::optional<ModeChange> pending_;
  // The side fill is set after the next update has drawn the lower panel.
  bool side_ends_due_ = false;
};

}  // namespace sh

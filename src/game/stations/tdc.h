// The Torpedo Data Computer station (tdc.cpp, F6) and its four panels
// (`tdc_*_panel_*`, dialogs 0x128ecc tube, 0x129208 solution, 0x12942c
// gyro, 0x1295a8 torpedo), which the periscope and the TBT also show in
// their TDC lower panel: the tube panel plus one of the other three
// (sub-panel 0x1282b8).
#pragma once

#include <memory>

#include "game/station.h"
#include "game/stations/attack_services.h"
#include "ui/event.h"

namespace sh {

// Where the TDC panels are shown: the TDC station (all four, on TDC.PCX),
// the periscope (SCOPETDC.SHP + SCOPTDC0/1/2.SHP) or the TBT (TBTTDC.SHP +
// TBTTDC0/1/2.SHP).
enum class TdcPanelSite : std::uint8_t { kTdc, kScope, kTbt };

// The TDC panels of one site.
class TdcPanels {
 public:
  TdcPanels(AttackServices& services, TdcPanelSite site);
  ~TdcPanels();
  TdcPanels(const TdcPanels&) = delete;
  TdcPanels& operator=(const TdcPanels&) = delete;

  // Draws the panels' background sprites on the screen and opens their
  // dialogs: on the scope and TBT, the tube panel and the sub-panel
  // `services.tdc_panel` (the `scope_load_overlays` / `tbt_load_overlays`
  // mode 1 branches); on the TDC station all four.
  void open();
  // The station's event, process and update calls for the open panels.
  void event(const ui::Event& event);
  void process();
  void update();
  // Closes the dialogs and frees the sprites.
  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// The TDC station (vtable 0x102ad8): TDC.PCX (TDC.PAL when rigged for
// red) with the four panels; must be reopened on rig for red.
class TdcStation final : public Station {
 public:
  explicit TdcStation(AttackServices& services);
  ~TdcStation() override;

  void open() override;
  void event(const ui::Event& event) override;
  void process() override;
  void update() override;
  void close() override;
  bool rig_red_redraw() const override { return true; }

 private:
  AttackServices& services_;
  std::unique_ptr<TdcPanels> panels_;
};

}  // namespace sh

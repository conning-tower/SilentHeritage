// The radar station (radar.cpp, F9, vtable slots 0x653c6..0x65374): the SJ
// surface search set on the left (A-scope, or PPI with the PPI scope, its
// power switch, range knob, hold switch, train knob and bearing counter,
// range readout) and the SD air search set on the right (A-scope, power
// switch, range knob, range readout), on RADAR.PCX (RADAR.PAL rigged for
// red); a set that is not fitted is covered by RADAR1.SHP. The sets
// themselves are the simulation's (sim/radar.h).
#pragma once

#include <cstdint>
#include <memory>

#include "game/station.h"
#include "game/stations/attack_services.h"
#include "sim/geometry.h"
#include "sim/radar.h"
#include "ui/event.h"

namespace sh {

// The centres of the SJ and SD scopes on the screen (0xaa, 0x99) and
// (0x1d5, 0x99).
inline constexpr fx::Point2 kSjScopeCentre{0xaa, 0x99};
inline constexpr fx::Point2 kSdScopeCentre{0x1d5, 0x99};
// The A-scope baseline row on the screen (0xc0).
inline constexpr int kAScopeBaseline = 0xc0;

// Returns the display range of range scale `scale` 0..4: 2500 << scale yd.
constexpr std::int32_t radar_display_range(std::int32_t scale) { return 0x9c4 << scale; }

// Returns the offset from the PPI centre at which `blip` is plotted on a
// scope of `radius` pixels showing `range_yd` (`sj_radar_draw_ppi`): the
// point (0, -range x radius / range_yd) turned by the blip's bearing
// relative to the bow (up).
fx::Point2 ppi_blip_offset(const RadarBlip& blip, std::int32_t radius, std::int32_t range_yd);

// Returns the A-scope column of `blip` on a scope showing `range_yd`
// (`radar_draw_ascope`): range x 180 / range_yd.
std::int32_t ascope_blip_column(const RadarBlip& blip, std::int32_t range_yd);

class RadarStation final : public Station {
 public:
  // `services` must outlive the station.
  explicit RadarStation(AttackServices& services);
  ~RadarStation() override;
  RadarStation(const RadarStation&) = delete;
  RadarStation& operator=(const RadarStation&) = delete;

  // `radar_screen` (0x652b2).
  void open() override;
  // `radar_station_event` (0x653c6).
  void event(const ui::Event& event) override;
  // `radar_station_process` (0x653e7).
  void process() override;
  // `radar_station_update` (0x653ff): the SJ and SD displays.
  void update() override;
  // `radar_station_close` (0x65374).
  void close() override;
  bool capturing() const override;
  bool expensive() const override { return true; }
  bool rig_red_redraw() const override { return true; }

 private:
  struct Impl;
  AttackServices& services_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sh

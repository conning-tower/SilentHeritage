// The gauges station (module gauges, station vtable 0x102b68): GAUGES.PCX
// with the shared navigation panel (`nav_panel.h`) and the boat's
// instruments: the fathometer, fuel, compressed air, buoyancy and battery
// dials, the oxygen bar, and either the engine lamps or the
// bathythermograph (`gauges_set_page`; the bathythermograph needs option
// bit 0x200000).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "assets/shp.h"
#include "game/mission.h"
#include "game/station.h"
#include "game/station_env.h"
#include "game/stations/nav_panel.h"
#include "sim/dive.h"

namespace sh {

// The instrument values, as `gauges_update_*` compute them.
// The fathometer (`gauges_update_fathometer` 0x3a71e): feet of water
// under the keel from the keel clearance (world units, positive aground),
// 0..700.
std::int32_t fathometer_feet(std::int32_t keel_clearance);
// The fuel needle (0x3a798): 0..512 of the tank.
std::int32_t fuel_needle(std::int32_t fuel, std::int32_t capacity);
// The compressed air needle (0x3a813): 0..512 for 0..100 %.
std::int32_t air_needle(std::int32_t air_pct);
// The buoyancy needle (0x3a860): 512 when dry, 0 at the flooding margin
// (SUBS +216 - +212) x 800.
std::int32_t buoyancy_needle(std::int32_t flood, const SubClassParams& sub);
// The battery needle's angle (0x3ac9a), tenths of a degree: 296 deg plus
// the charge's share of 127 deg (+1), in hundredths per unit.
std::int32_t battery_needle_angle(std::int32_t charge);
// The top row of the oxygen bar (0x3ae27): 90 empty .. 0 full.
std::int32_t oxygen_bar_offset(std::int32_t air, std::int32_t air_max);

// One point of the bathythermograph's trace (screen coordinates in its
// 0x8b x 0x90 window).
struct TracePoint {
  int x = 0;
  int y = 0;
  bool operator==(const TracePoint&) const = default;
};
// The bathythermograph trace (`gauges_draw_bathythermograph` 0x3aab0) down
// to `deepest_ft` (the dive's deepest point) with the thermal layer at
// `layer_ft`: 65.00 deg at the surface, cooling 0.01 deg per 2 ft, 0.10
// from 60 ft above to 30 ft below the layer, 0.20 from 40 ft above to 10
// ft below, 3.00 in the 30 ft above it. Returns the points where the
// trace moves (the first one plotted, the others joined by lines).
std::vector<TracePoint> bathythermograph_trace(std::int32_t deepest_ft, std::int32_t layer_ft);

// The deepest depth of the current dive (0x11b0e4), negated when the boat
// surfaces (`player_update`), kept for the bathythermograph.
class DiveRecorder : public FrameTask {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Starts at the boat's depth, as the player's init does.
  explicit DiveRecorder(const PlayerSub& player) : player_(player), deepest_(player.depth_ft()) {}
  void tick() override;
  std::int32_t deepest() const { return deepest_; }

 private:
  const PlayerSub& player_;
  std::int32_t deepest_;
};

class GaugesStation : public Station {
 public:
  // The station of `env`, showing the dive depths `recorder` keeps (a
  // frame task of the mission, which must outlive the station).
  GaugesStation(StationEnv& env, const DiveRecorder& recorder) : env_(env), recorder_(recorder) {}

  void open() override;    // gauges_screen (0x39d1c)
  void event(const ui::Event& event) override;
  void process() override;
  void update() override;  // gauges_station_update (0x39e8a)
  void close() override;   // gauges_station_close (0x39e40)
  bool rig_red_redraw() const override { return true; }

  NavPanel* panel() { return panel_.get(); }
  const DiveRecorder& dive_recorder() const { return recorder_; }

 private:
  void set_page(std::int32_t page, bool switching);  // gauges_set_page (0x3aebd)
  // Draws a GAUGES.SHP frame onto the station picture (the screen bitmap).
  void draw(std::int32_t frame, int x, int y);
  void draw_engine_slots();     // gauges_draw_engine_slots (0x3a8d0)
  void update_engine_lights();  // gauges_update_engine_lights (0x3a942)
  void draw_bathythermograph(); // 0x3aab0
  void update_dials();          // fathometer, fuel, battery, air, buoyancy, oxygen

  StationEnv& env_;
  const DiveRecorder& recorder_;
  Shp sprites_;
  // The station picture (the original's screen bitmap); the dials are
  // composed over it onto the screen.
  Image8 background_;
  std::unique_ptr<NavPanel> panel_;
  std::optional<std::int32_t> pending_page_;
  // The values last drawn (0x11c68c..0x11c6b8, 0x11c694[4], 0x11c6a4).
  std::int32_t last_keel_ = -1, last_fuel_ = -1, last_air_ = -1, last_flood_ = -1;
  std::int32_t last_battery_ = -1, last_oxygen_ = -1, last_trace_ = -1;
  std::array<std::int32_t, 4> last_engines_{};
  std::int32_t last_diesel_mode_ = -1;
};

}  // namespace sh

// The conn (module conn, station vtable 0x102bd4): the conning tower
// picture CONN.PCX with a hotspot on each instrument leading to its
// station, the periscope that slides up and down with the raise order and
// the cabin door that swings open on the way to the captain's quarters.
#pragma once

#include <cstdint>
#include <memory>

#include "assets/shp.h"
#include "game/station.h"
#include "game/station_env.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {

// The conn's hotspots (dialog 0x118f08, `conn_init_dialogs` 0x2c9e4),
// inclusive screen rectangles.
struct ConnHotspot {
  enum class Target : std::uint8_t {
    kPeriscope,       // raises the periscope, or goes to the scope when raised
    kPeriscopeLever,  // raises / lowers the periscope
    kChart,
    kGauges,
    kBridge,          // surfaced only
    kCabinDoor,
    kTdc,
    kStatus,
    kRadar,
  };
  Target target;
  int x0, y0, x1, y1;
};
const std::array<ConnHotspot, 9>& conn_hotspots();

// The periscope shaft offset (0x118ed8): 10 lowered .. 370 raised, moved
// by 11 up or 20 down every 5 ticks.
inline constexpr std::int32_t kPeriscopeDown = 10;
inline constexpr std::int32_t kPeriscopeUp = 0x172;

class ConnStation : public Station {
 public:
  explicit ConnStation(StationEnv& env) : env_(env) {}

  void open() override;     // conn_screen (0x2d13c)
  void event(const ui::Event& event) override;
  void process() override;
  void update() override;   // conn_station_update (0x2d220)
  void close() override;    // conn_station_close (0x2d1e4)
  bool rig_red_redraw() const override { return true; }

  // The periscope shaft offset and animation (0 none, 1 up, 2 down).
  std::int32_t shaft() const { return shaft_; }
  std::int32_t animation() const { return animation_; }
  // The cabin door frame 0..9 and animation (1 opening).
  std::int32_t door_frame() const { return door_frame_; }
  // Starts the periscope animation `animation` (0 stop, 1 up, 2 down)
  // (`conn_periscope_start_anim`, 0x2d298).
  void start_periscope(std::int32_t animation);

 private:
  void animate_periscope();                       // conn_periscope_animate (0x2d3c2)
  void draw_periscope();                          // conn_draw_periscope (0x2d510)
  void draw_door(bool into_buffer);               // conn_draw_cabin_door (0x2d68a)
  void animate_door();                            // conn_animate_cabin_door (0x2d6e3)
  void click(ConnHotspot::Target target);
  void hover(ConnHotspot::Target target, bool on);

  StationEnv& env_;
  std::unique_ptr<ui::Dialog> dialog_;
  Shp sprites_;  // CONN.SHP
  // The screen under the periscope (180 x 445 at (230, 12)), saved at open.
  Image8 periscope_background_;
  bool could_raise_ = false;      // 0x118ef8
  bool was_surfaced_ = false;     // 0x118efc
  bool raised_ = false;           // 0x118edc
  std::int32_t animation_ = 0;    // 0x118ed4
  std::int32_t shaft_ = kPeriscopeDown;  // 0x118ed8
  std::int32_t door_state_ = 0;   // 0x118eb4
  std::int32_t door_frame_ = 0;   // 0x118eb8
};

}  // namespace sh

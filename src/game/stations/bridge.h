// The bridge (F4, bridge.cpp, vtable 0x102a9c): the 640-wide view from the
// bridge along the boat's heading plus the look bearing, with the deck in
// front or behind, the bearing tape and the nav panel (BRDGNAV.SHP).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "assets/pcx.h"
#include "assets/shp.h"
#include "game/station.h"
#include "game/stations/attack_services.h"
#include "game/stations/view_drag.h"
#include "ui/dialog.h"

namespace sh {

// Returns the BRIDGE.SHP deck frame and the column of the deck's axis for
// look bearing `bearing` (`bridge_draw_deck_overlay`, 0x17b4c), or nothing
// when the view shows no deck (45..135 and 225..315 deg). Frames 0-8 are
// the foredeck, 9-17 the afterdeck, one per 15 deg off the axis; `mirror`
// is set when the deck is drawn flipped (looking right of the axis).
struct DeckView {
  std::int32_t frame = 0;
  std::int32_t x = 0;
  bool mirror = false;
};
std::optional<DeckView> bridge_deck_view(Angle10 bearing, int width = kBridgeViewWidth);

// The deck of the wide bridge (not in the original), wider than 640: the
// bow's frames up to 90 degrees off it and the stern's beyond, blended from
// one frame to the next as the bearing turns, the deck sliding sideways as
// the original's (faster than the world) until it is out of the view, where
// the original's stops at 45 degrees.
enum class DeckTransition : std::uint8_t {
  kStepped,     // the frame of the bearing, as the original
  kWarp,        // the nearer frame, resampled to the outline in between
  kWarpDither,  // both, resampled and mixed by an ordered dither
};
// The blending levels (the 4x4 dither's).
inline constexpr std::int32_t kDeckLevels = 16;
// The frames to draw: `frame`, and `next` where the dither level is under
// `weight` (0..kDeckLevels); their outlines resampled to the one in between
// when `warp`; with the hotspot at column `x` (off the view once the deck
// turned out of it: nothing drawn then), mirrored as `DeckView`.
struct WideDeckView {
  std::int32_t frame = 0;
  std::int32_t next = 0;
  std::int32_t weight = 0;
  bool warp = false;
  std::int32_t x = 0;
  bool mirror = false;
};
// Returns the deck of a `width`-wide view for look bearing `bearing`.
WideDeckView bridge_wide_deck_view(Angle10 bearing, int width, DeckTransition transition);

class BridgeStation final : public Station {
 public:
  explicit BridgeStation(AttackServices& services);
  ~BridgeStation() override;

  // bridge_screen (0x1773c)
  void open() override;
  // bridge_station_event (0x17941)
  void event(const ui::Event& event) override;
  // bridge_station_process (0x17952)
  void process() override;
  // bridge_station_update (0x17961): back to the conn when the bridge is
  // under water; the view is redrawn on the first frame, when the bearing
  // changed or every 6 ticks.
  void update() override;
  // bridge_station_close (0x178d2)
  void close() override;
  bool expensive() const override { return true; }
  bool outdoors() const override { return true; }
  bool shows_world() const override { return true; }
  bool capturing() const override {
    return (drag_ && drag_->dragging()) || (nav_panel_ && nav_panel_->capturing());
  }
  bool wide_layout() const override { return true; }

 private:
  void render_view();
  // Extends the strip under the view (BRDGNAV.SHP frame 0) to the screen's
  // edges (`make_panel_ends`; the wide screen, not in the original).
  void draw_strip_ends();
  void turn(std::int32_t step, int sign);

  AttackServices& s_;
  PcxImage background_;                 // BRIDGE.PCX
  VgaPalette pcx_palette_{};            // 0x11637c, for the overlay dimming
  std::optional<Shp> deck_;             // BRIDGE.SHP 0x116264
  std::optional<Shp> nav_;              // BRDGNAV.SHP 0x116260
  std::unique_ptr<ui::Dialog> dialog_;  // 0x116694
  std::unique_ptr<StationPanel> nav_panel_;
  Image8 scratch_;                  // 0x11626c, the transformed deck frame
  // The drag of the view (extended `drag_rotate`), while open.
  std::unique_ptr<ViewDrag> drag_;
  std::uint32_t render_mark_ = 0;   // ticks of the last render (counter 0x129bd4)
  std::int32_t tape_bearing_ = -1;  // 0x116274
  // The left column of the original's 640 columns on the screen (0 unless
  // wide).
  int origin_ = 0;
};

}  // namespace sh

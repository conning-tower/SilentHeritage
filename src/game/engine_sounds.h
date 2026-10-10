// The sound side of `player_update_engine_sounds` (0x5cb90), whose order
// logic is `PlayerSub::update_engines`: the telegraph bell and the
// captain's order speech when the telegraph moves, the "on diesels" report
// when the boat surfaces under way, and the diesel or motor loop whose
// sample follows the shaft power.
#pragma once

#include <cstdint>

#include "game/station.h"
#include "game/station_env.h"
#include "sim/player_sub.h"

namespace sh {

class EngineSounds {
 public:
  // The telegraph bell (played the frame after an order).
  static constexpr std::int32_t kSfxTelegraph = 7;

  // Plays through `sound`, which must outlive this object.
  explicit EngineSounds(SoundPort& sound) : sound_(&sound) {}

  // Runs one update for `sub` after a simulated frame: `station` is the
  // current station (null for none; its `outdoors` sets the loop's volume),
  // `engine_sounds` the game option. `quiet_order` (0x124ed8, set by the
  // dive and blow commands, which ring their own alarm) skips the speech of
  // a new telegraph order and is cleared once an order is seen.
  void update(const PlayerSub& sub, const Station* station, bool engine_sounds, bool& quiet_order);
  // Restarts the loop on the next update (0x11a8ec, set by
  // `sound_apply_options`, e.g. after a menu stopped every sound).
  void refresh() { refresh_ = true; }

  // The engine loop playing (an SFX id), 0 for none.
  std::int32_t loop_sfx() const { return loop_sfx_; }

 private:
  SoundPort* sound_;
  bool bell_pending_ = false;        // 0x124eb4
  std::int32_t telegraph_ = -1;      // 0x124e8c, -1 until the first update
  std::int32_t mode_ = 2;            // 0x124ea8: 0 motors, 1 diesels, 2 none yet
  std::int32_t power_ = 0;           // 0x124eb0
  std::int32_t loop_sfx_ = 0;        // 0x124eac
  const Station* station_ = nullptr; // 0x124eb8
  bool refresh_ = false;             // 0x11a8ec
};

}  // namespace sh

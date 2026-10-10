// The player's torpedo tubes and racks (player.cpp: `0x11ac98` contents,
// `0x11acc0` timers, `0x11ace8` damage, racks `0x11ad60`/`0x11adb0`):
// automatic reloading from the racks of the same room, one torpedo at a
// time per room (`tube_start_reload`, `player_tube_reload_time`,
// `player_torpedo_tube_reload`). Tubes 0..bow-1 are the bow tubes, the
// rest the stern tubes.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/context.h"

namespace sh {

// A tube's damage record (12 bytes of `0x11ace8`).
struct TubeDamage {
  static constexpr std::uint16_t kDamaged = 1;
  static constexpr std::uint16_t kDestroyed = 2;
  static constexpr std::uint16_t kRepairing = 4;
  static constexpr std::uint16_t kWaitSurface = 8;  // Repaired only surfaced.
  static constexpr std::uint16_t kOuterDoor = 0x2000;  // Outer door damage: surfaced repair.
  static constexpr std::uint16_t kRoomFlooded = 0x4000;

  std::int32_t severity = 0;     // %
  std::int32_t repair_time = 0;  // Seconds.
  std::uint16_t flags = 0;
};

class TorpedoTubes {
 public:
  static constexpr std::size_t kMaxTubes = 10;
  // A timer that never runs out (a tube emptied by hand).
  static constexpr std::int32_t kNoTimer = 0x7fffffff;
  // Crew messages.
  static constexpr std::int32_t kMsgLoaded = 0x17;    // #23 "Tube n loaded" (arg tube 1..10).
  static constexpr std::int32_t kMsgUnloaded = 0x18;  // #24

  // The tubes of `boat`, empty, with empty racks of its capacities.
  explicit TorpedoTubes(const SubClass& boat);

  // Loads tube `tube` with weapon id `type` (0: empty) and fills the racks,
  // as the mission loadout does.
  void set_tube(std::size_t tube, std::int32_t type) { content_.at(tube) = type; }
  std::vector<std::int32_t>& bow_rack() { return bow_rack_; }
  std::vector<std::int32_t>& stern_rack() { return stern_rack_; }

  // Returns the time (16.16 s) a reload of `tube` takes, after the longest
  // pending reload of its room (`player_tube_reload_time`, 0x5e4f2): 60 s
  // + rand(60), or 420 s + rand(120) with Realistic Reloads, plus
  // (2 - crew) x base / 2.
  std::int32_t reload_time(SimContext& ctx, std::size_t tube, std::int32_t crew) const;
  // Starts loading `tube` from the first torpedo of its room's rack, when
  // auto-loading and no torpedo is being moved by hand; the rack keeps it
  // with Unlimited Ammo (`tube_start_reload`, 0x5e427).
  void start_reload(SimContext& ctx, std::size_t tube, std::int32_t crew);
  // Runs the reloads for `dt` (`player_torpedo_tube_reload`, 0x5e271):
  // empty tubes start loading, timers of undamaged tubes run down, and a
  // finished tube is reported loaded (or emptied back to the rack). Nothing
  // happens while running silent.
  void update(SimContext& ctx, bool silent_running, Fixed16 dt, std::int32_t crew);

  std::int32_t bow_count() const { return bow_count_; }
  std::int32_t stern_count() const { return stern_count_; }
  std::int32_t count() const { return bow_count_ + stern_count_; }
  bool is_stern(std::size_t tube) const { return static_cast<std::int32_t>(tube) >= bow_count_; }
  // > 0: loaded weapon id, < 0: loading (-id), 0: empty.
  std::int32_t content(std::size_t tube) const { return content_.at(tube); }
  void clear(std::size_t tube) { content_.at(tube) = 0; }
  // 16.16 seconds to the end of the reload.
  std::int32_t timer(std::size_t tube) const { return timer_.at(tube); }
  // Sets a tube's timer (16.16 s), as the status page's hand loading does.
  void set_timer(std::size_t tube, std::int32_t value) { timer_.at(tube) = value; }
  TubeDamage& damage(std::size_t tube) { return damage_.at(tube); }
  const TubeDamage& damage(std::size_t tube) const { return damage_.at(tube); }
  const std::vector<std::int32_t>& bow_rack() const { return bow_rack_; }
  const std::vector<std::int32_t>& stern_rack() const { return stern_rack_; }

  // The Auto Torp Load game option (`0x11a8c4`).
  bool auto_load = true;
  // A torpedo is being moved by hand on the status page (`0x124e88`); it
  // stops the automatic reloads for the mission.
  bool moved_by_hand = false;

 private:
  std::int32_t bow_count_;
  std::int32_t stern_count_;
  std::array<std::int32_t, kMaxTubes> content_{};
  std::array<std::int32_t, kMaxTubes> timer_{};
  std::array<TubeDamage, kMaxTubes> damage_{};
  // Rack slots: > 0 a weapon id, 0 empty, < 0 -(id << 8 | tube) for a
  // torpedo being unloaded into it.
  std::vector<std::int32_t> bow_rack_;
  std::vector<std::int32_t> stern_rack_;
};

}  // namespace sh

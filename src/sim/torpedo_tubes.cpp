#include "sim/torpedo_tubes.h"

#include <string>

#include "sim/torpedo.h"

namespace sh {

TorpedoTubes::TorpedoTubes(const SubClass& boat)
    : bow_count_(static_cast<std::int32_t>(boat.bow_tubes)),
      stern_count_(static_cast<std::int32_t>(boat.stern_tubes)),
      bow_rack_(boat.bow_reloads, 0),
      stern_rack_(boat.stern_reloads, 0) {}

std::int32_t TorpedoTubes::reload_time(SimContext& ctx, std::size_t tube, std::int32_t crew) const {
  // player_tube_reload_time (0x5e4f2)
  const bool stern = is_stern(tube);
  const auto first = static_cast<std::size_t>(stern ? bow_count_ : 0);
  const auto last = static_cast<std::size_t>(stern ? bow_count_ + stern_count_ : bow_count_);
  std::int32_t queued = 0;
  for (std::size_t i = first; i < last; ++i) {
    if (queued < timer_[i] && timer_[i] != kNoTimer) {
      queued = timer_[i];
    }
  }
  const bool realistic = (ctx.env.options & combat_option::kRealisticReloads) != 0;
  const std::int32_t base = realistic ? 420 : 60;
  const std::int32_t spread = realistic ? 120 : 60;
  const std::int32_t roll = ctx.random.random_int(spread);
  return ((roll + base + (2 - crew) * base / 2) << 16) + queued;
}

void TorpedoTubes::start_reload(SimContext& ctx, std::size_t tube, std::int32_t crew) {
  // tube_start_reload (0x5e427)
  if (!auto_load || moved_by_hand) {
    return;
  }
  std::vector<std::int32_t>& rack = is_stern(tube) ? stern_rack_ : bow_rack_;
  for (std::int32_t& slot : rack) {
    if (slot > 0) {
      content_[tube] = -slot;
      timer_[tube] = reload_time(ctx, tube, crew);
      if ((ctx.env.options & combat_option::kUnlimitedAmmo) == 0) {
        slot = 0;
      }
      return;
    }
  }
}

void TorpedoTubes::update(SimContext& ctx, bool silent_running, Fixed16 dt, std::int32_t crew) {
  // player_torpedo_tube_reload (0x5e271)
  if (silent_running) {
    return;
  }
  for (std::size_t t = 0; t < static_cast<std::size_t>(count()); ++t) {
    if (content_[t] == 0) {
      start_reload(ctx, t, crew);
    }
  }
  for (std::size_t t = 0; t < kMaxTubes; ++t) {
    if (timer_[t] <= 0 || timer_[t] == kNoTimer || damage_[t].flags != 0) {
      continue;
    }
    timer_[t] -= dt.raw();
    if (timer_[t] > 0) {
      continue;
    }
    timer_[t] = 0;
    const auto number = static_cast<std::int32_t>(t) + 1;
    if (content_[t] == 0) {
      // A torpedo moved back to the rack.
      std::vector<std::int32_t>& rack = is_stern(t) ? stern_rack_ : bow_rack_;
      for (std::int32_t& slot : rack) {
        if (slot < 0 && static_cast<std::size_t>(-slot & 0xff) == t) {
          slot = -slot >> 8;
          break;
        }
      }
      ctx.crew_message(kMsgUnloaded, number);
    } else {
      content_[t] = -content_[t];
      ctx.crew_message(kMsgLoaded, number);
      if (ctx.env.speech) {
        // player_torpedo_tube_reload (0x5e271): "WO0011".."WO0020".
        ctx.events.speech.push_back("WO00" + std::to_string(11 + t) + ".RAW");
      }
    }
  }
}

}  // namespace sh

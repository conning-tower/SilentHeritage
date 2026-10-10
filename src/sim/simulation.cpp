#include "sim/simulation.h"

#include <algorithm>
#include <utility>

#include "core/fixed_math.h"
#include "sim/player_combat.h"

namespace sh {

Simulation::Simulation(const MissionConditions& env, const SeaFloor& floor, std::uint32_t seed)
    : env_(env), floor_(floor), random_(seed) {}

void Simulation::step(std::int32_t timer_ticks, bool station_opened) {
  clock_.advance(timer_ticks, station_opened, clock_observer_);
  // veh_simulate_frame (0x7e75a): game time gathers until it exceeds 0.01 s.
  pending_hundredths_ += clock_.frame_ticks();
  if (pending_hundredths_ > 1) {
    const std::int32_t dt = fx::div(fx::imul(static_cast<std::int16_t>(pending_hundredths_), 0x10000), 100 << 16);
    pending_hundredths_ = 0;
    simulate(Fixed16::from_raw(dt));
  }
  // The pointers to the removed vehicles are dropped first (slot +0xe8,
  // the collision partners, the TDC and deck-gun targets).
  for (const std::unique_ptr<Vehicle>& gone : vehicles_) {
    if (!gone->removed()) {
      continue;
    }
    for (const std::unique_ptr<Vehicle>& v : vehicles_) {
      v->forget(*gone);
      if (v->collision().partner == gone.get()) {
        v->collision().collided = false;
        v->collision().partner = nullptr;
      }
    }
    if (combat_ != nullptr) {
      combat_->forget(*gone);
    }
    sensors_.forget(*gone);
  }
  // The player's hydrophones listen to every live vehicle (slot +0x58) and
  // the removed ones are deleted, in one loop. Original bug kept: the index
  // still advances after a deletion, so the vehicle that moved into the
  // freed place is skipped this frame (no hydrophone check; deleted next
  // frame if removed too).
  SimContext ctx = context();
  for (std::size_t i = 0; i < vehicles_.size(); ++i) {
    if (!vehicles_[i]->removed()) {
      vehicles_[i]->hydrophone_check(ctx);
    } else {
      vehicles_.erase(vehicles_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  sensors_.end_frame();
}

void Simulation::simulate(Fixed16 dt) {
  std::int32_t cap = kUnrestrictedCompression;
  for (std::int32_t pass = 0; pass < 11; ++pass) {
    simulate_pass(dt.raw(), pass, cap);
  }
  clock_.set_compression_cap(cap);
}

void Simulation::simulate_pass(std::int32_t dt, std::int32_t pass, std::int32_t& cap) {
  // veh_simulate_pass (0x7e8de)
  std::int32_t max_step = (pass + 1) * 0x199a;
  if (pass > 1 && clock_.effective_compression() > 4) {
    max_step = (pass + 1) * 0x3334;
  }
  if (pass == 0) {
    max_step = 0x51e;
  }
  SimContext ctx = context();
  while (dt != 0) {
    const std::int32_t sub_step = std::min(dt, max_step);
    dt -= sub_step;
    // Indexed: vehicles spawned during the pass are updated in it too.
    for (std::size_t i = 0; i < vehicles_.size(); ++i) {
      Vehicle& v = *vehicles_[i];
      v.set_list_index(static_cast<std::int32_t>(i));
      if (v.update_pass() == pass && !v.removed()) {
        cap = std::min(cap, v.max_time_compression());
        v.update(ctx, Fixed16::from_raw(sub_step));
        v.update_bounds();
        v.collision().collided = false;
        check_collisions(vehicles_, i);
      }
    }
  }
}

Vehicle* Simulation::player() const {
  for (const std::unique_ptr<Vehicle>& v : vehicles_) {
    if (v->class_id() == VehicleClass::kPlayer) {
      return v.get();
    }
  }
  return nullptr;
}

MissionEvents Simulation::take_events() { return std::exchange(events_, MissionEvents{}); }

}  // namespace sh

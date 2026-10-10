#include "sim/contacts.h"

#include "sim/ai_ship.h"
#include "sim/aircraft_strike.h"
#include "sim/geometry.h"
#include "sim/intercept.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/radar.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSeconds5 = 0x50000;
constexpr std::int32_t kMsgMissionCompleted = 69;  // "Mission completed, sir."
constexpr std::int32_t kSeconds120 = 0x780000;
constexpr std::int32_t kSeconds300 = 0x12c0000;
// player_track_recon_target_in_view (0x60d2d): the camera's reach, the
// least light, the eye and target heights (world y) and the half field.
constexpr std::uint32_t kReconRangeYd = 25000;
constexpr std::int32_t kReconMinLightPct = 30;
constexpr std::int32_t kReconEyeY = 0x40;     // 4 ft
constexpr std::int32_t kReconTargetY = 0x50;  // 5 ft
constexpr std::int32_t kReconHalfFieldTenths = 21;

void count_down(std::int32_t& timer, std::int32_t dt) {
  timer -= dt;
  if (timer < 0) {
    timer = 0;
  }
}

// Returns the bearing of `v` from the boat relative to its bow, in degrees.
std::int32_t relative_degrees(const PlayerSub& player, const Vehicle& v) {
  const fx::Vec3 own = player.position();
  const fx::Vec3 pos = v.position();
  return fx::angle_sub(fx::heading_from_delta(pos.x - own.x, pos.z - own.z), player.motion().heading).tenths() / 10;
}

}  // namespace

void ContactReports::forget(const Vehicle& removed) {
  std::erase_if(contacts_, [&](const PlayerContact& c) { return c.vehicle == &removed; });
  if (gun_crew_target_ == &removed) {
    gun_crew_target_ = nullptr;
  }
  if (nearest_aircraft_ == &removed) {
    nearest_aircraft_ = nullptr;
  }
  if (rescue_pilot_ == &removed) {
    rescue_pilot_ = nullptr;
  }
  if (recon_target_ == &removed) {
    recon_target_ = nullptr;
  }
  if (recon_in_view_ == &removed) {
    recon_in_view_ = nullptr;
  }
}

void ContactReports::track_recon_target(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env,
                                        Vehicle& v, std::int32_t& nearest_yd) {
  // player_track_recon_target_in_view (0x60d2d)
  const fx::Vec3 own = player.position();
  const fx::Vec3 pos = v.position();
  const std::uint32_t distance = fx::approx_distance(own.x - pos.x, own.z - pos.z) / kUnitsPerYard;
  if (distance > kReconRangeYd) {
    return;
  }
  if (static_cast<std::int32_t>(distance) < nearest_yd) {
    recon_target_ = &v;
    nearest_yd = static_cast<std::int32_t>(distance);
  }
  if (&v != recon_target_ || env.light_pct < kReconMinLightPct) {
    return;
  }
  // From the waterline whatever the station; land in between does not
  // matter (only an out-of-range result fails).
  const SightingCheck seen =
      sighting_check(env.sighting(ctx.env), env.grid, {own.x, kReconEyeY, own.z}, {pos.x, kReconTargetY, pos.z});
  if (seen.distance_yd == SightingCheck::kOutOfRange) {
    return;
  }
  const PlayerCombat* combat = ctx.sim != nullptr ? ctx.sim->player_combat() : nullptr;
  const Angle10 view = combat != nullptr ? combat->view_bearing : Angle10(0);
  std::int32_t off = fx::angle_add(player.motion().heading, view).tenths() -
                     fx::heading_from_delta(pos.x - own.x, pos.z - own.z).tenths();
  if (off > 1800) {
    off -= 3600;
  }
  if (off < -1800) {
    off += 3600;
  }
  if (off > -kReconHalfFieldTenths && off < kReconHalfFieldTenths) {
    recon_in_view_ = &v;
  }
}

bool ContactReports::take_recon_photo(SimContext& ctx) {
  // player_take_recon_photo (0x60f44)
  if (recon_in_view_ == nullptr) {
    return false;
  }
  ctx.crew_message(kMsgMissionCompleted);
  const auto* report = dynamic_cast<const IntelReport*>(recon_in_view_);
  ctx.events.photo_recons.push_back(report != nullptr ? report->location_type() : -1);
  recon_in_view_->remove();
  recon_target_ = nullptr;
  recon_in_view_ = nullptr;
  return true;
}

void ContactReports::report(SimContext& ctx, PlayerSub& player, RadarSets& radar, const SensorEnvironment& env,
                            std::int32_t dt) {
  // player_report_contacts (0x5d713)
  const std::uint32_t options = ctx.env.options;
  Vehicle* const previous_rescue = rescue_pilot_;
  enemies_near_ = false;
  count_down(sd_hold_, dt);
  count_down(aircraft_call_, dt);
  count_down(spare_timer_, dt);
  count_down(torpedo_call_, dt);
  under_attack_ = false;
  rescue_pilot_ = nullptr;
  nearest_aircraft_ = nullptr;
  recon_in_view_ = nullptr;
  recon_target_ = nullptr;
  std::int32_t nearest_recon_yd = 0x7fffffff;
  gun_crew_target_.reset();
  torpedo_warning_ = false;
  contacts_.clear();
  const bool groups = (options & sensor_option::kContactGroups) != 0;
  if (groups) {
    group_activity_ = false;
  }
  std::int32_t nearest_d = 0x7fffffff;
  std::int32_t nearest_air_d = 0x7fffffff;
  std::int32_t nearest_torpedo_d = 0x7fffffff;
  Vehicle* nearest = nullptr;
  Vehicle* nearest_torpedo = nullptr;
  std::int32_t ships = 0;
  std::int32_t subs = 0;
  std::int32_t pilots = 0;
  std::int32_t aircraft = 0;
  std::int32_t torpedoes = 0;
  std::int32_t activity = 0;

  const PlayerLookoutState lookout = player_lookout_state(player, env.crew);
  const SightingConditions conditions = env.sighting(ctx.env);
  const fx::Vec3 own = player.position();
  if (ctx.sim != nullptr) {
    const auto& vehicles = ctx.sim->vehicles();
    for (std::size_t i = 0; i < vehicles.size(); ++i) {
      Vehicle& v = *vehicles[i];
      if (v.removed() || v.is_debris()) {
        continue;
      }
      if (v.has_attacked_player()) {
        under_attack_ = true;
      }
      if (const std::int32_t objective = v.recon_objective(); objective != 0) {
        ++activity;
        if (groups) {
          group_activity_ = true;
        }
        if (objective > 0) {
          track_recon_target(ctx, player, env, v, nearest_recon_yd);
        }
      }
      if (groups && (v.plane_count() != 0 || v.is_pilot())) {
        group_activity_ = true;
      }
      if (!v.is_contact() || &v == &player) {
        continue;
      }
      const bool was_sighted = v.sighted();
      v.refresh_sighted();
      const fx::Vec3 pos = v.position();
      const auto distance =
          static_cast<std::int32_t>(fx::approx_distance(own.x - pos.x, own.z - pos.z) / kUnitsPerYard);
      const std::int32_t height = v.visual_height();
      SightingCheck check =
          player_sighting_check(lookout, conditions, env.grid, pos.x, height, pos.z, v.exposure());
      std::int32_t d = check.distance_yd;
      std::int32_t& range = check.range.range_yd;
      std::int32_t radar_range = sj_fitted_range(options);
      if (v.is_small_craft()) {
        radar_range /= 4;
      }
      if (v.is_aircraft()) {
        if (d > 0) {
          d /= 4;
        }
        range /= 4;
      }
      if (v.is_aircraft() || v.plane_count() != 0) {
        if (v.plane_count() != 0) {
          ++activity;
        }
        if (range < distance || v.sinking()) {
          d = SightingCheck::kOutOfRange;
        }
        radar_range = sd_fitted_range(options);
      }
      const std::int32_t detect = v.comms_range_yd() > radar_range ? v.comms_range_yd() : radar_range;
      if (distance < detect && v.side() != player.side()) {
        if (!v.sinking()) {
          ++activity;
        }
        if (groups && env.contact_groups != nullptr) {
          env.contact_groups->detected(v);
        }
      }
      if (v.is_pilot()) {
        ++activity;
      }
      // Without Limited Visibility everything in range with a height counts
      // as seen (but not what land hides).
      if ((options & sensor_option::kLimitedVisibility) == 0 && height > 0 && d == SightingCheck::kOutOfRange &&
          v.plane_count() == 0 && !v.is_pilot()) {
        d = distance;
      }
      if (d < 0) {
        continue;
      }
      ContactKind kind = ContactKind::kOther;
      const bool hostile = v.side() != player.side();
      const auto note_target_range = [&] {
        if (&v == env.tdc_target) {
          tdc_target_range_ = range;
        }
        if (&v == env.gun_target) {
          gun_target_range_ = range;
        }
      };
      if (!v.is_submarine() && !v.is_pilot() && !v.is_aircraft() && v.plane_count() == 0 && !v.sinking() &&
          !v.is_torpedo() && hostile) {
        kind = ContactKind::kShip;
        ++ships;
        if (subs == 0 && d < nearest_d) {
          nearest_d = d;
          nearest = &v;
        }
        gun_crew_target_ = nearest;
        note_target_range();
      } else if ((!v.is_aircraft() && v.plane_count() == 0) || v.sinking()) {
        if (v.is_pilot()) {
          kind = ContactKind::kPilot;
          ++pilots;
          if (subs == 0 && ships == 0 && d < nearest_d) {
            nearest_d = d;
            nearest = &v;
          }
          note_target_range();
          if (distance < 201) {
            rescue_pilot_ = &v;
          }
        } else if (v.is_submarine() && !v.sinking() && hostile) {
          kind = ContactKind::kSubmarine;
          ++subs;
          if (d < nearest_d) {
            nearest_d = d;
            nearest = &v;
          }
          gun_crew_target_ = nearest;
          note_target_range();
        }
      } else {
        kind = ContactKind::kAircraft;
        if (d < nearest_air_d) {
          nearest_air_d = d;
          nearest_aircraft_ = &v;
        }
        ++aircraft;
      }
      if (v.is_torpedo()) {
        kind = ContactKind::kTorpedo;
      }
      if (v.is_torpedo() && v.torpedo_launcher() != &player && !was_sighted) {
        if (d < nearest_torpedo_d) {
          nearest_torpedo_d = d;
          nearest_torpedo = &v;
        }
        ++torpedoes;
      }
      v.mark_sighted(ctx.clock);
      v.add_track_time(dt, &v == env.tdc_target || &v == env.gun_target);
      if (v.is_torpedo()) {
        const std::int32_t t = motion_intercept_time({pos.x, pos.z, v.heading(), v.speed()},
                                                     {own.x, own.z, player.motion().heading, player.motion().speed},
                                                     16000, 200);
        if (t >= 0 && t < 31) {
          torpedo_warning_ = true;
        }
      }
      const Angle10 bearing = fx::heading_from_delta(pos.x - own.x, pos.z - own.z);
      contacts_.push_back({&v, kind, bearing, fx::angle_sub(bearing, player.motion().heading), d, range});
    }
  }
  activity_ = activity != 0;
  if (activity_) {
    if (Sensors* sensors = context_sensors(ctx)) {
      sensors->reset_quiet_time();
    }
  }

  // The calls: a torpedo first, then aircraft (or the SD range), then
  // submarines, ships and pilots (the last kind found wins), else the SJ
  // bearing or the rescue call.
  const auto call = [&](std::int32_t id, std::int32_t arg) {
    ctx.crew_message(id, arg);
    ctx.clock.reset_compression();
  };
  if (torpedoes != 0) {
    torpedo_call_ = kSeconds5;
    call(60, relative_degrees(player, *nearest_torpedo));
    enemies_near_ = true;
  }
  RadarSets::Reports& radar_reports = radar.reports();
  if (torpedo_call_ >= 1) {
    return;
  }
  if (aircraft == 0) {
    if (radar_reports.sd_range >= 0 && sd_hold_ == 0) {
      call(49, radar_reports.sd_range);
      radar_reports.sd_range = -1;
    }
    count_down(aircraft_repeat_, dt);
    count_down(pilot_repeat_, dt);
  } else {
    if (aircraft_repeat_ == 0) {
      aircraft_call_ = kSeconds5;
      call(57, relative_degrees(player, *nearest_aircraft_));
      radar_reports.sd_timer = 0;
      radar_reports.sd_range = -1;
    }
    aircraft_repeat_ = kSeconds120;
    enemies_near_ = true;
  }
  if (aircraft_call_ >= 1) {
    return;
  }
  std::int32_t message = 0;
  if (subs == 0) {
    count_down(sub_repeat_, dt);
  } else {
    if (sub_repeat_ == 0) {
      message = 59;
      ship_repeat_ += kSeconds5;
      pilot_repeat_ += kSeconds5;
    }
    sub_repeat_ = kSeconds300;
    enemies_near_ = true;
  }
  if (ships == 0) {
    count_down(ship_repeat_, dt);
  } else {
    if (ship_repeat_ == 0) {
      message = 56;
      pilot_repeat_ += kSeconds5;
    }
    ship_repeat_ = kSeconds300;
    enemies_near_ = true;
  }
  if (pilots == 0) {
    count_down(pilot_repeat_, dt);
  } else {
    if (pilot_repeat_ == 0) {
      message = 58;
    }
    pilot_repeat_ = kSeconds120;
  }
  if (message == 0) {
    if (radar_reports.sj_bearing < 0) {
      if (rescue_pilot_ != previous_rescue && rescue_pilot_ != nullptr) {
        call(68, -1);
      }
      return;
    }
    call(50, radar_reports.sj_bearing);
  } else {
    call(message, relative_degrees(player, *nearest));
    radar_reports.sj_timer = 0;
  }
  radar_reports.sj_bearing = -1;
}

}  // namespace sh

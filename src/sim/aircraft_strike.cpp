#include "sim/aircraft_strike.h"

#include <algorithm>
#include <memory>
#include <string>

#include "core/error.h"
#include "sim/clock.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/shell.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kMinute = 0x3c0000;
constexpr std::int32_t kHour = 0xe100000;
constexpr std::int32_t kMile = 0x14a00;
constexpr std::int32_t kEdgeMargin = 0x76b10;  // 10 nm.
constexpr std::int32_t kForgetAge = 0x1e00000;

// Returns the clock time `minutes` later as hour, minute, day, month, year
// (the report ETAs).
std::array<std::int32_t, 5> time_after(const GameClock& clock, std::int32_t minutes) {
  std::int32_t hour = clock.hour();
  std::int32_t minute = clock.minute();
  std::int32_t day = clock.day();
  std::int32_t month = clock.month();
  std::int32_t year = clock.year();
  for (; minutes > 0; --minutes) {
    if (++minute == 60) {
      minute = 0;
      if (++hour == 24) {
        hour = 0;
        if (days_in_month(year, month) < ++day) {
          day = 1;
          if (++month == 12) {
            month = 0;
            ++year;
          }
        }
      }
    }
  }
  return {hour, minute, day, month, year};
}

// player_radio_available (0x5e73f): at radar depth or above, radio working.
bool radio_available(const PlayerSub& player) {
  return player.depth_ft() <= player.sub().radar_depth_ft &&
         !player.systems()[static_cast<std::size_t>(PlayerSystem::kRadio)].impaired();
}

// The vehicles a strike aims at: those with a SHIPS.DAT type (vtable +0x94).
bool has_ship_type(const Vehicle& v) {
  return v.class_id() == VehicleClass::kShip || v.class_id() == VehicleClass::kEnemySub;
}

std::int32_t yards(std::int32_t dx, std::int32_t dz) {
  return static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
}

}  // namespace

const BaseRecord& AirTheatre::base(std::int32_t index) const {
  if (index < 0 || static_cast<std::size_t>(index) >= bases.size()) {
    throw DataError("air strike base " + std::to_string(index) + " out of range");
  }
  return bases[static_cast<std::size_t>(index)];
}

// ----------------------------------------------------------------- reports

IntelReport::IntelReport(const AirTheatre& theatre, Kind kind, std::int32_t base, std::int32_t delay_min,
                         std::int32_t duration_min)
    : theatre_(theatre), kind_(kind), base_(base), delay_min_(delay_min), duration_min_(duration_min) {}

void IntelReport::init(SimContext& ctx) {
  // intel_report_init (0x8afff)
  active_ = false;
  pending_ = false;
  const BaseRecord& b = theatre_.base(base_);
  if (kind_ == Kind::kAirStrike) {
    target_x_ = b.station.x;
    target_y_ = 0;
    target_z_ = b.station.z;
    return;
  }
  const auto is_target = [&](const Location& l) {
    return (l.type == LocationType::kPortFacilities || l.type == LocationType::kIndustrialFacilities) &&
           fx::approx_distance(l.x - b.station.x, l.y - b.station.z) / kMile < 4;
  };
  std::vector<std::int32_t> picks;
  for (std::size_t i = 0; i < theatre_.locations.size(); ++i) {
    if (is_target(theatre_.locations[i])) {
      picks.push_back(static_cast<std::int32_t>(i));
    }
  }
  const auto count = static_cast<std::int32_t>(picks.size());
  if (ctx.random.random_int(count + 1) == 0 || count == 0) {
    target_x_ = b.harbor.x;
    target_y_ = 0;
    target_z_ = b.harbor.z;
    location_type_ = -1;
  } else {
    // One location, drawn until an unused one comes up.
    std::int32_t pick = -1;
    while (pick < 0) {
      pick = picks[static_cast<std::size_t>(ctx.random.random_int(count))];
    }
    const Location& l = theatre_.locations[static_cast<std::size_t>(pick)];
    target_x_ = l.x;
    target_y_ = l.z;
    target_z_ = l.y;
    location_type_ = static_cast<std::int32_t>(l.type);
  }
  minute_timer_ = kMinute;
  pending_ = true;
}

void IntelReport::update(SimContext& ctx, Fixed16 dt_s) {
  // intel_report_update (0x8b277)
  if (removed()) {
    return;
  }
  const std::int32_t dt = dt_s.raw();
  const PlayerSub* player = context_player(ctx);
  if (!active_ && player != nullptr && radio_available(*player)) {
    active_ = true;
  }
  if (kind_ == Kind::kAirStrike) {
    return;
  }
  if (delay_min_ != 0) {
    minute_timer_ -= dt;
    if (minute_timer_ < 1) {
      minute_timer_ = kMinute;
      delay_min_ = std::max(delay_min_ - 1, 0);
    }
    return;
  }
  if (pending_) {
    eta_ = time_after(ctx.clock, duration_min_);
    RadioReport r;
    r.kind = RadioReport::Kind::kPhotoRecon;
    r.args = {location_type_, base_, eta_[0], eta_[1], eta_[2], eta_[3]};
    r.x = target_x_;
    r.z = target_z_;
    ctx.events.radio_reports.push_back(r);
    pending_ = false;
    minute_timer_ = kMinute;
  }
  minute_timer_ -= dt;
  if (minute_timer_ < 1) {
    minute_timer_ = kMinute;
    const std::int32_t left = duration_min_--;
    if (left < 1) {
      duration_min_ = 0;
      remove();
    }
  }
}

// ------------------------------------------------------------------ pilots

DownedPilot::DownedPilot(const fx::Vec3& position, Angle10 heading, const AirTheatre& theatre)
    : theatre_(theatre), position_(position), heading_(heading) {
  set_side(0);
}

void DownedPilot::init(SimContext& ctx) {
  // pilot_init (0x88ad2)
  last_x_ = -1;
  track_time_ = 0;
  visible_ = false;
  age_ = 0;
  attacker_ = nullptr;
  orientation_ = fx::mat3_from_euler(Angle10(-heading_.tenths()), Angle10(0), Angle10(0));
  inverse_ = fx::transpose(orientation_);
  const PlayerSub* player = context_player(ctx);
  const std::int32_t deep = -(player != nullptr ? player->sub().draft_ft : 0) * kUnitsPerFoot;
  do {
    reported_x_ = position_.x + (0x640 - ctx.random.random_int(0xc80)) * kUnitsPerYard;
    reported_z_ = position_.z + (0x640 - ctx.random.random_int(0xc80)) * kUnitsPerYard;
  } while (deep <= ctx.floor.height_at(reported_x_, reported_z_));
  report_hour_ = ctx.clock.hour();
  report_minute_ = ctx.clock.minute();
  report_day_ = ctx.clock.day();
  report_month_ = ctx.clock.month();
  accuracy_ = ctx.random.random_int(100);
  report_hours_ = 36;
  std::int32_t hours = report_hours_;
  if (theatre_.sea_picture == 2) {
    hours /= 2;
  }
  const std::int32_t r = ctx.random.random_int(hours / 4);
  report_timer_ = 0;
  radio_ = false;
  survival_timer_ = 0;
  survival_hours_ = hours - r;
  RadioReport report;
  report.kind = RadioReport::Kind::kDownedPilot;
  report.x = reported_x_;
  report.z = reported_z_;
  ctx.events.radio_reports.push_back(report);
  update_bounds();
}

void DownedPilot::update(SimContext& ctx, Fixed16 dt_s) {
  // pilot_update (0x89189)
  const std::int32_t dt = dt_s.raw();
  const PlayerSub* player = context_player(ctx);
  if (!radio_ && player != nullptr) {
    radio_ = radio_available(*player);
  }
  if (player != nullptr) {
    const Sensors* sensors = context_sensors(ctx);
    const SensorEnvironment no_sensors;
    const SensorEnvironment& env = sensors != nullptr ? sensors->environment() : no_sensors;
    const fx::Vec3 eye{player->position().x, player->depth_ft() * kUnitsPerFoot, player->position().z};
    sighting_range_yd_ =
        sighting_check(env.sighting(ctx.env), env.grid, eye, {position_.x, visual_height(), position_.z})
            .range.range_yd;
  }
  if (last_x_ < 0) {
    report_timer_ -= dt;
    if (report_timer_ < 1) {
      report_timer_ = kHour;
      report_hours_ = std::max(report_hours_ - 1, 0);
    }
  } else {
    age_ += dt;
    const Sensors* sensors = context_sensors(ctx);
    const bool selected =
        sensors != nullptr && (this == sensors->environment().tdc_target || this == sensors->environment().gun_target);
    if (track_time_ != 0 && (!visible_ || !selected)) {
      track_time_ = std::max(track_time_ - dt, 0);
    }
    if (age_ > kForgetAge) {
      last_x_ = -1;
      track_time_ = 0;
      visible_ = false;
    }
  }
  survival_timer_ -= dt;
  if (survival_timer_ < 1) {
    survival_timer_ = kHour;
    if (survival_hours_-- == 0) {
      survival_hours_ = 0;
      remove();
    }
  }
}

void DownedPilot::update_bounds() {
  collision().box = make_hull_box(position_, orientation_, inverse_, 0x30, 0x50, position_.y, position_.y + 0x70, 0x70);
}

bool DownedPilot::take_damage(SimContext& /*ctx*/, Vehicle* attacker, DamageKind /*kind*/,
                              std::int32_t /*distance*/, std::int32_t /*amount*/) {
  set_sinking();
  remove();
  attacker_ = attacker;
  return true;
}

void DownedPilot::forget(const Vehicle& removed) {
  if (attacker_ == &removed) {
    attacker_ = nullptr;
  }
}

void DownedPilot::mark_sighted(const GameClock& /*clock*/) {
  // pilot_set_spotted (0x89419): the exact raft position becomes known.
  visible_ = true;
  radio_ = true;
  age_ = 0;
  last_x_ = position_.x;
  last_z_ = position_.z;
  reported_x_ = position_.x;
  reported_z_ = position_.z;
  report_hours_ = 36;
  accuracy_ = 100;
}

std::int32_t DownedPilot::add_track_time(std::int32_t dt, bool selected) {
  if (dt >= 0 && selected) {
    track_time_ += dt;
  }
  return track_time_;
}

// ----------------------------------------------------------- bomber groups

AirGroup::AirGroup(const AirTheatre& theatre, std::int32_t base, std::int32_t planes, std::int32_t launch_delay_min,
                   std::int32_t hold_min)
    : theatre_(theatre), base_(base), planes_(planes), launch_delay_min_(launch_delay_min), hold_min_(hold_min) {}

void AirGroup::init(SimContext& ctx) {
  // airgroup_init (0x899f8)
  set_side(0);
  timers_ = {kMinute, 0, 0};
  strike_done_ = false;
  report_ = nullptr;
  report_created_ = false;
  const BaseRecord& b = theatre_.base(base_);
  target_x_ = b.station.x;
  target_z_ = b.station.z;
  std::int32_t best = 1000000;
  const Location* nearest = nullptr;
  for (const Location& l : theatre_.locations) {
    const std::int32_t d = yards(l.x - target_x_, l.y - target_z_);
    if (d < best) {
      best = d;
      nearest = &l;
    }
  }
  if (nearest != nullptr) {
    target_x_ = nearest->x;
    target_z_ = nearest->y;
  }
  targets_near_ = target_x_;
  std::int32_t near = 0;
  if (ctx.sim != nullptr) {
    for (const auto& v : ctx.sim->vehicles()) {
      if (v->side() != side() && has_ship_type(*v)) {
        const fx::Vec3 p = v->position();
        if (yards(p.x - targets_near_, p.z - target_z_) < 5000) {
          ++near;
        }
      }
    }
  }
  if (near > 0) {
    targets_near_ = near;
  }
  if (std::ranges::none_of(theatre_.air_edges, [](std::int32_t flag) { return flag != 0; })) {
    throw DataError("bomber group: no map edge to start from");
  }
  std::int32_t edge = 0;
  do {
    edge = ctx.random.random_int(4);
  } while (theatre_.air_edges[static_cast<std::size_t>(edge)] == 0);
  std::int32_t column = 0;
  std::int32_t row = 0;
  switch (edge) {
    case 0:
      column = ctx.random.random_int(theatre_.grid_width);
      row = theatre_.grid_height;
      break;
    case 1:
      row = 0;
      column = ctx.random.random_int(theatre_.grid_width);
      break;
    case 2:
      column = 0;
      row = ctx.random.random_int(theatre_.grid_height);
      break;
    default:
      column = theatre_.grid_width;
      row = ctx.random.random_int(theatre_.grid_height);
      break;
  }
  const std::int32_t cell = theatre_.cell_size();
  start_x_ = column * cell + cell / 2;
  start_z_ = cell * theatre_.grid_height - (row * cell + cell / 2);
  spotted_ = false;
  over_target_ = false;
  const PlayerSub* player = context_player(ctx);
  heading_ = fx::angle_add(player != nullptr ? player->motion().heading : Angle10(0), Angle10(1800));
  pitch_ = Angle10(0);
  roll_ = Angle10(0);
  wanted_heading_ = -1;
  altitude_ft_ = 2500;
  turn_ = 0;
  throttle_ = 100;
  position_ = {start_x_, altitude_ft_ * kUnitsPerFoot, start_z_};
  pos_x_ = static_cast<std::int64_t>(position_.x) << 16;
  pos_y_ = static_cast<std::int64_t>(position_.y) << 16;
  pos_z_ = static_cast<std::int64_t>(position_.z) << 16;
  heading_acc_ = static_cast<std::int64_t>(heading_.tenths()) << 16;
  roll_acc_ = 0;
  orientation_ = fx::mat3_from_euler(Angle10(-heading_.tenths()), Angle10(0), roll_);
  seed_ = static_cast<std::uint32_t>(ctx.random.rand());
}

void AirGroup::update(SimContext& ctx, Fixed16 dt_s) {
  // airgroup_update (0x89efa)
  const std::int32_t dt = dt_s.raw();
  PlayerSub* player = context_player(ctx);
  const fx::Vec3 own = player != nullptr ? player->position() : fx::Vec3{};
  if (player != nullptr) {
    const Sensors* sensors = context_sensors(ctx);
    const SensorEnvironment no_sensors;
    const SensorEnvironment& env = sensors != nullptr ? sensors->environment() : no_sensors;
    const fx::Vec3 eye{own.x, player->depth_ft() * kUnitsPerFoot, own.z};
    sighting_range_yd_ = sighting_check(env.sighting(ctx.env), env.grid, eye, position_).range.range_yd;
  }
  if (launch_delay_min_ != 0) {
    timers_[0] -= dt;
    if (timers_[0] > 0) {
      return;
    }
    timers_[0] = kMinute;
    if (launch_delay_min_-- <= 0) {
      launch_delay_min_ = 0;
    }
    return;
  }
  // Once only: the original tests +0x1a8, which airgroup_fly leaves set
  // when it removes the report.
  if (!report_created_ && ctx.sim != nullptr) {
    const auto distance = static_cast<std::int32_t>(fx::approx_distance(start_x_ - target_x_, start_z_ - target_z_));
    const std::int32_t eta_min = distance / kMile * 60 / 200 - ctx.random.random_int(10) + hold_min_;
    const std::array<std::int32_t, 5> eta = time_after(ctx.clock, eta_min);
    auto report = std::make_unique<IntelReport>(theatre_, IntelReport::Kind::kAirStrike, base_);
    report->set_eta(eta);
    report_ = &ctx.sim->add(std::move(report));
    report_created_ = true;
    RadioReport r;
    r.kind = RadioReport::Kind::kAirStrike;
    r.args = {eta[0], eta[1], eta[2], eta[3], base_, 0};
    r.x = target_x_;
    r.z = target_z_;
    ctx.events.radio_reports.push_back(r);
    timers_[0] = 0;
  }
  const auto miles = static_cast<std::uint32_t>(fx::approx_distance(own.x - position_.x, own.z - position_.z)) /
                     static_cast<std::uint32_t>(kMile);
  if (miles < 90 || strike_done_) {
    ctx.clock.limit_effective_compression(0x200);
  }
  fly(dt);
  for (std::int32_t& t : timers_) {
    t = std::max(t - dt, 0);
  }
  if (hold_min_ != 0 && timers_[0] < 1) {
    timers_[0] = kMinute;
    --hold_min_;
  }
  if (miles < 20 && over_target_ && timers_[0] == 0 && ctx.sim != nullptr) {
    std::int32_t x = 0;
    std::int32_t z = 0;
    if (targets_near_ == target_x_ || ctx.random.random_int(3) != 0) {
      x = target_x_ + 24000 - ctx.random.random_int(1000) * kUnitsPerYard;
      z = target_z_ + 24000 - ctx.random.random_int(1000) * kUnitsPerYard;
    } else {
      std::int32_t k = ctx.random.random_int(targets_near_);
      fx::Vec3 p;
      for (const auto& v : ctx.sim->vehicles()) {
        if (v->side() == side() || !has_ship_type(*v)) {
          continue;
        }
        p = v->position();
        if (yards(p.x - target_x_, p.z - target_z_) < 5000) {
          if (k == 0) {
            break;
          }
          --k;
        }
      }
      x = p.x + 0x1c20 - ctx.random.random_int(300) * kUnitsPerYard;
      z = p.z + 0x1c20 - ctx.random.random_int(300) * kUnitsPerYard;
    }
    std::int32_t y = ctx.floor.height_at(x, z);
    if (y < 0) {
      y = 0xa0;
    }
    ctx.sim->add(std::make_unique<Bomb>(fx::Vec3{x, y, z}, heading_, speed_tenths_ / 10, 0, this));
    timers_[0] = 0xa0000 / planes_;
    timers_[0] += ctx.random.random_int(timers_[0] / 3);
  }
  if (strike_done_ && timers_[1] == 0 && ctx.sim != nullptr) {
    const std::int32_t seconds = fx::trunc(timers_[2]);
    timers_[1] = 0x75300000 / ((seconds + 90) / 90);
    timers_[1] /= planes_ / 3;
    const std::int32_t cell = theatre_.cell_size();
    const std::int32_t deep = -(player != nullptr ? player->sub().draft_ft : 0) * kUnitsPerFoot;
    std::int32_t tries = 600;
    std::int32_t x = 0;
    std::int32_t z = 0;
    const auto inside = [&] {
      return x <= theatre_.grid_width * cell - kEdgeMargin && kEdgeMargin <= z &&
             z <= cell * theatre_.grid_height - kEdgeMargin;
    };
    do {
      do {
        x = position_.x + (25000 - ctx.random.random_int(50000)) * kUnitsPerYard;
        z = position_.z + (25000 - ctx.random.random_int(50000)) * kUnitsPerYard;
        if (--tries == 0) {
          break;
        }
      } while (deep <= ctx.floor.height_at(x, z) || x < kEdgeMargin);
    } while (tries > 0 && !inside());
    if (tries > 0) {
      const Angle10 heading(ctx.random.random_int(360) * 10);
      ctx.sim->add(std::make_unique<DownedPilot>(fx::Vec3{x, 0, z}, heading, theatre_));
    }
  }
}

void AirGroup::fly(std::int32_t dt) {
  // airgroup_fly (0x8a613)
  if (hold_min_ != 0) {
    return;
  }
  std::int32_t wanted = fx::heading_from_delta(target_x_ - position_.x, target_z_ - position_.z).tenths();
  const auto miles = static_cast<std::uint32_t>(fx::approx_distance(target_x_ - position_.x, target_z_ - position_.z)) /
                     static_cast<std::uint32_t>(kMile);
  if (!over_target_ && miles == 0) {
    if (start_x_ == target_x_ && start_z_ == target_z_) {
      remove();
    } else {
      over_target_ = true;
    }
  }
  if (!over_target_ || miles < 2) {
    if (over_target_) {
      wanted = wanted_heading_;
    }
  } else {
    // Past the target: home to the start.
    over_target_ = false;
    strike_done_ = true;
    timers_[2] = 0x30c0000;
    target_x_ = start_x_;
    target_z_ = start_z_;
    if (report_ != nullptr) {
      report_->remove();
    }
  }
  if (strike_done_ && timers_[2] == 0) {
    strike_done_ = false;
  }
  wanted_heading_ = wanted;
  if (wanted >= 0 && heading_.tenths() != wanted) {
    std::int32_t err = fx::angle_sub(heading_, Angle10(wanted)).tenths();
    if (err > 1800) {
      err -= 3600;
    }
    if (fx::abs(err) < 2) {
      err = 0;
    }
    if (fx::abs(err) > 900) {
      err = err / fx::abs(err) * 900;
    }
    turn_ = err * 750 / 900;
  }
  roll_acc_ += fx::imul(fx::mul(fx::imul(turn_ - roll_.tenths(), 0x10000), dt), 10) / 10;
  roll_ = Angle10(static_cast<std::int32_t>(roll_acc_ >> 16));
  heading_acc_ -= roll_acc_ >> 4;
  while (heading_acc_ < 0) {
    heading_acc_ += static_cast<std::int64_t>(3600) << 16;
  }
  heading_ = Angle10(static_cast<std::int32_t>(heading_acc_ >> 16));
  orientation_ = fx::mat3_from_euler(Angle10(-heading_.tenths()), Angle10(0), roll_);
  const std::int32_t knots = (1800 - pitch_.tenths() / 2) * (throttle_ * 200 / 100) / 1800;
  speed_ = Fixed16::from_raw(fx::imul(knots, 0x10000));
  speed_tenths_ = fx::trunc(fx::imul(knots, 0xa0000));
  const std::int32_t units = fx::div(fx::mul(speed_.raw(), kUnitsPerNauticalMile), 3600);
  const fx::Vec3 v = fx::transform(orientation_, {0, 0, units});
  pos_x_ += fx::mul(v.x, dt);
  pos_y_ += fx::mul(v.y, dt);
  pos_z_ += fx::mul(v.z, dt);
  position_ = {static_cast<std::int32_t>(pos_x_ >> 16), static_cast<std::int32_t>(pos_y_ >> 16),
               static_cast<std::int32_t>(pos_z_ >> 16)};
}

void AirGroup::forget(const Vehicle& removed) {
  if (report_ == &removed) {
    report_ = nullptr;
  }
}

std::int32_t AirGroup::radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const {
  // airgroup_radar_cross_section (0x8abd4)
  const Angle10 to_player = fx::heading_from_delta(-(position_.x - observer.x), -(position_.z - observer.z));
  std::int32_t a = fx::angle_sub(to_player, observer_heading).tenths();
  if (a > 1800) {
    a = fx::angle_sub(Angle10(3600), Angle10(a)).tenths();
  }
  a = fx::abs(900 - a);
  return ((900 - a) / 90 + 10) * 800;
}

}  // namespace sh

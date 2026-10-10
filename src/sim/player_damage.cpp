#include "sim/player_damage.h"

#include <algorithm>
#include <string>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/player_sub.h"
#include "sim/torpedo.h"
#include "sim/torpedo_tubes.h"

namespace sh {

namespace {

constexpr std::uint32_t kSystemTable = 0x10e544;
constexpr std::uint32_t kCompartmentTable = 0x10e6c4;
constexpr std::uint32_t kTubeText = 0xf7993;          // "Tube "
constexpr std::uint32_t kPressureHullText = 0xf7979;  // " pressure hull"

}  // namespace

const std::array<SystemInfo, 24>& system_info() {
  static const std::array<SystemInfo, 24> table = [] {
    const ExeImage& exe = game_exe();
    std::array<SystemInfo, 24> t;
    for (std::uint32_t i = 0; i < t.size(); ++i) {
      const std::uint32_t a = kSystemTable + 16 * i;
      SystemInfo& s = t[i];
      s.flags = exe.u8(a);
      s.repair_surfaced = (exe.u8(a + 1) & 0x10) != 0;
      s.compartment = exe.i32(a + 4);
      s.name = std::string(exe.string_pointer(a + 8));
      check_exe_table(s.flags <= 3 && s.compartment >= 0 && s.compartment < 21 && !s.name.empty(), "system",
                      kSystemTable);
    }
    return t;
  }();
  return table;
}

const std::array<CompartmentInfo, 21>& compartment_info() {
  static const std::array<CompartmentInfo, 21> table = [] {
    const ExeImage& exe = game_exe();
    std::array<CompartmentInfo, 21> t;
    for (std::uint32_t i = 0; i < t.size(); ++i) {
      const std::uint32_t a = kCompartmentTable + 0x1c * i;
      CompartmentInfo& c = t[i];
      c.name = std::string(exe.string_pointer(a));
      c.flags = exe.u16(a + 4);
      // Up to three neighbours, -1 terminated (index 0 holds none).
      for (std::uint32_t n = 0; n < c.neighbours.size(); ++n) {
        const std::int32_t v = exe.i32(a + 8 + 4 * n);
        if (i == 0 || v == -1) {
          break;
        }
        check_exe_table(v > 0 && v < 21, "compartment", kCompartmentTable);
        c.neighbours[n] = v;
      }
      check_exe_table(i == 0 || !c.name.empty(), "compartment", kCompartmentTable);
    }
    return t;
  }();
  return table;
}

namespace {

constexpr std::size_t kPeriscope = 4;
constexpr std::size_t kPressureHull = 5;
constexpr std::size_t kSdRadar = 7;
constexpr std::size_t kSjRadar = 8;
constexpr std::size_t kBowTubes = 11;
constexpr std::size_t kSternTubes = 12;
constexpr std::size_t kConningTower = 8;
constexpr std::size_t kFwdTorpedoRoom = 1;
constexpr std::size_t kAftTorpedoRoom = 16;

constexpr std::uint32_t kOptSdRadar = 0x40000;
constexpr std::uint32_t kOptSjRadar = 0x30000;

constexpr std::int32_t kMsgRanAground = 0x1e;
constexpr std::int32_t kMsgDamaged = 0x1f;     // #31 "<system> damaged".
constexpr std::int32_t kMsgRepaired = 0x20;    // #32
constexpr std::int32_t kMsgChlorine = 0x26;    // #38
constexpr std::int32_t kMsgFlooding = 0x28;    // #40
constexpr std::int32_t kMsgNearCrush = 0x48;   // #72
constexpr std::int32_t kSfxHit = 0x20;
constexpr std::int32_t kSfxLost = 0x21;
constexpr std::int32_t kSfxCreak = 0x25;       // 0x25 / 0x26.

constexpr std::int32_t kMaxMargin = 300;
constexpr std::int32_t kLethalMargin = 30;
constexpr std::int32_t kCertainMargin = 3;
constexpr std::int32_t kDestroyedAbove = 85;
constexpr std::int32_t kMaxWater = 0x280000;   // 40
constexpr std::int32_t kPumpRate = 0x28f;      // 0.01 per second.
constexpr std::int32_t kSeepRate = 0x24e;      // 0.009 per second.
constexpr std::int32_t kLeakRate = 0x312;      // x 0.012 per second.
constexpr std::int32_t kChlorineAir = 0x8acb4;  // Air units per second.
constexpr std::int32_t kFireAir = 0x3b38;       // Per fire per second.

// The systems grounding damages (0x10cb70).
constexpr std::array<std::size_t, 4> kGroundingSystems = {2, 6, 13, 11};

std::string tube_name(std::size_t tube) { return std::string(game_exe().c_string(kTubeText)) + std::to_string(tube + 1); }

std::string hull_name(std::size_t c) { return compartment_info()[c].name + std::string(game_exe().c_string(kPressureHullText)); }

// Counts the fitted diesels and motors (engine-room parts not marked absent).
std::int32_t fitted_parts(const PlayerSub& sub, std::size_t part) {
  std::int32_t n = 0;
  for (const EngineRoom& room : sub.engines()) {
    if (room[part].fitted) {
      ++n;
    }
  }
  return n;
}

// Returns `base + 10 - rand(20)` (the original's draw), clamped to 0..100.
std::int32_t roll_severity(SimContext& ctx, std::int32_t base) {
  return std::clamp(10 - ctx.random.random_int(20) + base, 0, 100);
}

bool submerged(const PlayerSub& sub) { return sub.sub().draft_ft < sub.depth_ft(); }

}  // namespace

bool PlayerDamage::has_compartment(std::size_t c) const {
  return c < kCompartments && boat_->compartment_rects.at(c).x1 != -1 && compartment_info()[c].flags != 0;
}

std::int32_t PlayerDamage::compartment_count() const {
  std::int32_t n = 0;
  for (std::size_t c = 1; c < kCompartments; ++c) {
    if (has_compartment(c)) {
      ++n;
    }
  }
  return n;
}

bool PlayerDamage::system_fitted(const PlayerSub& sub, std::size_t s, std::uint32_t options) const {
  const std::int32_t diesels = fitted_parts(sub, 1);
  const std::int32_t motors = fitted_parts(sub, 0);
  const auto guns = static_cast<std::int32_t>(boat_->deck_gun_count);
  switch (s) {
    case kSdRadar: return (options & kOptSdRadar) != 0;
    case kSjRadar: return (options & kOptSjRadar) != 0;
    case 14: return guns > 0;
    case 15: return guns > 1;
    case 17: return diesels > 1;
    case 18: return diesels > 2;
    case 19: return diesels > 3;
    case 21: return motors > 1;
    case 22: return motors > 2;
    case 23: return motors > 3;
    default: return true;
  }
}

std::int32_t PlayerDamage::repair_time(SimContext& ctx, std::int32_t severity, std::int32_t crew) {
  // player_repair_time (0x5e8fc)
  const std::int32_t roll = ctx.random.random_int(3600);
  return std::max(severity * (3 - crew) * 10800 / 100 + (1800 - roll), 60);
}

bool PlayerDamage::take_damage(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, DamageKind kind,
                               std::int32_t distance, std::int32_t amount, std::int32_t crew) {
  // player_take_damage (0x5c144)
  const bool below_bridge = sub.sub().bridge_height_ft < sub.depth_ft();
  std::int32_t margin = distance;
  if (kind == DamageKind::kShellfire) {
    margin = 110 - amount * 2;
    if (below_bridge && ctx.random.random_int(25) != 0) {
      return false;
    }
  } else if (kind == DamageKind::kTorpedo) {
    margin = std::clamp(90 - amount, 0, 5);
  } else if (kind == DamageKind::kGunfire) {
    margin = 0;
    if (below_bridge) {
      return false;
    }
  }
  apply(ctx, sub, tubes, kind, margin, crew);
  return true;
}

void PlayerDamage::damage_system(SimContext& ctx, PlayerSub& sub, std::size_t s, std::int32_t severity,
                                 std::int32_t crew) {
  SystemState& sys = sub.systems()[s];
  std::int32_t v = roll_severity(ctx, severity + sys.severity);
  if ((system_info()[s].flags & 1) == 0) {
    v = 100;
  }
  if ((system_info()[s].flags & 2) == 0 && v > kDestroyedAbove) {
    v = kDestroyedAbove;
  }
  const std::int32_t time = repair_time(ctx, v, crew);
  sys.flags = v > kDestroyedAbove ? 2 : 1;
  sys.severity = v;
  sys.repair_time = time;
  ctx.crew_message(kMsgDamaged, system_info()[s].name);
}

void PlayerDamage::damage_tubes(SimContext& ctx, TorpedoTubes& tubes, bool bow, std::int32_t severity,
                                std::int32_t crew) {
  const std::int32_t chance = severity / 50 + 1;
  const auto first = static_cast<std::size_t>(bow ? 0 : tubes.bow_count());
  const auto last = static_cast<std::size_t>(bow ? tubes.bow_count() : tubes.count());
  for (std::size_t t = first; t < last; ++t) {
    TubeDamage& d = tubes.damage(t);
    if (ctx.random.random_int(chance) == 0 || (d.flags & TubeDamage::kDestroyed) != 0) {
      continue;
    }
    const std::int32_t v = roll_severity(ctx, severity + d.severity);
    std::uint16_t flags = v > kDestroyedAbove ? TubeDamage::kDestroyed : TubeDamage::kDamaged;
    const std::int32_t time = repair_time(ctx, v, crew);
    if (ctx.random.random_int(2) == 0) {
      flags |= TubeDamage::kOuterDoor;
    }
    d.flags = flags;
    d.severity = v;
    d.repair_time = time;
    ctx.crew_message(kMsgDamaged, tube_name(t));
  }
}

void PlayerDamage::apply(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, DamageKind kind, std::int32_t margin,
                         std::int32_t crew) {
  // player_apply_system_damage (0x5fa94)
  if (kind == DamageKind::kGrounding) {
    ctx.events.screen_shake = true;
    run_aground(ctx, sub, tubes, crew);
    return;
  }
  ctx.events.screen_shake = true;
  const std::uint32_t options = ctx.env.options;
  if ((options & combat_option::kInvulnerable) != 0) {
    return;
  }
  std::array<SystemState, 24>& systems = sub.systems();
  std::int32_t code = 0;
  bool shielded = false;  // Submerged: the hit only reaches the masts.
  switch (kind) {
    case DamageKind::kTorpedo:
      code = end_code::kSunkByTorpedo;
      ctx.sfx(kSfxHit);
      break;
    case DamageKind::kDepthCharge:
      code = end_code::kDepthCharged;
      break;
    case DamageKind::kRam:
    case DamageKind::kShellfire: {
      if (kind == DamageKind::kRam) {
        margin = 45;
      } else {
        code = end_code::kSunkByGunfire;
      }
      bool sound = true;
      if (sub.sub().bridge_height_ft < sub.depth_ft()) {
        shielded = true;
        sound = false;
        if (systems[kPeriscope].flags != 2) {
          systems[kPeriscope].flags = 2;
          systems[kPeriscope].severity = 100;
          ctx.crew_message(kMsgDamaged, system_info()[kPeriscope].name);
          sound = true;
        }
        if (sub.depth_ft() <= sub.sub().radar_depth_ft) {
          for (const std::size_t radar : {kSdRadar, kSjRadar}) {
            const std::uint32_t fitted = radar == kSdRadar ? kOptSdRadar : kOptSjRadar;
            SystemState& r = systems[radar];
            if ((options & fitted) == 0 || r.flags != 0) {
              continue;
            }
            if (ctx.random.random_int(2) == 0) {
              r.flags = 1;
              r.severity = ctx.random.random_int(40) + 30;
              r.repair_time = repair_time(ctx, r.severity, crew);
            } else {
              r.flags = 2;
              r.severity = 100;
            }
            ctx.crew_message(kMsgDamaged, system_info()[radar].name);
            sound = true;
          }
        }
      }
      if (sound) {
        ctx.sfx(kSfxHit);
      } else if (shielded) {
        ctx.events.screen_shake = false;
      }
      break;
    }
    case DamageKind::kGunfire: {
      code = end_code::kCrushed;
      margin = 30;
      if (sub.sub().awash_depth_ft < sub.depth_ft()) {
        shielded = true;
        const std::int32_t pick = ctx.random.random_int(3);
        std::string name;
        if (pick == 0 && (options & kOptSdRadar) != 0) {
          SystemState& r = systems[kSdRadar];
          r.flags = 1;
          r.severity += ctx.random.random_int(10) + 10;
          r.repair_time = repair_time(ctx, r.severity, crew);
          name = system_info()[kSdRadar].name;
        } else if (pick == 1 && (options & kOptSjRadar) != 0) {
          SystemState& r = systems[kSjRadar];
          r.flags = 1;
          r.severity = ctx.random.random_int(10) + 10;
          r.repair_time = repair_time(ctx, r.severity, crew);
          name = system_info()[kSjRadar].name;
        } else {
          CompartmentState& tower = compartments_[kConningTower];
          tower.flags = CompartmentState::kBreached;
          tower.leak = ctx.random.random_int(10) + 10;
          tower.repair_timer = repair_time(ctx, tower.leak, crew);
          name = hull_name(kConningTower);
        }
        ctx.crew_message(kMsgDamaged, std::move(name));
      }
      break;
    }
    case DamageKind::kBomb:
      code = end_code::kBombed;
      break;
    case DamageKind::kGrounding:
      break;
  }
  if (shielded || margin > kMaxMargin) {
    return;
  }
  if (margin < kLethalMargin && (margin < kCertainMargin || ctx.random.random_int(2) == 0)) {
    ctx.sfx(kSfxLost);
    ctx.events.game_over = code;
    return;
  }
  const std::int32_t reach = kind == DamageKind::kGunfire ? kLethalMargin * 4 : ctx.random.random_int(margin);
  if (reach > kLethalMargin * 4) {
    return;
  }
  const std::int32_t level = std::min<std::int32_t>(2, static_cast<std::int32_t>((options & 0xf) >> 2));
  std::int32_t severity = 100 - std::max(margin - kLethalMargin, 0) * 90 / (kMaxMargin - kLethalMargin);
  severity -= (2 - level) * severity / 3;
  std::int32_t hits = std::max(severity / 25, 1);
  if (kind == DamageKind::kGunfire) {
    hits = ctx.random.random_int(3 - level) != 0 ? 1 : 0;
    severity = ctx.random.random_int(5) + 10;
  }
  for (std::int32_t h = 0; h < hits; ++h) {
    std::int32_t picked = -1;
    for (std::int32_t tries = 1; tries <= 1000; ++tries) {
      const auto s = static_cast<std::size_t>(ctx.random.random_int(24));
      if (s != kPressureHull && system_fitted(sub, s, options) && (systems[s].flags & 2) == 0 &&
          ((system_info()[s].flags & 2) != 0 || systems[s].severity != kDestroyedAbove)) {
        picked = static_cast<std::int32_t>(s);
        break;
      }
    }
    if (picked == static_cast<std::int32_t>(kBowTubes)) {
      damage_tubes(ctx, tubes, true, severity, crew);
    } else if (picked == static_cast<std::int32_t>(kSternTubes)) {
      damage_tubes(ctx, tubes, false, severity, crew);
    } else if (picked != -1) {
      damage_system(ctx, sub, static_cast<std::size_t>(picked), severity, crew);
    }
  }
  const std::int32_t breaches = std::max(hits / 2, 1);
  for (std::int32_t h = 0; h < breaches; ++h) {
    std::int32_t picked = -1;
    for (std::int32_t tries = 1; tries <= 1000; ++tries) {
      const auto c = static_cast<std::size_t>(ctx.random.random_int(20) + 1);
      const std::uint16_t f = compartment_info()[c].flags;
      if ((f & (compartment_flag::kHull | compartment_flag::kCanBurn)) != 0 && has_compartment(c) &&
          compartments_[c].leak != 100 && (kind != DamageKind::kGunfire || (f & compartment_flag::kInner) == 0)) {
        picked = static_cast<std::int32_t>(c);
        break;
      }
    }
    if (picked == -1) {
      continue;
    }
    CompartmentState& comp = compartments_[static_cast<std::size_t>(picked)];
    if ((comp.flags & CompartmentState::kFire) == 0 || ctx.random.random_int(3) != 0) {
      const std::int32_t v = roll_severity(ctx, severity + comp.leak);
      const std::int32_t time = repair_time(ctx, v, crew);
      hull_damage_ += v;
      comp.flags = CompartmentState::kBreached;
      comp.leak = v;
      comp.repair_timer = time / 3;
      ctx.crew_message(kMsgDamaged, hull_name(static_cast<std::size_t>(picked)));
    } else {
      comp.flags = static_cast<std::uint16_t>((comp.flags | CompartmentState::kFire) & ~CompartmentState::kRepairing);
      comp.fire_timer = ctx.random.random_int(3600) + 300;
    }
  }
  compute_damage_pct(sub, tubes, options);
  hull_pct_ = std::min(hull_damage_ / compartment_count() * 100 / 40, 100);
  if (hull_pct_ == 100 && code != 0) {
    ctx.sfx(kSfxLost);
    ctx.events.game_over = code;
  }
}

void PlayerDamage::run_aground(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, std::int32_t crew) {
  // player_run_aground (0x60511)
  ctx.crew_message(kMsgRanAground);
  ctx.clock.reset_compression();
  const std::int32_t speed = sub.motion().speed_tenths;
  if ((ctx.env.options & combat_option::kInvulnerable) != 0 || speed <= 19) {
    return;
  }
  std::size_t s = 0;
  for (std::int32_t tries = 1;; ++tries) {
    s = kGroundingSystems[static_cast<std::size_t>(ctx.random.random_int(4))];
    if (tries > 1000) {
      return;
    }
    if ((sub.systems()[s].flags & 2) == 0) {
      break;
    }
  }
  const std::int32_t severity = speed * 70 / 220 + (10 - ctx.random.random_int(20));
  if (s != kBowTubes) {
    damage_system(ctx, sub, s, severity, crew);
    return;
  }
  const std::int32_t chance = severity / 50 + 1;
  for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.bow_count()); ++t) {
    TubeDamage& d = tubes.damage(t);
    if (ctx.random.random_int(chance) == 0 || (d.flags & TubeDamage::kDestroyed) != 0) {
      continue;
    }
    const std::int32_t v = roll_severity(ctx, severity + d.severity);
    const std::int32_t time = repair_time(ctx, v, crew);
    d.flags = static_cast<std::uint16_t>(TubeDamage::kOuterDoor |
                                         (v > kDestroyedAbove ? TubeDamage::kDestroyed : TubeDamage::kDamaged));
    d.severity = v;
    d.repair_time = time;
    ctx.crew_message(kMsgDamaged, tube_name(t));
  }
}

void PlayerDamage::flood(PlayerSub& /*sub*/, std::size_t c, std::int32_t amount) {
  // player_flood_compartment (0x5f91d)
  CompartmentState& comp = compartments_[c];
  comp.water = std::min(comp.water + amount, kMaxWater);
  if ((comp.flags & CompartmentState::kWater1) == 0 && comp.water > 0x10000) {
    comp.flags = static_cast<std::uint16_t>((comp.flags & 0xfd7f) | CompartmentState::kWater1);
  }
  if ((comp.flags & CompartmentState::kWater15) == 0 && comp.water > 0xf0000) {
    comp.flags = static_cast<std::uint16_t>((comp.flags & 0xf97f) | 0x600);
  }
  if (comp.water > 0x1e0000) {
    comp.flags = static_cast<std::uint16_t>((comp.flags & 0xf17f) | 0xe00);
  }
}

void PlayerDamage::pump(PlayerSub& sub, std::size_t c, std::int32_t amount) {
  // player_pump_compartment (0x5f9b0): the pumps need battery or the surface.
  CompartmentState& comp = compartments_[c];
  if (sub.battery() != 0 || !submerged(sub)) {
    comp.water = std::max(comp.water - amount, 0);
    if ((comp.flags & CompartmentState::kWater1) != 0 && comp.water < 0x10001) {
      comp.flags ^= CompartmentState::kWater1;
    }
    if ((comp.flags & CompartmentState::kWater15) != 0 && comp.water < 0xf0001) {
      comp.flags ^= CompartmentState::kWater15;
    }
    if ((comp.flags & CompartmentState::kWater30) != 0 && comp.water < 0x1e0001) {
      comp.flags ^= CompartmentState::kWater30;
    }
  }
  if ((comp.flags & CompartmentState::kWater1) != 0) {
    sub.dive().flood += fx::trunc(fx::imul(comp.water - 0x10000, 80));
  }
}

void PlayerDamage::compute_damage_pct(const PlayerSub& sub, const TorpedoTubes& tubes, std::uint32_t options) {
  // player_compute_damage_pct (0x5f7cc)
  std::int32_t total = 0;
  std::int32_t count = 0;
  const std::array<SystemState, 24>& systems = sub.systems();
  for (std::size_t s = 0; s < systems.size(); ++s) {
    if (s == kPressureHull || s == kBowTubes || s == kSternTubes || !system_fitted(sub, s, options)) {
      continue;
    }
    ++count;
    if ((systems[s].flags & 2) != 0) {
      total += 100;
    } else if ((systems[s].flags & 1) != 0) {
      total += systems[s].severity;
    }
  }
  for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.count()); ++t) {
    ++count;
    if ((tubes.damage(t).flags & TubeDamage::kDestroyed) != 0) {
      total += 100;
    } else if ((tubes.damage(t).flags & TubeDamage::kDamaged) != 0) {
      total += tubes.damage(t).severity;
    }
  }
  damage_pct_ = total / count;
}

void PlayerDamage::update(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, Fixed16 dt, std::int32_t crew) {
  // player_damage_update (0x5ebe7)
  elapsed_ += dt.raw();
  const std::int32_t seconds = fx::trunc(elapsed_);
  if (seconds == 0) {
    return;
  }
  damage_pct_ = 0;
  hull_pct_ = 0;
  while (elapsed_ > 0xffff) {
    elapsed_ -= 0x10000;
  }
  const std::uint32_t options = ctx.env.options;
  const std::int32_t depth = sub.depth_ft();
  const bool under = submerged(sub);
  const std::int32_t flood_before = sub.dive().flood;
  const std::int32_t pumped = fx::mul(seconds << 16, kPumpRate);
  if (under) {
    seepage_ += fx::mul(seconds << 16, kSeepRate);
  }
  if ((sub.battery() > 0 || !under) && seepage_ > 0) {
    seepage_ = std::max(seepage_ - pumped, 0);
  }
  sub.dive().flood = fx::trunc(fx::imul(seepage_, 80));
  const std::int32_t test_depth = sub.sub().test_depth_ft;
  if (crush_depth_ == -1 && under) {
    const std::int32_t roll = ctx.random.random_int(40);
    crush_depth_ = test_depth + test_depth * 60 / 100 + (10 - roll);
  } else if (crush_depth_ != -1 && !under) {
    crush_depth_ = -1;
  }

  std::int32_t hull_average = 0;
  if ((options & combat_option::kInvulnerable) == 0) {
    std::array<SystemState, 24>& systems = sub.systems();
    for (std::size_t s = 0; s < systems.size(); ++s) {
      if (s == kPressureHull || s == kBowTubes || s == kSternTubes || !system_fitted(sub, s, options) ||
          (systems[s].flags & 2) != 0) {
        continue;
      }
      SystemState& sys = systems[s];
      const SystemInfo& info = system_info()[s];
      const CompartmentState& room = compartments_[static_cast<std::size_t>(info.compartment)];
      const bool room_hit = (room.flags & (CompartmentState::kFire | CompartmentState::kWater30)) != 0;
      // A fire or deep water in the room may break it further.
      if (room_hit && !info.repair_surfaced && ctx.random.random_int(7200) == 0) {
        if ((sys.flags & 1) == 0 && (info.flags & 1) != 0) {
          const std::int32_t roll = ctx.random.random_int(75);
          sys.flags |= 1;
          sys.severity = roll + 10;
          sys.repair_time = repair_time(ctx, roll + 10, crew);
          ctx.crew_message(kMsgDamaged, info.name);
        } else if ((sys.flags & 2) == 0 && (room.flags & CompartmentState::kWater30) == 0 && (info.flags & 2) != 0) {
          sys.flags = static_cast<std::uint8_t>((sys.flags & 0xfc) | 2);
          sys.severity = ctx.random.random_int(15) + kDestroyedAbove;
        }
      }
      if ((sys.flags & 1) == 0) {
        continue;
      }
      if (room_hit) {
        sys.flags &= 0xfb;
        continue;
      }
      if (!info.repair_surfaced) {
        sys.flags |= 4;
      } else if ((sys.flags & 4) == 0) {
        sys.flags |= 8;
        if (!under) {
          sys.flags = static_cast<std::uint8_t>((sys.flags ^ 8) | 4);
        }
      } else if (under) {
        sys.flags = static_cast<std::uint8_t>((sys.flags ^ 4) | 8);
      }
      if ((sys.flags & 4) != 0) {
        sys.repair_time -= seconds;
        if (sys.repair_time < 1) {
          sys.repair_time = 0;
          sys.flags = 0;
          ctx.crew_message(kMsgRepaired, info.name);
        }
      }
    }

    std::int32_t fires = 0;
    std::int32_t rooms = 0;
    for (std::size_t c = 1; c < kCompartments; ++c) {
      if (!has_compartment(c)) {
        continue;
      }
      ++rooms;
      CompartmentState& comp = compartments_[c];
      const CompartmentInfo& info = compartment_info()[c];
      if ((comp.flags & CompartmentState::kChlorineGas) != 0) {
        if (under) {
          sub.consume_air(fx::mul(dt.raw(), kChlorineAir));
        } else {
          comp.flags ^= CompartmentState::kChlorineGas;
        }
      }
      if (comp.water != 0) {
        if ((comp.flags & CompartmentState::kBreached) == 0) {
          pump(sub, c, pumped);
        }
        if ((info.flags & compartment_flag::kChlorine) != 0 &&
            (comp.flags & (CompartmentState::kWater15 | CompartmentState::kWater30)) != 0 &&
            (comp.flags & CompartmentState::kChlorineGas) == 0 && under) {
          comp.flags |= CompartmentState::kChlorineGas;
          ctx.crew_message(kMsgChlorine);
          ctx.clock.reset_compression();
        }
      }
      if ((comp.flags & CompartmentState::kFire) != 0) {
        comp.fire_timer -= seconds;
        ++fires;
        if (comp.fire_timer < 1) {
          comp.fire_timer = 0;
          comp.flags &= static_cast<std::uint16_t>(~CompartmentState::kFire);
          --fires;
        }
      }
      if ((comp.flags & CompartmentState::kBreached) == 0) {
        continue;
      }
      if ((comp.flags & (CompartmentState::kRepairing | CompartmentState::kFire)) == 0) {
        comp.flags |= CompartmentState::kRepairing;
      }
      if ((info.flags & compartment_flag::kHull) == 0) {
        continue;
      }
      // Water comes in faster deeper, into the neighbours if any.
      const std::int32_t rate = fx::mul(fx::mul(fx::imul(comp.leak, 0x10000), kPumpRate), kLeakRate);
      const std::int32_t inflow = fx::mul(std::max(depth / 74, 1) * rate, seconds << 16);
      std::int32_t neighbours = 0;
      while (neighbours < 3 && info.neighbours[static_cast<std::size_t>(neighbours)] != -1) {
        ++neighbours;
      }
      if (neighbours == 0) {
        flood(sub, c, inflow);
      } else {
        for (std::int32_t n = neighbours - 1; n >= 0; --n) {
          flood(sub, static_cast<std::size_t>(info.neighbours[static_cast<std::size_t>(n)]), inflow / neighbours);
        }
      }
      pump(sub, c, pumped);
      if ((comp.flags & (CompartmentState::kWater1 | CompartmentState::kFire)) == 0 &&
          (info.flags & compartment_flag::kCanBurn) != 0 && ctx.random.random_int(36000) == 1) {
        comp.flags = static_cast<std::uint16_t>((comp.flags | CompartmentState::kFire) & ~CompartmentState::kRepairing);
        comp.fire_timer = ctx.random.random_int(3600) + 300;
      }
      if ((comp.flags & CompartmentState::kRepairing) != 0) {
        comp.repair_timer -= seconds;
        if (comp.repair_timer < 1) {
          comp.repair_timer = 0;
          comp.flags ^= CompartmentState::kBreached | CompartmentState::kRepairing;
          ctx.crew_message(kMsgRepaired, hull_name(c));
        }
      }
    }

    // The tubes: a flooded room floods its tubes; outer-door damage is
    // repaired surfaced only.
    std::array<std::int32_t, 2> damaged{};
    std::array<std::int32_t, 2> destroyed{};
    const std::uint16_t fwd_room = compartments_[kFwdTorpedoRoom].flags;
    const std::uint16_t aft_room = compartments_[kAftTorpedoRoom].flags;
    for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.count()); ++t) {
      TubeDamage& d = tubes.damage(t);
      const std::size_t end = tubes.is_stern(t) ? 1 : 0;
      if ((d.flags & TubeDamage::kDestroyed) != 0) {
        ++destroyed[end];
        continue;
      }
      const std::uint16_t room = end == 0 ? fwd_room : aft_room;
      d.flags &= static_cast<std::uint16_t>(~TubeDamage::kRoomFlooded);
      if ((room & (CompartmentState::kWater15 | CompartmentState::kWater30)) != 0) {
        d.flags |= TubeDamage::kRoomFlooded;
      }
      if ((d.flags & TubeDamage::kDamaged) == 0) {
        continue;
      }
      ++damaged[end];
      if ((d.flags & TubeDamage::kOuterDoor) == 0) {
        if ((room & (CompartmentState::kFire | CompartmentState::kWater30)) != 0) {
          d.flags &= static_cast<std::uint16_t>(~TubeDamage::kRepairing);
        } else {
          d.flags |= TubeDamage::kRepairing;
        }
      } else if ((d.flags & TubeDamage::kRepairing) == 0) {
        d.flags |= TubeDamage::kWaitSurface;
        if (!under) {
          d.flags = static_cast<std::uint16_t>((d.flags ^ TubeDamage::kWaitSurface) | TubeDamage::kRepairing);
        }
      } else if (under) {
        d.flags = static_cast<std::uint16_t>((d.flags ^ TubeDamage::kRepairing) | TubeDamage::kWaitSurface);
      }
      if ((d.flags & TubeDamage::kRepairing) != 0) {
        d.repair_time -= seconds;
        if (d.repair_time < 1) {
          d.repair_time = 0;
          d.flags &= 0xdffa;
          ctx.crew_message(kMsgRepaired, tube_name(t));
        }
      }
    }
    systems[kBowTubes].flags = static_cast<std::uint8_t>((damaged[0] != 0 ? 1 : 0) | (destroyed[0] != 0 ? 2 : 0));
    systems[kSternTubes].flags = static_cast<std::uint8_t>((damaged[1] != 0 ? 1 : 0) | (destroyed[1] != 0 ? 2 : 0));
    if (fires != 0) {
      sub.consume_air(fx::mul(fires * kFireAir, dt.raw()));
    }
    hull_average = hull_damage_ / rooms;
    SystemState& hull = systems[kPressureHull];
    if (hull_average < 1 || hull.flags != 0) {
      if (hull_average > 20 && (hull.flags & 2) == 0) {
        hull.flags = 2;
      }
    } else {
      hull.flags = 1;
    }
    hull_pct_ = hull_average * 100 / 40;
    compute_damage_pct(sub, tubes, options);
    // The engine parts show their systems' state.
    for (std::size_t k = 0; k < 4; ++k) {
      for (std::size_t part = 0; part < 2; ++part) {
        EnginePart& e = sub.engines()[k][part];
        if (!e.fitted) {
          continue;
        }
        const SystemState& sys = systems[(part == 1 ? 16 : 20) + k];
        if ((sys.flags & 2) != 0) {
          e.flags |= EnginePart::kDamaged | EnginePart::kDestroyed;
        } else if ((sys.flags & 1) != 0) {
          e.flags |= EnginePart::kDamaged;
        } else {
          e.flags &= static_cast<std::uint8_t>(~EnginePart::kDamaged);
        }
      }
    }
  }

  // The crush depth comes down to the test depth as the hull is damaged.
  const std::int32_t crush = crush_depth_ - (crush_depth_ - test_depth) * hull_average / 40;
  if (crush_depth_ != -1 && crush * 85 / 100 <= depth) {
    const std::int32_t warning = crush * 90 / 100;
    if (warning <= depth && last_depth_ < warning) {
      ctx.crew_message(kMsgNearCrush);
      ctx.clock.reset_compression();
    }
    std::int32_t creak = 100;
    if (depth < crush) {
      creak = 100 - (crush - depth) * 100 / (crush - crush * 85 / 100);
    }
    // (The original skips the creak while one is still playing.)
    if (ctx.random.random_int(60) < creak) {
      ctx.sfx(kSfxCreak + ctx.random.random_int(2));
    }
  }
  last_depth_ = depth;
  if ((options & combat_option::kInvulnerable) == 0 && crush_depth_ != -1 && crush <= depth) {
    ctx.events.game_over = end_code::kCrushed;
    ctx.sfx(kSfxLost);
  }
  std::int32_t level = (sub.sub().ballast_max - sub.sub().ballast_neutral) * 800 / 5;
  for (int i = 0; i < 2; ++i, level *= 2) {
    if (flood_before < level && level <= sub.dive().flood) {
      ctx.crew_message(kMsgFlooding);
      return;
    }
  }
}

}  // namespace sh

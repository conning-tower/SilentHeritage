#include "game/frontend_results.h"

#include <format>
#include <memory>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "core/fixed_math.h"
#include "game/mission.h"
#include "game/options_realism.h"
#include "sim/enemy_sub.h"
#include "sim/ship.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

// The ship types of the patrol log besides the SHIPS.DAT ones (0..20).
constexpr std::int32_t kSampan = 0x15;
// Types below this one are warships (carriers to destroyers).
constexpr std::int32_t kFirstNonWarship = 7;
// Types below this one keep their class name in the sunk list.
constexpr std::int32_t kFirstUnnamedType = 12;
// The score of a rescued pilot or a photo reconnaissance.
constexpr std::int32_t kMissionTaskScore = 500;

// SH.EXE addresses of `mission_results_summary` (its labels are read
// inline by address).
namespace addr {
constexpr std::uint32_t kMonths = 0x10e910;       // char*[12] "January"..
constexpr std::uint32_t kFates = 0x10eb00;        // char*[4] "Operational"..
constexpr std::uint32_t kStatuses = 0x10eb10;     // char*[3] "Escaped"..
constexpr std::uint32_t kCrews = 0x10eb1c;        // char*[3] "Green"..
constexpr std::uint32_t kDamageLevels = 0x10eb28;  // char*[4] "None"..
constexpr std::uint32_t kShipTypes = 0x10ea04;    // {char*, char*}[24] "Aircraft Carrier"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;     // i32 1930
}  // namespace addr

LogAngle log_angle(const GeoAngle& a) { return {a.whole_degrees, a.minutes, a.hemisphere}; }

// `int_to_padded_str` (0x5908d) with width 2.
std::string two_digits(std::int32_t v) { return std::format("{:02}", v); }

}  // namespace

BoatFate fate_of_end_code(std::int32_t end_code) {
  if (end_code == 6) {
    return BoatFate::kCaptured;
  }
  if (end_code == 9) {
    return BoatFate::kRecalled;
  }
  if (end_code >= 10 && end_code <= 0x10) {
    return BoatFate::kKilled;
  }
  return BoatFate::kOperational;
}

bool returns_to_port(std::int32_t end_code) {
  return end_code == 4 || end_code == 5 || end_code == 8 || end_code == 0x11 || end_code == 0x13;
}

MissionResults compute_mission_results(const MissionOutcome& o, std::uint32_t options) {
  // mission_compute_results (0xa1c87)
  MissionResults r;
  r.month = o.month;
  r.day = o.day;
  r.year = o.year;
  r.hour = o.hour;
  r.minute = o.minute;
  r.minutes_elapsed = o.minutes_elapsed;
  r.sub = o.boat_name.substr(0, 99);
  r.class_name = o.class_name.substr(0, 29);
  r.fuel = o.fuel;
  r.crew = o.crew;
  r.difficulty = o.difficulty / 10;
  r.hull_damage_pct = o.hull_damage_pct;
  r.system_damage_pct = o.system_damage_pct;
  r.fate = fate_of_end_code(o.end_code);
  if (o.end_code == 7) {
    r.boat_lost = -1;
  }
  if (r.fate == BoatFate::kKilled) {
    r.hull_damage_pct = 100;
    r.system_damage_pct = 100;
  }
  r.patrol_area = o.patrol_area.substr(0, 39);
  r.latitude = o.latitude;
  r.longitude = o.longitude;

  std::int32_t score = 0;
  for (const PatrolLogEntry& e : o.patrol_log) {
    r.tonnage += e.tonnage;
    r.sunk.push_back({e, SunkStatus::kSunk, 100});
    if (e.type == kSampan) {
      ++r.ships_sunk;
      ++r.sampans_sunk;
    } else if (e.type == PatrolLogEntry::kRescuedPilot) {
      ++r.pilots_rescued;
      score += kMissionTaskScore;
    } else if (e.type == PatrolLogEntry::kPhotoRecon) {
      ++r.photo_recons;
      score += kMissionTaskScore;
    } else {
      ++r.ships_sunk;
      score += e.tonnage;
      if (e.type < kFirstNonWarship) {
        ++r.warships_sunk;
      }
    }
  }

  const bool single_mission = (options & option::kModeCareer) == 0;
  if (single_mission) {
    for (const AfloatVessel& v : o.afloat) {
      SunkListEntry e;
      e.log = {.day = o.day,
               .month = o.month,
               .year = o.year,
               .hour = o.hour,
               .minute = o.minute,
               .latitude = v.latitude,
               .longitude = v.longitude,
               .type = v.type,
               .name = v.type < kFirstUnnamedType ? v.class_name.substr(0, 24) : std::string(),
               .tonnage = v.tonnage};
      e.damage_pct = 100 - v.hit_points * 100 / v.hull_strength;
      e.status = e.damage_pct != 0 ? SunkStatus::kDamaged : SunkStatus::kEscaped;
      if (e.status == SunkStatus::kDamaged) {
        // The share of the damage the player did; the original divides by
        // zero when others did it all.
        const std::int32_t player_hull = v.hull_strength - v.other_damage;
        e.damage_pct = player_hull != 0 ? 100 - v.hit_points * 100 / player_hull : 100;
        score += v.tonnage / 2 * e.damage_pct / 100;
      }
      r.sunk.push_back(std::move(e));
    }
  }

  const std::int32_t rating = realism_rating(options, o.rules.keen_escort_sonar);
  r.realism_pct = realism_rating_percent(rating);
  if (single_mission) {
    score -= score / 2 * (1000 - o.difficulty) / 1000;
    score -= score / 4 * r.hull_damage_pct / 100;
    score -= score / 8 * r.system_damage_pct / 100;
  }
  // The rounded percentage of the 64-bit product rating x 100.
  const std::int32_t percent = fx::mul(rating, 100);
  r.score = score * percent / 100;
  return r;
}

MissionOutcome mission_outcome(const Mission& mission, std::int32_t end_code,
                               std::span<const PatrolLogEntry> patrol_log) {
  MissionOutcome o;
  o.end_code = end_code;
  const GameClock& c = mission.sim().clock();
  o.day = c.day();
  o.month = c.month();
  o.year = c.year();
  o.hour = c.hour();
  o.minute = c.minute();
  o.minutes_elapsed = c.minutes_elapsed();
  o.boat_name = mission.player_name();
  o.class_name = mission.player_class().display_name;
  o.profile_sprite = mission.player_class().sprite_profile;
  const World& world = mission.world();
  o.patrol_area = world.region_name;
  const PlayerSub& player = mission.player();
  const fx::Vec3 at = player.position();
  o.latitude = log_angle(world.latitude_at(at.z));
  o.longitude = log_angle(world.longitude_at(at.x));
  o.fuel = player.fuel();
  o.crew = mission.player_crew();
  o.difficulty = mission.difficulty_rating();
  o.rules = mission.sim().conditions().rules;
  o.hull_damage_pct = mission.player_combat().damage.hull_pct();
  o.system_damage_pct = mission.player_combat().damage.damage_pct();
  o.patrol_log.assign(patrol_log.begin(), patrol_log.end());
  // The vehicles whose `is_hostile` slot answers (ships and enemy
  // submarines) and that are not sinking; slot +0xa0 picks the class
  // fields read.
  for (const std::unique_ptr<Vehicle>& v : mission.sim().vehicles()) {
    if (!v->is_hostile(player.side()) || v->sinking()) {
      continue;
    }
    const ShipClass* cls = nullptr;
    std::int32_t hit_points = 0;
    std::int32_t other_damage = 0;
    if (const auto* sub = dynamic_cast<const EnemySub*>(v.get())) {
      cls = sub->setup().ship_class;
      hit_points = sub->setup().hit_points;
      other_damage = sub->setup().damage;
    } else if (const auto* ship = dynamic_cast<const Ship*>(v.get())) {
      cls = ship->setup().ship_class;
      hit_points = ship->setup().hit_points;
      other_damage = ship->setup().damage;
    }
    if (cls == nullptr) {
      continue;
    }
    const fx::Vec3 p = v->position();
    o.afloat.push_back({.type = static_cast<std::int8_t>(cls->ship_type),
                        .class_name = cls->name,
                        .tonnage = static_cast<std::int32_t>(cls->displacement_tons),
                        .hull_strength = static_cast<std::int32_t>(cls->hull_strength),
                        .hit_points = hit_points,
                        .other_damage = other_damage,
                        .latitude = log_angle(world.latitude_at(p.z)),
                        .longitude = log_angle(world.longitude_at(p.x))});
  }
  return o;
}

std::string mission_results_summary(const MissionResults& r, const ExeImage& exe, const Font& font) {
  // mission_results_summary (0xa4c95)
  const auto table = [&exe](std::uint32_t address, std::int32_t i, std::uint32_t stride = 4) {
    return std::string(exe.string_pointer(address + static_cast<std::uint32_t>(i) * stride));
  };
  const auto damage_level = [](std::int32_t pct) {
    return (pct > 0 ? 1 : 0) + (pct > 30 ? 1 : 0) + (pct > 70 ? 1 : 0);
  };
  const auto position = [](const LogAngle& lat, const LogAngle& lon) {
    return std::format("{}-{}{}, {}-{}{}", lat.degrees, two_digits(lat.minutes), lat.hemisphere, lon.degrees,
                       two_digits(lon.minutes), lon.hemisphere);
  };
  const auto s = [&exe](std::uint32_t address) { return std::string(exe.c_string(address)); };
  std::string t;
  const auto pad = [&](int width) { ui::menu_pad_text_to_width(t, width, font); };
  // Two columns: a label at 0, its value at 0x50, a label at 0xfa, its
  // value at 0x15e.
  const auto row = [&](const std::string& l1, const std::string& v1, const std::string& l2, const std::string& v2,
                       std::string_view end) {
    t += l1;
    pad(0x50);
    t += v1;
    pad(0xfa);
    t += l2;
    pad(0x15e);
    t += v2;
    t += end;
  };
  row(s(0xfc8e8), std::format("{} {}, {}", table(addr::kMonths, r.month), r.day, r.year + exe.i32(addr::kBaseYear)),
      s(0xfc942), two_digits(r.hour) + ":" + two_digits(r.minute), "\n");
  row(s(0xfc98e), r.sub, s(0xfc9ae), r.class_name, "\n");
  row(s(0xfc9df), table(addr::kCrews, r.crew), s(0xfca00), table(addr::kFates, static_cast<std::int32_t>(r.fate)),
      "\n");
  if (r.fate == BoatFate::kOperational || r.fate == BoatFate::kRecalled) {
    row(s(0xfca32),
        std::format("{} ({}%)", table(addr::kDamageLevels, damage_level(r.hull_damage_pct)), r.hull_damage_pct),
        s(0xfca87),
        std::format("{} ({}%)", table(addr::kDamageLevels, damage_level(r.system_damage_pct)),
                    r.system_damage_pct),
        "\n");
  }
  row(s(0xfcadf), position(r.latitude, r.longitude), s(0xfcb72), r.patrol_area, "\n");
  row(s(0xfcba8), std::to_string(r.ships_sunk), s(0xfcbcf), std::to_string(r.tonnage), "\n");
  row(s(0xfcc02), std::format("{}%", r.difficulty), s(0xfcc38), std::format("{}", r.realism_pct),
      "%\n");
  if (r.pilots_rescued != 0 || r.photo_recons != 0) {
    row(s(0xfcc73), std::to_string(r.pilots_rescued), s(0xfcc9c), std::to_string(r.photo_recons),
        "\n");
  }
  t += s(0xfccd4);
  pad(0x50);
  t += std::to_string(r.score);
  t += "\n\n";
  if (!r.sunk.empty()) {
    bool header = false;
    for (const SunkListEntry& e : r.sunk) {
      if (e.log.type >= PatrolLogEntry::kRescuedPilot) {
        continue;
      }
      if (!header) {
        t += s(0xfcd06);
        pad(0x5a);
        t += s(0xfcd1c);
        pad(0x14a);
        t += s(0xfcd3b);
        pad(0x19a);
        t += s(0xfcd50);
        t += "\n\n";
        header = true;
      }
      t += position(e.log.latitude, e.log.longitude);
      pad(0x5a);
      t += table(addr::kShipTypes, e.log.type, 8);
      if (!e.log.name.empty()) {
        t += " (" + e.log.name + ")";
      }
      pad(0x14a);
      t += std::to_string(e.log.tonnage);
      pad(0x19a);
      t += table(addr::kStatuses, static_cast<std::int32_t>(e.status));
      if (e.status == SunkStatus::kDamaged && e.damage_pct > 0) {
        t += std::format(" ({}%)", e.damage_pct);
      }
      t += "\n";
    }
    t += "\n";
  }
  t += s(0xfce8e);
  return t;
}

}  // namespace sh

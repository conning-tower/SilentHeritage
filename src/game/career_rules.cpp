#include "game/career_rules.h"

#include <algorithm>
#include <cstddef>

#include "assets/campaign_month.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "core/fixed_math.h"
#include "core/install.h"
#include "sim/clock.h"

namespace sh {

namespace {

// The war's end 0x10e3b4..0x10e3bc (15 August 1945) and the date from
// which ComSubSoWesPac exists, 0x10e450 / 0x10e458 (April 1942).
constexpr std::int32_t kWarEndMonth = 7;
constexpr std::int32_t kWarEndDay = 15;
constexpr std::int32_t kWarEndYear = 15;
constexpr std::int32_t kSoWesPacMonth = 3;
constexpr std::int32_t kSoWesPacYear = 12;
// 0x103360: the promotion thresholds by rank.
constexpr std::array<std::int32_t, 2> kPromotionPatrols = {2, 5};
// The location text of a career that ended (0xf0d88, 0xf0da3, 0xf0dbe,
// 0xf0dd9).
constexpr const char* kNoLocation = "****";

// Reads an i32 of a home port's record at `offset`, 0 past its end (the
// original reads on into the next record).
std::int32_t port_i32(const HomePort& port, std::size_t offset) {
  return offset + 4 <= port.raw.size() ? static_cast<std::int32_t>(read_u32le(port.raw, offset)) : 0;
}

// The weight of `zone` in campaign month `month` (+0xe4 [zone][month]).
std::int32_t zone_weight(const HomePort& port, std::int32_t zone, std::int32_t month) {
  return port_i32(port, 0xe4 + static_cast<std::size_t>(zone) * 0xb4 + static_cast<std::size_t>(month) * 4);
}

std::int32_t career_month(const CareerRecord& r) { return campaign_month_index_abs(r.month, r.year + kCalendarBaseYear); }

// `homeport_load_dat(-1)`: ComSubSoWesPac is ComSubsAsiatic before it
// existed.
HomePort adjust_command(HomePort port, const CareerRecord& r) {
  if (career_month(r) < campaign_month_index_abs(kSoWesPacMonth, kSoWesPacYear + kCalendarBaseYear) &&
      port.command == SubCommand::kComSubSoWesPac) {
    port.command = SubCommand::kComSubsAsiatic;
  }
  return port;
}

std::int32_t command_index(SubCommand c) {
  // Transfers count Task Force 42 with ComSubSoWesPac.
  const auto i = static_cast<std::int32_t>(c);
  return i == 3 ? 2 : i;
}

}  // namespace

CampaignData CampaignData::load(const Install& install, const GameTables& tables) {
  CampaignData data;
  data.home_ports = tables.home_ports;
  data.zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "DATA/PZ.DAT");
  std::vector<bool> enabled;
  enabled.reserve(data.zones.size());
  for (const PatrolZone& z : data.zones) {
    enabled.push_back(install.exists("DATA/" + z.world));
  }
  data.set_enabled(std::move(enabled));
  return data;
}

void CampaignData::set_enabled(std::vector<bool> enabled) {
  zone_enabled = std::move(enabled);
  const auto leading = std::ranges::find(zone_enabled, false) - zone_enabled.begin();
  all_maps = leading > 12;
}

// ------------------------------------------------------------ calendar

void career_advance_hours(CareerRecord& r, std::int32_t hours) {
  // career_advance_hours (0x1c174)
  std::int32_t h = 0;
  for (; hours > 0; --hours) {
    if (++h == 24) {
      h = 0;
      ++r.day;
      if (r.day > days_in_month(r.year, r.month)) {
        r.day = 1;
        if (++r.month == 12) {
          r.month = 0;
          ++r.year;
        }
      }
    }
  }
}

bool date_after_war_end(std::int32_t day, std::int32_t month, std::int32_t year) {
  return year > kWarEndYear ||
         (year == kWarEndYear && (month > kWarEndMonth || (month == kWarEndMonth && day >= kWarEndDay)));
}

std::int32_t campaign_month_index_abs(std::int32_t month, std::int32_t full_year) {
  if (full_year == 1941) {
    return 0;
  }
  return month + (full_year - 1942) * 12 + 1;
}

std::int32_t jitter_transit_hours(std::int32_t hours, WatcomRandom& random) {
  const std::int32_t spread = hours * 5 / 100;
  return hours + spread / 2 - random.random_int(spread);
}

// ----------------------------------------------------------- home ports

std::vector<std::size_t> open_home_ports(const CampaignData& data, const CareerRecord& r) {
  // homeport_next_open (0x4d5cc)
  std::vector<std::size_t> open;
  const std::int32_t month = career_month(r);
  const std::int32_t today = month * 31 + r.day;
  for (std::size_t i = 0; i < data.home_ports.size(); ++i) {
    const HomePort& p = data.home_ports[i];
    bool listed = false;
    for (std::size_t z = 0; z < data.zones.size(); ++z) {
      if (zone_weight(p, static_cast<std::int32_t>(z), month) > 0 && z < data.zone_enabled.size() &&
          data.zone_enabled[z]) {
        listed = true;
      }
    }
    if (!listed) {
      continue;
    }
    const std::int32_t opens =
        campaign_month_index_abs(p.open_date.month, p.open_date.year) * 31 + p.open_date.day;
    const std::int32_t closes =
        campaign_month_index_abs(p.close_date.month, p.close_date.year) * 31 + p.close_date.day;
    if (opens <= today && today < closes) {
      open.push_back(i);
    }
  }
  return open;
}

std::optional<HomePort> find_home_port(const CampaignData& data, const CareerRecord& r, std::string_view name) {
  // homeport_find (0x4db8d)
  for (const std::size_t i : open_home_ports(data, r)) {
    if (equals_ignore_case(data.home_ports[i].name, name)) {
      return adjust_command(data.home_ports[i], r);
    }
  }
  return std::nullopt;
}

// ----------------------------------------------------- patrol assignment

std::optional<std::int32_t> pick_patrol_zone(Career& career, const CampaignData& data, WatcomRandom& random) {
  // patrol_pick_zone (0x63724)
  CareerRecord& r = career.record;
  const std::optional<HomePort> port = find_home_port(data, r, r.home_port);
  if (!port) {
    throw DataError("career home port " + r.home_port + " is not open");
  }
  // campaign_month_index: months since December 1941 (the reimplementation
  // takes a 1-based month and a full year).
  const std::int32_t month = campaign_month_index(r.year + kCalendarBaseYear, r.month + 1);
  std::int32_t pick = -1;
  if (r.on_patrol == 0) {
    const auto count = static_cast<std::int32_t>(data.zones.size());
    std::vector<bool> eligible(data.zones.size());
    std::int32_t total = 0;
    for (int pass = 1; pass >= 0; --pass) {
      total = 0;
      for (std::int32_t z = 0; z < count; ++z) {
        const auto zi = static_cast<std::size_t>(z);
        eligible[zi] = data.zone_enabled[zi] &&
                       (career.summary.same_zone_repeats < 1 || pass == 0 || z != r.last_patrol_zone);
        if (eligible[zi]) {
          total += zone_weight(*port, z, month);
        }
      }
      if (total > 0) {
        const std::int32_t draw = random.random_int(total);
        std::int32_t sum = 0;
        for (std::int32_t z = 0; z < count; ++z) {
          if (eligible[static_cast<std::size_t>(z)]) {
            sum += zone_weight(*port, z, month);
            pick = z;
            if (sum > draw) {
              break;
            }
          }
        }
        break;
      }
    }
  } else {
    pick = r.last_patrol_zone;
  }
  r.last_patrol_zone = pick;
  if (pick < 0) {
    return std::nullopt;
  }
  return pick;
}

Transit compute_transit(const HomePort& port, std::int32_t zone, const CareerRecord& r) {
  // patrol_compute_transit (0x638d0)
  Transit t;
  t.hours = port.zone_distance_nm.at(static_cast<std::size_t>(zone)) / (r.transit_speed * 2 / 3);
  // The fuel use (16.16 per second) times 3600.0, rounded, in whole units
  // toward zero.
  const std::int32_t per_hour = fx::trunc(fx::mul(r.fuel_use, 0xe100000));
  t.fuel = per_hour * 35 / 100 * t.hours;
  return t;
}

std::uint32_t career_realism_options(std::uint32_t options, const CareerRecord& r) {
  // career_assign_patrol (0x62df9)
  const auto v = [&r](CareerRealism k, std::uint32_t mask) {
    return static_cast<std::uint32_t>(r.realism_value(k)) & mask;
  };
  options &= ~0xfffcu;
  options |= v(CareerRealism::kCombatLevel, 3) << 2 | v(CareerRealism::kSailingModel, 3) << 4 |
             v(CareerRealism::kLimitedVisibility, 1) << 7;
  options |= v(CareerRealism::kUnlimitedFuel, 1) << 9 | v(CareerRealism::kUnlimitedBattery, 1) << 10 |
             v(CareerRealism::kUnlimitedAmmo, 1) << 11 | v(CareerRealism::kRealisticReloads, 1) << 13 |
             v(CareerRealism::kDudTorpedoes, 1) << 14 | v(CareerRealism::kLimitedDepthDataOff, 1) << 12 |
             v(CareerRealism::kInvulnerable, 1) << 15;
  options |= v(CareerRealism::kRealisticChartsOff, 1) << 6 | v(CareerRealism::kRunAground, 1) << 8;
  return options;
}

std::optional<PatrolAssignment> assign_patrol(Career& career, const CampaignData& data, WatcomRandom& random) {
  // career_assign_patrol (0x62df9), up to the patrol orders screen.
  CareerRecord& r = career.record;
  PatrolAssignment a;
  a.previous_zone = r.last_patrol_zone;
  for (int tries = 0;; ++tries) {
    if (tries == 100000) {
      throw DataError("no patrol zone within the boat's fuel");
    }
    const std::optional<std::int32_t> zone = pick_patrol_zone(career, data, random);
    if (!zone) {
      return std::nullopt;
    }
    const std::optional<HomePort> port = find_home_port(data, r, r.home_port);
    const Transit t = compute_transit(*port, *zone, r);
    a.zone_index = *zone;
    a.zone = data.zones[static_cast<std::size_t>(*zone)];
    a.transit_hours = t.hours;
    a.transit_fuel = t.fuel;
    if (t.fuel <= 0 || t.fuel * 2 < r.fuel_capacity - r.fuel_capacity * 10 / 100) {
      return a;
    }
  }
}

void accept_patrol_zone(Career& career, PatrolAssignment& a) {
  // patrol_accept_zone (0x636b8)
  a.start_edges = a.zone.start_edges;
  if (career.record.last_patrol_zone == a.previous_zone) {
    ++career.summary.same_zone_repeats;
  } else {
    career.summary.same_zone_repeats = 0;
  }
}

// ---------------------------------------------------------- port time

std::int32_t compute_repair_days(const MissionResults& results, WatcomRandom& random) {
  // patrol_compute_repair_days (0x63971)
  const std::int32_t hull = results.hull_damage_pct * 25 / 100;
  const std::int32_t hull_spread = hull * 5 / 100;
  const std::int32_t hull_draw = random.random_int(hull_spread);
  const std::int32_t systems = results.system_damage_pct * 20 / 100;
  const std::int32_t systems_spread = systems * 5 / 100;
  const std::int32_t systems_draw = random.random_int(systems_spread);
  return hull + (hull_spread / 2 - hull_draw) + systems + (systems_spread / 2 - systems_draw);
}

std::int32_t roll_refit_days(WatcomRandom& random) { return random.random_int(7) + 5; }

PatrolBookkeeping apply_patrol_results(Career& career, PatrolAssignment& a, MissionResults& res,
                                       WatcomRandom& random) {
  // career_apply_patrol_results (0x1bc18)
  CareerRecord& r = career.record;
  PatrolBookkeeping out;
  ++r.patrols;
  ++r.promotion_patrols;
  ++r.patrols_since_new_boat;
  r.score += res.score;
  r.unread_score += res.score;
  ++r.patrols_from_port;
  r.tonnage += res.tonnage;
  r.ships_sunk += res.ships_sunk;
  r.sunk_since_bronze_star += res.ships_sunk;
  r.on_patrol = 0;
  r.location = r.home_port;
  r.pilots_rescued += res.pilots_rescued;
  ++career.summary.patrols_since_transfer;
  r.photo_recons += res.photo_recons;
  if (res.boat_lost != 0) {
    ++r.boats_lost;
    res.hull_damage_pct = 0;
    res.system_damage_pct = 0;
  }
  a.repair_days = compute_repair_days(res, random);
  a.refit_days = roll_refit_days(random);
  // career_advance_port_time (0x1c492): only the war's end is kept, the
  // date is replaced below.
  if (res.fate != BoatFate::kKilled && res.fate != BoatFate::kCaptured) {
    career_advance_hours(r, (a.repair_days + a.refit_days + 8) * 24);
    if (date_after_war_end(r.day, r.month, r.year)) {
      res.fate = BoatFate::kRecalled;
    }
  }
  r.status = CareerStatus::kInPort;
  switch (res.fate) {
    case BoatFate::kKilled:
      r.status = CareerStatus::kDeceased;
      r.location = kNoLocation;
      out.career_over = true;
      break;
    case BoatFate::kCaptured:
      r.status = CareerStatus::kCaptured;
      r.location = kNoLocation;
      out.career_over = true;
      break;
    case BoatFate::kRecalled:
      r.status = CareerStatus::kRetired;
      res.day = kWarEndDay;
      res.month = kWarEndMonth;
      res.year = kWarEndYear;
      r.location = kNoLocation;
      out.career_over = true;
      out.war_ended = true;
      break;
    case BoatFate::kOperational:
      break;
  }
  r.day = res.day;
  r.month = res.month;
  r.year = res.year;
  // The realism rating is computed here and overwritten (an original bug):
  // the test is on the ships sunk besides sampans, 2 or more counting
  // whatever the realism. With the rules' corrections (not in the
  // original), each real ship counts half at no realism and whole at 100 %:
  // ships x (100 + realism %) >= 400, so 4 ships below 34 %, 3 below 100 %,
  // 2 from 100 %.
  std::int32_t real_ships = res.ships_sunk - res.sampans_sunk;
  const bool counted = career.rules.value_or(MissionRules{}).fix_obvious_errors
                           ? real_ships * (100 + res.realism_pct) >= 400
                           : real_ships >= 2;
  if (!counted) {
    r.puc_streak = 0;
  } else {
    ++r.high_realism_patrols;
    ++r.nuc_streak;
    ++r.puc_streak;
  }
  if (real_ships == 0 && static_cast<std::int16_t>(r.raids_this_patrol) > 0 && (r.photo_recons > 0 || r.pilots_rescued > 0)) {
    real_ships = 1;
  }
  if (real_ships == 0 && res.fate != BoatFate::kRecalled) {
    ++r.poor_patrol_streak;
  } else {
    r.poor_patrol_streak = 0;
  }
  return out;
}

std::vector<AwardCode> check_relieved(Career& career, const MissionResults& res) {
  // career_check_relieved (0x1c01b)
  CareerRecord& r = career.record;
  std::vector<AwardCode> letters;
  if (r.status != CareerStatus::kInPort) {
    return letters;
  }
  bool relieved = false;
  if (res.fuel == 0) {
    letters.push_back(AwardCode::kPoorFuel);
    if (++r.fuel_failures > 3) {
      relieved = true;
    }
  }
  if (r.poor_patrol_streak > 0) {
    letters.push_back(AwardCode::kPoorPatrol);
    if (r.poor_patrol_streak > 2) {
      relieved = true;
    }
  }
  if (static_cast<std::int16_t>(r.boats_lost) > 1) {
    relieved = true;
  }
  if (relieved) {
    letters.push_back(AwardCode::kRelieved);
    r.status = CareerStatus::kRelieved;
    r.location = kNoLocation;
  }
  return letters;
}

std::vector<AwardCode> check_awards(Career& career, const MissionResults& res) {
  // career_check_awards (0x1be59)
  CareerRecord& r = career.record;
  std::vector<AwardCode> won;
  if (r.poor_patrol_streak != 0) {
    return won;
  }
  const std::int32_t damage = (res.system_damage_pct + res.hull_damage_pct) / 2;
  const bool alive = r.status != CareerStatus::kDeceased;
  bool given = false;
  std::int32_t& silver = r.award(Award::kSilverStar);
  if (silver < 5 && (silver + 1 <= res.warships_sunk ||
                     (damage > 50 && alive && silver + 1 <= res.ships_sunk - res.sampans_sunk))) {
    given = true;
    ++silver;
    won.push_back(AwardCode::kSilverStar);
  }
  std::int32_t& cross = r.award(Award::kNavyCross);
  if (cross < 5 && !given && (cross + 1) * 50000 <= r.score) {
    given = true;
    ++cross;
    won.push_back(AwardCode::kNavyCross);
  }
  std::int32_t& honor = r.award(Award::kMedalOfHonor);
  if (honor == 0 && !given && (r.score > 99999 || (damage > 50 && res.score > 39999))) {
    honor = 1;
    won.push_back(AwardCode::kMedalOfHonor);
  }
  std::int32_t& dsm = r.award(Award::kDsm);
  if (dsm < 5 && r.high_realism_patrols > 4 && alive) {
    r.high_realism_patrols = 0;
    ++dsm;
    won.push_back(AwardCode::kDsm);
  }
  std::int32_t& bronze = r.award(Award::kBronzeStar);
  if (bronze < 5 && (bronze + 1) * 4 <= r.sunk_since_bronze_star) {
    r.sunk_since_bronze_star = 0;
    ++bronze;
    won.push_back(AwardCode::kBronzeStar);
  }
  if (res.fate == BoatFate::kRecalled && r.patrols > 4) {
    ++r.award(Award::kAsiaticPacific);
    won.push_back(AwardCode::kAsiaticPacific);
  }
  std::int32_t& nuc = r.award(Award::kNuc);
  std::int32_t& puc = r.award(Award::kPuc);
  if (nuc + puc < 8) {
    bool unit = false;
    if (r.puc_streak > 5) {
      r.puc_streak = 0;
      ++puc;
      won.push_back(AwardCode::kPuc);
      unit = true;
    }
    if (!unit && r.nuc_streak > 2) {
      ++nuc;
      r.nuc_streak = 0;
      won.push_back(AwardCode::kNuc);
    }
  }
  return won;
}

NewCommand check_new_command(Career& career, const CampaignData& data) {
  // career_check_new_command (0x1c0c6)
  CareerRecord& r = career.record;
  const std::optional<HomePort> port = find_home_port(data, r, r.home_port);
  if (!port) {
    throw DataError("career home port " + r.home_port + " is not open");
  }
  NewCommand out;
  if (r.rank < 2 && kPromotionPatrols[static_cast<std::size_t>(r.rank)] < r.promotion_patrols) {
    ++r.rank;
    out.promoted = true;
  }
  const auto boat_class = static_cast<std::int8_t>(r.class_index);
  if ((port->command == SubCommand::kComSubPac && boat_class == 0) || (boat_class == 0 && r.patrols > 2) ||
      (boat_class < 8 && r.patrols < r.ships_sunk && r.patrols_since_new_boat > 3)) {
    out.offer_boat = true;
    out.from_fleet_boat = boat_class != 0;
    r.patrols_since_new_boat = 0;
  }
  return out;
}

// ------------------------------------------------------------ transfers

std::optional<HomePort> pick_transfer_port(const Career& career, const CampaignData& data, bool change_command,
                                           WatcomRandom& random) {
  // homeport_pick_transfer_port (0x4dbe0), with HOMEPORT.DAT's commands as
  // in the file.
  const CareerRecord& r = career.record;
  const CareerSummary& s = career.summary;
  const std::int32_t month = career_month(r);
  const bool has_command = s.current_command >= 0 || s.preferred_command >= 0;
  std::int32_t target = s.current_command >= 0 ? s.current_command : s.preferred_command;
  if (target == 3) {
    target = 2;
  }
  const std::vector<std::size_t> open = open_home_ports(data, r);
  std::vector<bool> eligible(open.size());
  std::int32_t total = 0;
  for (std::size_t i = 0; i < open.size(); ++i) {
    const HomePort& p = data.home_ports[open[i]];
    if ((p.flags & 1u) == 0) {
      continue;
    }
    const std::int32_t command = command_index(p.command);
    if ((!data.all_maps || !change_command || target != command) &&
        (!data.all_maps || change_command || target == command || !has_command)) {
      total += port_i32(p, 0x30 + static_cast<std::size_t>(month) * 4);
      eligible[i] = true;
    }
  }
  if (total == 0) {
    return std::nullopt;
  }
  const std::int32_t draw = random.random_int(total);
  std::int32_t sum = 0;
  for (std::size_t i = 0; i < open.size(); ++i) {
    const HomePort& p = data.home_ports[open[i]];
    if (eligible[i]) {
      sum += port_i32(p, 0x30 + static_cast<std::size_t>(month) * 4);
      if (draw < sum) {
        if (equals_ignore_case(r.home_port, p.name)) {
          return std::nullopt;
        }
        return p;
      }
    }
  }
  return std::nullopt;
}

std::optional<HomePort> transfer_home_port(Career& career, PatrolAssignment& a, const CampaignData& data,
                                           bool change_command, bool no_travel, WatcomRandom& random) {
  // career_transfer_home_port (0x4de43)
  CareerRecord& r = career.record;
  const std::int32_t month = r.month;
  const std::int32_t day = r.day;
  const std::int32_t year = r.year;
  if (!no_travel) {
    const std::int32_t base = change_command ? 10 : 4;
    a.travel_days = base + random.random_int(base);
    career_advance_hours(r, a.travel_days * 24);
  }
  std::optional<HomePort> port = pick_transfer_port(career, data, change_command, random);
  if (!port) {
    r.month = month;
    r.day = day;
    r.year = year;
    a.travel_days = 0;
    return std::nullopt;
  }
  r.home_port = port->name.substr(0, 19);
  r.location = r.home_port;
  career.summary.current_command = static_cast<std::int32_t>(port->command);
  r.patrols_from_port = 0;
  if (change_command) {
    career.summary.patrols_since_transfer = 0;
    career.summary.transfer_offered = 0;
  }
  return port;
}

std::optional<HomePort> check_transfer(Career& career, PatrolAssignment& a, const CampaignData& data,
                                       WatcomRandom& random) {
  // career_check_transfer (0x4dd7e)
  CareerRecord& r = career.record;
  CareerSummary& s = career.summary;
  int transfer = find_home_port(data, r, r.home_port) ? 0 : 1;
  if (transfer == 0 && r.patrols_from_port > 2 && random.random_int(100) < (r.patrols_from_port - 2) * 40) {
    transfer = 1;
  }
  if (s.same_zone_repeats > 1) {
    transfer = 1;
  }
  if (data.all_maps && s.patrols_since_transfer > 4) {
    if (s.preferred_command < 0) {
      if (random.random_int(100) < (r.patrols_from_port - 4) * 10) {
        transfer = 2;
      }
    } else if (r.ships_sunk / r.patrols > 0) {
      s.transfer_offered = -1;
    }
  }
  if (transfer == 0) {
    return std::nullopt;
  }
  return transfer_home_port(career, a, data, transfer > 1, false, random);
}

// ------------------------------------------------------- top commanders

std::vector<TopCommanderRow> top_commanders(const std::vector<Commander>& commanders, const CareerRecord& r) {
  // cmdlist_build_top_ten (0x78e30)
  const std::int32_t months = campaign_month_index(r.year + kCalendarBaseYear, r.month + 1);
  std::vector<std::int32_t> sums(commanders.size(), 0);
  for (std::size_t c = 0; c < commanders.size(); ++c) {
    for (std::int32_t m = 0; m < months; ++m) {
      // The original sums on into the next record past the 45 months.
      sums[c] += m < kCampaignMonths ? commanders[c].tonnage_by_month[static_cast<std::size_t>(m)] : 0;
    }
  }
  std::array<std::int32_t, 10> slots;
  slots.fill(-1);
  std::vector<bool> taken(commanders.size(), false);
  for (std::size_t slot = 0; slot < 9; ++slot) {
    std::int32_t best = 0;
    std::int32_t index = -1;
    for (std::size_t c = 0; c < commanders.size(); ++c) {
      if (!taken[c] && best < sums[c]) {
        index = static_cast<std::int32_t>(c);
        best = sums[c];
      }
    }
    slots[slot] = index;
    if (index >= 0) {
      taken[static_cast<std::size_t>(index)] = true;
    }
  }
  const auto player = static_cast<std::int32_t>(commanders.size());
  std::size_t at = 0;
  while (at < slots.size() && slots[at] >= 0 && sums[static_cast<std::size_t>(slots[at])] >= r.tonnage) {
    ++at;
  }
  if (at < slots.size()) {
    for (std::size_t i = slots.size() - 1; i > at; --i) {
      slots[i] = slots[i - 1];
    }
    slots[at] = player;
  }
  std::vector<TopCommanderRow> rows;
  for (const std::int32_t s : slots) {
    if (s == player) {
      rows.push_back({.player = true, .commander = -1, .tonnage = r.tonnage});
    } else if (s >= 0) {
      rows.push_back({.player = false, .commander = s, .tonnage = sums[static_cast<std::size_t>(s)]});
    }
  }
  return rows;
}

}  // namespace sh

#include "game/frontend_briefing.h"

#include <format>
#include <span>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "game/mission.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

// SH.EXE addresses of `mission_briefing` (its labels are read inline by
// address).
namespace addr {
constexpr std::uint32_t kPatrolTitle = 0xfceb0;   // "PATROL BRIEFING"
constexpr std::uint32_t kMissionTitle = 0xfcec0;  // "MISSION BRIEFING"
constexpr std::uint32_t kMonths = 0x10e910;       // char*[12] "January"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;     // i32 1930
constexpr std::uint32_t kCrews = 0x10eb1c;        // char*[3] "Green"..
constexpr std::uint32_t kWeathers = 0x10e98c;     // char*[4] "Clear"..
constexpr std::uint32_t kSeaStates = 0x10e99c;    // char*[3] "Calm"..
constexpr std::uint32_t kMoonPhases = 0x10e9ec;   // char*[5] "Full"..
// The equipment lines, indexed by option bits: SD radar (bit 18), SJ radar
// (bits 16-17), PPI (bit 19), bathythermograph (bit 21); "" when absent.
constexpr std::uint32_t kSdRadar = 0x10eae8;
constexpr std::uint32_t kSjRadar = 0x10eadc;
constexpr std::uint32_t kPpi = 0x10eaf0;
constexpr std::uint32_t kBathythermograph = 0x10eaf8;
}  // namespace addr

// The column of the values (`menu_pad_text_to_width(text, 0x4b)`) and the
// indent of the equipment lines.
constexpr int kValueColumn = 0x4b;
constexpr int kEquipmentIndent = 10;
// The fog density from which the weather reads "Foggy".
constexpr std::int32_t kFoggyPct = 0x1f;

// `int_to_padded_str` (0x5908d): the last `digits` digits of `value`,
// zero-padded, a negative value as 0.
std::string padded(std::int32_t value, int digits) {
  std::string s(static_cast<std::size_t>(digits), '0');
  std::int32_t v = value < 0 ? 0 : value;
  for (int i = digits - 1; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = static_cast<char>('0' + v % 10);
    v /= 10;
  }
  return s;
}

}  // namespace

MissionBriefing mission_briefing_facts(const Mission& mission) {
  MissionBriefing b;
  const MissionConditions& c = mission.sim().conditions();
  b.options = c.options;
  b.difficulty = mission.difficulty_rating();
  const GameClock& clock = mission.sim().clock();
  b.day = clock.day();
  b.month = clock.month();
  b.year = clock.year();
  b.hour = clock.hour();
  b.minute = clock.minute();
  b.sub = mission.player_name();
  b.class_name = mission.player_class().display_name;
  b.crew = mission.player_crew();
  const World& world = mission.world();
  b.patrol_area = world.region_name;
  const PlayerSub& player = mission.player();
  const fx::Vec3 at = player.position();
  b.latitude = world.latitude_at(at.z);
  b.longitude = world.longitude_at(at.x);
  b.fuel = player.fuel();
  b.fuel_capacity = static_cast<std::int32_t>(mission.player_class().fuel_capacity);
  b.heading = player.heading();
  b.speed = player.speed();
  b.depth_ft = player.depth_ft();
  b.fog_pct = mission.fog_pct();
  b.weather = c.weather;
  b.sea_state = c.sea_state;
  b.sun = mission.sky().sun_times();
  b.moon = mission.sky().moon_times();
  b.moon_phase = mission.sky().state().moon_phase;
  // The tubes the class has, then its forward and aft racks, each torpedo
  // counted under its WEAPONS.DAT entry.
  const SubClass& cls = mission.player_class();
  const TorpedoLoad& load = mission.torpedoes();
  const std::vector<Torpedo>& types = mission.tables().weapons.torpedoes;
  std::vector<std::int32_t> counts(types.size(), 0);
  const auto count = [&](std::span<const std::int32_t> slots, std::uint32_t used) {
    for (std::size_t i = 0; i < used && i < slots.size(); ++i) {
      for (std::size_t t = 0; t < types.size(); ++t) {
        if (slots[i] != 0 && types[t].id == static_cast<std::uint32_t>(slots[i])) {
          ++counts[t];
          break;
        }
      }
    }
  };
  count(load.tubes, cls.bow_tubes + cls.stern_tubes);
  count(load.bow_racks, cls.bow_reloads);
  count(load.stern_racks, cls.stern_reloads);
  for (std::size_t t = 0; t < types.size(); ++t) {
    if (counts[t] != 0) {
      b.torpedoes.push_back({types[t].long_name, counts[t]});
    }
  }
  return b;
}

std::string mission_briefing_title(const MissionBriefing& briefing, const ExeImage& exe) {
  return std::string(
      exe.c_string((briefing.options & option::kModeCareer) != 0 ? addr::kPatrolTitle : addr::kMissionTitle));
}

std::string mission_briefing_text(const MissionBriefing& b, const ExeImage& exe, const Font& font) {
  // mission_briefing (0xa5a37)
  const auto s = [&exe](std::uint32_t address) { return std::string(exe.c_string(address)); };
  const auto table = [&exe](std::uint32_t address, std::uint32_t i) {
    return std::string(exe.string_pointer(address + i * 4));
  };
  const auto index = [](std::int32_t i) { return static_cast<std::uint32_t>(i); };
  const bool patrol = (b.options & option::kModeCareer) != 0;
  std::string t;
  // A label, its value at the value column, then `end`.
  const auto line = [&](std::uint32_t label, const std::string& value, std::uint32_t end) {
    t += s(label);
    ui::menu_pad_text_to_width(t, kValueColumn, font);
    t += value;
    t += s(end);
  };
  const auto time = [](std::int32_t h, std::int32_t m) { return padded(h, 2) + ":" + padded(m, 2); };
  const auto position = [](const GeoAngle& a) {
    return std::format("{}-{}{}", a.whole_degrees, padded(a.minutes, 2), a.hemisphere);
  };

  if (!patrol) {
    line(0xfcef9, std::to_string(b.difficulty / 10), 0xfcf20);
  }
  line(0xfcf31,
       std::format("{}{}{}{}{}", table(addr::kMonths, index(b.month)), s(0xfcf52), b.day, s(0xfcf6e),
                   b.year + exe.i32(addr::kBaseYear)),
       0xfcf8b);
  line(0xfcf9a, padded(b.hour, 2) + s(0xfcfbb) + padded(b.minute, 2), 0xfcfd7);
  line(0xfcfe6, b.sub, 0xfd006);
  line(0xfd015, b.class_name, 0xfd037);
  line(0xfd046, table(addr::kCrews, index(b.crew)), 0xfd067);
  line(0xfd076, b.patrol_area, 0xfd09d);
  line(0xfd0ac, position(b.latitude) + s(0xfd0fa) + position(b.longitude), 0xfd140);
  if (patrol) {
    const std::int32_t pct = b.fuel_capacity != 0 ? b.fuel * 100 / b.fuel_capacity : 0;
    line(0xfd14f, std::to_string(b.fuel) + s(0xfd170) + std::to_string(pct), 0xfd196);
  }
  line(0xfd1a7, padded(b.heading.tenths() / 10, 3), 0xfd1cb);
  // The whole knots, truncated towards zero.
  const std::int32_t raw = b.speed.raw();
  const std::int32_t knots = raw < 0 ? -((-raw) >> 16) : raw >> 16;
  line(0xfd1da, std::to_string(knots) + s(0xfd1fc), 0xfd210);
  line(0xfd21f, std::to_string(b.depth_ft) + s(0xfd241), 0xfd254);
  t += s(0xfd263);
  if (!patrol) {
    line(0xfd272, b.fog_pct < kFoggyPct ? table(addr::kWeathers, index(b.weather)) : s(0xfd289), 0xfd29c);
    line(0xfd2ab, table(addr::kSeaStates, index(b.sea_state)), 0xfd2ca);
    line(0xfd2d9, time(b.sun.rise_hour, b.sun.rise_minute), 0xfd318);
    line(0xfd327, time(b.sun.set_hour, b.sun.set_minute), 0xfd365);
    line(0xfd374, table(addr::kMoonPhases, index(b.moon_phase)), 0xfd394);
    line(0xfd3a3, time(b.moon.rise_hour, b.moon.rise_minute), 0xfd3e3);
    line(0xfd3f2, time(b.moon.set_hour, b.moon.set_minute), 0xfd431);
    t += s(0xfd440);
  }
  t += s(0xfd45c);
  for (const MissionBriefing::Torpedoes& e : b.torpedoes) {
    ui::menu_pad_text_to_width(t, kEquipmentIndent, font);
    t += e.name + s(0xfd483) + std::to_string(e.count) + s(0xfd4a8);
  }
  const std::uint32_t o = b.options;
  for (const std::string& item : {table(addr::kSdRadar, (o >> 18) & 1), table(addr::kSjRadar, (o >> 16) & 3),
                                  table(addr::kPpi, (o >> 19) & 1), table(addr::kBathythermograph, (o >> 21) & 1)}) {
    ui::menu_pad_text_to_width(t, kEquipmentIndent, font);
    t += item;
  }
  t += s(0xfd4f9);
  return t;
}

}  // namespace sh

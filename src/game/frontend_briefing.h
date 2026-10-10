// The text of the MISSION BRIEFING / PATROL BRIEFING screen
// (`mission_briefing`, 0xa5a37) that `init_simulation` shows once a new
// mission is built (`mission_briefing_screen`, 0xa5a07; not for a saved
// mission or a career resumed on patrol).
//
//   MissionBriefing facts = mission_briefing_facts(mission);
//   std::string text = mission_briefing_text(facts, exe, menu_small_font);
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/world.h"
#include "core/fixed.h"
#include "sim/astronomy.h"

namespace sh {

class ExeImage;
class Font;
class Mission;

// What `mission_briefing` reads of the mission just built.
struct MissionBriefing {
  // The options word `0x11a85c`: bit 0 career (PATROL BRIEFING), the
  // equipment bits 16..21.
  std::uint32_t options = 0;
  std::int32_t difficulty = 0;  // 0x125ff0, 0..1000
  // The clock 0x129c10..0x129c20: year since 1930, month 0-based.
  std::int32_t day = 1;
  std::int32_t month = 0;
  std::int32_t year = 0;
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::string sub;         // 0x11ac08, e.g. "(SS-187) Sturgeon"
  std::string class_name;  // SUBS.DAT +10 (0x11b12a)
  std::int32_t crew = 0;   // 0x11ac2b, 0 green .. 2 elite
  std::string patrol_area;  // the .WLD region name (0x170f4c)
  GeoAngle latitude;        // the boat's position
  GeoAngle longitude;
  std::int32_t fuel = 0;           // 0x11ac88, gallons
  std::int32_t fuel_capacity = 0;  // SUBS.DAT +268 (0x11b22c)
  Angle10 heading;                 // 0x11aa3c
  Fixed16 speed;                   // 0x11aa58, knots
  std::int32_t depth_ft = 0;       // 0x11ab0c
  std::int32_t fog_pct = 0;        // 0x170f88
  std::int32_t weather = 0;        // 0x125f60, 0 clear .. 3 overcast
  std::int32_t sea_state = 0;      // 0x125f64
  RiseSetTimes sun;                // 0x129c2c..0x129c38
  RiseSetTimes moon;               // 0x129c3c..0x129c48
  std::int32_t moon_phase = 0;     // 0x129c28
  // The torpedoes in the tubes and racks per WEAPONS.DAT torpedo (its
  // name, +10), in the table's order, those carried only.
  struct Torpedoes {
    std::string name;
    std::int32_t count = 0;
  };
  std::vector<Torpedoes> torpedoes;
};

// Returns what the briefing reads of `mission` as built.
MissionBriefing mission_briefing_facts(const Mission& mission);

// Returns the dialog title: "PATROL BRIEFING" for a career patrol (options
// bit 0), else "MISSION BRIEFING".
std::string mission_briefing_title(const MissionBriefing& briefing, const ExeImage& exe);

// `mission_briefing` (0xa5a37): the briefing text, its values aligned with
// `menu_pad_text_to_width` in `font` (the text box's font); strings from
// SH.EXE. A single mission's text adds the difficulty and the weather and
// sky lines, a patrol's the fuel.
std::string mission_briefing_text(const MissionBriefing& briefing, const ExeImage& exe, const Font& font);

}  // namespace sh

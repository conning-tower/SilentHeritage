// The chart's info texts (the hover popup's lines: `chart_contact_report`
// and the other `chart_*_report` / `*_info` functions of module chart,
// 0x22171..0x29eac) and the strategic contact groups' chart symbols
// (`convoy_group_chart_draw` and the other kinds' slot +0x14, their hit
// rectangles slot +0x20). Every string comes from SH.EXE.
//
// The lines are the original's six 75-character buffers in order; the
// popup shows them up to the first empty one (`chart_popup_lines`).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "assets/bases.h"
#include "assets/exe_image.h"
#include "assets/world.h"
#include "core/fixed.h"
#include "game/patrol_contacts.h"
#include "sim/clock.h"
#include "sim/tdc.h"
#include "sim/vehicle.h"

namespace sh {

struct ShipClass;
class AirGroup;
class DownedPilot;
class IntelReport;

// Returns `lines` up to the first empty one (what `chart_show_info_popup`
// sizes and shows).
std::vector<std::string> chart_popup_lines(const std::vector<std::string>& lines);

// Returns "Loc: <lat>, <lon>" of world point (`x`, `z`) with SH.EXE's
// "Loc: " at `loc` and ", " at `comma` (`world_pos_to_latlon` and
// `chart_format_latlon`); only "Loc: " on a map without bounds.
std::string chart_location_line(const ExeImage& exe, const World& world, std::int32_t x, std::int32_t z,
                                std::uint32_t loc, std::uint32_t comma);

// Where the chart shows a vehicle (vehicle slot +0x3c: `ship_chart_position`
// 0x27862, `enemy_sub_chart_position` 0x278de; the other classes have
// none).
struct ChartFix {
  std::int32_t x = 0;
  std::int32_t z = 0;
  std::int32_t heading = -1;  // Tenths of a degree, -1 unknown (not seen).
  std::int32_t age = 0;       // 16.16 s since seen; 0 without Limited Visibility.
};

// Returns the chart position of `v` (ships and enemy submarines): the true
// one, or with Limited Visibility (`limited`) the last-known one, none if
// never seen.
std::optional<ChartFix> vehicle_chart_fix(const Vehicle& v, bool limited);

// The live members of a contact group in the simulation's list order, with
// whether each is its convoy's leader (vehicle slot +0xac(3)).
struct GroupMember {
  const Vehicle* vehicle = nullptr;
  bool leader = false;
};

// The time and date the chart stamps on a refreshed contact.
struct ChartStamp {
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t month = 0;  // 0-based
  std::int32_t day = 0;
};

// The refresh at the start of a group's chart draw while it is
// materialized (`convoy_group_chart_draw` 0x27a08 and the patrol, anchored
// and sampan draws): the group's contact becomes the convoy leader seen
// (with its heading), else its member nearest to the player at
// (`player_x`, `player_z`) (heading unknown); timer 8 h less its age. A
// route group also takes the member's heading and speed. Does nothing for
// a finished or unmaterialized group or one with no member on the chart.
void refresh_group_contact(ContactGroup& g, const std::vector<GroupMember>& members, std::int32_t player_x,
                           std::int32_t player_z, const ChartStamp& now, bool limited);

// A contact group's chart symbol: a CHART.SHP frame drawn with its colour
// 0x30 as `colour`, and for route groups with a known heading a 15-pixel
// tick behind it.
struct GroupSymbol {
  std::int32_t x = 0;  // World point.
  std::int32_t z = 0;
  std::size_t frame = 0;
  std::uint8_t colour = 0;
  std::optional<Angle10> heading;
};

// Returns the symbol of `g` when its contact is valid and records its
// frame (`report.symbol_frame`), as the kinds' chart draws do; the colour
// fades over the 8 hours of the contact in five steps.
std::optional<GroupSymbol> group_symbol(ContactGroup& g, const ExeImage& exe);

// Returns the lines of `g`'s contact report (vehicle slot +0x24:
// `chart_contact_report` 0x28338, `chart_patrol_contact_report` 0x28d7b,
// `chart_anchored_ships_report` 0x29437, `chart_sampans_sighted_report`
// 0x29a62).
std::vector<std::string> group_report_lines(const ContactGroup& g, const ExeImage& exe, const World& world);

// Returns the lines of a port's info (`chart_port_info` 0x22171): name,
// location, owner, size and kind.
std::vector<std::string> port_info_lines(const BaseRecord& base, BaseSide owner, const ExeImage& exe,
                                         const World& world);

// What `chart_ship_contact_info` (0x22f02) and `chart_contact_info`
// (0x23ce6) show of a ship or an enemy submarine.
struct HullContact {
  const ShipClass* cls = nullptr;
  bool submarine = false;
  bool seen = false;     // On the chart at its true position.
  bool sinking = false;  // Shown sinking.
  std::int32_t sighted_hour = 0;
  std::int32_t sighted_minute = 0;
  SolutionEstimate estimate;  // The crew's estimate (`tdc_estimate_solution`).
  std::uint32_t options = 0;  // 0x11a85c
};

// Returns the lines of a ship's or an enemy submarine's info.
std::vector<std::string> hull_contact_lines(const HullContact& c, const ExeImage& exe);

// Returns the lines of a bomber group's report (`chart_aircraft_sighted_report`
// 0x26619), zoomed out and spotted only.
std::vector<std::string> air_group_report_lines(const AirGroup& g, const ExeImage& exe, const World& world);

// Returns the lines of an air strike or photo-recon report
// (`chart_air_strike_recon_report` 0x2603d); `base_name` is its base's.
std::vector<std::string> intel_report_lines(const IntelReport& r, const std::string& base_name, const ExeImage& exe,
                                            const World& world);

// Returns the lines of a downed pilot's report when zoomed out
// (`chart_downed_pilot_report` 0x25b88): sighted or radioed, when the
// position went out and where.
std::vector<std::string> pilot_report_lines(const DownedPilot& pilot, const ExeImage& exe, const World& world);

// Returns the lines of a downed pilot's info when zoomed in
// (`chart_downed_pilot_report` 0x25b88): range, bearing and solution of
// `estimate`.
std::vector<std::string> pilot_contact_lines(const SolutionEstimate& estimate, const ExeImage& exe);

}  // namespace sh

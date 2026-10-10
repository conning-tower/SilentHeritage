// Edit / Patrol Area and the ORDERS popup (module guiuser): the PATROL AREA
// dialog over the dated Pacific map with a frame and a hotspot per zone,
// and the button grid the chart panels' orders buttons open.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/patrol_zones.h"
#include "core/install.h"
#include "editor/editor_ui.h"
#include "editor/options.h"
#include "editor/scenario.h"

namespace sh::editor {

namespace se_addr {
// The dated Pacific maps: 0x11-byte entries {char name[13], i32 first
// campaign month}, ended by a month of 10000.
inline constexpr std::uint32_t kPacificMaps = 0x9f194;
}  // namespace se_addr

// Returns the months since December 1941 of (`month` 0-based, `year` since
// 1930) (`campaign_month_index`, SE 0x2b6ca); the date must not be before
// December 1941 (the original loops forever).
std::int32_t campaign_month_index(std::int32_t month, std::int32_t year);

// Returns the SPAC_x.SHP of a scenario date (`load_dated_pacific_map`, SE
// 0x25883): the last map of the table whose first month is not after it.
std::string dated_pacific_map(const SeExe& se, std::int32_t month, std::int32_t year);

// The zones `load_pz_dat` (SE 0x45ff6) enables (0xa50d4): those whose
// DATA\<world> exists and starts with "AWLD", a NUL and version 7.
std::vector<bool> patrol_zone_flags(const Install& install, const SeExe& se, const std::vector<PatrolZone>& zones);

// The zones PATROL AREA lists, in PZ.DAT order, and the list row of the
// scenario's zone.
struct PatrolAreaList {
  std::vector<std::size_t> zones;  // PZ.DAT indexes (0xa6d44)
  std::size_t current = 0;         // 0xa6d3c: 0 when no listed zone has the world
};

// Builds the list of `cmd_patrol_area`: the enabled zones, with `limit`
// ("Limit by date: Patrol Areas") only those whose first..last month and
// year contain (`month` 0-based, `year` since 1930); the current zone is
// the last listed one whose world equals `world` (case-sensitive: the
// caller upper-cases the scenario's world, as the original does).
PatrolAreaList patrol_area_list(const std::vector<PatrolZone>& zones, const std::vector<bool>& enabled,
                                std::string_view world, std::int32_t month, std::int32_t year, bool limit);

// Edit / Patrol Area (`cmd_patrol_area`, SE 0x24e00): first "Save changes
// to scenario?" (`ask_save_changes` with `save`, File / Save); then the map
// and the zone list (the scenario's world upper-cased in place, as the
// original's `strupr`). Hovering a zone selects it; OK with another zone
// changes the world (`Scenario::change_world`; centring the chart is the
// caller's). Returns true when the world changed. Throws `DataError` when
// a map or the new world cannot be read.
bool cmd_patrol_area(EditorUi& ui, Scenario& scenario, const EditorOptions& options,
                     const std::function<int()>& save);

// `orders_popup_run` (SE 0x2a03e): the ORDERS button grid, one cell per
// label (a cell of `disabled` greyed), `initial` selected, over the greyed
// palette. Returns the chosen index, or -3 when cancelled.
int orders_popup_run(EditorUi& ui, const std::vector<std::string>& labels, const std::vector<bool>& disabled,
                     int initial);

}  // namespace sh::editor

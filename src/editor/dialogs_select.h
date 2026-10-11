// The picture dialogs of the toolbox (module guiuser): SELECT SUB TYPE
// (chaining to SELECT SUB NAME), SELECT SUB NAME, SELECT SHIP TYPE and
// SELECT AIRCRAFT TYPE. Each greys the palette up to colour 225 itself,
// puts its SBCHART frame's colours above it, shows six list rows and a
// class sheet of the selected entry, and puts the palette back.
#pragma once

#include <cstdint>
#include <optional>

#include "assets/scn_file.h"
#include "editor/editor_ui.h"
#include "editor/options.h"
#include "editor/scenario.h"

namespace sh::editor {

// `select_sub_type` (SE 0x258ba) for the player's class `sub_class` (a
// SUBS.DAT index) and boat name `name`, with the classes of the scenario
// date when "Limit by date: Subs" is on. SELECT sets `sub_class` to the
// SUBS.DAT class of the selected row (SE stored the row, an original bug
// fixed in the source) and, when the class changed, chains to SELECT SUB
// NAME for it; CANCEL keeps `sub_class`. Returns the dialog result (SELECT
// SUB NAME's when chained). `commissioned_by` filters SELECT SUB NAME as
// `select_sub_name`'s.
int select_sub_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options, std::int32_t& sub_class,
                    ScnChars<31>& name, const std::optional<ScenarioDate>& commissioned_by = std::nullopt);

// `select_sub_name` (SE 0x26526), the player panel's name button: the
// CARSUB.DAT boats of SUBS.DAT class `sub_class` (no date filter; with
// `commissioned_by`, the boats `sub_name_list` keeps, not in the
// original); SELECT copies the chosen "(SS-<hull>) <name>" into `name`.
// Returns the dialog result.
int select_sub_name(EditorUi& ui, const EditorData& data, std::int32_t sub_class, ScnChars<31>& name,
                    const std::optional<ScenarioDate>& commissioned_by = std::nullopt);

// `select_ship_type` (SE 0x26747) for the SHIPS.DAT class `ship_class`
// (-1 none), with the classes of the scenario date when "Limit by date:
// Ships" is on. Sets `ship_class` to the chosen SHIPS.DAT index, -1 on
// CANCEL.
void select_ship_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options, std::int32_t& ship_class);

// `select_aircraft_type` (SE 0x27350) for the AIRCRAFT.DAT type
// `aircraft` (-1 none), with "Limit by date: Aircraft". Sets `aircraft` to
// the AIRCRAFT.DAT index of the selected row even on CANCEL (the caller
// checks the result). Returns the dialog result.
int select_aircraft_type(EditorUi& ui, const Scenario& scenario, const EditorOptions& options,
                         std::int32_t& aircraft);

}  // namespace sh::editor

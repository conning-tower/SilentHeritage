// The Edit menu's settings dialogs (module guiuser, dialogs of
// `guiuser_init_dialogs`): ENVIRONMENT, EQUIPMENT, TORPEDOES, BRIEFING TEXT,
// OPTIONS and PASSWORD, each run on a working copy of the scenario and
// committed only by OK (which also sets the modified flag, except OPTIONS).
// The menu dispatcher greys the palette around them.
#pragma once

#include "editor/editor_ui.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "game/saves_options.h"

namespace sh::editor {

// Edit / Environment (`cmd_environment`, SE 0x27f25): date, time, weather,
// sea, fog, gradient, with the weather picture, the sky box and the sun and
// moon times at the player's position. OK writes the header, the sighting
// fog and light, the base owners of the new date, the visibility and the
// equipment re-validated by date. Returns true on OK.
bool cmd_environment(EditorUi& ui, Scenario& scenario, const EditorOptions& options);

// Edit / Equipment (`cmd_equipment`, SE 0x2904f): surface radar, scope, air
// radar, bathythermograph; OK repacks the equipment word and updates the
// visibility (radar range). Returns true on OK.
bool cmd_equipment(EditorUi& ui, Scenario& scenario, const EditorOptions& options);

// Edit / Torpedoes (`cmd_torpedoes`, SE 0x29677): a cycle per tube of the
// player's class and a reload row per torpedo it may carry; OK writes the
// tube torpedoes and the reload counts. Returns true on OK.
bool cmd_torpedoes(EditorUi& ui, Scenario& scenario, const EditorOptions& options);

// Edit / Briefing Text (`cmd_briefing_text`, SE 0x28fd0): the 1000-byte
// briefing in an editable text box with scroll arrows. Returns true on OK.
bool cmd_briefing_text(EditorUi& ui, Scenario& scenario);

// Edit / Options (`cmd_options`, SE 0x294c7): the "Limit by date", display
// and scenario check boxes. OK stores them (the scenario's end on lost
// contact and randomize also as the new-scenario defaults), saves
// SBOPTION.SAV into `saves`, marks the scenario modified when its two
// flags changed and re-validates the equipment when its limit was switched
// on. Returns true on OK.
bool cmd_options(EditorUi& ui, Scenario& scenario, EditorOptions& options, const SaveDir& saves);

// Edit / Password (`cmd_set_password`, SE 0x2b327): SET PASSWORD on the
// decoded password; a new non-empty one is confirmed ("SETTING PASSWORD TO
// <X>") and stored encoded. Returns true when the password was stored.
bool cmd_set_password(EditorUi& ui, Scenario& scenario);

// `ask_password` (SE 0x2b47b), on loading `header`: when it has a password,
// ENTER PASSWORD up to three times (case-insensitive, "ACCESS DENIED!"
// after a wrong one). Returns true when access is granted.
bool ask_password(EditorUi& ui, const ScnHeader& header);

}  // namespace sh::editor

// The live state of the touch command panel (extended `panel_status`, not
// in the original), read from the player's boat: the depth order in effect
// and the state of each torpedo tube. Read only: nothing of the game is
// changed.
#pragma once

#include <optional>

#include "platform/command_panel.h"

namespace sh {

class PlayerSub;
class TorpedoTubes;

// Returns the depth command whose order is in effect on `sub`: blowing
// ballast, a crash dive (the planes at -80), periscope or radar depth
// ordered, or surfacing (no depth ordered, the planes on rise or the boat
// on the surface); none for any other order (a depth set on the dial,
// planes by hand).
std::optional<PanelCommand> depth_order_in_effect(const PlayerSub& sub);

// Returns the state of `tubes` for the panel: past the boat's tubes
// absent, damaged with any damage flag, then loaded, reloading (with the
// whole seconds left, rounded up) or empty. The labels are left empty.
PanelStatus command_panel_status(const PlayerSub& sub, const TorpedoTubes& tubes);

}  // namespace sh

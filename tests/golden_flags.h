// The improvements of the extended variant (core/features.h) and what each
// must keep of the golden traces (golden_trace.h): an interface
// improvement keeps the simulation's lines, one declared to change the
// simulation keeps nothing. Each agent adding a flag lists it here.
#pragma once

#include <iterator>
#include <string_view>

#include "core/features.h"

namespace sh::test {

// `keeps_simulation`: the improvement alone keeps the lines of `kSimKinds`
// (and `kProgramSimKinds`).
struct FlagExpectation {
  std::string_view name;
  bool Features::* flag;
  bool keeps_simulation;
};
inline constexpr FlagExpectation kFlagExpectations[] = {
    {"wide_screen", &Features::wide_screen, true},
    // The views draw from a generator of their own, so the game's generator
    // no longer sees the views' draws.
    {"draw_random", &Features::draw_random, false},
    {"crew_log", &Features::crew_log, true},
    {"chart_tools", &Features::chart_tools, true},
    // The front end's SUB CLASS screen only (golden_game_test.cpp).
    {"last_boat", &Features::last_boat, true},
    // Pointer gestures only (drags, the wheel): the scripts' keys play the
    // game as before.
    {"drag_rotate", &Features::drag_rotate, true},
    {"precise_dials", &Features::precise_dials, true},
    // Reads the boat for the touch panel only.
    {"panel_status", &Features::panel_status, true},
    // The menus' outside clicks: no script uses them.
    {"outside_click_closes", &Features::outside_click_closes, true},
    // The scenario editor only.
    {"editor_no_size_limit", &Features::editor_no_size_limit, true},
    {"editor_fixes", &Features::editor_fixes, true},
    // The torpedo types' pictures, cards, tube marks and knob labels.
    {"torpedo_types", &Features::torpedo_types, true},
};
// Every improvement must be listed above (`Features` holds flags and the
// one-byte `command_bar`, covered by its own golden case).
static_assert(sizeof(Features) == std::size(kFlagExpectations) * sizeof(bool) + sizeof(CommandBarStyle));

}  // namespace sh::test

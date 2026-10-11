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
    // Its views draw from a render generator of their own, so the game's
    // generator no longer sees the views' draws.
    {"wide_screen", &Features::wide_screen, false},
    {"crew_log", &Features::crew_log, true},
    {"chart_tools", &Features::chart_tools, true},
    // The front end's SUB CLASS screen only (golden_game_test.cpp).
    {"last_boat", &Features::last_boat, true},
};
// Every improvement must be listed above (`Features` holds only flags).
static_assert(sizeof(Features) == std::size(kFlagExpectations) * sizeof(bool));

}  // namespace sh::test

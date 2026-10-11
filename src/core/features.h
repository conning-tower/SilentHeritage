// The improvements beyond the original game (not in the original), each
// switched on its own. The faithful variant runs with none of them, the
// extended variant with all (CMake option SH_VARIANT, core/edition.h); the
// extended build's `--faithful` switches them all off.
//
// An improvement to the interface (screens, controls, logs) must leave the
// simulation exactly as the original runs it: the same state, the same draws
// from the game's random generator. One that changes the simulation says so
// in its comment.
#pragma once

#include "core/edition.h"

namespace sh {

struct Features {
  // Screens wider than 4:3: the chart and the 3D views fill the window's
  // width.
  bool wide_screen = false;
  // The crew's message log: clicking the message strip of the command bar
  // lists the crew's messages with their game times.
  bool crew_log = false;
  // The chart's annotation tools: marks, lines and notes drawn on the map.
  bool chart_tools = false;

  // Returns the original game's set: no improvement.
  static constexpr Features faithful() { return {}; }
  // Returns every improvement.
  static constexpr Features extended() { return {.wide_screen = true, .crew_log = true, .chart_tools = true}; }
  // Returns the set of this build's variant.
  static constexpr Features for_build() { return edition::kExtended ? extended() : faithful(); }

  friend constexpr bool operator==(const Features&, const Features&) = default;
};

}  // namespace sh

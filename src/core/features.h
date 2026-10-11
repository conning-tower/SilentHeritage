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
  // The SUB CLASS screen of the encounters selects the class last chosen
  // when it is in service at the date, instead of the first.
  bool last_boat = false;
  // The bridge, periscope, TBT and deck gun views turn by dragging them
  // (the sea follows the pointer), with inertia after a fast release.
  bool drag_rotate = false;
  // The settable dials (ordered course, helm and depth; the TDC's knobs,
  // counters and torpedo depth) take a pending value by a horizontal drag
  // or the wheel, shown as an amber ghost and given on release.
  bool precise_dials = false;
  // The touch command panel shows the depth order in effect and each
  // tube's state.
  bool panel_status = false;
  // The scenario editor without the memory budget of SE's 16 MB DOS
  // machine: no gauge, no SCENARIO IS TOO LARGE (the files keep their
  // estimate).
  bool editor_no_size_limit = false;
  // The scenario editor's corrections of SE's plain errors and limits:
  // cancelling SELECT SHIP TYPE leaves the scenario unmodified, cancelling
  // SELECT A CITY keeps an existing mission, a ship <-> submarine class
  // change keeps the course and the orders still valid, enemy subs get
  // Intercept and ships Follow Path, SELECT SUB NAME follows "Limit by
  // date: Subs", EQUIPMENT sets the radar detector, the airstrike's hold
  // time is named so, the palette is complete without SH, a failed
  // SBOPTION.SAV write is reported, the reloads load by torpedo type and
  // the torpedoes' service months are read right.
  // The .SCN files keep the records the original game reads.
  bool editor_fixes = false;

  // Returns the original game's set: no improvement.
  static constexpr Features faithful() { return {}; }
  // Returns every improvement.
  static constexpr Features extended() {
    return {.wide_screen = true,
            .crew_log = true,
            .chart_tools = true,
            .last_boat = true,
            .drag_rotate = true,
            .precise_dials = true,
            .panel_status = true,
            .editor_no_size_limit = true,
            .editor_fixes = true};
  }
  // Returns the set of this build's variant.
  static constexpr Features for_build() { return edition::kExtended ? extended() : faithful(); }

  friend constexpr bool operator==(const Features&, const Features&) = default;
};

}  // namespace sh

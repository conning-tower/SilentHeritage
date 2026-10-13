// The improvements beyond the original game (not in the original), each
// switched on its own. The classic variant runs with none of them, the
// extended variant with all (CMake option SH_VARIANT, core/edition.h); the
// extended build's `--classic` switches them all off.
//
// An improvement to the interface (screens, controls, logs) must leave the
// simulation exactly as the original runs it: the same state, the same draws
// from the game's random generator. One that changes the simulation says so
// in its comment.
#pragma once

#include <cstdint>
#include <string_view>

#include "core/edition.h"

namespace sh {

// The command bar's layouts (not in the original), one choice of the
// options page.
enum class CommandBarStyle : std::uint8_t {
  kOriginal,       // the original bar alone ("ORIGINAL BAR")
  // The original bar and the floating ORDERS button of touch screens, as in
  // the classic game ("FLOATING ORDERS").
  kOrdersButton,
  kBarOrders,      // the ORDERS button in the bar ("BAR WITH ORDERS")
  // The ORDERS button in the bar, - and + around its time compression.
  kBarOrdersTime,
};

// Returns the options page's label of `style`, in the menu fonts (at most
// 178 pixels wide in the in-mission page's GUI_S.FNT).
constexpr std::string_view command_bar_label(CommandBarStyle style) {
  switch (style) {
    case CommandBarStyle::kOriginal:
      return "ORIGINAL BAR";
    case CommandBarStyle::kOrdersButton:
      return "FLOATING ORDERS";
    case CommandBarStyle::kBarOrders:
      return "BAR WITH ORDERS";
    case CommandBarStyle::kBarOrdersTime:
      break;
  }
  return "ALL COMMANDS";
}

struct Features {
  // Screens wider than 4:3: the chart and the bridge fill the window's
  // width; the other screens are shown centred between side fills.
  bool wide_screen = false;
  // The views draw their random details (shell tracers, the bomber
  // formations' spread, the radar's A-scope noise) from a generator of
  // their own. This changes the simulation: in the original they draw from
  // the game's generator, so what is displayed changes the game's random
  // sequence; with this the simulation is the same whatever station shows.
  bool draw_random = false;
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
  // The command bar's layout. In the bar layouts the ORDERS panel opens
  // above the bar's ORDERS button and the bar's buttons take touches a
  // little above it.
  CommandBarStyle command_bar = CommandBarStyle::kOrdersButton;
  // A click outside the in-mission menus with a Cancel (1MC, crew log, time
  // compression, game options, detail level, end mission) closes them as
  // their Cancel or Esc does.
  bool outside_click_closes = false;
  // The Esc box of a mission is the MISSION menu: END MISSION?'s (END
  // PATROL?'s) Cancel, Abort and Return To Base with GAME OPTIONS (Alt-O's)
  // and SAVE MISSION (SAVE CAREER) between them.
  bool mission_menu = false;
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
  // The torpedo types told apart: each Mark 18 its own pictures, the status
  // page's boxes for the types carried, the tube buttons marked by type.
  // (Mixed loads and the Mark 27 are rules: MissionRules.)
  bool torpedo_types = false;

  // Returns the original game's set: no improvement.
  static constexpr Features classic() { return {}; }
  // Returns every improvement.
  static constexpr Features extended() {
    return {.wide_screen = true,
            .draw_random = true,
            .crew_log = true,
            .chart_tools = true,
            .last_boat = true,
            .drag_rotate = true,
            .precise_dials = true,
            .panel_status = true,
            .command_bar = CommandBarStyle::kBarOrdersTime,
            .outside_click_closes = true,
            .mission_menu = true,
            .editor_no_size_limit = true,
            .editor_fixes = true,
            .torpedo_types = true};
  }
  // Returns the set of this build's variant.
  static constexpr Features for_build() { return edition::kExtended ? extended() : classic(); }

  // Whether the ORDERS button is in the bar, and - and + around its time
  // compression.
  constexpr bool orders_in_bar() const {
    return command_bar == CommandBarStyle::kBarOrders || command_bar == CommandBarStyle::kBarOrdersTime;
  }
  constexpr bool bar_time_buttons() const { return command_bar == CommandBarStyle::kBarOrdersTime; }
  // Whether touch screens show the floating ORDERS button.
  constexpr bool floating_orders() const { return command_bar == CommandBarStyle::kOrdersButton; }

  friend constexpr bool operator==(const Features&, const Features&) = default;
};

}  // namespace sh

// The TOOLBOX's object panels and add tools (module sbchart, SE
// docs/editor-design.md "Modules"): the sub-dialog `sbchart_toolbox_open`
// links under the TOOLBOX for the selected object (player sub 0xab4d8, ship
// 0xaba54, enemy sub 0xac0dc, aircraft 0xac7c4, airstrike 0xacba8, photo
// recon 0xad090) with every field's callback (`toolbox_*`), the ORDERS and
// mission menus, the convoy refresh of `sbchart_toolbox_update`, and the
// icon buttons that add objects (`sbchart_tool_add_*`).
//
// The chart screen owns one and calls it from its TOOLBOX panel; the
// toolbox starts the chart's click modes and runs the selection dialogs
// through `ChartHooks`.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editor/chart_screen.h"
#include "ui/controls.h"
#include "ui/dial_control.h"

namespace sh::editor {

// Returns `value` with its decimal digit at `place` (1, 10, 100) advanced
// (9 wraps to 0) until `accept` takes it, as the toolbox's digit cycles do.
// Original bug (digit cycles hang): the original loops forever when no
// digit fits; here the value stays after ten tries.
std::int32_t cycle_digit(std::int32_t value, std::int32_t place, const std::function<bool(std::int32_t)>& accept);

// Returns a duration in minutes with its digit `digit` (0 hour tens, 1
// hour units, 2 minute tens, 3 minute units) advanced, as the airstrike and
// photo-recon time cycles (`toolbox_strike_delay_digit_*`,
// `toolbox_strike_hold_digit_*`): hours 0..99, minute tens 0..5.
std::int32_t cycle_time_digit(std::int32_t minutes, int digit);

// The digit `digit` of a duration (see `cycle_time_digit`).
std::int32_t time_digit(std::int32_t minutes, int digit);

// The orders menu of a ship (`toolbox_ship_orders_button`) or enemy sub
// (`toolbox_esub_orders_button`), or the mission menu of an aircraft
// (`sbchart_pick_aircraft_mission`): the labels, the values they set, which
// are greyed, and the entry of the current value (0 when absent).
struct OrdersMenu {
  std::vector<std::string> labels;
  std::vector<std::int32_t> values;
  std::vector<bool> disabled;
  int initial = 0;
};

// The menu for `vessel` (by its SHIPS.DAT role for a ship: None / Convoy
// Leader / Convoy Member, None / Escort / Patrol, None / Fishing; None /
// Patrol for an enemy sub) in `scenario`. A ship of an unknown class or of
// the submarine role gets an empty menu.
OrdersMenu vessel_orders_menu(const Scenario& scenario, const Vessel& vessel);
// The aircraft mission menu (Patrol / Escort / Follow Path).
OrdersMenu aircraft_mission_menu(const Scenario& scenario, AircraftMission current);

class Toolbox {
 public:
  // The heading entry of the dial panels (0xab5ac / 0xabb28 / 0xac1b0).
  static constexpr int kHeadingEntryX = 28;
  static constexpr int kHeadingEntryY = 244;

  // `chart`, `env`, `scenario` and `hooks` must outlive the toolbox.
  Toolbox(ChartScreen& chart, const ChartScreenEnv& env, Scenario& scenario, const ChartHooks& hooks);

  // `sbchart_toolbox_open`'s type dialog: appends the panel of `selected`
  // to the TOOLBOX dialog `toolbox`.
  void add_controls(ui::Dialog& toolbox, EditorObject& selected);
  // The TOOLBOX dialog was closed.
  void closed();
  // `sbchart_toolbox_update` past the gauge: a Convoy Leader's changed
  // heading or speed goes to its members, a changed speed to its escorts.
  void update();
  // An icon button: SELECT SHIP TYPE then ADD SHIP / ADD SUB, SELECT
  // AIRCRAFT TYPE and its mission, or a new airstrike or photo recon and
  // SELECT A CITY.
  void tool(ToolboxTool tool);

  // The heading text the dial and its digits show and edit (0xaad34).
  const std::string& heading_text() const { return heading_entry_.text; }

 private:
  using IndexFn = std::function<int(bool advance)>;

  void add_player(ui::Dialog& d, PlayerSub& player);
  void add_vessel(ui::Dialog& d, Vessel& vessel);
  void add_aircraft(ui::Dialog& d, Aircraft& aircraft);
  void add_mission(ui::Dialog& d, SpecialMission& mission);
  // The heading group shared by the player, ship and enemy-sub panels.
  void add_heading(ui::Dialog& d, std::function<void(std::int32_t)> set, bool disabled);
  void add_indicators(ui::Dialog& d);

  // Controls with the SE font and string conventions.
  ui::Label& label(ui::Dialog& d, int x, int y, std::uint32_t text, bool small = false);
  ui::TextField& field(ui::Dialog& d, int x0, int y0, int x1, int y1, std::function<std::string()> text,
                       TextAlign align = TextAlign::kCentre);
  ui::CycleButton& cycle(ui::Dialog& d, int x, int y, int w, std::vector<std::string> strings, IndexFn index);
  ui::CycleButton& digit(ui::Dialog& d, int x, int y, IndexFn index);
  std::vector<std::string> table(std::uint32_t address, std::size_t count) const;
  std::string location_text(WorldPoint p, std::uint32_t separator) const;

  // Runs a selection dialog hook with the chart's panel closed around it.
  int run_dialog(const std::function<int()>& dialog);
  int run_orders_menu(const OrdersMenu& menu);

  // The field callbacks with side effects beyond their record.
  void ship_orders(Vessel& vessel);
  void sub_orders(Vessel& vessel);
  void vessel_class(Vessel& vessel);
  void player_class(PlayerSub& player);
  void player_name(PlayerSub& player);
  void player_depth_preset(PlayerSub& player, std::uint32_t SubClass::* depth);
  void aircraft_type(Aircraft& aircraft);
  void aircraft_mission(Aircraft& aircraft);
  std::int32_t convoy_cycle(std::int32_t current) const;
  void edited();  // the modified flag and the overlay redraw

  // The player's sub class limits (`sb_sub_class_draft`,
  // `sb_sub_class_test_depth`) and the water depth under a point.
  std::int32_t water_depth(WorldPoint p) const;
  const SubClass* player_class_record(const PlayerSub& player) const;
  std::int32_t enemy_sub_max_speed(const Vessel& vessel) const;  // sb_esub_max_speed

  ChartScreen& chart_;
  const ChartScreenEnv& env_;
  Scenario& scenario_;
  const ChartHooks& hooks_;
  // The heading entry (not a control of the dialog: the dial and its
  // digits read and write its text) and the player's name buffer (0xaad38).
  ui::SpinEntry heading_entry_;
  ScnChars<31> player_name_{};
  std::vector<ui::Control*> heading_controls_;  // the dial and the digits
  ui::CycleButton* guns_ = nullptr;
  ui::CycleButton* name_button_ = nullptr;
  // The convoy refresh (0xaad08 speed, 0xaad0c heading, 0xaad10 escorts).
  std::int32_t leader_speed_ = -1;
  std::int32_t leader_heading_ = -1;
  bool escort_refresh_ = false;
};

}  // namespace sh::editor

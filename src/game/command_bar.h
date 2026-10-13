// The in-game command bar (guiuser, dialog 0x11e540): the strip at the
// bottom of every station with the station's sprite buttons, the time
// compression button, the clock and the crew message line, and the 67
// global hotkeys (stations, telegraph, helm, planes, time compression,
// weapons, menus) that live in it as key-only controls. Also the crew
// message log (`crew_message`) and the in-game menus the hotkeys open
// (1MC, game options, detail level, time compression, quit, end mission).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/crew_messages.h"
#include "assets/exe_image.h"
#include "core/display_config.h"
#include "game/crew_log.h"
#include "sim/context.h"
#include "game/mission_loop.h"
#include "game/station.h"
#include "game/station_env.h"
#include "gfx/text.h"
#include "gfx/viewport.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {

// The bar's top row on the screen (dialog 0x11e540 at y 459).
inline constexpr int kCommandBarTop = 0x1cb;

// A station button of the bar (12 bytes: normal and pressed GLOBAL.SHP
// frames, callback).
struct BarButton {
  std::uint8_t frame = 0;
  std::uint8_t pressed_frame = 0;
  std::function<void()> action;
};

// A station's bar configuration (the argument of `control_bar_open`).
struct BarConfig {
  // Placed from the right edge leftwards, 0x49 pixels apart.
  std::vector<BarButton> buttons;
  // The ESC (and right click) action; unset: the END MISSION menu
  // (`cmd_abort_mission`).
  std::function<void()> on_escape;
  bool time_compression = true;
  bool clock = true;
  bool hotkeys = true;
};

// The actions of the 67 global hotkeys, in the order of their records
// (0x11e600 + n * 0x38, handles 0x108ec8[67]).
enum class HotkeyAction : std::uint8_t {
  kStationConn, kStationTbt, kStationPeriscope, kStationChart, kStationGauges, kStationBridge,
  kStationDeckGun, kStationTdc, kStationStatus, kStationRadar, kStationLogbook,
  kCompressionUp, kCompressionDown, kTogglePeriscope,
  kAllStop, kAheadOneThird, kAheadTwoThirds, kAheadStandard, kAheadFull, kAheadFlank,
  kBackOneThird, kBackTwoThirds, kBackFull, kBackEmergency,
  kRudderRight, kRudderLeft, kPlanesRise, kPlanesDive, kStraightAndLevel,
  kBlowBallast, kSurface, kPeriscopeDepth, kCrashDive, kSteerViewBearing, kRepeatMessage,
  kFireTube1, kFireTube2, kFireTube3, kFireTube4, kFireTube5, kFireTube6, kFireTube7, kFireTube8,
  kFireTube9, kFireTube10,
  kDebugBboxes, kDetailLevel, kPadlockTarget, kDebugDamage, kRadarDepth, kToggleSjRadar,
  kToggleSdRadar, kViewAhead, kCompressionReset, kQuitToDos, kManDeckGun, kGameOptions,
  kDebugKillTarget, kDebugMemstat, kDebugRoutes, kDebugGroupLabels, kStationCalendar,
  kStationRadioLog, kDebugBases, kMenu1mc, kRescuePilot, kReconPhoto,
};

// One hotkey record: its key code (`scan << 8 | ascii`; 0xffXX any scan
// code for character XX, 0xfeXX any scan code) and action.
struct Hotkey {
  std::uint16_t key = 0;
  HotkeyAction action = HotkeyAction::kStationConn;
};

// Returns the 67 hotkeys in record order.
const std::array<Hotkey, 67>& hotkey_table();

// The telegraph step each digit key orders ('0' stop, '1'..'5' ahead,
// '6'..'9' back one third .. emergency stored as 9..6).
std::int32_t telegraph_for_digit(char digit);

// The crew message log (0x11e024..0x11e040): a 20-slot ring of the last
// messages, the slot shown on the bar and the display state.
class CrewMessageLog {
 public:
  static constexpr std::size_t kSlots = 20;
  // The text a slot keeps (0x32 bytes with the NUL).
  static constexpr std::size_t kTextMax = 0x31;

  struct Entry {
    std::int32_t id = 0;
    std::int32_t arg1 = -1;
    std::int32_t arg2 = -1;
    std::string text;
  };

  // `crew_message_log_reset` (0x4c76e): empties the ring and pushes
  // message 0.
  void reset();
  // `crew_message_queue_push` (0x4c91c): stores a message in the next slot
  // (dropping the oldest when full) and shows it.
  void push(std::int32_t id, std::int32_t arg1, std::int32_t arg2, std::string_view text);
  // `key_repeat_message` (0x4c7c1): shows the previous message again, back
  // through the ring. Returns false when the ring holds one message only.
  bool repeat();

  // The slot shown (0x11e030), the newest (0x11e03c) and oldest (0x11e038).
  std::size_t shown() const { return shown_; }
  std::size_t head() const { return head_; }
  std::size_t tail() const { return tail_; }
  const Entry& entry(std::size_t slot) const { return ring_.at(slot); }

 private:
  std::array<Entry, kSlots> ring_{};
  std::size_t head_ = 0;    // 0x11e03c
  std::size_t tail_ = 0;    // 0x11e038
  std::size_t shown_ = 0;   // 0x11e030
  std::size_t cursor_ = 0;  // 0x11e034
};

// Returns the bar text of `entry` (`crew_message_format_current`,
// 0x4ca19): "<speaker>: " then the message text with its placeholders
// (0x01/0x02 the arguments, 0x03 the text, 0x09/0x0a the arguments as 3
// digits, 0x11/0x12 an "s" when the argument is 2 or more) filled in; a
// text the message has no 0x03 for is appended (not in the original: the
// extended reports' suffixes).
std::string format_crew_message(const CrewMessages& messages, const CrewMessageLog::Entry& entry);

class ControlBar : public CommandBar {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // The bar of the stations of `env`, which must outlive it; reads the
  // crew message table and the menu data from `env.exe`. Only `env.gui`
  // and `env.exe` are used before the first `open` or message.
  explicit ControlBar(StationEnv& env);
  ControlBar(const ControlBar&) = delete;
  ControlBar& operator=(const ControlBar&) = delete;
  ~ControlBar() override;

  // `control_bar_open` (0x48d02): builds the bar for `config` and draws it.
  void open(const BarConfig& config);
  // CommandBar: the main loop's calls.
  void event(const ui::Event& event) override;  // control_bar_handle_event
  void process() override;                      // control_bar_process
  void update() override;                       // control_bar_update
  void close() override;                        // control_bar_close
  bool is_open() const { return open_; }
  // Lets ESC and right clicks through to the station instead of the bar
  // (0x11dfcc, set while the chart plots waypoints).
  void set_escape_passthrough(bool on) { escape_passthrough_ = on; }

  // `crew_message(id, arg1, arg2, text)` (0x4c843): plays the message's
  // speech when the speech option is on, logs it and shows it on the bar.
  // Unknown ids are ignored.
  void crew_message(std::int32_t id, std::int32_t arg1 = -1, std::int32_t arg2 = -1, std::string_view text = {});
  // Posts the reports of a simulated frame.
  void post(const std::vector<CrewReport>& reports);
  // `crew_message_log_reset`.
  void reset_log();
  const CrewMessageLog& log() const { return log_; }
  // The mission's crew messages with their times (`Features::crew_log`;
  // empty without it).
  const CrewLog& history() const { return history_; }
  // Opens the crew log (`Features::crew_log`: the message strip's click,
  // `L`) unless a menu is open.
  void open_crew_log();
  // The bar text of the message shown.
  std::string current_message() const;
  // The display state 0x11e024 (0 none, 1 shown, 2 ending).
  std::int32_t message_state() const { return message_state_; }
  const CrewMessages& messages() const { return messages_; }
  // 0x124e94: the AA guns manned (1MC menu; `PlayerCombat::aa_guns`).
  bool aa_manned() const;

  // Runs the action of a hotkey (the callback of its record).
  void run(HotkeyAction action);

  // The commands shared with the stations.
  // `player_can_raise_periscope` (0x60784) / `player_is_surfaced` (0x607a8).
  bool can_raise_periscope() const;
  bool surfaced() const;
  // `player_toggle_periscope` (0x6073e): raises or lowers the periscope
  // with crew messages #3 / #4.
  void toggle_periscope();
  // Selects station `id` if it is installed.
  void select_station(StationId id);
  // `cmd_toggle_rig_for_red(auto)` (0x60f99): with the Auto Red Lights
  // option, rigs for red below 30 % light and secures above (crew
  // messages #74 / #75 unless `initial`).
  void auto_rig_for_red(bool initial);
  // Toggles rig for red (the 1MC menu): reopens the station if it uses
  // red-light colours, crew message #74 / #75.
  void toggle_rig_for_red();

  // The in-game menus.
  void menu_1mc();               // 0x4c1a6
  void menu_time_compression();  // cmd_time_compression_box 0x49094
  void menu_quit();              // cmd_quit_to_dos 0x4be84
  // cmd_abort_mission 0x48fa1; with `Features::mission_menu`, the MISSION
  // menu, whose Game Options and Save run `menu_game_options` and `on_save`
  // and return to the game.
  void menu_end_mission();
  void menu_game_options();      // cmd_game_options 0x4bba0
  void menu_detail_level();      // cmd_detail_level 0x4bb4b
  // The EXTENDED OPTIONS page of the mission (not in the original): the
  // interface improvements of `kFeatureOptions`, those that cannot change
  // during a mission greyed, then the window's settings of `display_options`
  // when set; changes apply at once.
  void menu_extended_options();
  // `ask_yes_no` (0x4c670): `done(true)` on YES.
  void ask_yes_no(std::string title, std::string line1, std::string line2, std::function<void(bool)> done);
  // `show_message_box` (0x4c38d): an OK box.
  void message_box(std::string title, std::string line1, std::string line2, std::function<void()> done = {});

  // Called when the GAME OPTIONS / DETAIL LEVEL menus changed a setting
  // (`goption_save` / `gdetail_save` follow), and for the Save button
  // (`cmd_save_mission` / `croster_save_career_as`; disabled when unset).
  std::function<void()> on_options_changed;
  std::function<void()> on_detail_changed;
  std::function<void()> on_save;
  // With the extended variant's settings (not in the original): GAME
  // OPTIONS gains an EXTENDED button, and this is called with the
  // improvements its page changed (`MissionUiState::features`).
  std::function<void(const Features&)> on_features_changed;
  // Not in the original: opens the touch command panel above the bar's
  // ORDERS button (`Features::command_bar`), given its screen rectangle;
  // without it the bar has no ORDERS button.
  std::function<void(const ui::Rect&)> on_orders;
  // Not in the original: the window's settings for the EXTENDED page
  // (`FrontendHost::display_options`; unset or nothing: not shown), and
  // called with them at each change there.
  std::function<std::optional<DisplayOptions>()> display_options;
  std::function<void(const DisplaySettings&)> on_display_changed;

 private:
  class SpriteButton;
  // GAME OPTIONS' EXTENDED button was pressed (not in the original).
  bool extended_requested_ = false;
  // The window's settings the EXTENDED page edits.
  DisplaySettings display_edit_;
  void build(const BarConfig& config);
  // Whether the bar shows the ORDERS button (not in the original).
  bool orders_shown() const { return env_.ui.features.orders_in_bar() && on_orders != nullptr; }
  void redraw();                 // control_bar_redraw (0x48f37)
  void draw_message();           // control_bar_draw_message (0x49156)
  void after_menu();
  // Returns the SH.EXE string at `address`.
  std::string str(std::uint32_t address) const;
  void apply_end_reason();
  std::int32_t mission_clock_effective() const;
  PlayerSub& player();
  SimContext sim_context();
  void flush_sim_events();

  // Player commands (module player).
  void rudder(std::int32_t step);
  void planes_rise();
  void planes_dive();
  void straight_and_level();
  void radar_depth();
  void steer_view_bearing();
  void view_ahead();
  void toggle_silent_running();
  void jettison_debris();
  void man_aa_guns();
  void fire_tube(std::size_t tube);
  void station_key(HotkeyAction action);

  StationEnv& env_;
  CrewMessages messages_;
  CrewMessageLog log_;
  CrewLog history_;
  std::unique_ptr<ui::Dialog> dialog_;
  BarConfig config_;
  bool open_ = false;
  bool escape_passthrough_ = false;  // 0x11dfcc
  std::int32_t button_slots_ = 0;    // 0x11dfec: buttons plus the compression button
  std::int32_t shown_compression_ = 1;  // 0x11dfe4
  SpriteButton* compression_button_ = nullptr;
  ui::TextField* clock_field_ = nullptr;
  std::int32_t message_state_ = 0;  // 0x11e024
  bool message_drawn_ = false;      // 0x11e028
  std::int32_t message_skip_ = 0;   // 0x11e02c
  std::int32_t end_reason_ = 0;     // 0x11dff8
};

// Draws `text` like `gui_draw_text` with each glyph a pixel closer to the
// previous one (not in the original: the extended bar's message strip).
void draw_tight_text(const Viewport& vp, int x, int y, const Font& font, std::string_view text,
                     const GlyphColourTable& colours);

}  // namespace sh

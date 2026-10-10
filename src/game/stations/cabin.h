// The captain's quarters (module cquarter, station vtable 0x102a90): the
// cabin picture CAPCAB.PCX with hotspots on the desk's books and the wall
// calendar, the calendar page (date, sun and moon times, sea and
// weather), the patrol log book (the boat's log of sinkings and missions)
// and the radio log (the messages received). The pages are the global
// 0x11bd00 (`MissionUiState::cabin_page`): 0 cabin, 1 ship ID book, 2
// calendar, 3 patrol log, 4 radio log.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/world.h"
#include "game/station.h"
#include "game/station_env.h"
#include "sim/clock.h"
#include "sim/sky.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {

class IdBook;

// A latitude or longitude as the logs store it: whole degrees, minutes and
// the hemisphere letter.
struct LogAngle {
  std::int32_t degrees = 0;
  std::int32_t minutes = 0;
  char hemisphere = 'N';
};

// One entry of the patrol log (0x4b-byte records of the list 0x11b110,
// `player_log_event` 0x5e594).
struct PatrolLogEntry {
  // The description types besides the ship types 0..21 (`ShipType`).
  static constexpr std::int32_t kRescuedPilot = 0x16;
  static constexpr std::int32_t kPhotoRecon = 0x17;

  std::int32_t day = 1;     // +4
  std::int32_t month = 0;   // +8, 0-based
  std::int32_t year = 0;    // +0xc, since 1930
  std::int32_t hour = 0;    // +0x10
  std::int32_t minute = 0;  // +0x14
  LogAngle latitude;        // +0x18
  LogAngle longitude;       // +0x21
  std::int32_t type = 0;    // +0x2a
  std::string name;         // +0x2e, at most 24 characters: the class or place
  std::int32_t tonnage = 0;  // +0x47
};

// One received radio message (the list 0x11b11c).
struct RadioMessage {
  bool unread = true;            // +4
  std::int32_t day = 1;          // +8
  std::int32_t month = 0;        // +0xc
  std::int32_t year = 0;         // +0x10, since 1930
  std::int32_t hour = 0;         // +0x14
  std::int32_t minute = 0;       // +0x18
  std::int32_t template_index = 0;  // +0x1c, into RMSG.DAT
  std::array<std::int32_t, 10> args{};  // +0x20, taken in turn by the placeholders
  std::string to;                // +0x48
  std::string from;              // +0x6b
  LogAngle latitude;             // +0x8e
  LogAngle longitude;            // +0x97
};

// The logs the cabin shows. The simulation fills them (`player_log_event`
// and the radio message code) through `CabinStation::logs`.
struct CabinLogs {
  std::vector<PatrolLogEntry> patrol;
  std::vector<RadioMessage> radio;
  // The career's tonnage before this patrol (0x1168e8), shown in a career.
  std::int32_t career_tonnage = 0;
};

// The names a radio message's placeholders refer to.
struct RadioTextContext {
  std::string boat_name;                 // 0x11ac08
  std::span<const std::string> base_names;  // the patrol map's MAP<n>BASE.DAT
  std::span<const std::string> location_types;  // the .WLD location types (0x10eb54)
};

// Returns the text of `message` from its RMSG.DAT template
// (`cquarter_format_message`, 0x33dc7), upper-cased.
std::string format_radio_message(const RadioMessage& message, std::string_view text, const RadioTextContext& names);

// Returns the patrol log entry of a ship of `ship_class` sunk now, the
// boat at world (`x`, `z`) of `world` (`player_log_event(class, -1)`).
PatrolLogEntry sinking_log_entry(const ShipClass& ship_class, const GameClock& clock, const World& world,
                                 std::int32_t x, std::int32_t z);

// Returns the patrol log entry of a downed pilot taken aboard now, the
// boat at world (`x`, `z`) of `world` (`player_log_event(0, 0)`).
PatrolLogEntry rescue_log_entry(const GameClock& clock, const World& world, std::int32_t x, std::int32_t z);

// Returns the patrol log entry of a photo-recon target photographed now,
// the boat at world (`x`, `z`) of `world`: its place is
// `location_types[location_type]`, or "Harbor" for -1
// (`player_log_event(report, 1)`).
PatrolLogEntry photo_recon_log_entry(const GameClock& clock, const World& world, std::int32_t x, std::int32_t z,
                                     std::int32_t location_type, std::span<const std::string> location_types);

// The calendar page's lines (`cquarter_calendar_page`, 0x32a2c), in
// drawing order: weekday, day, month and year, sunrise, sunset, moonrise,
// moonset, moon, sea, weather. `sky` gives the day's sun and moon times
// and moon phase (the clock's +0x24..+0x44) and the fog density.
std::vector<std::string> calendar_lines(const GameClock& clock, const Sky& sky, std::int32_t sea_state,
                                        std::int32_t weather);

class CabinStation : public Station {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // The station of `env`; the logs start empty and the radio log at its
  // first page (0x11b114 = 1).
  explicit CabinStation(StationEnv& env);
  ~CabinStation() override;

  void open() override;     // cquarter_screen (0x3260b)
  void event(const ui::Event& event) override;
  void process() override;
  void update() override;   // cquarter_station_update (0x3298c)
  void close() override;    // cquarter_station_close (0x327c5)
  // `cquarter_station_uses_red_light` (0x35f34): the cabin and the calendar.
  bool rig_red_redraw() const override { return env_.ui.cabin_page == 0 || env_.ui.cabin_page == 2; }

  CabinLogs& logs() { return logs_; }
  // The .WLD location type names (0x10eb54).
  std::span<const std::string> location_types() const { return location_types_; }
  // The page open, the patrol log page (0x119280) and its count (0x11928c),
  // the radio log page (0x11b114) and its count (0x119294).
  std::int32_t open_page() const { return open_page_; }
  std::int32_t log_page() const { return log_page_; }
  std::int32_t log_pages() const { return log_pages_; }
  std::int32_t radio_page() const { return radio_page_; }
  std::int32_t radio_pages() const { return radio_pages_; }
  // The texts of the radio log's two text boxes (0x1192a0, 0x1193cc).
  const std::string& radio_text(int side) const;

 private:
  class RadioSkin;

  void open_page(std::int32_t page);   // cquarter_open_page (0x32640)
  void close_page();                   // cquarter_close_page (0x327e0)
  void goto_page(std::int32_t page);   // cquarter_goto_page (0x34327)
  void draw_picture(const PcxImage& pcx);
  void draw_calendar();                // cquarter_calendar_page (0x32a2c)
  void draw_patrol_log();              // cquarter_patrol_log (0x330de)
  void draw_radio_log();               // cquarter_radio_messages (0x33928)
  ui::Area& add_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::function<void()> click,
                     std::function<std::size_t()> cursor);
  const PcxImage& picture(const char* name);

  StationEnv& env_;
  CabinLogs logs_;
  std::unique_ptr<ui::Dialog> dialog_;
  std::unique_ptr<IdBook> id_book_;  // the ship ID book page (0x119268)
  std::unique_ptr<RadioSkin> radio_skin_;
  ui::TextBox* radio_boxes_[2] = {nullptr, nullptr};
  std::vector<std::string> location_types_;
  // The fonts of the pages: CALEND1..3 (0x119270..0x119278), SCRIPT or
  // MENU_S (0x11927c).
  Font calendar_fonts_[3];
  Font page_font_;
  std::vector<std::pair<std::string, PcxImage>> pictures_;
  std::int32_t open_page_ = -1;
  std::int32_t calendar_day_ = -1;   // 0x11926c
  std::int32_t log_page_ = 1;        // 0x119280
  std::int32_t log_pages_ = 1;       // 0x11928c
  std::size_t log_drawn_ = 0;        // the entries when the log was drawn (0x119288)
  bool log_dirty_ = false;           // 0x119284
  std::int32_t radio_page_ = 1;      // 0x11b114
  std::int32_t radio_pages_ = 1;     // 0x119294
  std::size_t radio_drawn_ = 0;      // the messages when the log was drawn (0x11929c)
  bool radio_dirty_ = false;         // 0x119298
};

}  // namespace sh

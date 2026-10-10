// The status station (module status, station vtable 0x102afc): the damage
// page (the boat's systems as lights over the profile STATUSBG.PCX, the
// compartments of the class's status board with their alerts, and the
// report of the selected compartment) and the torpedo page (the tubes and
// racks, where torpedoes are dragged by hand between them, and the data of
// the boat's torpedo types). F8 or the bar buttons swap the pages.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/shp.h"
#include "core/image.h"
#include "game/station.h"
#include "game/station_env.h"
#include "gfx/text.h"

namespace sh {

// The texts of the status pages, read from SH.EXE.
struct StatusTexts {
  // A system's static data (`0x10e544`, 16 bytes each).
  struct System {
    std::uint8_t flags = 0;        // 1 can be damaged, 2 can be destroyed
    std::int32_t compartment = 0;  // +4
    std::string name;              // +8
    std::string help;              // +0xc, empty for none
  };
  std::array<System, 24> systems;
  std::array<std::string, 21> compartments;    // 0x10e6c4 + 0x1c x c
  std::string engine_room;                     // replaces "Fwd Engine Room" on boats with < 3 diesels
  std::array<std::string, 4> severities;       // 0x10e354: minor .. critical
  std::array<std::string, 4> leak_severities;  // 0x10e364: minor leaking ..
  std::array<std::string, 4> repair_states;    // 0x10e374: not under repair ..
  std::array<std::string, 5> alerts;           // 0x10e384: fire, chlorine, flooding x 3
  std::array<std::string, 3> motors;           // 0x10e398: Steam, Oxygen, Electric
  // The labels of the damage page (`status_draw_compartment_report`) and of
  // the torpedo card, and the parts of the repair times.
  std::string no_damage;            // 0xfa16c
  std::string hull_damaged;         // 0xfa181
  std::string severity;             // 0xfa1a2
  std::string status;               // 0xfa1c3
  std::string repair_time;          // 0xfa1e2
  std::string no_repair_time;       // 0xfa206 "--"
  std::string torpedoes_blocked;    // 0xfa21f
  std::string tube;                 // 0xfa24e
  std::string tube_gap;             // 0xfa26a
  std::string outer_door_damaged;   // 0xfa278
  std::string tube_damaged;         // 0xfa296
  std::string system_damaged;       // 0xfa305 " damaged"
  std::string hours;                // 0xfa3ad " hours "
  std::string hour;                 // 0xfa3b5 " hour "
  std::string minutes;              // 0xfa3d2 " min"
  std::string range;                // 0xfa3f8
  std::string yards;                // 0xfa42d
  std::string speed;                // 0xfa43f
  std::string knots;                // 0xfa474
  std::string warhead;              // 0xfa486
  std::string pounds;               // 0xfa4a5
  std::string motor;                // 0xfa4b5
  // The light positions (x, y) before the boat's equipment is placed:
  // int32[24][2] at 0x10e294.
  std::array<std::array<int, 2>, 24> light_positions{};

  static StatusTexts load(const ExeImage& exe);
};

// `status_severity_index` (0x75514): 0 below 21 %, 1 below 51, 2 below 81, 3.
std::int32_t status_severity_index(std::int32_t severity);
// `status_repair_state_text` (0x75538)'s index: 0 not under repair, 1
// destroyed (flag 2: unable to repair), 2 under repair (4), 3 waiting to
// surface (8); the later flags win.
std::int32_t status_repair_state(std::uint32_t flags);
// `status_format_repair_time` (0x7555f): the seconds left as "N hour(s) M
// min" with the words of `texts`, rounded up to the next minute.
std::string status_format_repair_time(std::int32_t seconds, const StatusTexts& texts);

class StatusStation : public Station {
 public:
  explicit StatusStation(StationEnv& env);

  void open() override;     // status_screen (0x73d2d)
  void event(const ui::Event& event) override;  // status_station_event (0x73dce)
  void process() override;
  void update() override;   // status_station_update (0x73de8)
  void close() override;    // status_station_close (0x73d8f)

  // The torpedo dragged by hand (0x127c44: the weapon id, negative for a
  // stern torpedo; 0 none) and the tube it came from (0x127c50, -1: a rack).
  std::int32_t dragged() const { return drag_; }
  std::int32_t drag_source() const { return drag_tube_; }
  // The hotspots of the page (screen rectangles, inclusive) by kind: 10
  // tubes, 11 bow rack, 12 stern rack, 0xb compartments (damage page; the
  // index is the compartment - 1).
  struct Hotspot {
    int x0, y0, x1, y1;
    std::int32_t kind;
    std::int32_t index;
  };
  const std::vector<Hotspot>& hotspots() const { return hotspots_; }

 private:
  struct Rect {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
  };
  // A compartment of the damage page with the screen saved under it.
  struct Compartment {
    bool present = false;
    Rect rect;
    Image8 background;
  };

  void draw_screen();             // status_draw_screen (0x73d69)
  void close_page();
  void swap_page(std::int32_t page);  // status_show_torpedo_page / _damage_page

  // Damage page.
  void draw_system_lights();                 // status_draw_system_lights (0x74656)
  void draw_compartments();                  // status_draw_compartments (0x74864)
  void animate_alerts(bool restore);         // status_animate_compartment_alerts (0x7495a)
  void draw_selected();                      // status_draw_selected_compartment (0x74aec)
  void draw_compartment();                   // status_draw_compartment (0x74c28)
  std::vector<std::int32_t> compartment_systems(std::int32_t c) const;  // status_list_compartment_systems
  void update_damage_page();                 // status_damage_page_update (0x743c4)
  void damage_event(const ui::Event& event);

  // Torpedo page.
  void draw_tubes(bool redraw);              // status_draw_tubes (0x7622e)
  void draw_racks(bool redraw);              // status_draw_torpedo_racks (0x765f2)
  void draw_torpedo_info(int x, int y, std::uint32_t weapon_id);  // status_draw_torpedo_info (0x75922)
  void draw_dragged(bool force);             // status_draw_dragged_torpedo (0x76078)
  void erase_dragged();                      // status_erase_dragged_torpedo (0x76194)
  void update_torpedo_page();                // status_torpedo_page_update (0x75ee7)
  void torpedo_event(const ui::Event& event);
  std::int32_t reload_time(std::size_t tube);

  // Text helpers.
  void shadowed_text(int x, int y, std::string_view text);  // status_draw_shadowed_text (0x754d1)
  void wrapped_text(std::string text, int x, int& y, int width);  // status_draw_wrapped_text (0x75645)
  // Returns the topmost hotspot under (x, y), or nothing.
  const Hotspot* hit(int x, int y) const;
  void set_cursor(std::int32_t cursor);
  void refresh_cursor();

  StationEnv& env_;
  StatusTexts texts_;
  Font font_;  // MENU_S.FNT (0x127c38)
  Shp sprites_;  // STATUS.SHP or STATUST.SHP (0x127c34)
  // The glyph tables 0x127d54 (ink 0xd8) and 0x127e54 (ink 0).
  GlyphColourTable light_{};
  GlyphColourTable dark_{};
  std::vector<Hotspot> hotspots_;
  std::int32_t cursor_ = -1;  // 0x127c40
  std::optional<std::int32_t> pending_page_;
  // The page drawn (closing follows it, whatever 0x11bcd4 says since).
  std::int32_t drawn_page_ = -1;

  // Damage page state.
  std::array<std::array<int, 2>, 24> light_pos_{};  // 0x10e294, adjusted per boat
  std::int32_t diesels_ = 0;                        // 0x1281b8
  std::int32_t motors_ = 0;                         // 0x1281bc
  std::array<Compartment, 20> compartments_{};
  std::array<std::uint8_t, 24> seen_system_flags_{};  // 0x127f54
  std::array<std::uint16_t, 21> seen_comp_flags_{};   // 0x127fb4
  struct SeenSystem {
    std::int32_t severity = 0;
    std::int32_t minutes = 0;
    std::uint8_t flags = 0;
  };
  std::array<SeenSystem, 24> seen_systems_{};          // 0x12800c
  std::int32_t seen_selection_ = 0;                    // 0x128008
  std::array<std::int32_t, 3> seen_selected_{};        // 0x128134.. leak, repair, flags
  struct SeenTube {
    std::int32_t severity = 0;
    std::int32_t repair = 0;
    std::uint16_t flags = 0;
  };
  std::array<SeenTube, 10> seen_tube_damage_{};        // 0x128140
  std::int32_t alert_frame_ = 0;                       // 0x1281c0

  // Torpedo page state.
  std::array<std::int32_t, 10> seen_tubes_{};       // 0x127c54
  std::array<std::uint16_t, 10> seen_tube_flags_{};  // 0x127c7c
  std::vector<std::int32_t> seen_bow_rack_;          // 0x127cf4
  std::vector<std::int32_t> seen_stern_rack_;        // 0x127ca4
  std::int32_t drag_ = 0;        // 0x127c44
  std::int32_t drag_tube_ = -1;  // 0x127c50
  int drag_x_ = 0;               // 0x127c48
  int drag_y_ = 0;               // 0x127c4c
  int drawn_x_ = -1;             // 0x127d44
  int drawn_y_ = -1;             // 0x127d48
  Image8 drag_background_;       // 0x127d4c
  Image8 scratch_;               // sprite_draw_transformed's buffer
};

}  // namespace sh

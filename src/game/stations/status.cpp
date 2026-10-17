#include "game/stations/status.h"

#include <cstdlib>
#include <format>
#include <utility>

#include "assets/pcx.h"
#include "core/error.h"
#include "core/fixed_math.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/torpedo_art.h"
#include "gfx/sprite.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The hotspot kinds (the `focus` field of the original's raw hotspots).
constexpr std::int32_t kTubeHotspot = TorpedoRoom::kTubeHotspot;
constexpr std::int32_t kBowRackHotspot = TorpedoRoom::kBowRackHotspot;
constexpr std::int32_t kCompartmentHotspot = 11;
constexpr std::int32_t kSternRackHotspot = TorpedoRoom::kSternRackHotspot;

// The bar buttons (GLOBAL.SHP frames): conn, and the other page.
constexpr std::uint8_t kConnFrame = 2;
constexpr std::uint8_t kTorpedoPageFrame = 0x10;
constexpr std::uint8_t kDamagePageFrame = 8;

// Cursors.
constexpr std::int32_t kCursorCross = 1;
constexpr std::int32_t kCursorSelect = 0x13;
constexpr std::int32_t kCursorBusy = 0x1b;

// The animation counter 0x129bd8 of `game_timer_tick`.
constexpr std::size_t kAlertTimer = 6;

// The torpedo page's type cards: their row, and (not in the original) the
// strip saved under them with the frame of a highlighted card, and the
// highlight colour (white in STATUSBG.PCX).
constexpr int kCardsY = TorpedoRoom::kCardsY;
constexpr int kCardsStripY = kCardsY - 2;
constexpr int kCardsHeight = 104;
// The racks' first row.
constexpr int kRacksY = TorpedoRoom::kRacksY;
constexpr std::uint8_t kHighlightColour = 240;

// The compartment report panel (STATUS.SHP frame 1) at (461, 154).
constexpr int kPanelX = 0x1cd;
constexpr int kPanelY = 0x9a;


// The radar fits of the options word (bits 16-18).
constexpr std::uint32_t kSjFitted = 0x30000;
constexpr std::uint32_t kSdFitted = 0x40000;
constexpr std::uint32_t kUnlimitedTorpedoes = 0x800;

// Returns `s` of the system table excluded on this boat (SD and SJ when
// not fitted), as `status_list_compartment_systems` filters them.
bool radar_absent(std::size_t s, std::uint32_t options) {
  return (s == 7 && (options & kSdFitted) == 0) || (s == 8 && (options & kSjFitted) == 0);
}

// Copies the `w` x `h` screen part at (x, y) into `out` (clipped).
Image8 grab(const Image8& screen, int x, int y, int w, int h) {
  Image8 out(w, h);
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      if (screen.contains(x + i, y + j)) {
        out.set(i, j, screen.at(x + i, y + j));
      }
    }
  }
  return out;
}

// Copies `src` back onto `vp` at (x, y) relative to its origin, clipped.
void put(const Viewport& vp, const Image8& src, int x, int y) {
  const ClipRect c = vp.clip();
  for (int j = 0; j < src.height(); ++j) {
    for (int i = 0; i < src.width(); ++i) {
      const int sx = vp.x0 + x + i;
      const int sy = vp.y0 + y + j;
      if (c.contains(sx, sy)) {
        vp.bitmap->set(sx, sy, src.at(i, j));
      }
    }
  }
}

}  // namespace

StatusTexts StatusTexts::load(const ExeImage& exe) {
  StatusTexts t;
  const auto str = [&exe](std::uint32_t pointer) {
    const std::uint32_t p = exe.u32(pointer);
    return p == 0 ? std::string() : std::string(exe.c_string(p));
  };
  for (std::uint32_t s = 0; s < 24; ++s) {
    const std::uint32_t a = 0x10e544 + 16 * s;
    t.systems[s] = {exe.u8(a), static_cast<std::int32_t>(exe.u32(a + 4)), str(a + 8), str(a + 12)};
  }
  for (std::uint32_t c = 0; c < 21; ++c) {
    t.compartments[c] = str(0x10e6c4 + 0x1c * c);
  }
  t.engine_room = std::string(exe.c_string(0xf5c4e));
  for (std::uint32_t i = 0; i < 4; ++i) {
    t.severities[i] = str(0x10e354 + 4 * i);
    t.leak_severities[i] = str(0x10e364 + 4 * i);
    t.repair_states[i] = str(0x10e374 + 4 * i);
  }
  for (std::uint32_t i = 0; i < 3; ++i) {
    t.motors[i] = str(0x10e398 + 4 * i);
  }
  for (std::uint32_t i = 0; i < 5; ++i) {
    t.alerts[i] = str(0x10e384 + 4 * i);
  }
  const auto text = [&exe](std::uint32_t address) { return std::string(exe.c_string(address)); };
  t.no_damage = text(0xfa16c);
  t.hull_damaged = text(0xfa181);
  t.severity = text(0xfa1a2);
  t.status = text(0xfa1c3);
  t.repair_time = text(0xfa1e2);
  t.no_repair_time = text(0xfa206);
  t.torpedoes_blocked = text(0xfa21f);
  t.tube = text(0xfa24e);
  t.tube_gap = text(0xfa26a);
  t.outer_door_damaged = text(0xfa278);
  t.tube_damaged = text(0xfa296);
  t.system_damaged = text(0xfa305);
  t.hours = text(0xfa3ad);
  t.hour = text(0xfa3b5);
  t.minutes = text(0xfa3d2);
  t.range = text(0xfa3f8);
  t.yards = text(0xfa42d);
  t.speed = text(0xfa43f);
  t.knots = text(0xfa474);
  t.warhead = text(0xfa486);
  t.pounds = text(0xfa4a5);
  t.motor = text(0xfa4b5);
  for (std::uint32_t i = 0; i < t.light_positions.size(); ++i) {
    const std::uint32_t a = 0x10e294 + 8 * i;
    t.light_positions[i] = {exe.i32(a), exe.i32(a + 4)};
    if (t.light_positions[i][0] < 0 || t.light_positions[i][0] >= kScreenWidth || t.light_positions[i][1] < 0 ||
        t.light_positions[i][1] >= kScreenHeight) {
      throw DataError(std::format("SH.EXE: status light {} off the screen (table 0x10e294)", i));
    }
  }
  return t;
}

std::int32_t status_severity_index(std::int32_t severity) {
  if (severity < 0x15) {
    return 0;
  }
  if (severity < 0x33) {
    return 1;
  }
  return severity < 0x51 ? 2 : 3;
}

std::int32_t status_repair_state(std::uint32_t flags) {
  std::int32_t state = (flags & 2) != 0 ? 1 : 0;
  if ((flags & 4) != 0) {
    state = 2;
  }
  if ((flags & 8) != 0) {
    state = 3;
  }
  return state;
}

std::string status_format_repair_time(std::int32_t seconds, const StatusTexts& texts) {
  std::int32_t minutes = (seconds + 0x3c) / 0x3c;
  std::int32_t hours = 0;
  if (minutes > 0x3b) {
    hours = minutes / 0x3c;
    minutes %= 0x3c;
  }
  std::string s;
  if (hours > 0) {
    s += std::to_string(hours) + (hours < 2 ? texts.hour : texts.hours);
  }
  if (minutes > 0) {
    s += std::to_string(minutes) + texts.minutes;
  }
  return s;
}

StatusStation::StatusStation(StationEnv& env) : env_(env), texts_(StatusTexts::load(env.exe)) {
  room_.set_sprites(&sprites_, nullptr);
  light_[0xff] = 0xff;
  dark_[0xff] = 0xff;
}

void StatusStation::open() {
  // status_screen (0x73d2d)
  env_.ui.redraw = true;
  draw_screen();
  env_.dac.fade_out(0x100);
}

void StatusStation::close() {
  // status_station_close (0x73d8f)
  close_page();
  env_.mission.player_combat().tubes.moved_by_hand = false;
}

void StatusStation::close_page() {
  // status_damage_page_close (0x742be) / status_torpedo_page_close (0x75ea8)
  hotspots_.clear();
  if (drawn_page_ == 0) {
    for (Compartment& c : compartments_) {
      c = {};
    }
    ++env_.ui.selected_compartment;
  }
  drawn_page_ = -1;
}

void StatusStation::swap_page(std::int32_t page) {
  // status_show_torpedo_page / status_show_damage_page (0x73e2d / 0x73e34)
  ++env_.ui.pause;
  close_page();
  env_.bar.close();
  env_.ui.status_page = page;
  draw_screen();
  env_.bar.process();
  --env_.ui.pause;
}

void StatusStation::process() {
  // The page buttons swap the page once the bar's dialog has returned.
  if (pending_page_) {
    const std::int32_t page = *pending_page_;
    pending_page_.reset();
    swap_page(page);
  }
}

void StatusStation::update() {
  // status_station_update (0x73de8)
  if (env_.ui.status_page == 0) {
    update_damage_page();
  } else if (env_.ui.status_page == 1) {
    update_torpedo_page();
  }
  if (first_frame()) {
    env_.dac.fade_in(env_.palette, 10);
  }
}

void StatusStation::draw_screen() {
  // status_draw_screen (0x73d69)
  font_ = Font::load(env_.install, env_.paths.path(GameDir::kArt, "MENU_S.FNT"));
  const std::int32_t page = env_.ui.status_page;
  if (page != 0 && page != 1) {
    return;
  }
  ++env_.ui.pause;
  drawn_page_ = page;
  Mission& mission = env_.mission;
  PlayerSub& player = mission.player();
  Image8& screen = env_.screen();
  const Viewport full = Viewport::full(screen);
  const std::uint32_t options = mission.sim().conditions().options;
  load_screen_pcx(env_, "STATUSBG.PCX");
  light_[0] = 0xd8;
  light_[0xff] = 0xff;
  const Shp silhouette =
      Shp::load(env_.install, env_.paths.path(GameDir::kArt, mission.player_class().sprite_silhouette));
  hotspots_.clear();
  cursor_ = -1;
  if (page == 0) {
    light_[7] = 0xd8;
    dark_[7] = 0;
    draw_sprite_clipped(full, silhouette.frame(1), 1, 1);
    env_.bar.open({{BarButton{kConnFrame, kConnFrame + 1, [this] { env_.bar.select_station(StationId::kConn); }},
                    BarButton{kTorpedoPageFrame, kTorpedoPageFrame + 1, [this] { pending_page_ = 1; }}},
                   {}, true, true, true});
    sprites_ = Shp::load(env_.install, env_.paths.path(GameDir::kArt, "STATUS.SHP"));
    alert_frame_ = 0;
    seen_selection_ = 0;
    env_.ui.timers[kAlertTimer] = 0x14;
    --env_.ui.selected_compartment;
    draw_sprite_clipped(full, sprites_.frame(0), 5, 0x9a);
    draw_sprite_clipped(full, sprites_.frame(2), 8, 0xa2);
    // The equipment row: the radars, the deck guns, the fitted diesels and
    // motors, left to right; their lights follow them.
    light_pos_ = texts_.light_positions;
    int x = 0x36;
    if ((options & kSdFitted) != 0) {
      draw_sprite_clipped(full, sprites_.frame(7), x, 0x12d);
      x = 0x56;
      light_pos_[7][0] = 0x3f;
    }
    if ((options & kSjFitted) != 0) {
      draw_sprite_clipped(full, sprites_.frame((options & kSjFitted) == kSjFitted ? 9 : 8), x, 0x12d);
      light_pos_[8][0] = x + 9;
      x += 0x20;
    }
    for (std::uint32_t g = 0; g < mission.player_class().deck_gun_count; ++g) {
      draw_sprite_clipped(full, sprites_.frame(10 + g), x, 0x12d);
      light_pos_[14 + g][0] = x + 0xe;
      x += 0x28;
    }
    diesels_ = 0;
    for (std::size_t r = 0; r < 4; ++r) {
      if (player.engines()[r][1].fitted) {
        light_pos_[16 + r][0] = x + 10;
        draw_sprite_clipped(full, sprites_.frame(3 + r), x, 0x12d);
        x += 0x21;
        ++diesels_;
      }
    }
    motors_ = 0;
    for (std::size_t r = 0; r < 4; ++r) {
      if (player.engines()[r][0].fitted) {
        light_pos_[20 + r][0] = x + 8;
        draw_sprite_clipped(full, sprites_.frame(0xc + r), x, 0x12d);
        x += 0x1e;
        ++motors_;
      }
    }
    const auto& systems = player.systems();
    for (std::size_t s = 0; s < 24; ++s) {
      seen_system_flags_[s] = systems[s].flags;
      seen_systems_[s] = {systems[s].severity, systems[s].repair_time, systems[s].flags};
    }
    const auto& comps = mission.player_combat().damage.compartments();
    for (std::size_t c = 0; c < 21; ++c) {
      seen_comp_flags_[c] = comps[c].flags;
    }
    draw_system_lights();
    // The compartments of the status board, each with the screen under it.
    const SubClass& boat = mission.player_class();
    for (std::size_t i = 0; i < 20; ++i) {
      const SubClass::Rect& r = boat.compartment_rects[i + 1];
      Compartment& c = compartments_[i];
      c = {};
      if (r.x1 == -1) {
        continue;
      }
      c.present = true;
      c.rect = {r.x1, r.y1, r.x2, r.y2};
      hotspots_.push_back({r.x1, r.y1, r.x2, r.y2, kCompartmentHotspot, static_cast<std::int32_t>(i)});
      c.background = grab(screen, r.x1, r.y1, r.x2 - r.x1 + 1, r.y2 - r.y1 + 1);
    }
    draw_compartments();
  } else {
    draw_sprite_clipped(full, silhouette.frame(0), 1, 1);
    env_.bar.open({{BarButton{kConnFrame, kConnFrame + 1, [this] { env_.bar.select_station(StationId::kConn); }},
                    BarButton{kDamagePageFrame, kDamagePageFrame + 1, [this] { pending_page_ = 0; }}},
                   {}, true, true, true});
    sprites_ = Shp::load(env_.install, env_.paths.path(GameDir::kArt, "STATUST.SHP"));
    if (env_.ui.features.torpedo_types) {
      art_ = std::make_unique<TorpedoArt>(
          sprites_, load_pcx(env_.install, env_.paths.path(GameDir::kArt, "STATUSBG.PCX")).palette);
    } else {
      art_.reset();
    }
    room_.set_sprites(&sprites_, art_.get());
    drag_ = 0;
    drag_tube_ = -1;
    const TorpedoTubes& tubes = mission.player_combat().tubes;
    for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.count()); ++t) {
      seen_tubes_[t] = tubes.content(t);
      seen_tube_flags_[t] = tubes.damage(t).flags;
    }
    seen_bow_rack_ = tubes.bow_rack();
    seen_stern_rack_ = tubes.stern_rack();
    draw_sprite_clipped(full, sprites_.frame(1), 0x144, 0x9a);
    draw_sprite_clipped(full, sprites_.frame(2), 6, 0x9a);
    if (art_) {
      racks_background_ = grab(env_.screen(), 0, kRacksY, kScreenWidth, kCardsStripY - kRacksY);
    }
    draw_tubes(false);
    draw_racks(false);
    // The boat's torpedo types: the available ones of the TDC's table; with
    // the extended variant's pictures, only those carried at the start.
    card_ids_.clear();
    const std::vector<std::uint32_t>& start = mission.start_torpedo_types();
    for (const TorpedoChoice& c : mission.player_combat().torpedo_types) {
      if (c.available && (!art_ || std::ranges::find(start, c.weapon_id) != start.end())) {
        card_ids_.push_back(c.weapon_id);
      }
    }
    cards_background_ = grab(env_.screen(), 0, kCardsStripY, kScreenWidth, kCardsHeight);
    highlighted_ = 0;
    draw_cards();
    const SpriteFrame& drag = sprites_.frame(3);
    drag_background_ = Image8(drag.width(), drag.height());
  }
  --env_.ui.pause;
}

// --- Damage page ----------------------------------------------------------

std::vector<std::int32_t> StatusStation::compartment_systems(std::int32_t c) const {
  // status_list_compartment_systems (0x7545f): the damaged systems there.
  std::vector<std::int32_t> list;
  const auto& systems = env_.mission.player().systems();
  const std::uint32_t options = env_.mission.sim().conditions().options;
  for (std::size_t s = 0; s < 24; ++s) {
    if (texts_.systems[s].compartment == c && systems[s].flags != 0 && !radar_absent(s, options)) {
      list.push_back(static_cast<std::int32_t>(s));
    }
  }
  return list;
}

void StatusStation::draw_system_lights() {
  // status_draw_system_lights (0x74656): a column of up to three lights per
  // system: working (frames 0x14/0x15), damaged (0x12/0x13) when it can be
  // damaged, destroyed (0x10/0x11) when it can be destroyed.
  const Viewport full = Viewport::full(env_.screen());
  const std::uint32_t options = env_.mission.sim().conditions().options;
  const std::uint32_t guns = env_.mission.player_class().deck_gun_count;
  const auto& systems = env_.mission.player().systems();
  for (std::size_t s = 0; s < 24; ++s) {
    if (radar_absent(s, options) || (s == 14 && guns < 1) || (s == 15 && guns < 2) ||
        (s >= 17 && s <= 19 && diesels_ < static_cast<std::int32_t>(s - 15)) ||
        (s >= 21 && s <= 23 && motors_ < static_cast<std::int32_t>(s - 19))) {
      continue;
    }
    const int x = light_pos_[s][0];
    int y = light_pos_[s][1];
    const std::uint8_t flags = systems[s].flags;
    draw_sprite_clipped(full, sprites_.frame(flags == 0 ? 0x15 : 0x14), x, y);
    y += 0x22;
    if ((texts_.systems[s].flags & 1) != 0) {
      draw_sprite_clipped(full, sprites_.frame((flags & 1) != 0 ? 0x13 : 0x12), x, y);
    }
    y += 0x22;
    if ((texts_.systems[s].flags & 2) != 0) {
      draw_sprite_clipped(full, sprites_.frame((flags & 2) != 0 ? 0x11 : 0x10), x, y);
    }
  }
}

void StatusStation::draw_compartments() {
  // status_draw_compartments (0x74864): the board cleared, a frame around
  // each damaged compartment, the alerts and the selection.
  Image8& screen = env_.screen();
  for (const Compartment& c : compartments_) {
    if (c.present) {
      screen.blit(c.background, c.rect.x0, c.rect.y0);
    }
  }
  const auto& comps = env_.mission.player_combat().damage.compartments();
  for (std::size_t i = 0; i < 20; ++i) {
    const Compartment& c = compartments_[i];
    if (!c.present) {
      continue;
    }
    if ((comps[i + 1].flags & 3) != 0 || !compartment_systems(static_cast<std::int32_t>(i + 1)).empty()) {
      ui::gui_focus_rect(Viewport::full(screen), c.rect.x0, c.rect.y0, c.rect.x1 - c.rect.x0 + 1,
                         c.rect.y1 - c.rect.y0 + 1, 0x44, false);
    }
  }
  env_.ui.timers[kAlertTimer] = 0x14;
  animate_alerts(false);
  draw_selected();
}

void StatusStation::animate_alerts(bool restore) {
  // status_animate_compartment_alerts (0x7495a): every 20 ticks, the
  // water level (frames 0x16.., lower for more water) or the flames
  // (0x1a..) of each compartment, 4 frames.
  std::int32_t& timer = env_.ui.timers[kAlertTimer];
  if (timer < 0x14) {
    return;
  }
  timer = 0;
  const auto& comps = env_.mission.player_combat().damage.compartments();
  for (std::size_t i = 0; i < 20; ++i) {
    const Compartment& c = compartments_[i];
    const std::uint16_t flags = comps[i + 1].flags;
    if (!c.present || (flags & (CompartmentState::kFire | CompartmentState::kWater1)) == 0) {
      continue;
    }
    const std::int32_t level = (flags & CompartmentState::kWater30) != 0   ? 2
                               : (flags & CompartmentState::kWater15) != 0 ? 1
                                                                           : 0;
    int base = 0x16;
    int x = 0;
    int dy = level == 0 ? 4 : level == 1 ? 2 : 0;
    if ((flags & CompartmentState::kFire) != 0) {
      x = (c.rect.x1 - c.rect.x0 + 1) / 2;
      base = 0x1a;
      dy = 0;
    }
    const Viewport inner{&env_.screen(), c.rect.x0 + 1, c.rect.y0 + 1, c.rect.x1 - 1, c.rect.y1 - 1};
    const int h = inner.y1 - inner.y0 + 1;
    if (restore) {
      put(inner, c.background, -1, -1);
    }
    draw_sprite_clipped(inner, sprites_.frame(static_cast<std::size_t>(base + alert_frame_)), x, dy + h);
  }
  if (++alert_frame_ > 3) {
    alert_frame_ = 0;
  }
}

void StatusStation::draw_selected() {
  // status_draw_selected_compartment (0x74aec): corner marks in colour 15.
  const std::int32_t sel = env_.ui.selected_compartment;
  if (sel < 0 || sel >= 20) {
    return;
  }
  const SubClass::Rect& r = env_.mission.player_class().compartment_rects[static_cast<std::size_t>(sel) + 1];
  const Viewport full = Viewport::full(env_.screen());
  const int x0 = r.x1;
  const int y0 = r.y1;
  const int x1 = r.x2;
  const int y1 = r.y2;
  ui::draw_line(full, x0, y0, x0 + 2, y0, 0xf);
  ui::draw_line(full, x0, y0, x0, y0 + 2, 0xf);
  ui::draw_line(full, x1, y0, x1 - 2, y0, 0xf);
  ui::draw_line(full, x1, y0, x1, y0 + 2, 0xf);
  ui::draw_line(full, x0, y1, x0 + 2, y1, 0xf);
  ui::draw_line(full, x0, y1, x0, y1 - 2, 0xf);
  ui::draw_line(full, x1, y1, x1 - 2, y1, 0xf);
  ui::draw_line(full, x1, y1, x1, y1 - 2, 0xf);
  draw_compartment();
}

void StatusStation::shadowed_text(int x, int y, std::string_view text) {
  TorpedoRoom::shadowed_text(Viewport::full(env_.screen()), x, y, text, font_, light_);
}

void StatusStation::wrapped_text(std::string text, int x, int& y, int width) {
  // status_draw_wrapped_text (0x75645): breaks at the last space once the
  // width is reached (at the start when there was no space yet).
  std::size_t line = 0;
  std::size_t space = 0;
  std::size_t i = 0;
  int w = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      space = i;
    }
    w += font_.glyph_width(static_cast<std::uint8_t>(text[i]));
    if (w >= width) {
      shadowed_text(x, y, std::string_view(text).substr(line, space - line));
      y += 9;
      i = space + 1;
      w = 0;
      line = i;
      continue;
    }
    ++i;
  }
  if (i != line) {
    shadowed_text(x, y, std::string_view(text).substr(line));
  }
}

void StatusStation::draw_compartment() {
  // status_draw_compartment (0x74c28): the report panel of the selected
  // compartment.
  Image8& screen = env_.screen();
  const Viewport full = Viewport::full(screen);
  const SpriteFrame& panel = sprites_.frame(1);
  const int w = panel.width();
  int x = kPanelX;
  int y = kPanelY;
  draw_sprite_clipped(full, panel, x, y);
  const std::int32_t c = env_.ui.selected_compartment + 1;
  std::string name = texts_.compartments[static_cast<std::size_t>(c)];
  if (c == 13 && env_.mission.player().sub().diesel_count < 3) {
    name = texts_.engine_room;
  }
  const Font& title = env_.gui.skin().font(ui::FontSlot::kSmall);
  const int tw = title.text_width(name);
  const int tx = x + (w - tw) / 2;
  gui_draw_text(full, tx, y + 5, title, name, &dark_);
  y += 4;
  gui_draw_text(full, tx, y, title, name, &light_);
  ui::draw_line(full, tx, y + 0xe, tx + tw - 1, y + 0xe, 0);
  ui::draw_line(full, tx, y + 0xd, tx + tw - 1, y + 0xd, 0xd8);
  y += 0x14;
  const std::vector<std::int32_t> systems = compartment_systems(c);
  const CompartmentState& comp = env_.mission.player_combat().damage.compartments()[static_cast<std::size_t>(c)];
  if (comp.flags == 0 && systems.empty()) {
    const std::string& text = texts_.no_damage;
    shadowed_text(x + (w - font_.text_width(text)) / 2, y, text);
    return;
  }
  x += 8;
  if ((comp.flags & CompartmentState::kBreached) != 0) {
    shadowed_text(x, y, texts_.hull_damaged);
    y += 9;
    shadowed_text(x, y, texts_.severity + texts_.leak_severities[static_cast<std::size_t>(status_severity_index(comp.leak))]);
    y += 9;
    shadowed_text(x, y, texts_.status + texts_.repair_states[static_cast<std::size_t>(status_repair_state(comp.flags))]);
    y += 9;
    shadowed_text(x, y, texts_.repair_time + ((comp.flags & CompartmentState::kRepairing) != 0
                                               ? status_format_repair_time(comp.repair_timer, texts_)
                                               : texts_.no_repair_time));
    y += 9;
  }
  std::int32_t alert = -1;
  if ((comp.flags & CompartmentState::kFire) != 0) {
    alert = 0;
  } else if ((comp.flags & CompartmentState::kChlorineGas) != 0) {
    alert = 1;
  } else if ((comp.flags & CompartmentState::kWater1) != 0) {
    alert = (comp.flags & CompartmentState::kWater30) != 0   ? 4
            : (comp.flags & CompartmentState::kWater15) != 0 ? 3
                                                             : 2;
  }
  if (alert >= 0) {
    wrapped_text(texts_.alerts[static_cast<std::size_t>(alert)], x, y, w - 0x10);
    y += 9;
  }
  if ((c == 1 || c == 16) && alert > 2) {
    wrapped_text(texts_.torpedoes_blocked, x, y, w - 0x10);
    y += 9;
  }
  const TorpedoTubes& tubes = env_.mission.player_combat().tubes;
  const auto& states = env_.mission.player().systems();
  for (const std::int32_t s : systems) {
    if (s == 11 || s == 12) {
      const std::int32_t first = s == 11 ? 0 : tubes.bow_count();
      const std::int32_t last = s == 11 ? tubes.bow_count() : tubes.count();
      for (std::int32_t t = first; t < last; ++t) {
        const TubeDamage& d = tubes.damage(static_cast<std::size_t>(t));
        if ((d.flags & 3) == 0) {
          continue;
        }
        y += 5;
        shadowed_text(x, y,
                      texts_.tube + std::to_string(t + 1) + texts_.tube_gap +
                          ((d.flags & TubeDamage::kOuterDoor) != 0 ? texts_.outer_door_damaged : texts_.tube_damaged));
        y += 9;
        shadowed_text(x, y, texts_.status + texts_.repair_states[static_cast<std::size_t>(status_repair_state(d.flags))]);
        y += 9;
        shadowed_text(x, y, texts_.repair_time + ((d.flags & TubeDamage::kRepairing) != 0
                                                   ? status_format_repair_time(d.repair_time, texts_)
                                                   : texts_.no_repair_time));
        y += 9;
      }
      continue;
    }
    const SystemState& st = states[static_cast<std::size_t>(s)];
    y += 5;
    shadowed_text(x, y, texts_.systems[static_cast<std::size_t>(s)].name + texts_.system_damaged);
    y += 9;
    shadowed_text(x, y, texts_.severity + texts_.severities[static_cast<std::size_t>(status_severity_index(st.severity))]);
    y += 9;
    shadowed_text(x, y, texts_.status + texts_.repair_states[static_cast<std::size_t>(status_repair_state(st.flags))]);
    y += 9;
    shadowed_text(x, y, texts_.repair_time + ((st.flags & 4) != 0 ? status_format_repair_time(st.repair_time, texts_)
                                                                : texts_.no_repair_time));
    y += 9;
    const std::string& help = texts_.systems[static_cast<std::size_t>(s)].help;
    if (!help.empty()) {
      wrapped_text(help, x, y, w - 0x10);
      y += 9;
    }
  }
}

void StatusStation::update_damage_page() {
  // status_damage_page_update (0x743c4): redraws what changed.
  const auto& systems = env_.mission.player().systems();
  const auto& comps = env_.mission.player_combat().damage.compartments();
  bool lights = false;
  bool board = false;
  bool report = false;
  for (std::size_t s = 0; s < 24; ++s) {
    if (seen_system_flags_[s] != systems[s].flags) {
      lights = board = true;
      seen_system_flags_[s] = systems[s].flags;
    }
  }
  for (std::size_t c = 0; c < 21; ++c) {
    if (seen_comp_flags_[c] != comps[c].flags) {
      board = true;
      seen_comp_flags_[c] = comps[c].flags;
    }
  }
  if (seen_selection_ != env_.ui.selected_compartment) {
    board = true;
    seen_selection_ = env_.ui.selected_compartment;
  }
  if (env_.ui.selected_compartment != -1) {
    const std::int32_t c = env_.ui.selected_compartment + 1;
    const CompartmentState& comp = comps[static_cast<std::size_t>(c)];
    const std::array<std::int32_t, 3> now{comp.leak, comp.repair_timer, comp.flags};
    if (now != seen_selected_) {
      seen_selected_ = now;
      report = true;
    }
    const TorpedoTubes& tubes = env_.mission.player_combat().tubes;
    for (const std::int32_t s : compartment_systems(c)) {
      SeenSystem& seen = seen_systems_[static_cast<std::size_t>(s)];
      const SystemState& st = systems[static_cast<std::size_t>(s)];
      if (seen.severity != st.severity || st.repair_time / 0x3c != seen.minutes || seen.flags != st.flags) {
        seen = {st.severity, st.repair_time / 0x3c, st.flags};
        report = true;
      }
      if (s == 11 || s == 12) {
        const std::int32_t first = s == 11 ? 0 : tubes.bow_count();
        const std::int32_t last = s == 11 ? tubes.bow_count() : tubes.count();
        for (std::int32_t t = first; t < last; ++t) {
          const TubeDamage& d = tubes.damage(static_cast<std::size_t>(t));
          SeenTube& st2 = seen_tube_damage_[static_cast<std::size_t>(t)];
          if (st2.severity != d.severity || st2.repair != d.repair_time || st2.flags != d.flags) {
            st2 = {d.severity, d.repair_time, d.flags};
            report = true;
          }
        }
      }
    }
  }
  if (env_.ui.redraw) {
    refresh_cursor();
  }
  if (lights) {
    draw_system_lights();
  }
  if (!board) {
    animate_alerts(true);
    if (report) {
      draw_compartment();
    }
  } else {
    draw_compartments();
  }
}

void StatusStation::damage_event(const ui::Event& event) {
  // status_station_event (0x73dce), damage page: the cross over a
  // compartment, a click selects it.
  std::int32_t cursor = static_cast<std::int32_t>(ui::CursorId::kPointer);
  if (const Hotspot* h = hit(event.x, event.y); h != nullptr && h->kind == kCompartmentHotspot) {
    cursor = kCursorCross;
    if ((event.flags & ui::kEventLeftDown) != 0) {
      env_.ui.selected_compartment = h->index;
    }
  }
  set_cursor(cursor);
}

// --- Torpedo page ---------------------------------------------------------

void StatusStation::draw_tubes(bool redraw) {
  // status_draw_tubes (0x7622e)
  const TorpedoTubes& tubes = env_.mission.player_combat().tubes;
  std::array<TorpedoRoom::Tube, TorpedoLoad::kTubes> shown{};
  const auto count = static_cast<std::size_t>(tubes.count());
  for (std::size_t t = 0; t < count; ++t) {
    shown[t] = {tubes.content(t), tubes.damage(t).flags != 0, tubes.timer(t) > 0};
  }
  room_.draw_tubes(Viewport::full(env_.screen()), std::span(shown).first(count), tubes.bow_count(),
                   redraw ? nullptr : &hotspots_);
}

void StatusStation::draw_racks(bool redraw) {
  // status_draw_torpedo_racks (0x765f2)
  const TorpedoTubes& tubes = env_.mission.player_combat().tubes;
  room_.draw_racks(Viewport::full(env_.screen()), tubes.bow_rack(), tubes.stern_rack(),
                   art_ ? &racks_background_ : nullptr, redraw ? nullptr : &hotspots_);
}

void StatusStation::draw_cards() {
  card_x_ = room_.card_positions(card_ids_.size());
  for (std::size_t i = 0; i < card_ids_.size(); ++i) {
    room_.draw_card(Viewport::full(env_.screen()), card_x_[i], kCardsY, card_ids_[i], env_.tables, font_, light_,
                    texts_);
  }
}

void StatusStation::highlight_card(std::uint32_t id) {
  // The card of the type being moved, framed (not in the original).
  if (id == highlighted_) {
    return;
  }
  erase_dragged();
  drawn_x_ = drawn_y_ = -1;
  put(Viewport::full(env_.screen()), cards_background_, 0, kCardsStripY);
  draw_cards();
  highlighted_ = id;
  const auto it = std::ranges::find(card_ids_, id);
  if (it == card_ids_.end()) {
    return;
  }
  const int x = card_x_[static_cast<std::size_t>(it - card_ids_.begin())];
  const int w = sprites_.frame(0).width();
  const int h = sprites_.frame(0).height();
  const Viewport full = Viewport::full(env_.screen());
  for (int k = 1; k <= 2; ++k) {
    ui::draw_line(full, x - k, kCardsY + 2 - k, x + w - 1 + k, kCardsY + 2 - k, kHighlightColour);
    ui::draw_line(full, x - k, kCardsY + 1 + h + k, x + w - 1 + k, kCardsY + 1 + h + k, kHighlightColour);
    ui::draw_line(full, x - k, kCardsY + 2 - k, x - k, kCardsY + 1 + h + k, kHighlightColour);
    ui::draw_line(full, x + w - 1 + k, kCardsY + 2 - k, x + w - 1 + k, kCardsY + 1 + h + k, kHighlightColour);
  }
}

void StatusStation::erase_dragged() {
  // status_erase_dragged_torpedo (0x76194)
  if (drag_ == 0 || drawn_x_ == -1) {
    return;
  }
  const int x = drawn_x_ - (drag_background_.width() - 1) / 2;
  const int y = drawn_y_ - (drag_background_.height() - 1) / 2;
  put(Viewport::full(env_.screen()), drag_background_, x, y);
}

void StatusStation::draw_dragged(bool force) {
  // status_draw_dragged_torpedo (0x76078): the torpedo under the pointer,
  // the screen under it kept.
  if (drag_ == 0) {
    drawn_x_ = drawn_y_ = -1;
    return;
  }
  if (drawn_x_ == drag_x_ && drawn_y_ == drag_y_ && !force) {
    return;
  }
  erase_dragged();
  drawn_x_ = drag_x_;
  drawn_y_ = drag_y_;
  const int x = drawn_x_ - (drag_background_.width() - 1) / 2;
  const int y = drawn_y_ - (drag_background_.height() - 1) / 2;
  drag_background_ = grab(env_.screen(), x, y, drag_background_.width(), drag_background_.height());
  if (art_ && static_cast<std::uint32_t>(std::abs(drag_)) <= TorpedoArt::kTypes) {
    // The type's own picture (not in the original: its outline).
    room_.draw_torpedo(Viewport::full(env_.screen()), 0, static_cast<std::uint32_t>(std::abs(drag_)), true, drawn_x_,
                       drawn_y_, drag_ < 0);
  } else {
    draw_sprite_clipped(Viewport::full(env_.screen()), sprites_.frame(drag_ < 0 ? 0xb : 0xa), drawn_x_, drawn_y_);
  }
}

void StatusStation::update_torpedo_page() {
  // status_torpedo_page_update (0x75ee7)
  const TorpedoTubes& tubes = env_.mission.player_combat().tubes;
  bool tubes_changed = false;
  bool racks_changed = false;
  for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.count()); ++t) {
    if (seen_tubes_[t] != tubes.content(t) || seen_tube_flags_[t] != tubes.damage(t).flags) {
      seen_tubes_[t] = tubes.content(t);
      seen_tube_flags_[t] = tubes.damage(t).flags;
      tubes_changed = true;
    }
  }
  if (seen_bow_rack_ != tubes.bow_rack() || seen_stern_rack_ != tubes.stern_rack()) {
    seen_bow_rack_ = tubes.bow_rack();
    seen_stern_rack_ = tubes.stern_rack();
    tubes_changed = racks_changed = true;
  }
  if (tubes_changed) {
    refresh_cursor();
    erase_dragged();
    if (drag_ != 0) {
      drawn_x_ = drawn_y_ = -1;
    }
  }
  if (env_.ui.redraw) {
    refresh_cursor();
  }
  if (tubes_changed) {
    draw_tubes(true);
  }
  if (racks_changed) {
    draw_racks(true);
  }
  if (art_) {
    highlight_card(static_cast<std::uint32_t>(std::abs(drag_)));
  }
  draw_dragged(tubes_changed);
}

std::int32_t StatusStation::reload_time(std::size_t tube) {
  SimContext ctx = env_.mission.sim().context();
  return env_.mission.player_combat().tubes.reload_time(ctx, tube, env_.mission.player_combat().crew);
}

void StatusStation::torpedo_event(const ui::Event& event) {
  // status_station_event (0x73dce), torpedo page: drag and drop between
  // the tubes and the racks.
  TorpedoTubes& tubes = env_.mission.player_combat().tubes;
  if (drag_ != 0) {
    drag_x_ = event.x;
    drag_y_ = event.y;
    if (drag_y_ > 0x1c1) {
      // The original also moves the mouse up there.
      drag_y_ = 0x1b8;
    }
  }
  const bool down = (event.flags & ui::kEventLeftDown) != 0;
  const bool unlimited = (env_.mission.sim().conditions().options & kUnlimitedTorpedoes) != 0;
  std::int32_t cursor = static_cast<std::int32_t>(ui::CursorId::kPointer);
  const Hotspot* h = hit(event.x, event.y);
  const auto drop = [this] {
    erase_dragged();
    drag_ = 0;
    drag_tube_ = -1;
  };
  if (h != nullptr && h->kind == kTubeHotspot) {
    const auto t = static_cast<std::size_t>(h->index);
    const std::int32_t content = tubes.content(t);
    const bool damaged = tubes.damage(t).flags != 0;
    if (drag_ != 0 && content == 0 && down &&
        ((drag_tube_ == -1 && tubes.timer(t) == 0) || h->index == drag_tube_)) {
      // A torpedo put in a tube of its room: it loads, or goes back at once
      // into the tube it came from.
      const bool stern = tubes.is_stern(t);
      if ((!stern && drag_ > 0) || (stern && drag_ < 0)) {
        tubes.set_tube(t, std::abs(drag_));
        if (drag_tube_ == -1) {
          tubes.set_timer(t, reload_time(t));
          tubes.set_tube(t, -tubes.content(t));
        } else {
          tubes.set_timer(t, 0);
        }
        drop();
      }
    } else if (drag_ == 0 && content < 0 && tubes.timer(t) > 0 && !damaged) {
      cursor = kCursorBusy;
      if (down) {
        const std::int32_t minutes = std::max(fx::trunc(tubes.timer(t)) / 60, 1);
        env_.bar.crew_message(0x19, h->index + 1, minutes);
      }
    } else if (drag_ == 0 && content == 0 && tubes.timer(t) > 0 && !damaged) {
      cursor = kCursorBusy;
      if (down) {
        const std::int32_t minutes = std::max(fx::trunc(tubes.timer(t)) / 60, 1);
        env_.bar.crew_message(0x1a, h->index + 1, minutes);
      }
    } else if (drag_ == 0 && content > 0 && !damaged) {
      cursor = kCursorSelect;
      if (down) {
        // A loaded torpedo taken out of its tube.
        drag_ = tubes.is_stern(t) ? -content : content;
        drag_x_ = event.x;
        drag_y_ = event.y;
        drag_tube_ = h->index;
        tubes.clear(t);
        tubes.set_timer(t, TorpedoTubes::kNoTimer);
      }
    }
  } else if (h != nullptr && (h->kind == kBowRackHotspot || h->kind == kSternRackHotspot)) {
    const bool bow = h->kind == kBowRackHotspot;
    std::vector<std::int32_t>& rack = bow ? tubes.bow_rack() : tubes.stern_rack();
    std::int32_t& slot = rack.at(static_cast<std::size_t>(h->index));
    if (slot > 0 && drag_ == 0) {
      cursor = kCursorSelect;
      if (down) {
        drag_ = bow ? slot : -slot;
        drag_tube_ = -1;
        drag_x_ = event.x;
        drag_y_ = event.y;
        if (!unlimited) {
          slot = 0;
        }
      }
    } else if (down && (bow ? drag_ > 0 : drag_ < 0) && (slot == 0 || unlimited)) {
      // Into the rack; from a tube it takes the unloading time.
      const std::int32_t type = std::abs(drag_);
      slot = type;
      if (drag_tube_ != -1) {
        slot = -(type * 0x100 + drag_tube_);
        tubes.set_timer(static_cast<std::size_t>(drag_tube_), reload_time(static_cast<std::size_t>(drag_tube_)));
      }
      drop();
    }
  }
  if (drag_ != 0) {
    tubes.moved_by_hand = true;
  }
  set_cursor(cursor);
}

// --- Events and cursor ----------------------------------------------------

void StatusStation::event(const ui::Event& event) {
  if (env_.ui.status_page == 0) {
    damage_event(event);
  } else if (env_.ui.status_page == 1) {
    torpedo_event(event);
  }
}

const StatusStation::Hotspot* StatusStation::hit(int x, int y) const {
  const Hotspot* found = nullptr;
  for (const Hotspot& h : hotspots_) {
    if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) {
      found = &h;
    }
  }
  return found;
}

void StatusStation::set_cursor(std::int32_t cursor) {
  if (cursor != cursor_) {
    cursor_ = cursor;
    env_.set_cursor(static_cast<std::size_t>(cursor));
  }
}

void StatusStation::refresh_cursor() {
  // The original posts a mouse event at the pointer so that the cursor is
  // chosen again.
  ui::Event e;
  e.flags = ui::kEventMoved;
  e.x = env_.gui.mouse().x();
  e.y = env_.gui.mouse().y();
  env_.gui.events().post(e);
  cursor_ = static_cast<std::int32_t>(ui::CursorId::kPointer);
}

}  // namespace sh

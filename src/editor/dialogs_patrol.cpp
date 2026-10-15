#include "editor/dialogs_patrol.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include "assets/dat_tables.h"
#include "assets/shp.h"
#include "editor/dialogs_boxes.h"
#include "editor/environment.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

constexpr int kButtonWidth = 100;

// The map's corner on the dialog (`patrol_area_draw_map`): the PZ.DAT
// orders rectangles are offset by it.
constexpr int kMapX = 11;
constexpr int kMapY = 49;
// The zone frames: every listed zone, then the selected one.
constexpr std::uint8_t kZoneColour = 0xa4;
constexpr std::uint8_t kSelectedColour = 0xf;
// The focus field (+0x20) the zone hotspots carry, which `patrol_area_idle`
// recognises them by.
constexpr int kZoneHotspotTag = 0xb;
// The info box: the group box 0xa6f30.
constexpr ui::Rect kInfoBox{431, 49, 629, 94};
// The palette of the SPAC map.
constexpr int kMapFirstColour = 128;
constexpr int kMapLastColour = 253;

// SE strings.
constexpr std::uint32_t kTitle = 0x906e0;
constexpr std::uint32_t kOk = 0x906ec;
constexpr std::uint32_t kCancel = 0x906ef;
constexpr std::uint32_t kDatesActive = 0x90a3b;  // "Dates Active:"
constexpr std::uint32_t kSlash1 = 0x90a61;
constexpr std::uint32_t kThru = 0x90a7b;
constexpr std::uint32_t kSlash2 = 0x90a9a;
constexpr std::uint32_t kAwld = 0x93220;
constexpr std::uint32_t kOrdersTitle = 0x9086e;

ui::Rect zone_rect(const PatrolZone& z) {
  return {z.orders_rect[0] + kMapX, z.orders_rect[1] + kMapY, z.orders_rect[2] + kMapX, z.orders_rect[3] + kMapY};
}

// Copies the pixels of `r` (viewport coordinates) out of `gfx`.
Image8 save_region(const Viewport& gfx, const ui::Rect& r) {
  Image8 out(r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
  for (int y = 0; y < out.height(); ++y) {
    for (int x = 0; x < out.width(); ++x) {
      const int bx = gfx.x0 + r.x0 + x;
      const int by = gfx.y0 + r.y0 + y;
      if (gfx.bitmap->contains(bx, by)) {
        out.set(x, y, gfx.bitmap->at(bx, by));
      }
    }
  }
  return out;
}

class PatrolAreaDialog {
 public:
  PatrolAreaDialog(EditorUi& ui, const std::vector<PatrolZone>& zones, const PatrolAreaList& list, const Shp& map)
      : ui_(ui), zones_(zones), list_(list), map_(map), shown_(static_cast<int>(list.current)) {}

  // Runs the dialog; returns the PZ.DAT index chosen, or nothing.
  std::optional<std::size_t> run();

 private:
  const PatrolZone& listed(int row) const { return zones_[list_.zones[static_cast<std::size_t>(row)]]; }
  void draw_map(ui::Dialog& d, const Viewport& gfx);
  void show_zone(ui::Dialog& d, const Viewport& gfx);
  void idle(ui::Dialog& d, const Viewport& gfx, bool first);

  EditorUi& ui_;
  const std::vector<PatrolZone>& zones_;
  const PatrolAreaList& list_;
  const Shp& map_;
  ui::List* list_control_ = nullptr;
  int shown_;                     // 0xa6d3c: the zone framed
  bool fade_in_ = false;          // 0xa6d48
  std::vector<Image8> under_;     // 0xa6d14: the map under each zone
};

void PatrolAreaDialog::draw_map(ui::Dialog& d, const Viewport& gfx) {
  // patrol_area_draw_map (0x25297); every call adds the hotspots and saves
  // the map again, as the original.
  fade_in_ = false;
  draw_sprite_clipped(gfx, map_.frame(0), kMapX, kMapY);
  const int count = static_cast<int>(list_.zones.size());
  for (int i = 0; i < count; ++i) {
    const ui::Rect r = zone_rect(listed(i));
    ui::gui_focus_rect(gfx, r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1, kZoneColour, false);
  }
  under_.clear();
  for (int i = 0; i < count; ++i) {
    const ui::Rect r = zone_rect(listed(i));
    ui::Hotspot h;
    h.x0 = d.x() + r.x0;
    h.y0 = d.y() + r.y0;
    h.x1 = d.x() + r.x1;
    h.y1 = d.y() + r.y1;
    h.sub = i;
    h.focus = kZoneHotspotTag;
    // The original leaves the owner uninitialised; owned here, so that the
    // dialog's close removes them (the original pops them after the run).
    // They lie after the dialog's registered hotspots, which it ignores.
    h.owner = &d;
    d.hotspots().add(h);
    under_.push_back(save_region(gfx, r));
  }
}

void PatrolAreaDialog::show_zone(ui::Dialog& d, const Viewport& gfx) {
  // patrol_area_show_zone (0x2543a)
  if (list_.zones.empty() || list_control_->selection < 0) {
    return;  // the original reads outside the list
  }
  const ui::Rect old = zone_rect(listed(shown_));
  gfx.bitmap->blit(under_[static_cast<std::size_t>(shown_)], gfx.x0 + old.x0, gfx.y0 + old.y0);
  d.add_dirty(old);
  const PatrolZone& zone = listed(list_control_->selection);
  const ui::Rect r = zone_rect(zone);
  const int w = r.x1 - r.x0 + 1;
  const int h = r.y1 - r.y0 + 1;
  ui::gui_focus_rect(gfx, r.x0, r.y0, w, h, kSelectedColour, false);
  ui::gui_focus_rect(gfx, r.x0 + 1, r.y0 + 1, w - 2, h - 2, kSelectedColour, false);
  d.add_dirty(r);

  const ui::Rect& b = kInfoBox;
  ui::GuiResources& res = ui_.resources();
  ui::gui_fill_rect(gfx, b.x0 + 2, b.y0 + 2, b.x1 - b.x0 - 3, b.y1 - b.y0 - 3, res.colours.b[1]);
  const Font& font = ui_.font1();
  gui_draw_text(gfx, b.x0 + (b.x1 - b.x0 + 1 - font.text_width(zone.name)) / 2, b.y0 + 8, font, zone.name,
                &res.table);
  gui_draw_text(gfx, b.x0 + 8, b.y0 + 0x19, font, ui_.str(kDatesActive), &res.table);
  const std::string dates =
      format_month_year(clamp_to_war(zone.first_month, zone.first_year - kScenarioYearBase), ui_.str(kSlash1)) +
      ui_.str(kThru) +
      format_month_year(clamp_to_war(zone.last_month, zone.last_year - kScenarioYearBase), ui_.str(kSlash2));
  gui_draw_text(gfx, b.x0 + 8 + 0x5a, b.y0 + 0x19, font, dates, &res.table);
  d.add_dirty(b);
}

void PatrolAreaDialog::idle(ui::Dialog& d, const Viewport& gfx, bool first) {
  // patrol_area_idle (0x25180)
  ui::List& list = *list_control_;
  if (fade_in_) {
    ui_.palette_fade_in(ui_.palette(), 0, 10);
    fade_in_ = false;
  }
  if (first) {
    list.dirty = true;
    fade_in_ = true;
    list.selection = shown_;
    list.top = shown_;
  }
  if (list.selection != shown_ || first) {
    show_zone(d, gfx);
    shown_ = list.selection;
  }
  ui::Gui& gui = ui_.gui();
  const int hit = gui.hotspots().hit_test(gui.mouse().x(), gui.mouse().y());
  if (hit == -1) {
    return;
  }
  const ui::Hotspot& h = gui.hotspots()[hit];
  if (h.control != nullptr || h.focus != kZoneHotspotTag) {
    return;
  }
  list.selection = h.sub;
  if (list.selection != shown_) {
    show_zone(d, gfx);
    list.dirty = true;
    shown_ = list.selection;
    list.top = list.selection;
  }
  if (gui.mouse().buttons() != 0) {
    // Original bug (docs/editor-design.md "PATROL AREA map click", fixed): SE fades
    // the screen out and sets a flag that fades it out on every later idle
    // call and is never cleared, while the dialog, which ignores the zone
    // hotspots, stays open: black until OK, CANCEL or Esc, and black again
    // in every later PATROL AREA of the session. The press accepts the
    // zone here, as OK does after the fade.
    ui_.palette_fade_out(0, 0x100);
    d.set_result(ui::kResultAccept);
  }
}

std::optional<std::size_t> PatrolAreaDialog::run() {
  EditorUi& ui = ui_;
  EditorDialog d(ui, 0, 0, 640, 459, ui::kDialogFramed | ui::kDialogCancel);
  d.emplace<ui::OwnerDraw>([this, &d](const Viewport& gfx, ui::OwnerDraw&) { draw_map(d, gfx); });
  d.emplace<ui::TitleBar>(ui.str(kTitle), ui::return_cancel);
  // The list's on_activate (`dialog_return_accept`) is dropped by the list.
  list_control_ = &d.emplace<ui::List>(
      432, 100, 169, [this] { return static_cast<int>(list_.zones.size()); },
      [this](int row) { return listed(row).name; });
  d.emplace<ui::Button>(484, 381, kButtonWidth, ui.str(kOk), ui::return_accept);
  d.emplace<ui::Button>(484, 414, kButtonWidth, ui.str(kCancel), ui::return_cancel);
  d.emplace<ui::GroupBox>(9, 47, 428, 442);
  d.emplace<ui::GroupBox>(431, 49, 629, 94);
  d.emplace<ui::GroupBox>(431, 254, 629, 375);
  d.set_idle([this, &d](const Viewport& gfx, int first) { idle(d, gfx, first != 0); });
  int result = d.run();
  if (list_control_->selection == -1) {
    result = ui::kResultCancel;
  }
  if (result == ui::kResultCancel || list_.zones.empty()) {
    return std::nullopt;
  }
  return list_.zones[static_cast<std::size_t>(list_control_->selection)];
}

std::string upper(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}

}  // namespace

std::int32_t campaign_month_index(std::int32_t month, std::int32_t year) {
  std::int32_t m = kWarStart.month;
  std::int32_t y = kWarStart.year;
  std::int32_t index = 0;
  while (m != month || y != year) {
    ++index;
    if (++m == 12) {
      m = 0;
      ++y;
    }
  }
  return index;
}

std::string dated_pacific_map(const SeExe& se, std::int32_t month, std::int32_t year) {
  const std::int32_t index = campaign_month_index(month, year);
  std::uint32_t n = 0;
  while (se.i32(se_addr::kPacificMaps + n * 0x11 + 0xd) <= index) {
    ++n;
  }
  return std::string(se.str(se_addr::kPacificMaps + (n - 1) * 0x11));
}

std::vector<bool> patrol_zone_flags(const Install& install, const SeExe& se, const std::vector<PatrolZone>& zones) {
  std::vector<bool> flags;
  flags.reserve(zones.size());
  for (const PatrolZone& z : zones) {
    bool ok = false;
    if (const std::optional<std::vector<std::uint8_t>> data = install.try_read("DATA/" + z.world);
        data && data->size() >= 6) {
      const std::string_view head(reinterpret_cast<const char*>(data->data()), 6);
      // strcmp(head, "AWLD"): the magic, then a NUL; the version is byte 5.
      ok = head.substr(0, 4) == se.str(kAwld) && head[4] == '\0' && head[5] == 7;
    }
    flags.push_back(ok);
  }
  return flags;
}

PatrolAreaList patrol_area_list(const std::vector<PatrolZone>& zones, const std::vector<bool>& enabled,
                                std::string_view world, std::int32_t month, std::int32_t year, bool limit) {
  PatrolAreaList out;
  const std::int32_t full_year = year + kScenarioYearBase;
  for (std::size_t i = 0; i < zones.size(); ++i) {
    if (!enabled[i]) {
      continue;
    }
    const PatrolZone& z = zones[i];
    if (limit && (full_year < z.first_year || z.last_year < full_year ||
                  (full_year == z.first_year && month + 1 < z.first_month) ||
                  (full_year == z.last_year && z.last_month < month + 1))) {
      continue;
    }
    if (z.world == world) {
      out.current = out.zones.size();
    }
    out.zones.push_back(i);
  }
  return out;
}

bool cmd_patrol_area(EditorUi& ui, Scenario& scenario, const EditorOptions& options,
                     const std::function<int()>& save) {
  if (ask_save_changes(ui, scenario.modified, save) == ui::kResultCancel) {
    return false;
  }
  const std::vector<PatrolZone>& zones = scenario.data().zones;
  // The original's strupr changes the scenario's world name itself.
  const std::string world = upper(scn_text(scenario.header.world));
  set_scn_text(scenario.header.world, world);
  const PatrolAreaList list =
      patrol_area_list(zones, patrol_zone_flags(ui.install(), ui.se(), zones), world, scenario.header.month,
                       scenario.header.year, options.limit_patrol_areas != 0);

  // The screen palette with the map's colours, faded in by the idle.
  const VgaPalette saved = ui.palette();
  ui.palette_fade_out(0, 0x100);
  const Shp map = Shp::load(
      ui.install(), "ART/" + dated_pacific_map(ui.se(), scenario.header.month, scenario.header.year));
  VgaPalette colours = ui.palette();
  sprite_apply_palette(map.frame(0), colours);
  std::copy(colours.begin() + kMapFirstColour * 3, colours.begin() + (kMapLastColour + 1) * 3,
            ui.palette().begin() + kMapFirstColour * 3);
  // The list rows in FONT1, 4 pixels down (0x9bc68 / 0x9bc6c).
  EditorSkin& skin = ui.skin();
  const Font* list_font = skin.list_font;
  const int text_offset = skin.metrics().list_text_y;
  skin.list_font = &ui.font1();
  skin.mutable_metrics().list_text_y = 4;
  std::optional<std::size_t> chosen;
  {
    PatrolAreaDialog dialog(ui, zones, list, map);
    chosen = dialog.run();
  }
  skin.list_font = list_font;
  skin.mutable_metrics().list_text_y = text_offset;
  ui.palette() = saved;
  ui.palette_fade_in(ui.palette(), 0, 10);

  if (!chosen || equals_ignore_case(zones[*chosen].world, scn_text(scenario.header.world))) {
    return false;
  }
  scenario.change_world(zones[*chosen].world);
  return true;
}

int orders_popup_run(EditorUi& ui, const std::vector<std::string>& labels, const std::vector<bool>& disabled,
                     int initial) {
  ui.palette_grey_out(0x32, 0xff);
  const int count = static_cast<int>(labels.size());
  std::vector<ui::GridCell> cells;
  int y = 0x2e;
  for (int i = 0; i < count; ++i) {
    cells.push_back({.x = 0xc,
                     .y = y,
                     .w = 0xcc,
                     .label = labels[static_cast<std::size_t>(i)],
                     .up = i == 0 ? -1 : i - 1,
                     .down = i == count - 1 ? -1 : i + 1,
                     .left = -1,
                     .right = -1,
                     .key = 0,
                     .disabled = disabled[static_cast<std::size_t>(i)]});
    y += 0x17;
  }
  const int h = y + 0xc;
  EditorDialog d(ui, 203, (kScreenHeight - h) / 2, 227, h, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d.emplace<ui::TitleBar>(ui.str(kOrdersTitle), ui::return_cancel);
  // orders_popup_on_select (0x2a172): the index + 1.
  d.emplace<ui::ButtonGrid>(std::move(cells), [](int cell) { return cell + 1; }, initial);
  const int result = d.run();
  ui.palette_restore_saved(0xff);
  return result - 1;
}

}  // namespace sh::editor

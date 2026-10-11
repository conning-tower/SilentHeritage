#include "editor/dialogs_settings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

#include "editor/dialogs_boxes.h"
#include "editor/environment.h"
#include "editor/loadout.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

constexpr unsigned kDialogFlags = ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel;
constexpr int kButtonWidth = 100;

// Tables of SE.EXE shared by the dialogs.
constexpr std::uint32_t kDigits = 0x988dc;  // "0" .. "9"
// Save Scenario's write error, also OPTIONS' with the corrections.
constexpr std::uint32_t kWriteFailed = 0x913ea;  // "UNABLE TO WRITE TO FILE:"
constexpr std::uint32_t kWriteFailedTitle = 0x91403;
// The height of an EQUIPMENT row: the radar detector's (not in the
// original).
constexpr int kEquipmentRowH = 44;

// A label in FONT1 (0xa5050), as most of the editor's labels.
ui::Label& font1_label(EditorUi& ui, ui::Dialog& d, int x, int y, std::uint32_t text) {
  return d.emplace<ui::Label>(x, y, ui.str(text), ui::FontSlot::kCustom, true, &ui.font1());
}

// A text field in FONT1 filled with colour 6.
ui::TextField& font1_field(EditorUi& ui, ui::Dialog& d, int x0, int y0, int x1, int y1,
                           std::function<std::string()> getter, TextAlign align) {
  auto& f = d.emplace<ui::TextField>(x0, y0, x1, y1, std::move(getter), ui::FontSlot::kCustom, align);
  f.fill = 6;
  f.custom = &ui.font1();
  return f;
}

// A cycle button over `strings` driven by `index(advance)`.
ui::CycleButton& cycle(ui::Dialog& d, int x, int y, int w, std::vector<std::string> strings,
                       std::function<int(bool)> index) {
  return d.emplace<ui::CycleButton>(x, y, w, std::move(strings), std::move(index));
}

// ------------------------------------------------------------ ENVIRONMENT

// The preview of ENVIRONMENT: the group box (34, 255)-(352, 350) of the
// weather picture and the sky box (304, 361)-(346, 403) inside the group
// box 0xa7520 (the boxes' corners moved in by 2).
constexpr ui::Rect kWeatherBox{34, 255, 352, 350};
constexpr ui::Rect kSkyBox{304, 361, 346, 403};
// The palette ranges of the preview: the weather picture's colours, tinted
// by the light, and the moon's.
constexpr int kWeatherFirstColour = 209;
constexpr int kWeatherLastColour = 245;
constexpr int kMoonFirstColour = 246;
constexpr int kMoonLastColour = 250;

class EnvironmentDialog {
 public:
  EnvironmentDialog(EditorUi& ui, Scenario& scenario)
      : ui_(ui), scenario_(scenario), form_(EnvironmentForm::from_header(scenario.header)) {}

  bool run();

 private:
  // `environment_update_lighting` (0x28283) with a viewport: the light, the
  // tinted DAC colours, the sky box and its moon; `first` within a full
  // paint, else the box is added to the dirty list and the dialog
  // invalidated.
  void update_lighting(ui::Dialog& d, const Viewport& gfx, bool first);
  // `environment_redraw_weather` (0x280a7), also the owner draw with
  // `first`: the weather picture with its colours, then the light.
  void redraw_weather(ui::Dialog& d, const Viewport& gfx, bool first);
  // Wraps a form callback: redraws the day digits when it clamped the day.
  std::function<int(bool)> callback(std::int32_t (EnvironmentForm::*fn)(bool));

  EditorUi& ui_;
  Scenario& scenario_;
  EnvironmentForm form_;
  EnvironmentSky sky_;
  ui::CycleButton* day_tens_ = nullptr;
  ui::CycleButton* day_units_ = nullptr;
};

void EnvironmentDialog::update_lighting(ui::Dialog& d, const Viewport& gfx, bool first) {
  const WorldPoint player = scenario_.player().position(scenario_.object_env());
  sky_ = compute_environment_sky(form_, scenario_.data(), scenario_.world().world(), player.x, player.y);
  // Original bug (docs/editor-design.md "ENVIRONMENT preview light", fixed): the
  // preview writes the sighting light global 0xf6224, which SE keeps after
  // CANCEL (the visibility used the light of the last date and time
  // shown); `run` puts it back.
  scenario_.sighting_light_pct = sky_.light_pct;
  const VgaPalette& palette = ui_.palette();
  VgaPalette& dac = ui_.dac();
  for (int i = kWeatherFirstColour; i <= kWeatherLastColour; ++i) {
    for (std::size_t c = 0; c < 3; ++c) {
      const auto at = static_cast<std::size_t>(i) * 3 + c;
      std::int32_t v = palette[at] - palette[at] * sky_.darkening[c] / 100;
      if (form_.fog == 1) {
        const std::int32_t grey = sky_.light_pct * 0x3f / 100;
        v = std::clamp((grey - v) * 70 / 100 + v, 0, grey);
      }
      dac[at] = static_cast<std::uint8_t>(v);
    }
  }
  ui_.set_dac_from_palette(kMoonFirstColour, kMoonLastColour);
  ui::gui_fill_rect(gfx, kSkyBox.x0, kSkyBox.y0, kSkyBox.x1 - kSkyBox.x0 + 1, kSkyBox.y1 - kSkyBox.y0 + 1, 0);
  draw_sprite_clipped(gfx, ui_.sbchart().frame(sbchart_frame::kMoonFirst + static_cast<std::size_t>(sky_.moon_phase)),
                      kSkyBox.x0 + 6, kSkyBox.y0 + 6);
  if (!first) {
    d.add_dirty(kSkyBox);
    d.invalidate();
  }
  form_.lighting_dirty = false;
}

void EnvironmentDialog::redraw_weather(ui::Dialog& d, const Viewport& gfx, bool first) {
  if (!first) {
    ui::gui_fill_rect(gfx, kWeatherBox.x0, kWeatherBox.y0, kWeatherBox.x1 - kWeatherBox.x0 + 1,
                      kWeatherBox.y1 - kWeatherBox.y0 + 1, ui_.resources().colours.b[1]);
  }
  const SpriteFrame& weather = ui_.sbchart().frame(sbchart_frame::kWeatherFirst + static_cast<std::size_t>(form_.weather));
  // The colours go to the screen palette only; `update_lighting` sets the
  // DAC.
  const VgaPalette weather_colours = ui_.frame_colours(weather);
  std::copy(weather_colours.begin() + kWeatherFirstColour * 3,
            weather_colours.begin() + (kWeatherLastColour + 1) * 3, ui_.palette().begin() + kWeatherFirstColour * 3);
  const VgaPalette moon_colours = ui_.frame_colours(ui_.sbchart().frame(sbchart_frame::kMoonFirst));
  std::copy(moon_colours.begin() + kMoonFirstColour * 3, moon_colours.begin() + (kMoonLastColour + 1) * 3,
            ui_.palette().begin() + kMoonFirstColour * 3);
  draw_sprite_clipped(gfx, weather, kWeatherBox.x0, kWeatherBox.y0);
  update_lighting(d, gfx, first);
  if (!first) {
    d.add_dirty(kWeatherBox);
    d.invalidate();
  }
  form_.weather_dirty = false;
}

std::function<int(bool)> EnvironmentDialog::callback(std::int32_t (EnvironmentForm::*fn)(bool)) {
  return [this, fn](bool advance) {
    const std::int32_t index = (form_.*fn)(advance);
    if (form_.day_dirty) {
      form_.day_dirty = false;
      day_tens_->dirty = true;
      day_units_->dirty = true;
    }
    return index;
  };
}

bool EnvironmentDialog::run() {
  EditorUi& ui = ui_;
  const std::int32_t light = scenario_.sighting_light_pct;
  ui::Dialog d(ui.gui(), 126, 1, 387, 454, kDialogFlags);
  d.emplace<ui::OwnerDraw>([this, &d](const Viewport& gfx, ui::OwnerDraw&) { redraw_weather(d, gfx, true); });
  d.emplace<ui::TitleBar>(ui.str(0x906f6), ui::return_cancel);
  d.emplace<ui::GroupBox>(32, 48, 356, 85);
  d.emplace<ui::Label>(50, 62, ui.str(0x90702));
  const std::vector<std::string> digits = ui.strings(kDigits, 10);
  cycle(d, 103, 57, 132, ui.strings(0x9ef38, 12), callback(&EnvironmentForm::cycle_month));
  day_tens_ = &cycle(d, 239, 57, 16, digits, callback(&EnvironmentForm::cycle_day_tens));
  day_units_ = &cycle(d, 256, 57, 16, digits, callback(&EnvironmentForm::cycle_day_units));
  cycle(d, 276, 57, 53, ui.strings(0x9bef4, 12), callback(&EnvironmentForm::cycle_year));
  d.emplace<ui::GroupBox>(32, 89, 356, 126);
  d.emplace<ui::Label>(48, 103, ui.str(0x90708));
  d.emplace<ui::Label>(225, 103, ui.str(0x9070e));
  cycle(d, 189, 98, 16, digits, callback(&EnvironmentForm::cycle_hour_tens));
  cycle(d, 205, 98, 16, digits, callback(&EnvironmentForm::cycle_hour_units));
  cycle(d, 231, 98, 16, digits, callback(&EnvironmentForm::cycle_minute_tens));
  cycle(d, 248, 98, 16, digits, callback(&EnvironmentForm::cycle_minute_units));
  d.emplace<ui::GroupBox>(32, 130, 356, 249);
  d.emplace<ui::Label>(48, 149, ui.str(0x90710));
  cycle(d, 185, 143, 152, ui.strings(0x9efb4, 4), callback(&EnvironmentForm::cycle_weather));
  d.emplace<ui::Label>(48, 172, ui.str(0x90719));
  cycle(d, 185, 167, 152, ui.strings(0x9efc4, 3), callback(&EnvironmentForm::cycle_sea));
  d.emplace<ui::Label>(48, 195, ui.str(0x90729));
  cycle(d, 185, 191, 152, ui.strings(0x9efd0, 2), callback(&EnvironmentForm::cycle_fog));
  d.emplace<ui::Label>(48, 218, ui.str(0x9072e));
  cycle(d, 185, 214, 152, ui.strings(0x9efd8, 5), callback(&EnvironmentForm::cycle_gradient));
  d.emplace<ui::GroupBox>(32, 253, 356, 410);
  d.emplace<ui::Line>(34, 352, 354, 352, 3);
  d.emplace<ui::Line>(34, 353, 354, 353, 3);
  d.emplace<ui::Line>(34, 354, 354, 354, 2);
  d.emplace<ui::Line>(34, 355, 354, 355, 2);
  d.emplace<ui::GroupBox>(302, 359, 348, 405);
  d.emplace<ui::Button>(34, 417, kButtonWidth, ui.str(0x90738), ui::return_cancel);
  d.emplace<ui::Button>(255, 417, kButtonWidth, ui.str(0x9073f), ui::return_accept);
  const SeExe& se = ui.se();
  const std::array<std::array<int, 4>, 4> fields{
      {{49, 367, 165, 380}, {49, 385, 165, 395}, {177, 367, 289, 380}, {177, 385, 289, 395}}};
  const std::array<RiseSet, 4> texts{RiseSet::kSunrise, RiseSet::kSunset, RiseSet::kMoonrise, RiseSet::kMoonset};
  for (std::size_t i = 0; i < fields.size(); ++i) {
    font1_field(ui, d, fields[i][0], fields[i][1], fields[i][2], fields[i][3],
                [this, &se, which = texts[i]] { return rise_set_text(se, which, sky_); }, TextAlign::kLeft);
  }
  // environment_idle (0x2825c)
  d.set_idle([this, &d](const Viewport& gfx, int) {
    if (form_.weather_dirty) {
      redraw_weather(d, gfx, false);
    }
    if (form_.lighting_dirty) {
      update_lighting(d, gfx, false);
    }
  });
  if (d.run() == ui::kResultCancel) {
    scenario_.sighting_light_pct = light;
    return false;
  }
  // The original then copies the preview's colours 209..250 into the DAC,
  // which the dispatcher's `palette_restore_saved(0xff)` overwrites.
  form_.commit(scenario_.header);
  scenario_.sighting_fog_pct = form_.fog;
  scenario_.update_base_owners();
  refresh_environment(scenario_);
  scenario_.update_all_visibility();
  return true;
}

// ------------------------------------------------------------ TORPEDOES

// The run-time controls of TORPEDOES (`torpedoes_add_controls`, tables
// 0x9bf30..0x9c058): the bow tubes in the right column, the stern tubes in
// the left one, and the reload rows.
constexpr int kMaxBowTubes = 6;
constexpr int kMaxSternTubes = 4;
constexpr int kMaxReloadRows = 3;
// The extended variant's list (not in the original): up to five rows,
// packed, the dialog taller by a row each past three.
constexpr int kMaxExtendedReloadRows = 5;
constexpr int kReloadRowHeight = 22;
constexpr std::array<int, 6> kTubeNumberY{80, 101, 123, 145, 167, 189};
constexpr std::uint32_t kTubeNumbers = 0x91269;  // "1", "2", .. "10", 2 bytes apart

// The extended variant's message (not in the original) when a cycle of
// TORPEDOES skipped a fourth torpedo type.
void explain_type_limit(EditorUi& ui, TorpedoForm& form) {
  if (form.take_refused_fourth()) {
    show_message_box(ui, "TORPEDOES", "A boat carries at most", "three torpedo types.", "");
  }
}

}  // namespace

bool cmd_environment(EditorUi& ui, Scenario& scenario, const EditorOptions& options) {
  EnvironmentDialog dialog(ui, scenario);
  if (!dialog.run()) {
    return false;
  }
  validate_equipment_by_date(scenario, options);
  scenario.modified = true;
  return true;
}

bool cmd_equipment(EditorUi& ui, Scenario& scenario, const EditorOptions& options, bool radar_detector) {
  const SeExe& se = ui.se();
  EquipmentForm form = EquipmentForm::unpack(scenario.header.equipment, scenario.header, options);
  // The radar detector's row below the bathythermograph's makes the dialog
  // taller, kept centred (not in the original).
  const int extra = radar_detector ? kEquipmentRowH : 0;
  ui::Dialog d(ui.gui(), 157, 109 - extra / 2, 312, 259 + extra, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(0x90742), ui::return_cancel);
  d.emplace<ui::GroupBox>(12, 48, 300, 121);
  font1_label(ui, d, 20, 62, 0x9074c);
  auto& radar = cycle(d, 153, 57, 142, ui.strings(0x9bf08, 3), nullptr);
  font1_label(ui, d, 20, 95, 0x9075b);
  auto& scope = cycle(d, 153, 90, 142, ui.strings(0x9bf1c, 3),
                      [&form, &se](bool advance) { return form.cycle_scope(se, advance); });
  // equip_surface_radar_cycle (0x29252) also redraws the scope and disables
  // it without a surface radar, on every call.
  radar.index = [&form, &se, &scope](bool advance) {
    const std::int32_t index = form.cycle_surface_radar(se, advance);
    scope.dirty = true;
    scope.disabled = form.scope_disabled();
    return index;
  };
  d.emplace<ui::GroupBox>(12, 126, 300, 165);
  font1_label(ui, d, 20, 140, 0x90762);
  cycle(d, 153, 135, 142, ui.strings(0x9bf14, 2), [&form, &se](bool advance) { return form.cycle_air_radar(se, advance); });
  d.emplace<ui::GroupBox>(12, 170, 300, 209);
  font1_label(ui, d, 20, 184, 0x9076d);
  cycle(d, 153, 179, 142, ui.strings(0x9bf28, 2),
        [&form, &se](bool advance) { return form.cycle_bathythermograph(se, advance); });
  if (radar_detector) {
    // The bathythermograph's row again, with its None / fitted labels.
    d.emplace<ui::GroupBox>(12, 170 + extra, 300, 209 + extra);
    d.emplace<ui::Label>(20, 184 + extra, std::string(own_text::kRadarDetector), ui::FontSlot::kCustom, true,
                         &ui.font1());
    cycle(d, 153, 179 + extra, 142, ui.strings(0x9bf28, 2),
          [&form, &se](bool advance) { return form.cycle_radar_detector(se, advance); });
  }
  d.emplace<ui::Button>(12, 219 + extra, kButtonWidth, ui.str(0x9077f), ui::return_cancel);
  d.emplace<ui::Button>(200, 219 + extra, kButtonWidth, ui.str(0x90786), ui::return_accept);
  if (d.run() == ui::kResultCancel) {
    return false;
  }
  scenario.header.equipment = form.pack(scenario.header.equipment);
  scenario.update_all_visibility();
  scenario.modified = true;
  return true;
}

bool cmd_torpedoes(EditorUi& ui, Scenario& scenario, const EditorOptions& options) {
  TorpedoForm form(scenario, options);
  std::vector<std::string> choices{ui.str(se_addr::kEmpty)};
  for (std::size_t c = 1; c < form.choice_count(); ++c) {
    const Torpedo* t = form.torpedo(c);
    choices.push_back(t == nullptr ? std::string() : t->long_name);
  }
  const bool extended = scenario.data().torpedo_choices.extended;
  const std::size_t max_rows = static_cast<std::size_t>(extended ? kMaxExtendedReloadRows : kMaxReloadRows);
  int grow = 0;
  if (extended) {
    int rows = 0;
    for (std::size_t c = 1; c < form.choice_count() && c <= max_rows; ++c) {
      rows += form.unavailable(c) ? 0 : 1;
    }
    grow = std::max(0, rows - kMaxReloadRows) * kReloadRowHeight;
  }
  ui::Dialog d(ui.gui(), 100, 80 - grow / 2, 419, 350 + grow, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(0x90789), ui::return_cancel);
  d.emplace<ui::GroupBox>(14, 46, 207, 300 + grow);
  d.emplace<ui::GroupBox>(211, 46, 404, 300 + grow);
  font1_label(ui, d, 100, 57, 0x90793);
  font1_label(ui, d, 278, 57, 0x90797);
  font1_label(ui, d, 72, 214, 0x9079f);
  font1_label(ui, d, 268, 214, 0x907a8);
  font1_field(ui, d, 127, 214, 147, 223, [&form] { return std::to_string(form.capacity(true)); }, TextAlign::kLeft);
  font1_field(ui, d, 324, 214, 344, 223, [&form] { return std::to_string(form.capacity(false)); }, TextAlign::kLeft);
  d.emplace<ui::Button>(14, 311 + grow, kButtonWidth, ui.str(0x907b1), ui::return_cancel);
  d.emplace<ui::Button>(302, 311 + grow, kButtonWidth, ui.str(0x907b8), ui::return_accept);
  // torpedoes_add_controls (0x297aa): tubes numbered over both columns.
  const SeExe& se = ui.se();
  const int bow = std::min(form.bow_tubes(), kMaxBowTubes);
  const int stern = std::min(form.stern_tubes(), kMaxSternTubes);
  for (int i = 0; i < bow + stern; ++i) {
    const bool aft = i >= bow;
    const int row = aft ? i - bow : i;
    const int x = aft ? 31 : 228;
    const int number_x = aft ? 17 : 214;
    const auto tube = static_cast<std::size_t>(i);
    cycle(d, x, 74 + row * 22, 169, choices, [&form, &ui, tube](bool advance) {
      const std::int32_t choice = form.cycle_tube(tube, advance);
      explain_type_limit(ui, form);
      return choice;
    });
    const std::string number(se.str(kTubeNumbers + static_cast<std::uint32_t>(i) * 2));
    font1_field(ui, d, number_x, kTubeNumberY[static_cast<std::size_t>(row)], number_x + 10,
                kTubeNumberY[static_cast<std::size_t>(row)] + 9, [number] { return number; }, TextAlign::kRight);
  }
  const std::vector<std::string> digits = ui.strings(kDigits, 10);
  int packed = 0;
  for (std::size_t c = 1; c < form.choice_count() && c <= max_rows; ++c) {
    if (form.unavailable(c)) {
      continue;
    }
    const int row = extended ? packed++ : static_cast<int>(c - 1);
    const int y = 229 + row * kReloadRowHeight;
    for (const bool aft : {true, false}) {
      const int x = aft ? 154 : 352;
      d.emplace<ui::Label>(aft ? 27 : 225, y + 4, choices[c]);
      for (const bool tens : {true, false}) {
        cycle(d, tens ? x : x + 18, y, 16, digits, [&form, &ui, aft, c, tens](bool advance) {
          const std::int32_t digit = form.cycle_reload(aft, c, tens, advance);
          explain_type_limit(ui, form);
          return digit;
        });
      }
    }
  }
  if (d.run() == ui::kResultCancel) {
    return false;
  }
  form.commit(scenario);
  scenario.modified = true;
  return true;
}

bool cmd_briefing_text(EditorUi& ui, Scenario& scenario) {
  ui::Dialog d(ui.gui(), 53, 80, 534, 310, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(0x907bb), ui::return_cancel);
  auto& box = d.emplace<ui::TextBox>(20, 50, 450, 200, 15, 1000, std::string(scn_text(scenario.header.briefing)));
  // briefing_scroll_up / _down (0x2902c / 0x29043)
  d.emplace<ui::Button>(
      490, 52, 20, ui.str(0x907c9),
      [&d, &box](int) {
        box.scroll_up(d);
        box.dirty = true;
        return 0;
      },
      0, 20);
  d.emplace<ui::Button>(
      490, 228, 20, ui.str(0x907cb),
      [&d, &box](int) {
        box.scroll_down(d);
        box.dirty = true;
        return 0;
      },
      0, 20);
  d.emplace<ui::Button>(102, 265, kButtonWidth, ui.str(0x907cd), ui::return_cancel);
  d.emplace<ui::Button>(322, 265, kButtonWidth, ui.str(0x907d4), ui::return_accept);
  d.emplace<ui::GroupBox>(488, 80, 511, 223);
  if (d.run() == ui::kResultCancel) {
    return false;
  }
  set_scn_text(scenario.header.briefing, box.text);
  scenario.modified = true;
  return true;
}

bool cmd_options(EditorUi& ui, Scenario& scenario, EditorOptions& options, const SaveDir& saves,
                 bool report_write_error) {
  // The working copy 0xa6728..0xa6748; a check box flips its value with ~.
  EditorOptions work = options;
  work.end_on_lost_contact = scenario.header.end_on_lost_contact;
  work.randomize = scenario.header.randomize;
  const auto toggle = [](std::int32_t& value) {
    return [&value](bool flip) {
      if (flip) {
        value = ~value;
      }
      return value != 0;
    };
  };
  ui::Dialog d(ui.gui(), 157, 52, 321, 359, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(0x907d7), ui::return_cancel);
  d.emplace<ui::GroupBox>(17, 44, 303, 193);
  d.emplace<ui::CheckBox>(54, 70, ui.str(0x907df), toggle(work.limit_ships));
  d.emplace<ui::CheckBox>(54, 93, ui.str(0x907e5), toggle(work.limit_subs));
  d.emplace<ui::CheckBox>(54, 116, ui.str(0x907ea), toggle(work.limit_aircraft));
  d.emplace<ui::CheckBox>(54, 139, ui.str(0x907f3), toggle(work.limit_patrol_areas));
  d.emplace<ui::CheckBox>(54, 162, ui.str(0x90800), toggle(work.limit_equipment));
  font1_label(ui, d, 116, 54, 0x9080a);
  d.emplace<ui::GroupBox>(17, 195, 303, 253);
  d.emplace<ui::CheckBox>(54, 203, ui.str(0x90819), toggle(work.range_circles));
  d.emplace<ui::CheckBox>(54, 226, ui.str(0x9082f), toggle(work.convoy_icons));
  d.emplace<ui::GroupBox>(17, 255, 303, 313);
  d.emplace<ui::CheckBox>(54, 263, ui.str(0x90844), toggle(work.end_on_lost_contact));
  d.emplace<ui::CheckBox>(54, 286, ui.str(0x9085a), toggle(work.randomize));
  d.emplace<ui::Button>(19, 320, kButtonWidth, ui.str(0x90864), ui::return_cancel);
  d.emplace<ui::Button>(202, 320, kButtonWidth, ui.str(0x9086b), ui::return_accept);
  if (d.run() == ui::kResultCancel) {
    return false;
  }
  if (scenario.header.end_on_lost_contact != work.end_on_lost_contact ||
      scenario.header.randomize != work.randomize) {
    scenario.modified = true;
  }
  const bool equipment_limited = work.limit_equipment != options.limit_equipment && work.limit_equipment != 0;
  options = work;
  scenario.header.end_on_lost_contact = work.end_on_lost_contact;
  scenario.header.randomize = work.randomize;
  // As the original, a file that cannot be written goes unreported, but
  // with the corrections (SE's message of a failed scenario save; not in
  // the original).
  if (!options.save(saves) && report_write_error) {
    show_message_box(ui, ui.str(kWriteFailedTitle), ui.str(kWriteFailed),
                     std::string("SAVE\\") + std::string(EditorOptions::kFileName), "");
  }
  if (equipment_limited) {
    validate_equipment_by_date(scenario, options);
  }
  return true;
}

namespace {

// The PASSWORD dialog (0xa98b8) in SET (`enter` false) or ENTER mode
// (0xa6d2c), on `entry`. Returns the dialog result.
int run_password_dialog(EditorUi& ui, std::string& entry, bool enter) {
  ui::Dialog d(ui.gui(), 163, 166, 313, 147, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(enter ? 0x918ec : 0x91848), ui::return_cancel);
  // password_entry_on_done (0x2b552): spaces trimmed at both ends; Enter
  // ends the dialog in ENTER mode only, and not on an empty entry.
  auto& field = d.emplace<ui::TextEntry>(36, 76, 241, 15, entry, [enter](std::string& text, int) {
    const std::size_t first = text.find_first_not_of(' ');
    text.erase(0, first == std::string::npos ? text.size() : first);
    while (!text.empty() && text.back() == ' ') {
      text.pop_back();
    }
    if (enter && text.empty()) {
      return 0;
    }
    return enter ? 1 : 0;
  });
  d.emplace<ui::Button>(35, 110, kButtonWidth, ui.str(0x9092b), ui::return_cancel);
  // password_accept (0x2b5ca)
  d.emplace<ui::Button>(177, 110, kButtonWidth, ui.str(0x90932), [enter, &field](int) {
    return enter && field.text.empty() ? 0 : ui::kResultAccept;
  });
  font1_label(ui, d, 57, 54, enter ? 0x918fb : 0x91855);
  const int result = d.run();
  entry = field.text;
  return result;
}

std::string upper(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}

}  // namespace

bool cmd_set_password(EditorUi& ui, Scenario& scenario) {
  const ScnPasswordKey key = read_scn_password_key(ui.se().image());
  std::string entry = decode_scn_password(scenario.header.password, key);
  const std::string old = entry;
  if (run_password_dialog(ui, entry, false) == ui::kResultCancel || entry == old) {
    return false;
  }
  if (entry.empty()) {
    // Original bug (docs/editor-design.md "Password storage", fixed): SE copies the
    // empty entry with strcpy, which zeroes the first byte only, so the
    // other 14 kept the old password's encoded bytes (saved in the file).
    scenario.header.password = encode_scn_password("", key);
  } else {
    const std::string line1 = upper(ui.str(0x9187e) + entry);
    if (ask_yes_no(ui, ui.str(0x918d8), line1, ui.str(0x918b6), ui.str(0x9189f), ui.str(se_addr::kYes),
                   ui.str(se_addr::kNo)) == ui::kResultCancel) {
      return false;
    }
    scenario.header.password = encode_scn_password(entry, key);
  }
  scenario.modified = true;
  return true;
}

bool ask_password(EditorUi& ui, const ScnHeader& header) {
  if (header.password[0] == 0) {
    return true;
  }
  const std::string password = upper(decode_scn_password(header.password, read_scn_password_key(ui.se().image())));
  for (int tries = 3; tries > 0; --tries) {
    std::string entry;
    if (run_password_dialog(ui, entry, true) == ui::kResultCancel) {
      return false;
    }
    if (upper(entry) == password) {
      return true;
    }
    show_message_box(ui, ui.str(0x9193d), ui.str(0x9192e), ui.str(0x91918), ui.str(0x912cf));
  }
  return false;
}

}  // namespace sh::editor

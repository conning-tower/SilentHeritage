// `realism_menu` (0xa2863): the REALISM dialog 0x1879c0.

#include <array>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>

#include "assets/exe_image.h"
#include "game/frontend.h"
#include "game/mission_rules.h"
#include "game/options_realism.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// SH.EXE addresses.
constexpr std::uint32_t kMenuPcx = 0x110940;       // "M8.PCX"
constexpr std::uint32_t kTitle = 0xfbddb;          // "REALISM"
constexpr std::uint32_t kRatingLabel = 0xfbe9b;    // "REALISM RATING:"
constexpr std::uint32_t kPercent = 0xfc26c;        // "%"
constexpr std::uint32_t kCombatLevels = 0x1109d8;  // char*[4]
constexpr std::uint32_t kSailingModels = 0x1109e8;  // char*[3]
// The check box labels, in the order of `kRealismToggles`.
constexpr std::array<std::uint32_t, 10> kToggleLabels{0xfbdfe, 0xfbe0b, 0xfbe1b, 0xfbe28, 0xfbe3b,
                                                      0xfbe49, 0xfbe5b, 0xfbe6e, 0xfbe7e, 0xfbe8f};
// The extended variant's check boxes after the original's (not in the
// original); the rows then come closer to make room for them.
constexpr std::string_view kUndeafenedLabel = "UNDEAFENED ESCORTS";
constexpr std::string_view kDetectableRadarLabel = "DETECTABLE RADAR";
constexpr int kRowSpacing = 36;
constexpr int kExtendedRowSpacing = 30;

}  // namespace

int FrontEnd::realism_menu(bool load_and_save, bool cancel_disabled) {
  bool rating_dirty = false;  // 0x187238
  if (load_and_save) {
    RealismFile::load(saves_, options_);
    realism_rules_ = mission_rules();
  }
  const bool extended_items = extended_config_;
  const auto rate = [this, extended_items] {
    return realism_rating(options_, extended_items ? realism_rules_ : MissionRules{});
  };
  std::int32_t rating = rate();  // 0x18723c
  load_screen_pcx(exe_string(kMenuPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);

  ui::Dialog d(gui_, 52, 10, 534, 465, ui::kDialogTitled | ui::kDialogFramed);
  // realism_draw_rating_box (0xa28f1): the frame around the rating, its
  // inside cleared.
  d.emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    menu_skin_.draw_frame(gfx, 0x11e, 0x1a0, 0x39, 0x1f);
    ui::fill_rect(gfx, 0x124, 0x1a5, 0x150, 0x1b8, 0);
  });
  d.emplace<ui::TitleBar>(exe_string(kTitle));
  constexpr std::array<std::tuple<int, std::uint32_t, RealismPreset>, 3> kPresets{{
      {27, 0xfbde3, RealismPreset::kNovice},
      {205, 0xfbdea, RealismPreset::kIntermediate},
      {382, 0xfbdf7, RealismPreset::kExpert},
  }};
  for (const auto& [x, label, preset] : kPresets) {
    d.emplace<ui::Button>(x, 125, 121, exe_string(label), [&d, &rating_dirty, this, preset, extended_items](int) {
      rating_dirty = true;
      options_ = apply_realism_preset(options_, preset);
      if (extended_items) {
        realism_rules_ = apply_realism_preset_rules(realism_rules_, preset);
      }
      d.invalidate();
      return 0;
    });
  }
  for (std::size_t i = 0; i < std::size(kRealismToggles); ++i) {
    const RealismToggle toggle = kRealismToggles[i];
    const int x = i % 2 == 0 ? 28 : 271;
    const int y = 165 + static_cast<int>(i / 2) * (extended_items ? kExtendedRowSpacing : kRowSpacing);
    d.emplace<ui::CheckBox>(x, y, exe_string(kToggleLabels[i]), [this, toggle, &rating_dirty](bool flip) {
      if (flip) {
        options_ ^= toggle.bit;
        rating_dirty = true;
      }
      return toggle.checked(options_);
    });
  }
  if (extended_items) {
    const int y = 165 + static_cast<int>(std::size(kRealismToggles) / 2) * kExtendedRowSpacing;
    const auto item = [this, &rating_dirty](bool MissionRules::* member) {
      return [this, &rating_dirty, member](bool flip) {
        if (flip) {
          realism_rules_.*member = !(realism_rules_.*member);
          rating_dirty = true;
        }
        return realism_rules_.*member;
      };
    };
    d.emplace<ui::CheckBox>(28, y, std::string(kUndeafenedLabel), item(&MissionRules::keen_escort_sonar));
    d.emplace<ui::CheckBox>(271, y, std::string(kDetectableRadarLabel), item(&MissionRules::detectable_radar));
  }
  d.emplace<ui::Label>(180, 425, exe_string(kRatingLabel), ui::FontSlot::kFont1, false);
  auto& rating_field = d.emplace<ui::TextField>(
      292, 426, 336, 440,
      [this, &rating] { return std::format("{}{}", realism_rating_percent(rating), exe_string(kPercent)); },
      ui::FontSlot::kLarge, TextAlign::kCentre);
  rating_field.fill = 0;
  d.emplace<ui::CycleButton>(29, 349, 231, exe_strings(kCombatLevels, 4), [this, &rating_dirty](bool advance) {
    if (advance) {
      options_ = cycle_combat_level(options_);
      rating_dirty = true;
    }
    return combat_level(options_);
  });
  d.emplace<ui::CycleButton>(272, 349, 231, exe_strings(kSailingModels, 3), [this, &rating_dirty](bool advance) {
    if (advance) {
      options_ = cycle_sailing_model(options_);
      rating_dirty = true;
    }
    return sailing_model(options_);
  });
  auto& cancel = d.emplace<ui::Button>(40, 415, 121, exe_string(0xfbeab), ui::return_cancel);
  cancel.disabled = cancel_disabled;
  d.set_back(cancel);
  d.emplace<ui::Button>(367, 415, 121, exe_string(0xfbeb2), ui::return_accept);
  d.emplace<ui::GroupBox>(21, 117, 512, 390);
  add_exit_area(d);
  // realism_dialog_draw (0xa294f)
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
    if (rating_dirty) {
      rating_dirty = false;
      rating = rate();
      rating_field.dirty = true;
    }
  });
  const int result = run_menu_dialog(d);
  if (result == ui::kResultAccept && load_and_save) {
    RealismFile::save(saves_, options_);
    if (extended_items) {
      save_mission_rules(saves_, realism_rules_);
    }
  }
  return result;
}

}  // namespace sh

// The main menu's EXTENDED OPTIONS page (not in the original), in the look
// of the REALISM dialog.

#include <string>
#include <string_view>

#include "game/extended_config.h"
#include "game/frontend.h"
#include "ui/controls.h"

namespace sh {

namespace {

constexpr std::uint32_t kMenuPcx = 0x110940;  // "M8.PCX"
constexpr std::string_view kTitle = "EXTENDED OPTIONS";
constexpr std::string_view kNote = "THE INTERFACE CHANGES AT ONCE";
constexpr std::string_view kCancel = "CANCEL";
constexpr std::string_view kOriginal = "ORIGINAL";
constexpr std::string_view kOk = "OK";

}  // namespace

void FrontEnd::extended_options_screen() {
  load_screen_pcx(exe_string(kMenuPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);
  Features edited = features_;
  ui::Dialog d(gui_, 52, 10, 534, 465, ui::kDialogTitled | ui::kDialogFramed);
  d.emplace<ui::TitleBar>(std::string(kTitle));
  for (std::size_t i = 0; i < kFeatureOptions.size(); ++i) {
    const FeatureOption& o = kFeatureOptions[i];
    const int x = i % 2 == 0 ? 28 : 271;
    const int y = 125 + static_cast<int>(i / 2) * 36;
    d.emplace<ui::CheckBox>(x, y, std::string(o.label), [&edited, &o](bool flip) {
      if (flip) {
        edited.*o.member = !(edited.*o.member);
      }
      return edited.*o.member;
    });
  }
  d.emplace<ui::TextField>(22, 392, 511, 408, [] { return std::string(kNote); }, ui::FontSlot::kFont1,
                           TextAlign::kCentre);
  d.set_back(d.emplace<ui::Button>(40, 415, 121, std::string(kCancel), ui::return_cancel));
  // ORIGINAL: every improvement off, the original's interface.
  d.emplace<ui::Button>(205, 415, 121, std::string(kOriginal), [&edited, &d](int) {
    for (const FeatureOption& o : kFeatureOptions) {
      edited.*o.member = false;
    }
    d.invalidate();
    return 0;
  });
  d.emplace<ui::Button>(367, 415, 121, std::string(kOk), ui::return_accept);
  d.emplace<ui::GroupBox>(21, 117, 512, 390);
  add_exit_area(d);
  d.set_idle([this](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
  });
  if (run_menu_dialog(d) == ui::kResultAccept && edited != features_) {
    features_ = edited;
    save_features(saves_, features_);
  }
}

}  // namespace sh

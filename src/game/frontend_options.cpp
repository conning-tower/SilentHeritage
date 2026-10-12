// The main menu's EXTENDED OPTIONS page (not in the original), in the look
// of the REALISM dialog: the improvements of `kFeatureOptions` in an
// INTERFACE group, then the window's settings in a DISPLAY group (when the
// host has them), two controls per row.

#include <optional>
#include <string>
#include <string_view>

#include "game/extended_config.h"
#include "game/frontend.h"
#include "game/options_controls.h"
#include "ui/controls.h"

namespace sh {

namespace {

constexpr std::uint32_t kMenuPcx = 0x110940;  // "M8.PCX"
constexpr std::string_view kTitle = "EXTENDED OPTIONS";
constexpr std::string_view kInterface = "INTERFACE";
constexpr std::string_view kDisplay = "DISPLAY";
constexpr std::string_view kFullscreen = "FULLSCREEN";
constexpr std::string_view kSmoothing = "SMOOTHING";
constexpr std::string_view kNote = "THE INTERFACE CHANGES AT ONCE";
constexpr std::string_view kCancel = "CANCEL";
constexpr std::string_view kOriginal = "ORIGINAL";
constexpr std::string_view kOk = "OK";

// The groups: centred between `kGroupsTop` and `kGroupsBottom`, 9 pixels
// apart, the controls in two columns (the REALISM dialog's).
constexpr int kGroupsTop = 117;
constexpr int kGroupsBottom = 390;
constexpr int kGroupGap = 9;
constexpr int kColumnX[2] = {28, 271};
// A cycle button: as wide as the REALISM dialog's.
constexpr int kCycleWidth = 231;
// A group: its heading, then its rows from `kRowsTop` below its top.
constexpr int kHeadingTop = 6;
constexpr int kRowsTop = 26;
constexpr int kRowHeight = 36;
constexpr int kDisplayItems = 2;

constexpr int rows(int items) { return (items + 1) / 2; }
constexpr int group_height(int items) { return kRowsTop + rows(items) * kRowHeight + 2; }

static_assert(group_height(static_cast<int>(kFeatureOptions.size())) + kGroupGap + group_height(kDisplayItems) <=
                  kGroupsBottom - kGroupsTop,
              "the EXTENDED OPTIONS page is full: its items need another page");

// Returns the place of item `i` of a group whose top is `top`.
constexpr int item_x(std::size_t i) { return kColumnX[i % 2]; }
constexpr int item_y(int top, std::size_t i) { return top + kRowsTop + static_cast<int>(i / 2) * kRowHeight; }

// Adds the frame and heading of a group from `top` to `bottom`.
void add_group(ui::Dialog& d, int top, int bottom, std::string_view heading) {
  d.emplace<ui::TextField>(22, top + kHeadingTop, 511, top + kHeadingTop + 16,
                           [heading] { return std::string(heading); }, ui::FontSlot::kFont1, TextAlign::kCentre);
  d.emplace<ui::GroupBox>(21, top, 512, bottom);
}

}  // namespace

void FrontEnd::extended_options_screen() {
  load_screen_pcx(exe_string(kMenuPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);
  Features edited = features_;
  const std::optional<DisplayOptions> display = host_.display_options();
  DisplaySettings edited_display = display ? display->settings : DisplaySettings{};
  ui::Dialog d(gui_, 52, 10, 534, 465, ui::kDialogTitled | ui::kDialogFramed);
  d.emplace<ui::TitleBar>(std::string(kTitle));
  // The INTERFACE group, then the DISPLAY group.
  constexpr int interface_height = group_height(static_cast<int>(kFeatureOptions.size()));
  const int block = interface_height + (display ? kGroupGap + group_height(kDisplayItems) : 0);
  const int interface_top = kGroupsTop + (kGroupsBottom - kGroupsTop - block) / 2;
  const int display_top = interface_top + interface_height + kGroupGap;
  for (std::size_t i = 0; i < kFeatureOptions.size(); ++i) {
    add_feature_control(d, item_x(i), item_y(interface_top, i), kCycleWidth, kFeatureOptions[i], edited, false);
  }
  if (display) {
    add_setting_check_box(d, item_x(0), item_y(display_top, 0), kFullscreen, edited_display.fullscreen,
                          display->fullscreen_fixed ? std::optional<bool>(true) : std::nullopt);
    add_setting_check_box(d, item_x(1), item_y(display_top, 1), kSmoothing, edited_display.smoothing,
                          display->smoothing_fixed ? std::optional<bool>(false) : std::nullopt);
  }
  d.emplace<ui::TextField>(22, 392, 511, 408, [] { return std::string(kNote); }, ui::FontSlot::kFont1,
                           TextAlign::kCentre);
  d.set_back(d.emplace<ui::Button>(40, 415, 121, std::string(kCancel), ui::return_cancel));
  // ORIGINAL: every improvement as in the original's interface; the
  // window's settings stay.
  d.emplace<ui::Button>(205, 415, 121, std::string(kOriginal), [&edited, &d](int) {
    for (const FeatureOption& o : kFeatureOptions) {
      o.set(edited, o.get(Features::classic()));
    }
    d.invalidate();
    return 0;
  });
  d.emplace<ui::Button>(367, 415, 121, std::string(kOk), ui::return_accept);
  add_group(d, interface_top, interface_top + interface_height, kInterface);
  if (display) {
    add_group(d, display_top, display_top + group_height(kDisplayItems), kDisplay);
  }
  add_exit_area(d);
  d.set_idle([this](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
  });
  if (run_menu_dialog(d) != ui::kResultAccept) {
    return;
  }
  if (edited != features_) {
    features_ = edited;
    save_features(saves_, features_);
  }
  if (display && edited_display != display->settings) {
    host_.set_display_settings(edited_display);
  }
}

}  // namespace sh

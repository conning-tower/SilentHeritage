// The front end as the career screens see it: the screen, the GUI, the
// files and the shared helpers of `FrontEnd` (palette fades, the MENU
// dialogs, the message boxes, the SH.EXE strings). The career screens are
// free functions over a `CareerUi`, like the original's career modules
// calling the wscreens helpers.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "assets/pal.h"
#include "core/image.h"
#include "core/random.h"
#include "game/frontend.h"
#include "ui/dialog.h"

namespace sh {

class CareerUi {
 public:
  // `front_end` must outlive the context.
  explicit CareerUi(FrontEnd& front_end) : fe_(front_end) {}

  // The files and services.
  const Install& install() const { return fe_.install_; }
  const ShPaths& paths() const { return fe_.paths_; }
  const ExeImage& exe() const { return fe_.exe_; }
  const SaveDir& saves() const { return fe_.saves_; }
  FrontendHost& host() { return fe_.host_; }
  MissionRunner& runner() { return fe_.runner_; }

  // The screen 0x11a574, the screen palette 0x11a274 and the DAC.
  Image8& screen() { return fe_.screen_; }
  VgaPalette& palette() { return fe_.palette_; }
  VgaPalette& dac() { return fe_.dac_; }
  ui::Gui& gui() { return fe_.gui_; }
  const ui::GuiResources& gui_resources() const { return fe_.gui_res_; }
  const ui::MenuResources& menu_resources() const { return fe_.menu_res_; }
  FrontendMenuSkin& menu_skin() { return fe_.menu_skin_; }
  ui::DefaultSkin& default_skin() { return fe_.default_skin_; }
  const Shp& cursors() const { return fe_.cursors_; }
  WatcomRandom& random() { return fe_.random_; }
  // The options word 0x11a85c and the game options.
  std::uint32_t& options() { return fe_.options_; }
  const GameOptions& game_options() const { return fe_.game_options_; }

  // Where the player is, for the resume snapshot (`FrontEnd::location`).
  void set_location(const FrontEndLocation& location) { fe_.location_ = location; }

  // The helpers (see `FrontEnd`).
  void frame() { fe_.frame(); }
  void wait_clock(std::uint32_t ticks) { fe_.wait_clock(ticks); }
  void wait_for_press() { fe_.wait_for_press(); }
  void palette_fade_in(const VgaPalette& target, std::uint32_t delay, int step) {
    fe_.palette_fade_in(target, delay, step);
  }
  void palette_fade_out(std::uint32_t delay, int step) { fe_.palette_fade_out(delay, step); }
  void palette_grey_out(int percent, int last) { fe_.palette_grey_out(percent, last); }
  void palette_restore_saved(int last) { fe_.palette_restore_saved(last); }
  // Loads ART\<name> into the screen and its palette into `palette()`.
  void load_screen_pcx(std::string_view name) { fe_.load_screen_pcx(name); }
  std::string exe_string(std::uint32_t address) const { return fe_.exe_string(address); }
  std::vector<std::string> exe_strings(std::uint32_t address, int count) const {
    return fe_.exe_strings(address, count);
  }
  void add_exit_area(ui::Dialog& dialog) { fe_.add_exit_area(dialog); }
  int run_menu_dialog(ui::Dialog& dialog) { return fe_.run_menu_dialog(dialog); }
  int realism_menu(bool load_and_save, bool cancel_disabled) { return fe_.realism_menu(load_and_save, cancel_disabled); }
  // The extended variant's rules (not in the original): whether careers
  // keep rules (`FrontendServices::extended_config`), the rules of a game
  // begun now and those the REALISM dialog edits.
  bool extended_config() const { return fe_.extended_config_; }
  MissionRules mission_rules() const { return fe_.mission_rules(); }
  MissionRules& realism_rules() { return fe_.realism_rules_; }
  void show_message_box(std::string_view title, std::string_view line1, std::string_view line2) {
    fe_.show_message_box(title, line1, line2);
  }
  int ask_yes_no(std::string_view title, std::string_view line1, std::string_view line2, std::string_view yes,
                 std::string_view no) {
    return fe_.ask_yes_no(title, line1, line2, yes, no);
  }
  bool play_cinematic(std::string_view name, int x = -1, int y = -1) { return fe_.play_cinematic(name, x, y); }
  void show_fate_newspaper(int kind, const MissionOutcome& outcome) { fe_.show_fate_newspaper(kind, outcome); }
  void show_towed_out_of_fuel(const MissionOutcome& outcome) { fe_.show_towed_out_of_fuel(outcome); }
  void show_port_return(const MissionOutcome& outcome) { fe_.show_port_return(outcome); }
  std::string high_score_enter_name() { return fe_.high_score_enter_name(); }
  void show_port_departure(const std::string& profile_sprite) { fe_.show_port_departure(profile_sprite); }
  void show_mission_loading() { fe_.show_mission_loading(); }
  MissionResults mission_end_screens(const MissionOutcome& outcome, std::uint32_t options) {
    return fe_.mission_end_screens(outcome, options);
  }

 private:
  FrontEnd& fe_;
};

}  // namespace sh

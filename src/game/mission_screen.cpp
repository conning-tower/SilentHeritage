#include "game/mission_screen.h"

#include <string>
#include <utility>
#include <vector>

#include "assets/patrol_zones.h"
#include "game/stations/cabin.h"
#include "game/stations/chart.h"
#include "game/stations/conn.h"
#include "game/stations/idbook.h"
#include "game/stations/nav_panel.h"
#include "game/stations/status.h"
#include "ui/controls.h"

namespace sh {

namespace {

// The GAME OVER texts (char*[10] at 0x10e9c4, by `GameOverReason`).
constexpr std::uint32_t kGameOverTexts = 0x10e9c4;
constexpr std::uint32_t kGameOverTitle = 0xf002a;  // "GAME OVER"
// `show_game_over` waits 0x36 clock ticks after the fade.
constexpr std::int32_t kGameOverWait = 0x36;

// Returns the name of the command the mission's radio messages come from.
std::string radio_sender(const Install& install, const ExeImage& exe, const Mission& mission) {
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "DATA/PZ.DAT");
  return command_name(exe, command_of_map(zones, mission.scenario().header.world));
}

// Returns the retraces (70 Hz) in a frame of `ticks` 100 Hz ticks, at
// least one.
std::int32_t retraces(std::int32_t ticks) { return std::max<std::int32_t>(1, (ticks * 7 + 5) / 10); }

}  // namespace

MissionScreen::MissionScreen(MissionScreenSetup setup, std::unique_ptr<Mission> mission)
    : setup_(setup),
      mission_(std::move(mission)),
      dive_recorder_(mission_->player()),
      radio_(*mission_, radio_sender(setup.install, setup.exe, *mission_)),
      assets_(StationAssets::load(setup.install)),
      sound_(setup.sound, setup.install, setup.paths, setup.options, setup.sound_lock),
      env_{setup.install, setup.paths,   setup.exe,  setup.tables,  *mission_, setup.gui, palette_,
           dac_,          stations_,     bar_,       sound_,        *this,     ui_,       setup.options,
           setup.detail,  assets_},
      bar_(env_),
      attack_host_(env_),
      loop_(*mission_, stations_,
            MissionLoopHooks{*this, *this, bar_, *this, this, [this](const Station& next) { fit_screen(next); }}) {
  // The UI part of `init_simulation` (0x4f078).
  ui_.debug = setup.debug;
  ui_.features = setup.features;
  stations_.select(StationId::kConn);
  install(StationId::kConn, std::make_unique<ConnStation>(env_));
  install(StationId::kChart, std::make_unique<ChartStation>(env_));
  mission_->add_frame_task(dive_recorder_);
  mission_->add_frame_task(radio_);
  install(StationId::kGauges, std::make_unique<GaugesStation>(env_, dive_recorder_));
  install(StationId::kStatus, std::make_unique<StatusStation>(env_));
  install(StationId::kCabin, std::make_unique<CabinStation>(env_));
  attack_host_.nav_panels = [this](PanelSite site) -> std::unique_ptr<StationPanel> {
    switch (site) {
      case PanelSite::kBridge:
        return make_nav_panel(env_, NavPanelSite::kBridge);
      case PanelSite::kScope:
        return make_nav_panel(env_, NavPanelSite::kScope);
      case PanelSite::kTbt:
        return make_nav_panel(env_, NavPanelSite::kTbt);
    }
    return nullptr;
  };
  attack_host_.id_books = [this](PanelSite site) { return make_id_book_panel(env_, site); };
  bar_.reset_log();
  bar_.auto_rig_for_red(true);
  sound_.apply_options();
}

int MissionScreen::window_width() const {
  return ui_.features.wide_screen && setup_.screen_width ? setup_.screen_width() : kScreenWidth;
}

MissionScreen::Layout MissionScreen::layout_for(const Station& station) const {
  const int window = window_width();
  const int width = station.wide_layout() ? window : kScreenWidth;
  // The command bar across a wide window over a 640-wide station.
  return {.width = width, .bar_offset = width == kScreenWidth ? (window - kScreenWidth) / 2 : 0};
}

void MissionScreen::fit_screen(const Station& next) {
  const Layout layout = layout_for(next);
  const int width = layout.width;
  const int window = window_width();
  setup_.gui.set_screen_width(width);
  ui_.bar_offset = layout.bar_offset;
  if (ui_.bar_offset > 0) {
    ui_.bar_strip = Image8(window, kScreenHeight - kCommandBarTop, 0);
    setup_.gui.mouse().set_band(ClipRect{-ui_.bar_offset, kCommandBarTop, window - ui_.bar_offset - 1, kScreenHeight - 1});
  } else {
    ui_.bar_strip = Image8();
    setup_.gui.mouse().set_band(std::nullopt);
  }
  // Beside a 640-wide station: the brass, the wood for the TDC; a station
  // may set pictures of its own when it opens.
  if (ui_.features.wide_screen) {
    ui_.side_fill = SideFill();
    ui_.side_fill.kind = &next == stations_.get(StationId::kTdc) ? SideFill::Kind::kWood : SideFill::Kind::kBrass;
    ++ui_.side_fill_changes;
  }
}

MissionScreen::~MissionScreen() {
  // Modal dialogs and the open station hold hotspots in the shared GUI.
  modals_.clear();
  // The stations go before the environment, bar and sound they use (the
  // chart's destructor clears `env_.panels`).
  cabin_ = nullptr;
  for (std::size_t i = 0; i < kStationCount; ++i) {
    stations_.install(static_cast<StationId>(i), nullptr);
  }
  // The front end's screen.
  setup_.gui.set_screen_width(kScreenWidth);
  setup_.gui.mouse().set_band(std::nullopt);
}

void MissionScreen::install(StationId id, std::unique_ptr<Station> station) {
  if (id == StationId::kCabin) {
    cabin_ = dynamic_cast<CabinStation*>(station.get());
  }
  stations_.install(id, std::move(station));
}

void MissionScreen::play_sfx_looped(std::int32_t sfx) { sound_.play_sfx(sfx, 1, -1); }

bool MissionScreen::frame(std::int32_t timer_ticks) {
  ui::Gui& gui = setup_.gui;
  gui.mouse().erase(gui.screen());
  if (dac_.fading()) {
    // The original's fades hold the game.
    for (std::int32_t i = retraces(timer_ticks); i > 0 && dac_.fading(); --i) {
      dac_.step();
    }
  } else if (game_over_wait_) {
    *game_over_wait_ -= timer_ticks;
    if (*game_over_wait_ <= 0) {
      game_over_wait_.reset();
    }
  } else if (!modals_.empty()) {
    step_modal();
  } else if (loop_running_) {
    if (ui_.pause == 0) {
      for (std::int32_t& t : ui_.timers) {
        t += timer_ticks;
      }
    }
    mission_->set_paused(ui_.pause > 0);
    if (const Station* open = loop_.open_station();
        open != nullptr && layout_for(*open) != Layout{gui.screen().width(), ui_.bar_offset}) {
      // The window changed its aspect, or the wide screen was turned on or
      // off: laid out again.
      stations_.request_reopen();
    }
    // mission_main_loop's step 1.
    shake_.update(env_.timer(GameTimer::kScreenShake));
    mission_->sim().conditions().screen_shaking = shake_.active();
    loop_running_ = loop_.frame(timer_ticks);
    ui_.redraw = false;
    if (loop_running_ && !mission_->paused()) {
      // `player_update` runs the light check every frame.
      bar_.auto_rig_for_red(false);
      engine_sounds_.update(mission_->player(), stations_.get(stations_.current()), setup_.options.engine_sounds != 0,
                            ui_.quiet_order);
    }
  } else {
    return false;
  }
  mission_->set_paused(ui_.pause > 0);
  gui.mouse().draw(gui.screen());
  return true;
}

void MissionScreen::show(GameOverReason reason) {
  // show_game_over (0x10358): the GAME OVER box (`show_text_popup`, dialog
  // 0x120514, drawn onto the screen), then the fade to blue. The original
  // first waits for a running screen shake to end; it ends at once here.
  shake_.finish();
  if (env_.panels != nullptr) {
    env_.panels->suspend_panels();
  }
  setup_.gui.mouse().hide();
  const std::string text(setup_.exe.c_string(
      setup_.exe.u32(kGameOverTexts + 4 * static_cast<std::uint32_t>(static_cast<std::int32_t>(reason)))));
  ui::Dialog popup(setup_.gui, 0x99, 0xc4, 0x146, 0x50, ui::kDialogFramed, &setup_.gui.screen());
  popup.move_by((setup_.gui.screen().width() - kScreenWidth) / 2, 0);  // centred on a wide screen
  popup.emplace<ui::TitleBar>(std::string(setup_.exe.c_string(kGameOverTitle)));
  auto& field = popup.emplace<ui::TextField>(9, 0x2f, 0x13c, 0x3a, [text] { return text; }, ui::FontSlot::kSmall,
                                             TextAlign::kCentre);
  field.use_colours = false;
  field.fill = 0;
  popup.open();
  popup.process();
  popup.close();
  sound_.stop_all();
  dac_.fade_to_blue();
  game_over_wait_ = kGameOverWait;
}

void MissionScreen::on_events(const MissionEvents& events) {
  // The helm's replies come before their crew messages, as in the boat's
  // update.
  for (const std::string& file : events.speech) {
    sound_.play_speech(file);
  }
  bar_.post(events.crew_messages);
  if (events.screen_shake || events.ran_aground) {
    shake_.trigger();
  }
  for (const MissionEvents::Sfx& s : events.sfx) {
    sound_.play_sfx(s.id, s.volume);
  }
  // `player_log_event(class, -1)`: the sinkings go to the patrol log.
  const PlayerSub& player = mission_->player();
  for (const ShipClass* sunk : events.ships_sunk) {
    if (sunk != nullptr && cabin_ != nullptr) {
      cabin_->logs().patrol.push_back(sinking_log_entry(*sunk, mission_->sim().clock(), mission_->world(),
                                                        player.position().x, player.position().z));
    }
  }
  for (std::int32_t i = 0; i < events.pilots_rescued && cabin_ != nullptr; ++i) {
    cabin_->logs().patrol.push_back(
        rescue_log_entry(mission_->sim().clock(), mission_->world(), player.position().x, player.position().z));
  }
  for (const std::int32_t type : events.photo_recons) {
    if (cabin_ != nullptr) {
      cabin_->logs().patrol.push_back(photo_recon_log_entry(mission_->sim().clock(), mission_->world(),
                                                            player.position().x, player.position().z, type,
                                                            cabin_->location_types()));
    }
  }
  for (RadioMessage& m : radio_.take_delivered()) {
    if (cabin_ != nullptr) {
      cabin_->logs().radio.push_back(std::move(m));
    }
  }
}

void MissionScreen::run_modal(std::unique_ptr<ui::Dialog> dialog, ModalFrame frame, std::function<void(int)> done) {
  modals_.push_back({std::move(dialog), frame, std::move(done), false});
}

void MissionScreen::begin_modal(Modal& m) {
  // The menus are placed for the 640-wide screen: centred on a wider one.
  m.dialog->move_by((setup_.gui.screen().width() - kScreenWidth) / 2, 0);
  ++ui_.pause;
  if (m.frame == ModalFrame::kMenu) {
    sound_.stop_all();
    if (env_.panels != nullptr) {
      env_.panels->suspend_panels();
    }
  }
  if (m.frame != ModalFrame::kPlain) {
    dac_.grey_out(0x46, 0xff);
  }
  m.started = true;
}

void MissionScreen::end_modal(ModalFrame frame) {
  if (frame == ModalFrame::kGrey || (frame == ModalFrame::kMenu && mission_->end_code() == 0)) {
    dac_.restore_saved(0xff);
  }
  if (frame == ModalFrame::kMenu) {
    if (env_.panels != nullptr && mission_->end_code() == 0) {
      env_.panels->resume_panels();
    }
    sound_.apply_options();
    engine_sounds_.refresh();
    ui_.redraw = true;
  }
  --ui_.pause;
}

void MissionScreen::step_modal() {
  Modal& m = modals_.front();
  int result = ui::kResultContinue;
  if (!m.started) {
    begin_modal(m);
    result = m.dialog->run_begin();
  }
  if (result == ui::kResultContinue) {
    result = m.dialog->run_step();
  }
  if (result == ui::kResultContinue) {
    return;
  }
  Modal finished = std::move(modals_.front());
  modals_.pop_front();
  if (finished.done) {
    finished.done(result);
  }
  end_modal(finished.frame);
}

}  // namespace sh

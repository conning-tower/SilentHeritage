#include "game/mission_loop.h"

#include <stdexcept>

namespace sh {

bool station_update_due(bool first_frame, bool expensive, std::int32_t frame_counter, std::int32_t compression) {
  const std::int32_t counter = first_frame || !expensive ? 0 : frame_counter;
  if (compression > 16) {
    return counter % (compression / 16) == 0;
  }
  return true;
}

MissionLoop::MissionLoop(Mission& mission, StationRegistry& stations, MissionLoopHooks hooks)
    : mission_(mission), stations_(stations), hooks_(hooks) {}

void MissionLoop::close_open_station() {
  if (open_ != nullptr) {
    hooks_.command_bar.close();
    open_->close();
  }
}

bool MissionLoop::frame(std::int32_t timer_ticks) {
  // mission_main_loop (0x1005d); the screen shake is the display's.
  if (mission_.end_code() != 0) {
    close_open_station();
    open_ = nullptr;
    return false;
  }
  Station* const current = stations_.get(stations_.current());
  if (current == nullptr) {
    throw std::logic_error("the current station is not installed");
  }
  if (stations_.reopen_requested() || current != open_) {
    close_open_station();
    open_ = current;
    if (hooks_.before_open) {
      hooks_.before_open(*open_);
    }
    open_->open();
    open_->first_frame_ = true;
    stations_.clear_reopen();
  }

  const bool ambient = hooks_.sound.sfx_playing(kSfxOutdoorAmbient);
  if (!open_->outdoors()) {
    if (ambient) {
      hooks_.sound.stop_sfx(kSfxOutdoorAmbient);
    }
  } else if (!ambient) {
    hooks_.sound.play_sfx_looped(kSfxOutdoorAmbient);
  }
  const bool speech = mission_.sim().conditions().speech;
  if (speech) {
    hooks_.sound.service_speech();
  }

  if (!mission_.paused()) {
    const StationId id = stations_.current();
    mission_.set_station(id == StationId::kChart   ? SensorStation::kChart
                         : id == StationId::kRadar ? SensorStation::kRadar
                                                   : SensorStation::kOther);
    const MissionEvents events = mission_.simulate_frame(timer_ticks, open_->first_frame());
    if (hooks_.events != nullptr) {
      hooks_.events->on_events(events);
    }
  }

  pump_events();

  hooks_.command_bar.update();
  if (station_update_due(open_->first_frame(), open_->expensive(), frame_counter_,
                         mission_.sim().clock().effective_compression())) {
    open_->update();
    open_->first_frame_ = false;
  }
  if (++frame_counter_ > 7) {
    frame_counter_ = 0;
  }
  check_game_over();
  if (speech) {
    hooks_.sound.service_speech();
  }
  return true;
}

void MissionLoop::pump_events() {
  // One pass at least; more while mouse moves are queued.
  while (true) {
    if (const std::optional<ui::Event> event = hooks_.input.get()) {
      last_event_flags_ = event->flags;
      if (!open_->capturing()) {
        hooks_.command_bar.event(*event);
      }
      open_->event(*event);
    }
    hooks_.command_bar.process();
    open_->process();
    if (mission_.end_code() != 0) {
      break;
    }
    if (!hooks_.input.pending() || (last_event_flags_ & ui::kEventMoved) == 0) {
      break;
    }
  }
}

void MissionLoop::check_game_over() {
  // show_game_over (0x10358)
  if (const std::optional<GameOverReason> reason = mission_.game_over()) {
    mission_.set_end_code(static_cast<std::int32_t>(*reason) + kEndGameOverBase);
    hooks_.game_over.show(*reason);
  }
}

}  // namespace sh

#include "game/engine_sounds.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "assets/game_exe.h"

namespace sh {

namespace {

// The captain's telegraph orders by step (char*[11] at 0x10cbc8): ahead
// stop..flank, back 1/3..emergency (steps 6..9 store emergency..1/3); a
// NULL, for silent running, gives an empty name.
constexpr std::uint32_t kOrderSpeech = 0x10cbc8;
constexpr std::int32_t kOrderSteps = 11;
// The engine loops by mode x 5 + power level 1..5 (int[11] at 0x10cc54):
// motors, then diesels, the quietest last.
constexpr std::uint32_t kLoopSfx = 0x10cc54;

std::string order_speech(std::int32_t telegraph) {
  if (telegraph < 0 || telegraph >= kOrderSteps) {
    throw std::out_of_range("telegraph step out of range");
  }
  return std::string(game_exe().string_pointer(kOrderSpeech + 4 * static_cast<std::uint32_t>(telegraph)));
}

std::int32_t engine_loop_sfx(std::int32_t index) { return game_exe().i32(kLoopSfx + 4 * static_cast<std::uint32_t>(index)); }

constexpr std::int32_t kThrustPerHp = 550;

}  // namespace

void EngineSounds::update(const PlayerSub& sub, const Station* station, bool engine_sounds, bool& quiet_order) {
  const std::int32_t mode = sub.diesel_mode() ? 1 : 0;
  const std::int32_t telegraph = sub.telegraph();
  if (bell_pending_) {
    sound_->play_sfx(kSfxTelegraph);
    bell_pending_ = false;
  }
  if (telegraph != telegraph_ && telegraph_ != -1) {
    bell_pending_ = true;
    const std::string speech = order_speech(telegraph);
    if (!quiet_order && !speech.empty()) {
      sound_->play_speech(speech + ".RAW");
    }
    quiet_order = false;
  }
  if (telegraph != telegraph_ || mode != mode_) {
    if (mode_ != mode && mode_ != 2 && mode == 1 && telegraph != 0) {
      sound_->play_speech("ONDIESEL.RAW");
    }
    telegraph_ = telegraph;
    mode_ = mode;
  }

  const std::int32_t power = sub.motion().power;
  if (station != station_ || refresh_) {
    power_ = -1;
  }
  if (power_ != power) {
    power_ = power;
    std::int32_t level = 0;
    if (engine_sounds) {
      const std::int32_t hp = std::abs(power / kThrustPerHp);
      const std::int32_t max_hp = mode == 1 ? sub.sub().diesel_hp : sub.sub().motor_hp;
      // The original reads past its table above full power.
      level = std::min((hp * 100 / max_hp + 19) / 20, 5);
    }
    const std::int32_t sfx = level < 1 ? 0 : engine_loop_sfx(mode * 5 + level);
    if (sfx != loop_sfx_ || station != station_ || refresh_) {
      if (loop_sfx_ > 0) {
        sound_->stop_sfx(loop_sfx_);
        loop_sfx_ = 0;
      }
      if (level > 0) {
        loop_sfx_ = sfx;
        // Heard below decks only: 50 - 25, on deck 10 - 25 (silent).
        const bool outdoors = station != nullptr && station->outdoors();
        sound_->play_sfx(loop_sfx_, std::max((outdoors ? 10 : 50) - 25, 0));
      }
    }
    refresh_ = false;
  }
  station_ = station;
}

}  // namespace sh

// `play_cinematic` (0x2c764): the full-screen Smacker videos of CINES.

#include <algorithm>

#include "assets/smacker.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/shpath.h"

namespace sh {

bool FrontEnd::play_cinematic(std::string_view name, int x, int y) {
  const std::string path = paths_.path(GameDir::kCines, std::string(name) + ".smk");
  if (!install_.exists(path)) {
    return false;
  }
  SmackerVideo video = SmackerVideo::load(install_, path);
  ui::Mouse& mouse = gui_.mouse();
  mouse.hide();
  mouse.freeze_motion();
  gui_.events().flush();

  // -1 centres the picture; its corner is rounded to a multiple of 4 as
  // `SmackToScreen` needs.
  const int left = (x == -1 ? (kScreenWidth - video.width()) / 2 : x) / 4 * 4;
  const int top = (y == -1 ? (kScreenHeight - video.height()) / 2 : y) / 4 * 4;
  screen_.fill(0);
  palette_fade_out(0, 0x100);

  VideoSound* sound = video.audio_tracks().empty() ? nullptr : host_.video_sound();
  if (sound != nullptr) {
    sound->start(video.audio_tracks().front());
  }
  const std::uint32_t start = gui_.ticks();
  bool skipped = false;
  while (!skipped && video.decode_next()) {
    if (video.palette_changed()) {
      // `vga_set_palette_256` uploads the Smacker palette's 6-bit values.
      const Palette& p = video.palette();
      for (std::size_t i = 0; i < p.colors.size(); ++i) {
        dac_[i * 3] = static_cast<std::uint8_t>(p.colors[i].r >> 2);
        dac_[i * 3 + 1] = static_cast<std::uint8_t>(p.colors[i].g >> 2);
        dac_[i * 3 + 2] = static_cast<std::uint8_t>(p.colors[i].b >> 2);
      }
    }
    screen_.blit(video.image(), left, top);
    if (sound != nullptr) {
      sound->queue(video.audio(0));
    }
    // `SmackWait` until the next frame is due, ending on any event but a
    // mouse move.
    const auto frames_done = static_cast<std::uint64_t>(video.frame_index() + 1);
    const std::uint64_t due = start + frames_done * video.frame_duration_us() / 10'000;
    do {
      frame();
      while (const std::optional<ui::Event> e = gui_.events().get()) {
        if ((e->flags & ui::kEventMoved) == 0) {
          skipped = true;
        }
      }
    } while (!skipped && gui_.ticks() < due);
  }
  if (sound != nullptr) {
    sound->stop();
  }
  gui_.events().flush();
  palette_fade_out(2, 2);
  screen_.fill(0);
  mouse.unfreeze_motion();
  mouse.show();
  return true;
}

}  // namespace sh

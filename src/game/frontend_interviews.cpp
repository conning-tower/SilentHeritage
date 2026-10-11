// The INTERVIEWS screen of the main menu (module intrview): the dialogs of
// `interviews_init_dialogs` (0xa7af8), `interviews_screen` (0xa8bff) and
// the interview player `play_interview_video` (0xa8ef1) with its slider
// hooks.

#include <algorithm>
#include <array>
#include <format>

#include "assets/exe_image.h"
#include "assets/smacker.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_extras.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "ui/controls.h"
#include "ui/draw.h"

namespace sh {

namespace {

// SH.EXE addresses.
constexpr std::uint32_t kTitle = 0xfdacc;     // "INTERVIEWS"
constexpr std::uint32_t kNextPage = 0xfdad7;  // "NEXT PAGE"
constexpr std::uint32_t kPrevPage = 0xfdae1;  // "PREV PAGE"
constexpr std::uint32_t kMainMenu = 0xfdaeb;  // "MAIN MENU"
constexpr std::uint32_t kStop = 0xfdaf5;      // "\x1b": the STOP button
constexpr std::uint32_t kRestart = 0xfdaf7;   // "\x1a": the RESTART button
constexpr std::uint32_t kLogo = 0xfdaf9;      // "SH-LOGO1.SHP"
constexpr std::uint32_t kButtonLabels = 0x11174c;  // "A".."P", 4 bytes apart
constexpr std::uint32_t kCaptions = 0x110acc;      // 16 strings, 200 bytes apart
constexpr std::uint32_t kCaptionSize = 200;

// The dialog results.
constexpr int kNextResult = 10;
constexpr int kPrevResult = 11;
constexpr int kMainMenuResult = 100;
// The video controls (dialog 0x18ab7c) reuse the first two callbacks.
constexpr int kStopResult = 1;
constexpr int kRestartResult = 2;

// Where the logo and the videos are drawn.
constexpr int kPictureX = 300;
constexpr int kPictureY = 121;
// The palette entries the videos use (0x150 / 3 up to 0x2fd / 3): blacked
// out around a video.
constexpr std::size_t kVideoFirstColour = 112;
constexpr std::size_t kVideoEndColour = 255;
// The logo's colours copied into the screen palette (0x180 / 3 ..).
constexpr std::size_t kLogoFirstColour = 128;

// The menu colours of the slider track hook.
constexpr std::uint8_t kTrackFill = 0x65;

}  // namespace

std::vector<InterviewEntry> interview_page(const ExeImage& exe, int page) {
  std::vector<InterviewEntry> entries;
  for (int i = 0; i < kInterviewsPerPage; ++i) {
    const auto n = static_cast<std::uint32_t>(page * kInterviewsPerPage + i);
    entries.push_back({.button = std::string(exe.c_string(kButtonLabels + n * 4)),
                       .caption = std::string(exe.c_string(kCaptions + n * kCaptionSize)),
                       .video = static_cast<int>(n) + 1});
  }
  return entries;
}

std::string interview_video_path(const ShPaths& paths, int video) {
  return std::format("{}{}.smk", paths.dir(GameDir::kBud), video);
}

ui::Rect InterviewSkin::slider_frame(const Viewport&, int, int, int, bool) { return {}; }  // 0xa8ce7

ui::Rect InterviewSkin::slider_left_rect(int, int, int) { return {}; }  // 0xa8d17

ui::Rect InterviewSkin::slider_right_rect(int, int, int) { return {}; }  // 0xa8d17

ui::Rect InterviewSkin::slider_track_rect(int x, int y, int w) { return {x, y, x + w - 1, y + 14}; }  // 0xa8d41

ui::Rect InterviewSkin::slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool) {
  // 0xa8d63: a sunken track over the menu fill, a raised 5-pixel thumb.
  const int pos = (w - 5) * permille / 1000;
  ui::gui_fill_rect(gfx, x - 2, y - 2, w + 4, 19, kTrackFill);
  bevel(gfx, x - 2, y - 2, x + w + 1, y + 16, false);
  bevel(gfx, x + pos, y, x + pos + 4, y + 14, true);
  return {x - 2, y - 2, x + w + 3, y + 18};
}

ui::Rect InterviewSkin::slider_arrow(const Viewport&, int, int, int, bool, bool, bool) { return {}; }  // 0xa8e2a

ui::Rect InterviewSkin::slider_focus(const Viewport&, int, int, int, bool) { return {}; }  // 0xa8ce7

void FrontEnd::interviews_screen() {
  // interviews_screen (0xa8bff): the page is kept between visits; codes
  // 1..8 play the page's video, 10 / 11 turn the page, 100 leaves.
  bool first = true;
  while (true) {
    const int result = interviews_run_dialog(first);
    first = false;
    if (result >= 1 && result <= 8) {
      play_interview_video(interview_page_ * kInterviewsPerPage + result);
    } else if (result == kNextResult) {
      interview_page_ = std::min(interview_page_ + 1, kInterviewPages - 1);
    } else if (result == kPrevResult) {
      interview_page_ = std::max(interview_page_ - 1, 0);
    } else if (result == kMainMenuResult) {
      return;
    }
  }
}

int FrontEnd::interviews_run_dialog(bool first) {
  // interviews_run_dialog (0xa8a72) on dialog 0x18a6e4, drawn on the screen.
  if (first) {
    palette_fade_out(0, 0x100);
  }
  ui::Dialog d(gui_, 0, 0, kScreenWidth, kScreenHeight, ui::kDialogFramed, &screen_);
  std::array<ui::Button*, 7> buttons{};
  d.emplace<ui::TitleBar>(exe_string(kTitle));
  const std::vector<InterviewEntry> page = interview_page(exe_, interview_page_);
  for (int i = 0; i < kInterviewsPerPage; ++i) {
    buttons[static_cast<std::size_t>(i)] = &d.emplace<ui::Button>(
        0x19, 0x91 + i * 0x32, 0x21, page[static_cast<std::size_t>(i)].button, [i](int) { return i + 1; });
  }
  buttons[4] = &d.emplace<ui::Button>(0x14, 0x172, 0x79, exe_string(kNextPage),
                                      [](int) { return kNextResult; });
  buttons[5] = &d.emplace<ui::Button>(0x91, 0x172, 0x79, exe_string(kPrevPage),
                                      [](int) { return kPrevResult; });
  buttons[6] = &d.emplace<ui::Button>(0xdd, 0x1ae, 0xc6, exe_string(kMainMenu),
                                      [](int) { return kMainMenuResult; });
  d.set_back(*buttons[6]);
  buttons[4]->disabled = interview_page_ == kInterviewPages - 1;
  buttons[5]->disabled = interview_page_ == 0;
  for (int i = 0; i < kInterviewsPerPage; ++i) {
    d.emplace<ui::TextBox>(0x41, 0x91 + i * 0x32, 0xaa, 0x1a, 8, 200, page[static_cast<std::size_t>(i)].caption)
        .read_only = true;
  }
  d.emplace<ui::GroupBox>(0x127, 0x74, 0x271, 0x16e);
  add_exit_area(d);
  // interviews_draw_logo (0xa8b86): on every message, SH-LOGO1.SHP drawn
  // in the right-hand box and its colours copied into the screen palette.
  const Shp logo = Shp::load(install_, paths_.path(GameDir::kArt, exe_string(kLogo)));
  d.emplace<ui::Area>(
      -1, -1, -1, -1,
      [this, &logo](ui::Dialog& dialog, ui::Area&, ui::Msg, int, const ui::Event&) {
        const SpriteFrame& f = logo.frame(0);
        draw_sprite_clipped(dialog.gfx(), f, kPictureX, kPictureY);
        VgaPalette colours = palette_;
        sprite_apply_palette(f, colours);
        std::copy(colours.begin() + kLogoFirstColour * 3, colours.begin() + kVideoEndColour * 3,
                  palette_.begin() + kLogoFirstColour * 3);
        return 0;
      },
      nullptr);
  d.emplace<ui::GroupBox>(10, 0x74, 0x115, 0x19f);
  // interviews_dialog_draw (0xa89b0): the fade-in, and every button greyed
  // once a video is chosen, before it plays.
  d.set_idle([this, &d, &buttons](const Viewport&, int arg) {
    if (arg != 0) {
      palette_fade_in(palette_, 0, 10);
    }
    if (d.result() > 0 && d.result() < 9) {
      for (ui::Button* b : buttons) {
        b->disabled = true;
      }
      d.invalidate();
      d.process();
    }
  });
  return run_menu_dialog(d);
}

bool FrontEnd::play_interview_video(int video) {
  // play_interview_video (0xa8ef1)
  const std::string path = interview_video_path(paths_, video);
  if (!install_.exists(path)) {
    return false;
  }
  SmackerVideo smk = SmackerVideo::load(install_, path);
  // The video's colours are black while the picture is cleared.
  const auto clear_picture = [&] {
    std::fill(palette_.begin() + kVideoFirstColour * 3, palette_.begin() + kVideoEndColour * 3, std::uint8_t{0});
    palette_fade_in(palette_, 0, 10);
    screen_.fill_rect(kPictureX, kPictureY, smk.width(), smk.height(), 0);
  };
  clear_picture();

  // interviews_install_gui_hooks (0xa8e5a) and the video controls dialog
  // 0x18ab7c; the slider shows the frame.
  InterviewSkin skin(gui_res_, menu_res_);
  skin.click_sound = menu_skin_.click_sound;
  gui_.set_skin(skin);
  int frame_number = 0;  // 0x18a674
  ui::Dialog d(gui_, 0, 0, 0x27e, 0x1de, 0, &screen_);
  d.set_back(d.emplace<ui::Button>(0x140, 0x178, 0x21, exe_string(kStop), [](int) { return kStopResult; }));
  d.emplace<ui::Button>(0x168, 0x178, 0x21, exe_string(kRestart), [](int) { return kRestartResult; });
  d.emplace<ui::GroupBox>(0x128, 0x173, 0x271, 0x19f);
  // The original creates the slider disabled (+0x2c), so its seek callback
  // (0xa8a67, the resync flag 0x18a678 read after each frame) never runs;
  // here it seeks (decoding from the start), in both variants.
  int seek_to = -1;
  auto& slider = d.emplace<ui::Slider>(0x19a, 0x182, 0xb4, static_cast<int>(smk.frame_count()) - 1, frame_number,
                                       [&seek_to](int& frame) { seek_to = frame; });
  d.open();

  VideoSound* sound = smk.audio_tracks().empty() ? nullptr : host_.video_sound();
  if (sound != nullptr) {
    sound->start(smk.audio_tracks().front());
  }
  // vga_set_palette_256: the whole DAC (the menu colours 0..111 are the
  // videos' too).
  const auto set_video_palette = [&] {
    const Palette& p = smk.palette();
    for (std::size_t i = 0; i < p.colors.size(); ++i) {
      dac_[i * 3] = static_cast<std::uint8_t>(p.colors[i].r >> 2);
      dac_[i * 3 + 1] = static_cast<std::uint8_t>(p.colors[i].g >> 2);
      dac_[i * 3 + 2] = static_cast<std::uint8_t>(p.colors[i].b >> 2);
    }
  };
  std::uint32_t start = gui_.ticks();
  // The frame played at `start` less one (0, after a seek the frame before
  // the one sought).
  int start_frame = 0;
  const auto frames = static_cast<int>(smk.frame_count());
  for (int n = 1; n <= frames; ++n) {
    smk.decode_next();
    if (smk.palette_changed()) {
      set_video_palette();
    }
    screen_.blit(smk.image(), kPictureX, kPictureY);
    if (sound != nullptr) {
      sound->queue(smk.audio(0));
    }
    slider.dirty = true;
    frame_number = n;
    // The controls run until the frame is due (SmackWait), events being
    // drained while they are mouse moves.
    const std::uint64_t due =
        start + static_cast<std::uint64_t>(n - start_frame) * smk.frame_duration_us() / 10'000;
    int result = 0;
    while (true) {
      bool moved = false;
      if (const std::optional<ui::Event> e = gui_.events().get()) {
        d.handle_event(*e);
        moved = (e->flags & ui::kEventMoved) != 0;
      }
      result = d.process();
      if (result == 0 && moved && gui_.events().peek()) {
        continue;
      }
      if (gui_.ticks() >= due) {
        break;
      }
      frame();
    }
    if (result == kStopResult) {
      break;
    }
    if (seek_to >= 0) {
      // Not in the original: the frame the slider was dragged to, its
      // sound from there.
      const int target = std::clamp(seek_to + 1, 1, frames);
      seek_to = -1;
      smk.rewind();
      for (int k = 1; k < target; ++k) {
        smk.decode_next();
      }
      if (target > 1) {
        set_video_palette();
      }
      n = target - 1;
      start = gui_.ticks();
      start_frame = n;
      if (sound != nullptr) {
        sound->stop();
        sound->start(smk.audio_tracks().front());
      }
      continue;
    }
    if (result == kRestartResult) {
      // SmackGoto(1): the video from its start.
      n = 0;
      start_frame = 0;
      smk.rewind();
      d.set_result(0);
      start = gui_.ticks();
      if (sound != nullptr) {
        sound->stop();
        sound->start(smk.audio_tracks().front());
      }
    }
  }
  if (sound != nullptr) {
    sound->stop();
  }
  d.close();
  // interviews_restore_gui_hooks (0xa8ea6), with menu_free_fonts.
  gui_.set_skin(default_skin_);
  clear_picture();
  gui_.events().flush();
  return true;
}

}  // namespace sh

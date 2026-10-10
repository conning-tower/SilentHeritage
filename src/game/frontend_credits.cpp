// `show_credits` (0x3579c, module credits): the credits scrolling up over
// SHCREDIT.PCX.

#include <vector>

#include "assets/exe_image.h"
#include "assets/pcx.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/draw.h"

namespace sh {

namespace {

constexpr std::uint32_t kCreditsPcx = 0xf3510;  // "shcredit.PCX"
constexpr std::uint32_t kCreditsShp = 0xf351d;  // "credits.shp"
constexpr std::uint32_t kLines = 0x1038c0;      // the 0x24-byte lines, ended by type -4
constexpr std::uint32_t kLineSize = 0x24;
// The version line: "(v", the version string, ")".
constexpr std::uint32_t kVersionOpen = 0xf356d;
constexpr std::uint32_t kVersion = 0x102f7c;
constexpr std::uint32_t kVersionClose = 0xf3588;

// The line types (+0); a type >= 0 is a credits.shp frame.
constexpr std::int32_t kEnd = -4;
constexpr std::int32_t kText = -3;
constexpr std::int32_t kUnderlined = -2;
constexpr std::int32_t kVersionLine = -1;

// The text colour: glyph shade 7 of the identity table drawn as 15.
constexpr std::uint8_t kTextColour = 0xf;

// One credits line: its type, its font (text: large when non-zero) or x
// (picture), the gap above it, its text, and where it was last drawn.
struct CreditLine {
  std::int32_t type = kEnd;
  std::int32_t font_or_x = 0;
  std::int32_t gap = 0;
  std::string text;
  bool drawn = false;
  ClipRect rect;
};

}  // namespace

void FrontEnd::show_credits() {
  palette_fade_out(0, 0x100);
  gui_.mouse().hide();
  gui_.events().flush();
  load_screen_pcx(exe_string(kCreditsPcx));
  const Shp sprites = Shp::load(install_, paths_.path(GameDir::kArt, exe_string(kCreditsShp)));
  const Image8 background = screen_;
  const Font& small = gui_res_.small;
  const Font& large = gui_res_.large;
  GlyphColourTable colours = gui_res_.identity;
  colours[7] = kTextColour;

  std::vector<CreditLine> lines;
  for (std::uint32_t a = kLines; exe_.i32(a) != kEnd; a += kLineSize) {
    CreditLine line;
    line.type = exe_.i32(a);
    line.font_or_x = exe_.i32(a + 4);
    line.gap = exe_.i32(a + 8);
    if (line.type == kText || line.type == kUnderlined) {
      line.text = std::string(exe_.string_pointer(a + 0xc));
    } else if (line.type == kVersionLine) {
      line.text = exe_string(kVersionOpen) + exe_string(kVersion) + exe_string(kVersionClose);
    }
    lines.push_back(std::move(line));
  }

  const Viewport screen = Viewport::full(screen_);
  // credits_draw_line (0x35aa7): a centred text, or a picture.
  const auto draw_line = [&](CreditLine& line, int y) {
    if (line.type < 0) {
      const Font& font = line.type != kVersionLine && line.font_or_x != 0 ? large : small;
      const int w = font.text_width(line.text);
      const int x = (kScreenWidth - w) / 2;
      gui_draw_text(screen, x, y, font, line.text, &colours);
      line.rect = {x, y, x + w - 1, y + font.height()};
      if (line.type == kUnderlined) {
        ui::draw_line(screen, line.rect.left, line.rect.bottom + 1, line.rect.right, line.rect.bottom + 1,
                      kTextColour);
        line.rect.bottom += 1;
      }
    } else {
      const SpriteFrame& f = sprites.frame(static_cast<std::size_t>(line.type));
      draw_sprite_clipped(screen, f, line.font_or_x, y);
      line.rect = {line.font_or_x + f.x0, y + f.y0, line.font_or_x + f.x1, y + f.y1};
    }
    line.drawn = true;
  };
  // credits_draw_frame (0x3599c): the lines drawn last are erased, then
  // those from 200 pixels above the screen down to its bottom are drawn
  // from `y` on. Returns true once the last line is 50 pixels above the
  // screen.
  const auto draw_frame = [&](int y) {
    int shown = 0;
    for (CreditLine& line : lines) {
      if (line.drawn) {
        for (int py = std::max(line.rect.top, 0); py <= std::min(line.rect.bottom, kScreenHeight - 1); ++py) {
          for (int px = std::max(line.rect.left, 0); px <= std::min(line.rect.right, kScreenWidth - 1); ++px) {
            screen_.set(px, py, background.at(px, py));
          }
        }
        line.drawn = false;
      }
      y += line.gap;
      if (y > -200) {
        ++shown;
        draw_line(line, y);
      }
      if (y > kScreenHeight) {
        ++shown;
        break;
      }
    }
    return shown == 0 && y < -50;
  };

  // The 100 Hz timer 0x129bd4 paces the scroll: 2 pixels every 5 ticks.
  std::uint32_t last_step = gui_.ticks() - 5;
  int y = kScreenHeight;
  bool faded_in = false;
  bool done = false;
  while (!done) {
    if (gui_.ticks() - last_step > 4) {
      done = draw_frame(y);
      last_step = gui_.ticks();
      y -= 2;
    }
    if (!faded_in) {
      faded_in = true;
      palette_fade_in(palette_, 2, 10);
    } else {
      frame();
    }
    while (const std::optional<ui::Event> e = gui_.events().get()) {
      if ((e->flags & ui::kEventMoved) == 0) {
        done = true;
      }
    }
  }
  palette_fade_out(2, 5);
  screen_.fill(0);
  gui_.mouse().show();
}

}  // namespace sh

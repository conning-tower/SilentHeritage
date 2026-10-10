// Viewer category "Fonts": the ART/*.FNT glyph sheets (16 codes per row)
// and a sample line, over the screen the font is
// drawn on, in that screen's palette.

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "core/install.h"
#include "gfx/text.h"

namespace sh {

namespace {

constexpr int kColumns = 16;
constexpr int kGap = 2;
constexpr int kMargin = 8;
constexpr std::string_view kSample = "Up periscope! Bearing 045, range 2,500 yards.";

class FontsCategory final : public Category {
 public:
  explicit FontsCategory(const ViewerServices& services) : services_(services) {
    for (std::size_t i = 0; i < identity_.size(); ++i) {
      identity_[i] = static_cast<std::uint8_t>(i);
    }
  }

  std::string_view name() const override { return "Fonts"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      for (const std::string& path : services_.install.list(services_.paths.dir_name(GameDir::kArt), ".FNT")) {
        files_.push_back(path.substr(path.rfind('/') + 1));
      }
    }
    return files_.size();
  }
  std::string item_name(std::size_t index) override { return files_[index]; }

  void select(std::size_t index) override {
    const std::string& file = files_[index];
    font_ = Font::load(services_.install, services_.paths.path(GameDir::kArt, file));
    pcx_ = std::string(font_palette_pcx(file.substr(0, file.rfind('.'))));
    const PcxImage screen = load_pcx(services_.install, services_.paths.path(GameDir::kArt, pcx_));
    background_ = screen.image;
    palette_ = screen.palette;
  }

  bool key(const InputEvent& event) override {
    if (event.key == SDLK_B) {
      show_background_ = !show_background_;
      return true;
    }
    return false;
  }

  void draw(Canvas& canvas) override {
    canvas.palette = to_palette(palette_);
    if (show_background_) {
      blit_image(canvas.area, background_, -canvas.pan_x, -canvas.pan_y);
    }
    int cell_w = 1;
    for (std::size_t c = 0; c < font_.glyph_count(); ++c) {
      cell_w = std::max(cell_w, font_.glyph_width(static_cast<std::uint8_t>(c)));
    }
    cell_w += kGap;
    const int cell_h = font_.height() + kGap;
    const int x0 = kMargin - canvas.pan_x;
    const int y0 = kMargin - canvas.pan_y;
    for (std::size_t c = 0; c < font_.glyph_count(); ++c) {
      const int col = static_cast<int>(c) % kColumns;
      const int row = static_cast<int>(c) / kColumns;
      font_draw_char(canvas.area, x0 + col * cell_w, y0 + row * cell_h, font_, static_cast<std::uint8_t>(c),
                     &identity_);
    }
    const int rows = (static_cast<int>(font_.glyph_count()) + kColumns - 1) / kColumns;
    const int sample_y = y0 + rows * cell_h + font_.height();
    gui_draw_text(canvas.area, x0, sample_y, font_, kSample, &identity_);
    canvas.content_width = std::max(kColumns * cell_w, font_.text_width(kSample)) + 2 * kMargin;
    canvas.content_height = (rows + 2) * cell_h + 2 * kMargin;
  }

  std::string status() const override {
    return std::format("height {}  {} codes  pal {}", font_.height(), font_.glyph_count(), pcx_);
  }

  std::string_view keys_help() const override { return "B background"; }

 private:
  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<std::string> files_;
  Font font_;
  std::string pcx_;
  Image8 background_;
  VgaPalette palette_{};
  GlyphColourTable identity_{};
  bool show_background_ = true;
};

}  // namespace

std::unique_ptr<Category> make_fonts_category(const ViewerServices& services) {
  return std::make_unique<FontsCategory>(services);
}

}  // namespace sh

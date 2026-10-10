// Viewer category "Skies": the DATA/*.ENV environments: the 1280-pixel sky
// panorama (in two halves) and the 3D view palette they make on the bridge,
// one frame per sea state.

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "assets/environment.h"
#include "assets/pcx.h"
#include "core/install.h"

namespace sh {

namespace {

constexpr std::string_view kStation = "BRIDGE.PCX";
constexpr std::string_view kSeaStateNames[kSeaStateCount] = {"calm", "moderate", "heavy"};

class SkiesCategory final : public Category {
 public:
  explicit SkiesCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Skies"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      for (const std::string& path : services_.install.list(services_.paths.dir_name(GameDir::kData), ".ENV")) {
        files_.push_back(path.substr(path.rfind('/') + 1));
      }
    }
    return files_.size();
  }
  std::string item_name(std::size_t index) override { return files_[index]; }

  void select(std::size_t index) override {
    file_ = files_[index];
    const Environment& env = services_.palettes.environment(file_);
    sky_ = decode_pcx(env.sky_pcx, file_ + ":sky").image;
    ambient_ = env.ambient_minimum_pct();
    sea_state_ = 0;
  }

  int frame_count() const override { return kSeaStateCount; }
  int frame() const override { return sea_state_; }
  void set_frame(int frame) override { sea_state_ = frame; }

  void draw(Canvas& canvas) override {
    canvas.palette = to_palette(services_.palettes.world(kStation, file_, static_cast<SeaState>(sea_state_)));
    const int half = sky_.width() / 2;
    constexpr int kTop = 8;
    constexpr int kGap = 8;
    // Each half is drawn through a viewport cut at its width.
    for (int i = 0; i < 2; ++i) {
      const int y = kTop + i * (sky_.height() + kGap);
      Viewport vp = canvas.area;
      vp.x1 = std::min(vp.x1, vp.x0 + half - 1);
      blit_image(vp, sky_, -i * half, y);
    }
    draw_palette_swatches(canvas.area, 8, kTop + 2 * (sky_.height() + kGap) + 8, 14);
  }

  std::string status() const override {
    return std::format("sky {}x{}  sea {}  night {}%  pal {} + {}", sky_.width(), sky_.height(),
                       kSeaStateNames[sea_state_], ambient_, kStation, file_);
  }

 private:
  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<std::string> files_;
  std::string file_;
  Image8 sky_;
  int ambient_ = 0;
  int sea_state_ = 0;
};

}  // namespace

std::unique_ptr<Category> make_skies_category(const ViewerServices& services) {
  return std::make_unique<SkiesCategory>(services);
}

}  // namespace sh

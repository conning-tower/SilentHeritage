// Viewer category "Maps": the DATA/*.WLD patrol maps, in two views: the
// painted navigation chart (chart screen palette) and the terrain/depth grid
// (its own 64 colours).

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "app/names.h"
#include "assets/world.h"
#include "core/install.h"

namespace sh {

namespace {

class MapsCategory final : public Category {
 public:
  static constexpr int kChartView = 0;
  static constexpr int kGridView = 1;

  explicit MapsCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Maps"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      for (const std::string& path : services_.install.list(services_.paths.dir_name(GameDir::kData), ".WLD")) {
        files_.push_back(path.substr(path.rfind('/') + 1));
      }
      // WORLD-1..WORLD-15 in map order rather than by name.
      std::ranges::stable_sort(files_, numbered_name_less);
    }
    return files_.size();
  }
  std::string item_name(std::size_t index) override { return files_[index]; }

  void select(std::size_t index) override {
    file_ = files_[index];
    const std::string path = services_.paths.path(GameDir::kData, file_);
    world_ = parse_world(services_.install.read(path), path);
    chart_palette_ = services_.palettes.chart(file_);
    grid_palette_ = VgaPalette{};
    std::ranges::copy(world_.grid_dac, grid_palette_.begin() + 3 * kGridDacFirstIndex);
    view_ = kChartView;
  }

  int frame_count() const override { return 2; }
  int frame() const override { return view_; }
  void set_frame(int frame) override { view_ = frame; }

  void draw(Canvas& canvas) override {
    const IndexedRaster& r = raster();
    canvas.palette = to_palette(view_ == kGridView ? grid_palette_ : chart_palette_);
    canvas.content_width = r.width;
    canvas.content_height = r.height;
    blit_pixels(canvas.area, r.pixels, r.width, r.height, -canvas.pan_x, -canvas.pan_y);
  }

  std::string status() const override {
    const IndexedRaster& r = raster();
    return std::format("{} {} {}x{}  {} landmarks  pal {}", world_.region_name,
                       view_ == kChartView ? "chart" : "grid", r.width, r.height, world_.locations.size(),
                       view_ == kChartView ? std::format("chart ({})", file_) : std::string("grid colours"));
  }

 private:
  const IndexedRaster& raster() const { return view_ == kGridView ? world_.grid : world_.chart; }

  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<std::string> files_;
  std::string file_;
  World world_;
  VgaPalette chart_palette_{};
  VgaPalette grid_palette_{};
  int view_ = kChartView;
};

}  // namespace

std::unique_ptr<Category> make_maps_category(const ViewerServices& services) {
  return std::make_unique<MapsCategory>(services);
}

}  // namespace sh

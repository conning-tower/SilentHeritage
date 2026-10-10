// Viewer category "Pictures": the ART PCX files (.PCX screens, .PCC
// textures, .PAL red-light screens), each with its own palette.

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "assets/pcx.h"
#include "core/install.h"

namespace sh {

namespace {

class PicturesCategory final : public Category {
 public:
  explicit PicturesCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Pictures"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      const std::string_view dir = services_.paths.dir_name(GameDir::kArt);
      for (const std::string_view ext : {".PCX", ".PCC", ".PAL"}) {
        for (const std::string& path : services_.install.list(dir, ext)) {
          files_.push_back(path.substr(path.rfind('/') + 1));
        }
      }
      std::ranges::sort(files_);
    }
    return files_.size();
  }
  std::string item_name(std::size_t index) override { return files_[index]; }

  void select(std::size_t index) override {
    const std::string path = services_.paths.path(GameDir::kArt, files_[index]);
    picture_ = decode_pcx(services_.install.read(path), path);
  }

  void draw(Canvas& canvas) override {
    canvas.palette = to_palette(picture_.palette);
    canvas.content_width = picture_.image.width();
    canvas.content_height = picture_.image.height();
    blit_image(canvas.area, picture_.image, -canvas.pan_x, -canvas.pan_y);
  }

  std::string status() const override {
    return std::format("{}x{}  own palette", picture_.image.width(), picture_.image.height());
  }

 private:
  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<std::string> files_;
  PcxImage picture_;
};

}  // namespace

std::unique_ptr<Category> make_pictures_category(const ViewerServices& services) {
  return std::make_unique<PicturesCategory>(services);
}

}  // namespace sh

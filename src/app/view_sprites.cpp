// Viewer category "Sprites": every sprite set (ART/*.SHP, the members of the
// ART/*.CAT archives, the chart coast tiles of each DATA/*.WLD and the map objects
// of DATA/*.OBS), frame by frame, in the palette of the screen that draws it
// (app/palettes.h).

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "app/names.h"
#include "assets/cat.h"
#include "assets/objects.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "assets/world.h"
#include "core/install.h"
#include "gfx/sprite.h"

namespace sh {

namespace {

enum class SourceKind : std::uint8_t { kShp, kCatMember, kCoastTiles, kObject };

struct SpriteSource {
  SourceKind kind = SourceKind::kShp;
  std::string file;          // in ART (SHP, CAT) or DATA (WLD, OBS)
  std::string member;        // CAT member name
  std::size_t object = 0;    // OBS object index
};

std::string stem(std::string_view file) { return std::string(file.substr(0, file.rfind('.'))); }

class SpritesCategory final : public Category {
 public:
  explicit SpritesCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Sprites"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      list_sources();
    }
    return sources_.size();
  }

  std::string item_name(std::size_t index) override {
    const SpriteSource& s = sources_[index];
    switch (s.kind) {
      case SourceKind::kShp:
        return s.file;
      case SourceKind::kCatMember:
        return s.file + ":" + s.member;
      case SourceKind::kCoastTiles:
        return s.file + ":COAST TILES";
      case SourceKind::kObject:
        return std::format("{}:{}", s.file, s.object);
    }
    return {};
  }

  void select(std::size_t index) override {
    const SpriteSource& s = sources_[index];
    const ShPaths& paths = services_.paths;
    world_file_ = std::string(PaletteBuilder::kDefaultWorld);
    switch (s.kind) {
      case SourceKind::kShp:
        shp_ = Shp::load(services_.install, paths.path(GameDir::kArt, s.file));
        recipe_ = sprite_palette_recipe(stem(s.file));
        break;
      case SourceKind::kCatMember:
        shp_ = Shp::parse(archives_.at(s.file).member(s.member), s.file + ":" + s.member);
        recipe_ = sprite_palette_recipe(stem(s.file));
        break;
      case SourceKind::kCoastTiles: {
        const std::string path = paths.path(GameDir::kData, s.file);
        const World w = parse_world(services_.install.read(path), path);
        shp_ = Shp::parse(w.coast_tiles_shp, path + ":coast tiles");
        recipe_ = sprite_palette_recipe("MAPICON");
        world_file_ = s.file;
        break;
      }
      case SourceKind::kObject:
        shp_ = Shp::parse(objects_.at(s.file)[s.object].shp, std::format("{}:{}", s.file, s.object));
        recipe_ = sprite_palette_recipe("OBS");
        break;
    }
    base_palette_ = services_.palettes.build(recipe_, world_file_);
    background_ = Image8();
    if (recipe_.kind != PaletteKind::kChart) {
      background_ = load_pcx(services_.install, paths.path(GameDir::kArt, recipe_.pcx)).image;
    }
    // Start on the first frame with pixels.
    const auto& frames = shp_.frames();
    frame_ = static_cast<int>(std::ranges::find_if(frames, [](const SpriteFrame& f) { return !f.empty(); }) -
                              frames.begin()) %
             std::max<int>(1, static_cast<int>(frames.size()));
  }

  int frame_count() const override { return static_cast<int>(shp_.frame_count()); }
  int frame() const override { return frame_; }
  void set_frame(int frame) override { frame_ = frame; }

  bool key(const InputEvent& event) override {
    switch (event.key) {
      case SDLK_B:
        background_shown_ = !background_shown_;
        return true;
      case SDLK_O:
        at_origin_ = !at_origin_;
        return true;
      case SDLK_R:
        rotate_ = !rotate_;
        return true;
      case SDLK_A:
        animate_ = !animate_;
        return true;
      case SDLK_EQUALS:
      case SDLK_PLUS:
      case SDLK_KP_PLUS:
        scale_ = Fixed16::from_raw(scale_.raw() * 5 / 4);
        return true;
      case SDLK_MINUS:
      case SDLK_KP_MINUS:
        scale_ = Fixed16::from_raw(std::max(0x1000, scale_.raw() * 4 / 5));
        return true;
      default:
        return false;
    }
  }

  void update(std::uint64_t now_us) override {
    const std::uint64_t ms = now_us / 1000;
    angle_ = Angle10(static_cast<std::int32_t>(ms * 36 / 100 % 3600));  // one turn in 10 s
    if (animate_ && shp_.frame_count() > 0) {
      frame_ = static_cast<int>(ms / 100 % shp_.frame_count());  // 10 frames per second
    }
  }

  void draw(Canvas& canvas) override {
    if (shp_.frame_count() == 0) {
      canvas.palette = to_palette(base_palette_);
      return;
    }
    const SpriteFrame& f = shp_.frame(static_cast<std::size_t>(frame_));
    VgaPalette pal = base_palette_;
    // The chart screen sets its palette once; elsewhere the exporter's
    // overlay of each frame's own colours is kept.
    if (recipe_.kind != PaletteKind::kChart) {
      sprite_apply_palette(f, pal);
    }
    canvas.palette = to_palette(pal);
    if (background_shown_) {
      blit_image(canvas.area, background_, -canvas.pan_x, -canvas.pan_y);
      canvas.content_width = background_.width();
      canvas.content_height = background_.height();
    }
    int x = canvas.width / 2;
    int y = canvas.height / 2;
    if (at_origin_) {
      x = -canvas.pan_x;
      y = -canvas.pan_y;
    } else if (!background_shown_ && (f.width() > canvas.width || f.height() > canvas.height)) {
      // A frame larger than the area scrolls from its box's top-left corner.
      x = static_cast<int>(-f.x0) - canvas.pan_x;
      y = static_cast<int>(-f.y0) - canvas.pan_y;
      canvas.content_width = f.width();
      canvas.content_height = f.height();
    }
    if (rotate_) {
      sprite_draw_transformed(canvas.area, f, x, y, scratch_, angle_, scale_, scale_, 0);
    } else {
      draw_sprite_clipped(canvas.area, f, x, y);
    }
  }

  std::string status() const override {
    std::string s;
    if (shp_.frame_count() > 0) {
      const SpriteFrame& f = shp_.frame(static_cast<std::size_t>(frame_));
      s = f.empty() ? "empty frame" : std::format("{}x{} at ({},{})", f.width(), f.height(), f.x0, f.y0);
      if (!f.palette.empty()) {
        s += std::format(" +{} colours", f.palette.size());
      }
    }
    s += "  pal " + PaletteBuilder::describe(recipe_, world_file_);
    if (at_origin_) {
      s += "  at 0,0";
    }
    if (rotate_) {
      s += std::format("  rot {} x{:.2f}", angle_.tenths() / 10, scale_.to_double());
    }
    return s;
  }

  std::string_view keys_help() const override { return "B backgr. O origin R rotate +- scale A anim"; }

 private:
  void list_sources() {
    const ShPaths& paths = services_.paths;
    const Install& install = services_.install;
    const std::string_view art = paths.dir_name(GameDir::kArt);
    const std::string_view data = paths.dir_name(GameDir::kData);
    auto file_of = [](const std::string& path) { return path.substr(path.rfind('/') + 1); };
    for (const std::string& path : install.list(art, ".SHP")) {
      sources_.push_back({SourceKind::kShp, file_of(path), {}, 0});
    }
    for (const std::string& path : install.list(art, ".CAT")) {
      const std::string file = file_of(path);
      const CatArchive& cat =
          archives_.emplace(file, CatArchive::load(install, paths.path(GameDir::kArt, file))).first->second;
      for (const CatEntry& e : cat.entries()) {
        sources_.push_back({SourceKind::kCatMember, file, e.name, 0});
      }
    }
    std::vector<std::string> worlds;
    for (const std::string& path : install.list(data, ".WLD")) {
      worlds.push_back(file_of(path));
    }
    std::ranges::sort(worlds, numbered_name_less);
    for (std::string& file : worlds) {
      sources_.push_back({SourceKind::kCoastTiles, std::move(file), {}, 0});
    }
    for (const std::string& path : install.list(data, ".OBS")) {
      const std::string file = file_of(path);
      const std::string dos = paths.path(GameDir::kData, file);
      const auto& objects = objects_.emplace(file, parse_objects(install.read(dos), dos)).first->second;
      for (std::size_t i = 0; i < objects.size(); ++i) {
        sources_.push_back({SourceKind::kObject, file, {}, i});
      }
    }
  }

  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<SpriteSource> sources_;
  std::map<std::string, CatArchive> archives_;
  std::map<std::string, std::vector<MapObject>> objects_;

  Shp shp_;
  PaletteRecipe recipe_;
  std::string world_file_;
  VgaPalette base_palette_{};
  Image8 background_;
  int frame_ = 0;

  bool background_shown_ = false;
  bool at_origin_ = false;
  bool rotate_ = false;
  bool animate_ = false;
  Angle10 angle_;
  Fixed16 scale_ = Fixed16::from_int(1);
  Image8 scratch_;
};

}  // namespace

std::unique_ptr<Category> make_sprites_category(const ViewerServices& services) {
  return std::make_unique<SpritesCategory>(services);
}

}  // namespace sh

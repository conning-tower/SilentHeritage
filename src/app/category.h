// The asset viewer's categories: each lists the items of one kind of file
// and draws the selected one. `Viewer` (app/viewer.h) owns the list, the
// keys common to all categories and the status line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "app/palettes.h"
#include "app/sound_system.h"
#include "app/ui_text.h"
#include "core/image.h"
#include "game/shpath.h"
#include "gfx/viewport.h"
#include "platform/display.h"

namespace sh {

class Install;

// What categories draw with and play through; all outlive the categories.
struct ViewerServices {
  const Install& install;
  const ShPaths& paths;
  PaletteBuilder& palettes;
  SoundSystem& sound;
  const UiText& text;
};

// The drawing target of a category.
struct Canvas {
  // The content area of the 640x480 frame; drawing coordinates are relative
  // to its top-left corner.
  Viewport area;
  int width = 0;
  int height = 0;
  // The scroll offset (Shift+arrows) to subtract from the content position.
  int pan_x = 0;
  int pan_y = 0;
  // Set by the category: the size of a scrollable picture, to which the
  // viewer limits the scroll offset.
  int content_width = 0;
  int content_height = 0;
  // The palette the frame is shown with, preset to the menu palette.
  Palette& palette;
};

class Category {
 public:
  virtual ~Category() = default;

  virtual std::string_view name() const = 0;
  // The items; the list may be built on first use.
  virtual std::size_t item_count() = 0;
  virtual std::string item_name(std::size_t index) = 0;
  // Loads item `index` (< item_count()) and shows its first frame. Throws
  // `DataError` if its file is malformed.
  virtual void select(std::size_t index) = 0;

  // The frames of the selected item (sprite frames, map views...).
  virtual int frame_count() const { return 1; }
  virtual int frame() const { return 0; }
  virtual void set_frame(int /*frame*/) {}

  // Enter or Space: plays the selected sound, message or video.
  virtual void activate() {}
  // Handles a category-specific key; returns false if it is not one.
  virtual bool key(const InputEvent& /*event*/) { return false; }
  // Advances animations and playback to `now_us` (microseconds).
  virtual void update(std::uint64_t /*now_us*/) {}

  // Draws the selected item into `canvas` (the area is already cleared to
  // colour 0) and sets the palette.
  virtual void draw(Canvas& canvas) = 0;
  // Returns the item details for the status line (size, palette source...).
  virtual std::string status() const = 0;
  // Returns the help line of the category-specific keys, if any.
  virtual std::string_view keys_help() const { return {}; }
};

std::unique_ptr<Category> make_pictures_category(const ViewerServices& services);
std::unique_ptr<Category> make_sprites_category(const ViewerServices& services);
std::unique_ptr<Category> make_maps_category(const ViewerServices& services);
std::unique_ptr<Category> make_skies_category(const ViewerServices& services);
std::unique_ptr<Category> make_fonts_category(const ViewerServices& services);
std::unique_ptr<Category> make_sounds_category(const ViewerServices& services);
std::unique_ptr<Category> make_messages_category(const ViewerServices& services);
std::unique_ptr<Category> make_videos_category(const ViewerServices& services);

}  // namespace sh

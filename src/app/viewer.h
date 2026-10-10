// The asset viewer: browses every original asset by category,
// drawn on the game's 640x480 indexed frame.
//
// Screen: the category or item list on the left, the selected item on the
// right, a status line at the bottom. Keys: arrows, PageUp/PageDown,
// Home/End move in the list; Enter opens a category, then plays the selected
// sound or video; Left/Right step through an item's frames; Shift+arrows
// scroll large pictures; Tab shows the item alone on the whole screen; Esc
// goes back, then quits.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/category.h"
#include "core/image.h"
#include "platform/display.h"

namespace sh {

class Viewer {
 public:
  static constexpr int kPanelWidth = 200;
  static constexpr int kStatusHeight = 16;

  // `services` must outlive the viewer.
  explicit Viewer(const ViewerServices& services);

  // Handles an input event; returns false when the viewer is to quit.
  bool handle(const InputEvent& event);
  // Advances playback and animations to `now_us` (microseconds).
  void update(std::uint64_t now_us);
  // Draws the screen into `frame` (640x480) and sets `palette`.
  void render(Image8& frame, Palette& palette);

  // Opens "<category>[/<item>[#<frame>]]" (names case-insensitive), as if
  // browsed to. Throws std::invalid_argument if a name is unknown, or
  // `DataError` if the item's file is malformed.
  void open(std::string_view path);

 private:
  enum class Level : std::uint8_t { kCategories, kItems };

  Category& category() { return *categories_[category_index_]; }
  std::size_t list_size();
  std::size_t& list_cursor();
  // Moves the list cursor by `delta` (clamped) and loads the item.
  void move_cursor(std::ptrdiff_t delta);
  void enter_category();
  void select_item(std::size_t index);
  void step_frame(int delta);

  void draw_panel(Image8& frame, const UiColours& colours);
  void draw_status(Image8& frame, const UiColours& colours);
  void draw_help(Canvas& canvas);
  std::string status_text();

  const ViewerServices& services_;
  std::vector<std::unique_ptr<Category>> categories_;
  std::vector<std::size_t> item_cursors_;  // per category
  std::size_t category_index_ = 0;
  Level level_ = Level::kCategories;
  bool item_ok_ = false;
  std::string error_;
  bool fullscreen_ = false;
  int pan_x_ = 0;
  int pan_y_ = 0;
  std::size_t list_top_ = 0;  // first visible line
};

}  // namespace sh

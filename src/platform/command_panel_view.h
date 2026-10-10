// Drawing of the floating command button and its panel
// (platform/command_panel.h) with an SDL renderer, over the game picture.
#pragma once

#include <array>

#include "platform/command_panel.h"

struct SDL_Renderer;
struct SDL_Texture;

namespace sh {

// The widget's textures on one renderer.
class CommandPanelView {
 public:
  // Uploads `icons` to `renderer` (not owned; must outlive the view).
  // Throws std::runtime_error if SDL cannot create a texture.
  CommandPanelView(SDL_Renderer* renderer, const CommandIcons& icons);
  ~CommandPanelView();
  CommandPanelView(const CommandPanelView&) = delete;
  CommandPanelView& operator=(const CommandPanelView&) = delete;

  // Draws `panel` in the renderer's output pixels (its logical
  // presentation is turned off meanwhile, then restored). The button
  // group is semi-transparent unless the panel is open or it is dragged.
  void draw(const CommandPanel& panel);

 private:
  SDL_Renderer* renderer_;
  SDL_Texture* button_ = nullptr;
  std::array<SDL_Texture*, 2> compression_{};
  std::array<SDL_Texture*, 2> compression_pressed_{};
  std::array<SDL_Texture*, kPanelCommandCount> icons_{};
  std::array<SDL_Texture*, kPanelCommandCount> pressed_{};
};

}  // namespace sh

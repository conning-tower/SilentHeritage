#include "platform/command_panel_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace sh {

namespace {

// The panel's colours: a dark steel plate with a brass edge, like the
// game's instrument panels.
constexpr SDL_Color kPlate{24, 28, 26, 215};
constexpr SDL_Color kEdge{150, 126, 70, 255};
constexpr SDL_Color kCell{52, 58, 54, 200};
constexpr SDL_Color kCellPressed{150, 126, 70, 200};
// The button's opacity while idle.
constexpr float kIdleAlpha = 0.7f;

SDL_Texture* upload(SDL_Renderer* renderer, const RgbaImage& image) {
  SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_ARGB8888,
                                               const_cast<std::uint32_t*>(image.pixels.data()), image.width * 4);
  if (surface == nullptr) {
    throw std::runtime_error(std::format("SDL_CreateSurfaceFrom: {}", SDL_GetError()));
  }
  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_DestroySurface(surface);
  if (texture == nullptr) {
    throw std::runtime_error(std::format("SDL_CreateTextureFromSurface: {}", SDL_GetError()));
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  // Sharp pixels at any size, without the shimmer of plain nearest.
  if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_PIXELART)) {
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
  }
  return texture;
}

SDL_FRect to_sdl(const PanelRect& r) { return {r.x, r.y, r.w, r.h}; }

void fill(SDL_Renderer* renderer, const PanelRect& r, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  const SDL_FRect f = to_sdl(r);
  SDL_RenderFillRect(renderer, &f);
}

void outline(SDL_Renderer* renderer, const PanelRect& r, SDL_Color c, float width) {
  fill(renderer, {r.x, r.y, r.w, width}, c);
  fill(renderer, {r.x, r.y + r.h - width, r.w, width}, c);
  fill(renderer, {r.x, r.y, width, r.h}, c);
  fill(renderer, {r.x + r.w - width, r.y, width, r.h}, c);
}

// Draws `texture` centred in `r`, as large as fits keeping its aspect ratio.
void draw_fitted(SDL_Renderer* renderer, SDL_Texture* texture, const PanelRect& r) {
  float w = 0;
  float h = 0;
  SDL_GetTextureSize(texture, &w, &h);
  const float s = std::min(r.w / w, r.h / h);
  const SDL_FRect dst{r.x + (r.w - w * s) / 2, r.y + (r.h - h * s) / 2, w * s, h * s};
  SDL_RenderTexture(renderer, texture, nullptr, &dst);
}

}  // namespace

CommandPanelView::CommandPanelView(SDL_Renderer* renderer, const CommandIcons& icons) : renderer_(renderer) {
  button_ = upload(renderer, icons.button);
  for (std::size_t i = 0; i < 2; ++i) {
    compression_[i] = upload(renderer, icons.compression[i]);
    compression_pressed_[i] = upload(renderer, icons.compression_pressed[i]);
  }
  for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
    icons_[i] = upload(renderer, icons.icons[i]);
    pressed_[i] = upload(renderer, icons.pressed[i]);
  }
}

CommandPanelView::~CommandPanelView() {
  SDL_DestroyTexture(button_);
  for (std::size_t i = 0; i < 2; ++i) {
    SDL_DestroyTexture(compression_[i]);
    SDL_DestroyTexture(compression_pressed_[i]);
  }
  for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
    SDL_DestroyTexture(icons_[i]);
    SDL_DestroyTexture(pressed_[i]);
  }
}

void CommandPanelView::draw(const CommandPanel& panel) {
  int logical_w = 0;
  int logical_h = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(renderer_, &logical_w, &logical_h, &mode);
  SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

  if (panel.is_open()) {
    const PanelRect p = panel.panel_rect();
    const float edge = std::max(1.0f, p.w / 300);
    fill(renderer_, p, kPlate);
    outline(renderer_, p, kEdge, edge);
    const auto rects = panel.icon_rects();
    const std::optional<PanelCommand> pressed = panel.pressed();
    for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
      const bool down = pressed && static_cast<std::size_t>(*pressed) == i;
      fill(renderer_, rects[i], down ? kCellPressed : kCell);
      draw_fitted(renderer_, down ? pressed_[i] : icons_[i], rects[i]);
    }
  }
  const float alpha = panel.is_open() || panel.dragging() ? 1.0f : kIdleAlpha;
  SDL_SetTextureAlphaModFloat(button_, alpha);
  draw_fitted(renderer_, button_, panel.button_rect());
  const auto parts = panel.compression_rects();
  const std::optional<PanelCommand> held = panel.compression_pressed();
  for (std::size_t i = 0; i < 2; ++i) {
    const bool down = held && static_cast<std::size_t>(*held) == kPanelCommandCount + i;
    SDL_Texture* t = down ? compression_pressed_[i] : compression_[i];
    SDL_SetTextureAlphaModFloat(t, alpha);
    draw_fitted(renderer_, t, parts[i]);
  }

  SDL_SetRenderLogicalPresentation(renderer_, logical_w, logical_h, mode);
}

}  // namespace sh

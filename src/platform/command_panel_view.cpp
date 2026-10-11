#include "platform/command_panel_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
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
// The live state (extended `panel_status`): the lamps and frames of the
// depth order in effect and of the tubes' states, the label text.
constexpr SDL_Color kLampGreen{70, 200, 90, 255};
constexpr SDL_Color kLampAmber{240, 170, 40, 255};
constexpr SDL_Color kLampRed{220, 50, 40, 255};
constexpr SDL_Color kLampGrey{90, 96, 92, 255};
constexpr SDL_Color kLampOff{30, 40, 34, 255};
constexpr SDL_Color kCellLit{96, 84, 50, 230};
constexpr SDL_Color kLabelBack{10, 12, 11, 220};
constexpr SDL_Color kLabelText{230, 225, 200, 255};
// The opacity of an empty or damaged tube's button.
constexpr float kDimAlpha = 0.55f;

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

void fill_circle(SDL_Renderer* renderer, float cx, float cy, float r, SDL_Color c) {
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  for (float dy = -r; dy <= r; dy += 1) {
    const float dx = std::sqrt(std::max(0.0f, r * r - dy * dy));
    SDL_RenderLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
  }
}

// Draws `text` in SDL's debug font centred on (cx, cy), `scale` pixels per
// font pixel.
void draw_text(SDL_Renderer* renderer, const std::string& text, float cx, float cy, float scale, SDL_Color c) {
  float sx = 1;
  float sy = 1;
  SDL_GetRenderScale(renderer, &sx, &sy);
  SDL_SetRenderScale(renderer, scale, scale);
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  const auto w = static_cast<float>(text.size() * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
  SDL_RenderDebugText(renderer, cx / scale - w / 2, cy / scale - SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE / 2.0f,
                      text.c_str());
  SDL_SetRenderScale(renderer, sx, sy);
}

// Returns the colour of a tube's frame and lamp.
SDL_Color tube_colour(TubeState state) {
  switch (state) {
    case TubeState::kLoaded:
      return kLampGreen;
    case TubeState::kReloading:
      return kLampAmber;
    case TubeState::kDamaged:
      return kLampRed;
    case TubeState::kEmpty:
    case TubeState::kAbsent:
      break;
  }
  return kLampGrey;
}

// Draws the lamp of a cell `r`: lit in `c`, or dark.
void draw_lamp(SDL_Renderer* renderer, const PanelRect& r, SDL_Color c, bool lit) {
  const float rad = r.w * 0.07f;
  const float cx = r.x + r.w - rad * 2;
  const float cy = r.y + rad * 2;
  fill_circle(renderer, cx, cy, rad + 1.5f, SDL_Color{20, 20, 20, 255});
  fill_circle(renderer, cx, cy, rad, lit ? c : kLampOff);
}

// Returns "m:ss" of `seconds`.
std::string minutes_seconds(std::int32_t seconds) {
  return std::format("{}:{:02}", std::max(seconds, 0) / 60, std::max(seconds, 0) % 60);
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
      if (panel.status()) {
        draw_live(panel, i, rects[i], down, edge);
        continue;
      }
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

namespace sh {

void CommandPanelView::draw_live(const CommandPanel& panel, std::size_t i, const PanelRect& r, bool down,
                                 float edge) {
  const PanelStatus& status = *panel.status();
  if (i < kDepthCommandCount) {
    const bool lit = status.depth_order && static_cast<std::size_t>(*status.depth_order) == i;
    fill(renderer_, r, down ? kCellPressed : lit ? kCellLit : kCell);
    draw_fitted(renderer_, down ? pressed_[i] : icons_[i], r);
    if (lit) {
      outline(renderer_, r, kLampGreen, edge * 3);
    }
    draw_lamp(renderer_, r, kLampGreen, lit);
    return;
  }
  const std::size_t tube = i - static_cast<std::size_t>(PanelCommand::kFireTube1);
  const TubeStatus& t = status.tubes[tube];
  if (t.state == TubeState::kAbsent) {
    return;
  }
  const SDL_Color c = tube_colour(t.state);
  fill(renderer_, r, down ? kCellPressed : kCell);
  // The button above a label strip.
  const float strip = r.h * 0.24f;
  const PanelRect button{r.x + r.w * 0.1f, r.y + r.h * 0.04f, r.w * 0.8f, r.h - strip - r.h * 0.06f};
  SDL_Texture* texture = t.state == TubeState::kLoaded && !down ? icons_[i] : pressed_[i];
  const bool dim = t.state == TubeState::kEmpty || t.state == TubeState::kDamaged;
  SDL_SetTextureAlphaModFloat(texture, dim ? kDimAlpha : 1.0f);
  draw_fitted(renderer_, texture, button);
  SDL_SetTextureAlphaModFloat(texture, 1.0f);
  if (t.state == TubeState::kDamaged) {
    SDL_SetRenderDrawColor(renderer_, kLampRed.r, kLampRed.g, kLampRed.b, 230);
    for (float o = -edge * 2; o <= edge * 2; o += 0.5f) {
      SDL_RenderLine(renderer_, button.x + o, button.y, button.x + button.w + o, button.y + button.h);
      SDL_RenderLine(renderer_, button.x + button.w + o, button.y, button.x + o, button.y + button.h);
    }
  }
  outline(renderer_, r, c, edge * (t.state == TubeState::kEmpty ? 1.5f : 3.0f));
  draw_lamp(renderer_, r, c, t.state != TubeState::kEmpty);
  const PanelRect label{r.x + edge * 3, r.y + r.h - strip - edge * 2, r.w - edge * 6, strip};
  std::string text = t.label;
  SDL_Color ink = kLabelText;
  if (t.state == TubeState::kReloading) {
    text = minutes_seconds(t.reload_seconds);
    ink = kLampAmber;
    const float done = panel.reload_progress(tube);
    fill(renderer_, {label.x, label.y - edge * 2, label.w * done, edge * 2}, kLampAmber);
  } else if (t.state == TubeState::kDamaged) {
    text = "DAMAGED";
    ink = kLampRed;
  } else if (t.state == TubeState::kEmpty) {
    text = "EMPTY";
    ink = kLampGrey;
  }
  if (text.empty()) {
    return;
  }
  fill(renderer_, label, kLabelBack);
  const float scale = std::max(1.0f, std::floor(std::min(label.h * 0.7f / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
                                                         label.w * 0.95f / (SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE *
                                                                            static_cast<float>(text.size())))));
  draw_text(renderer_, text, label.x + label.w / 2, label.y + label.h / 2, scale, ink);
}

}  // namespace sh

#include "platform/display.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/edition.h"
#include "platform/bios_keys.h"
#include "platform/command_panel.h"
#include "platform/command_panel_view.h"
#include "platform/pointer_warp.h"

namespace sh {

namespace {

[[noreturn]] void sdl_fail(std::string_view what) {
  throw std::runtime_error(std::format("{}: {}", what, SDL_GetError()));
}

MouseButton to_button(std::uint8_t b) {
  switch (b) {
    case SDL_BUTTON_LEFT:
      return MouseButton::kLeft;
    case SDL_BUTTON_RIGHT:
      return MouseButton::kRight;
    case SDL_BUTTON_MIDDLE:
      return MouseButton::kMiddle;
    default:
      return MouseButton::kNone;
  }
}

#ifndef SDL_PLATFORM_ANDROID
// Whether `key` is the fullscreen toggle: Alt with either Enter key.
bool is_fullscreen_toggle(const SDL_KeyboardEvent& key) {
  return (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) && (key.mod & SDL_KMOD_ALT) != 0;
}
#endif

}  // namespace

std::array<std::uint8_t, 3> side_fill_tint(const Palette& palette, std::array<std::uint8_t, 3>& brightest) {
  std::array<std::uint8_t, 3> top{};
  for (const Rgb& c : palette.colors) {
    top[0] = std::max(top[0], c.r);
    top[1] = std::max(top[1], c.g);
    top[2] = std::max(top[2], c.b);
  }
  std::array<std::uint8_t, 3> tint{};
  for (std::size_t i = 0; i < 3; ++i) {
    brightest[i] = std::max(brightest[i], top[i]);
    tint[i] = brightest[i] == 0 ? 0 : static_cast<std::uint8_t>(top[i] * 255 / brightest[i]);
  }
  return tint;
}

int wide_screen_width(int window_w, int window_h) {
  if (window_w <= 0 || window_h <= 0) {
    return kScreenWidth;
  }
  return std::clamp(window_w * kScreenHeight / window_h, kScreenWidth, kMaxScreenWidth);
}

AspectRange window_aspect_range(bool wide_screen) {
  constexpr float k43 = static_cast<float>(kScreenWidth) / kScreenHeight;
  return {k43, wide_screen ? static_cast<float>(kMaxScreenWidth) / kScreenHeight : k43};
}

struct Display::Impl {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  std::vector<std::uint32_t> argb = std::vector<std::uint32_t>(static_cast<std::size_t>(kScreenWidth) * kScreenHeight);
  // Whether the screen follows the window's aspect; the width of the frame
  // texture (the last frame shown) and of the logical screen.
  bool wide = false;
  int frame_w = kScreenWidth;
  int screen_w = kScreenWidth;
  SDL_ScaleMode scale_mode = SDL_SCALEMODE_LINEAR;
  // Beside a narrower frame: the fill set, the brass and wood textures
  // (by `SideFill::Kind`), the textures of the screen's own side pictures,
  // and the brightest palette channels seen (the tint's reference).
  SideFill side_fill;
  std::array<SDL_Texture*, 2> side_textures{};
  std::array<SDL_Texture*, 2> side_pictures{};
  // The picture across the bottom rows, if set, and its texture.
  const Image8* bottom_strip = nullptr;
  SDL_Texture* bottom_texture = nullptr;
  // The cursor beyond the frame, if set: its pixels and top-left corner in
  // the frame, and its texture (made at each `present`).
  std::optional<DecodedFrame> cursor;
  int cursor_x = 0;
  int cursor_y = 0;
  SDL_Texture* cursor_texture = nullptr;
  std::array<std::uint8_t, 3> brightest{};
  std::array<std::uint8_t, 3> tint{255, 255, 255};
  // Where the next picture shown is saved (empty: not saved).
  std::filesystem::path capture;
  // The characters of a text input event not returned yet.
  std::deque<InputEvent> pending;
  bool text_input = false;
  // The floating command button, if shown.
  CommandPanel* panel = nullptr;
  std::unique_ptr<CommandPanelView> panel_view;
  bool panel_shown = true;
  bool fullscreen = false;
  // The area the mouse is confined to (`confine_mouse`).
  std::optional<WindowRect> mouse_rect;
  // Whether the system pointer is over the window (its mouse enter and
  // leave events).
  bool pointer_inside = false;
  // Whether the system honours `warp_pointer`, judged from the motions
  // after the last warp (since `warp_ns`, SDL's clock).
  WarpCheck warp_check;
  std::uint64_t warp_ns = 0;
  bool refusal_logged = false;
  // The player's wide-screen setting (`set_wide_aspect`): whether the
  // window may be wider than 4:3 and a wide screen follows it.
  bool wide_aspect = false;
  // Called after each Alt+Enter.
  std::function<void(bool)> fullscreen_toggled;
  // The Enter key of the Alt+Enter being held: its repeats and release are
  // dropped.
  std::optional<SDL_Keycode> toggle_key;

  // Confines the mouse while fullscreen to the picture (away from the
  // other displays and the black bars of a screen of another shape), frees
  // it otherwise. The picture is the whole screen: the side fill beside a
  // narrower frame is in, where the cursor is drawn while the game's
  // coordinates stop at the frame.
  void confine_mouse() {
#ifndef SDL_PLATFORM_ANDROID  // always fullscreen, the pointer a finger
    if (!fullscreen) {
      mouse_rect.reset();
      SDL_SetWindowMouseRect(window, nullptr);
      return;
    }
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    SDL_RenderCoordinatesToWindow(renderer, 0, 0, &x0, &y0);
    SDL_RenderCoordinatesToWindow(renderer, static_cast<float>(screen_w), static_cast<float>(kScreenHeight), &x1, &y1);
    // Rounded outward, so that the edge rows and columns stay within reach
    // (the charts' edge scroll).
    const int left = static_cast<int>(std::floor(x0));
    const int top = static_cast<int>(std::floor(y0));
    mouse_rect = WindowRect{left, top, static_cast<int>(std::ceil(x1)) - left, static_cast<int>(std::ceil(y1)) - top};
    const SDL_Rect rect{mouse_rect->x, mouse_rect->y, mouse_rect->w, mouse_rect->h};
    if (!SDL_SetWindowMouseRect(window, &rect)) {
      SDL_Log("%s: cannot confine the mouse: %s", edition::kExecutable, SDL_GetError());
    }
#endif
  }

  // Keeps the window within its aspect ratios (`window_aspect_range`),
  // unless fullscreen: they apply again when it leaves fullscreen.
  void limit_aspect() {
#ifndef SDL_PLATFORM_ANDROID  // the window is the screen
    if (fullscreen) {
      return;
    }
    const AspectRange range = window_aspect_range(wide_aspect);
    if (!SDL_SetWindowAspectRatio(window, range.min, range.max)) {
      SDL_Log("%s: cannot keep the window's aspect ratio: %s", edition::kExecutable, SDL_GetError());
    }
#endif
  }

  // Logs once that the system does not move the pointer.
  void log_warp_refusal() {
    if (warp_check.refused() && !refusal_logged) {
      SDL_Log("%s: the system does not move the pointer; the game's pointer stays with it", edition::kExecutable);
      refusal_logged = true;
    }
  }

  void set_fullscreen(bool on) {
    if (!SDL_SetWindowFullscreen(window, on)) {
      SDL_Log("%s: cannot change the fullscreen state: %s", edition::kExecutable, SDL_GetError());
      return;
    }
    fullscreen = on;
    confine_mouse();
    limit_aspect();
  }

  // Gives the command button the window's current output size and display
  // scale: they change with a resize, a fold or unfold, a move to another
  // display or a rotation, before any touch.
  void sync_panel_window() {
    if (panel == nullptr) {
      return;
    }
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    if (w > 0 && h > 0) {
      panel->set_window(static_cast<float>(w), static_cast<float>(h), SDL_GetWindowDisplayScale(window));
    }
  }

  // Returns the left column of the frame on the screen.
  int frame_x() const { return (screen_w - frame_w) / 2; }

  // Sets the logical screen: the window's aspect when wide and the
  // player's setting is on (and never narrower than the frame), 640 wide
  // otherwise, letterboxed with black bars.
  void fit_screen() {
    int w = kScreenWidth;
    if (wide && wide_aspect) {
      int ww = 0;
      int wh = 0;
      SDL_GetWindowSize(window, &ww, &wh);
      w = std::max(wide_screen_width(ww, wh), frame_w);
    }
    if (w != screen_w) {
      screen_w = w;
      if (!SDL_SetRenderLogicalPresentation(renderer, screen_w, kScreenHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        sdl_fail("SDL_SetRenderLogicalPresentation");
      }
      confine_mouse();
    }
  }

  // Draws the side fill beside the frame at column `x`.
  void render_sides(int x) {
    const auto side = [&](SDL_Texture* t, bool left) {
      if (t == nullptr) {
        return;
      }
      float fw = 0;
      float fh = 0;
      SDL_GetTextureSize(t, &fw, &fh);
      const SDL_FRect dst{left ? static_cast<float>(x) - fw : static_cast<float>(x + frame_w), 0, fw,
                          static_cast<float>(kScreenHeight)};
      SDL_RenderTexture(renderer, t, nullptr, &dst);
    };
    if (side_fill.kind == SideFill::Kind::kBlack) {
      return;
    }
    if (side_fill.kind == SideFill::Kind::kPicture) {
      side(side_pictures[0], true);
      side(side_pictures[1], false);
      return;
    }
    // The picture repeated across the screen, one copy under the frame:
    // each side continues from the opposite edge of the picture.
    SDL_Texture* fill = side_textures[static_cast<std::size_t>(side_fill.kind)];
    if (fill != nullptr) {
      SDL_SetTextureColorMod(fill, tint[0], tint[1], tint[2]);
    }
    side(fill, true);
    side(fill, false);
  }

  // Converts `picture` through `lut` into `target`, (re)made at its size;
  // none for an empty picture.
  void upload_picture(SDL_Texture*& target, const Image8& picture, const std::array<std::uint32_t, 256>& lut) {
    float tw = 0;
    float th = 0;
    if (target != nullptr) {
      SDL_GetTextureSize(target, &tw, &th);
    }
    if (target != nullptr && (picture.width() != static_cast<int>(tw) || picture.height() != static_cast<int>(th))) {
      SDL_DestroyTexture(target);
      target = nullptr;
    }
    if (picture.width() == 0 || picture.height() == 0) {
      return;
    }
    if (target == nullptr) {
      target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, picture.width(),
                                  picture.height());
      if (target == nullptr) {
        sdl_fail("SDL_CreateTexture");
      }
      SDL_SetTextureScaleMode(target, scale_mode);
    }
    std::vector<std::uint32_t> pixels(picture.pixels().size());
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      pixels[i] = lut[picture.pixels()[i]];
    }
    SDL_UpdateTexture(target, nullptr, pixels.data(), picture.width() * 4);
  }

  // Makes the texture of the cursor beyond the frame, through `lut`: its
  // transparent pixels clear.
  void upload_cursor(const std::array<std::uint32_t, 256>& lut) {
    if (!cursor) {
      return;
    }
    const Image8& picture = cursor->image;
    float tw = 0;
    float th = 0;
    if (cursor_texture != nullptr) {
      SDL_GetTextureSize(cursor_texture, &tw, &th);
      if (picture.width() != static_cast<int>(tw) || picture.height() != static_cast<int>(th)) {
        SDL_DestroyTexture(cursor_texture);
        cursor_texture = nullptr;
      }
    }
    if (cursor_texture == nullptr) {
      cursor_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         picture.width(), picture.height());
      if (cursor_texture == nullptr) {
        sdl_fail("SDL_CreateTexture");
      }
      SDL_SetTextureBlendMode(cursor_texture, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureScaleMode(cursor_texture, scale_mode);
    std::vector<std::uint32_t> pixels(picture.pixels().size());
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      pixels[i] = cursor->opaque.pixels()[i] != 0 ? 0xff000000u | lut[picture.pixels()[i]] : 0;
    }
    SDL_UpdateTexture(cursor_texture, nullptr, pixels.data(), picture.width() * 4);
  }

  // Draws the cursor beyond the frame, the frame at column `x` of the
  // screen. In window space: the logical presentation clips to the screen,
  // and the cursor may be over the black bars.
  void render_cursor(int x) {
    if (!cursor || cursor_texture == nullptr) {
      return;
    }
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    SDL_RenderCoordinatesToWindow(renderer, static_cast<float>(x + cursor_x), static_cast<float>(cursor_y), &x0, &y0);
    SDL_RenderCoordinatesToWindow(renderer, static_cast<float>(x + cursor_x + cursor->image.width()),
                                  static_cast<float>(cursor_y + cursor->image.height()), &x1, &y1);
    // Window points to output pixels.
    const float density = SDL_GetWindowPixelDensity(window);
    const SDL_FRect dst{x0 * density, y0 * density, (x1 - x0) * density, (y1 - y0) * density};
    int logical_w = 0;
    int logical_h = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_RenderTexture(renderer, cursor_texture, nullptr, &dst);
    SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, mode);
  }

  // Gives the game a pointer motion to where the pointer is over the frame
  // at column `x` of the screen (none while the pointer is elsewhere).
  void post_pointer_motion(int x) {
#ifndef SDL_PLATFORM_ANDROID  // a finger, not a pointer
    if (!pointer_inside) {
      return;
    }
    float wx = 0;
    float wy = 0;
    SDL_GetMouseState(&wx, &wy);
    float rx = 0;
    float ry = 0;
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &rx, &ry);
    InputEvent e;
    e.type = EventType::kMouseMove;
    e.x = static_cast<int>(std::floor(rx)) - x;
    e.y = static_cast<int>(std::floor(ry));
    pending.push_back(e);
#else
    (void)x;
#endif
  }

  // Saves the picture rendered to `capture`.
  void save_capture() {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr || !SDL_SaveBMP(surface, capture.string().c_str())) {
      SDL_Log("%s: cannot save %s: %s", edition::kExecutable, capture.string().c_str(), SDL_GetError());
    }
    SDL_DestroySurface(surface);
    capture.clear();
  }

  // Shows the picture's texture and the command button.
  void render() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    const int x = frame_x();
    if (x > 0) {
      render_sides(x);
    }
    const SDL_FRect dst{static_cast<float>(x), 0, static_cast<float>(frame_w), static_cast<float>(kScreenHeight)};
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    if (bottom_strip != nullptr && bottom_texture != nullptr) {
      const auto w = static_cast<float>(bottom_strip->width());
      const auto h = static_cast<float>(bottom_strip->height());
      const SDL_FRect bottom{(static_cast<float>(screen_w) - w) / 2, static_cast<float>(kScreenHeight) - h, w, h};
      SDL_RenderTexture(renderer, bottom_texture, nullptr, &bottom);
    }
    render_cursor(x);
    if (panel_view && panel_shown) {
      sync_panel_window();
      panel_view->draw(*panel);
    }
    if (!capture.empty()) {
      save_capture();
    }
    SDL_RenderPresent(renderer);
  }

  // Passes a mouse event `e` (window coordinates) to the command button;
  // returns its result, or nothing when the event is not its own.
  std::optional<PanelInput> panel_input(const SDL_Event& e) {
    if (panel == nullptr || !panel_shown) {
      return std::nullopt;
    }
    sync_panel_window();
    // Window points to output pixels.
    const float density = SDL_GetWindowPixelDensity(window);
    PanelInput in;
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
      in = panel->move(e.motion.x * density, e.motion.y * density);
    } else if (e.button.button != SDL_BUTTON_LEFT) {
      in = panel->other_button(e.button.x * density, e.button.y * density);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      in = panel->press(e.button.x * density, e.button.y * density);
    } else {
      in = panel->release(e.button.x * density, e.button.y * density);
    }
    if (in.redraw) {
      render();
    }
    if (in.save_failed) {
      SDL_Log("%s: cannot save the command button's place", edition::kExecutable);
    }
    return in;
  }
};

Display::Display(std::string_view title, int scale, Scaling scaling, bool wide, int window_w, int window_h,
                 bool fullscreen)
    : impl_(std::make_unique<Impl>()) {
  impl_->wide = wide;
  impl_->wide_aspect = wide;
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (fullscreen) {
    // Created fullscreen: no window shown first.
    flags |= SDL_WINDOW_FULLSCREEN;
    impl_->fullscreen = true;
  }
#ifdef SDL_PLATFORM_ANDROID
  // Landscape only (either way up); the resizable window follows the
  // screen as a foldable folds and unfolds. Fullscreen hides the system
  // bars (immersive mode). A finger is the left mouse button.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
  flags |= SDL_WINDOW_FULLSCREEN;
  impl_->fullscreen = true;
#endif
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    sdl_fail("SDL_InitSubSystem(VIDEO)");
  }
  const std::string t(title);
  if (window_w <= 0 || window_h <= 0) {
    window_w = kScreenWidth * scale;
    window_h = kScreenHeight * scale;
  }
  if (!SDL_CreateWindowAndRenderer(t.c_str(), window_w, window_h, flags, &impl_->window,
                                   &impl_->renderer)) {
    sdl_fail("SDL_CreateWindowAndRenderer");
  }
  if (!SDL_SetRenderLogicalPresentation(impl_->renderer, kScreenWidth, kScreenHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    sdl_fail("SDL_SetRenderLogicalPresentation");
  }
  SDL_SetRenderVSync(impl_->renderer, 1);
  impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     kScreenWidth, kScreenHeight);
  if (impl_->texture == nullptr) {
    sdl_fail("SDL_CreateTexture");
  }
  set_scaling(scaling);
  impl_->limit_aspect();
  impl_->fit_screen();
  impl_->confine_mouse();
  // The game draws its own mouse. SDL only hides the cursor over its own
  // windows, so the OS cursor stays visible outside.
  SDL_HideCursor();
}

Display::~Display() {
  impl_->panel_view.reset();
  for (SDL_Texture* t : impl_->side_textures) {
    if (t != nullptr) {
      SDL_DestroyTexture(t);
    }
  }
  if (impl_->bottom_texture != nullptr) {
    SDL_DestroyTexture(impl_->bottom_texture);
  }
  if (impl_->cursor_texture != nullptr) {
    SDL_DestroyTexture(impl_->cursor_texture);
  }
  for (SDL_Texture* t : impl_->side_pictures) {
    if (t != nullptr) {
      SDL_DestroyTexture(t);
    }
  }
  SDL_DestroyTexture(impl_->texture);
  SDL_DestroyRenderer(impl_->renderer);
  SDL_DestroyWindow(impl_->window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Display::present(const Image8& frame, const Palette& palette) {
  if (frame.width() < kScreenWidth || frame.width() > kMaxScreenWidth || frame.height() != kScreenHeight) {
    throw std::invalid_argument("Display::present: frame must be 640 to 1120 x 480");
  }
  const int old_x = impl_->frame_x();
  if (frame.width() != impl_->frame_w) {
    SDL_Texture* texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             frame.width(), kScreenHeight);
    if (texture == nullptr) {
      sdl_fail("SDL_CreateTexture");
    }
    SDL_SetTextureScaleMode(texture, impl_->scale_mode);
    SDL_DestroyTexture(impl_->texture);
    impl_->texture = texture;
    impl_->frame_w = frame.width();
    impl_->argb.resize(frame.pixels().size());
    impl_->confine_mouse();
  }
  impl_->fit_screen();
  if (impl_->frame_x() != old_x) {
    // The pointer has not moved, but its place in the frame has.
    impl_->post_pointer_motion(impl_->frame_x());
  }
  std::array<std::uint32_t, 256> lut{};
  for (std::size_t i = 0; i < 256; ++i) {
    const Rgb c = palette.colors[i];
    lut[i] = (std::uint32_t{c.r} << 16) | (std::uint32_t{c.g} << 8) | c.b;
  }
  const auto px = frame.pixels();
  for (std::size_t i = 0; i < px.size(); ++i) {
    impl_->argb[i] = lut[px[i]];
  }
  SDL_UpdateTexture(impl_->texture, nullptr, impl_->argb.data(), impl_->frame_w * 4);
  if (impl_->wide) {
    impl_->tint = side_fill_tint(palette, impl_->brightest);
    if (impl_->side_fill.kind == SideFill::Kind::kPicture) {
      impl_->upload_picture(impl_->side_pictures[0], impl_->side_fill.left, lut);
      impl_->upload_picture(impl_->side_pictures[1], impl_->side_fill.right, lut);
    }
    impl_->upload_picture(impl_->bottom_texture, impl_->bottom_strip != nullptr ? *impl_->bottom_strip : Image8(), lut);
  }
  impl_->upload_cursor(lut);
  impl_->render();
}

std::optional<InputEvent> Display::poll() {
  if (!impl_->pending.empty()) {
    const InputEvent e = impl_->pending.front();
    impl_->pending.pop_front();
    return e;
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.timestamp >= impl_->warp_ns &&
        e.motion.which != SDL_TOUCH_MOUSEID && e.motion.which != SDL_PEN_MOUSEID) {
      // The pointer itself (no finger nor pen), moved since the last warp.
      impl_->warp_check.reported({e.motion.x, e.motion.y});
      impl_->log_warp_refusal();
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      if (const std::optional<PanelInput> in = impl_->panel_input(e); in && in->consumed) {
        if (in->command) {
          // Typed like a key of the keyboard: down, then up.
          const PanelKey k = panel_command_key(*in->command);
          InputEvent down;
          down.type = EventType::kKeyDown;
          down.key = k.key;
          down.mods = k.mods;
          InputEvent up = down;
          up.type = EventType::kKeyUp;
          up.mods = 0;
          impl_->pending.push_back(up);
          return down;
        }
        continue;
      }
    }
    SDL_ConvertEventToRenderCoordinates(impl_->renderer, &e);
    InputEvent out;
    switch (e.type) {
      case SDL_EVENT_QUIT:
        out.type = EventType::kQuit;
        return out;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        out.type = EventType::kFocusLost;
        return out;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        out.type = EventType::kFocusGained;
        return out;
      case SDL_EVENT_TEXT_INPUT: {
        if (!impl_->text_input) {
          break;
        }
        std::vector<InputEvent> chars = text_input_events(e.text.text);
        if (chars.empty()) {
          break;
        }
        impl_->pending.insert(impl_->pending.end(), chars.begin() + 1, chars.end());
        return chars.front();
      }
      case SDL_EVENT_WINDOW_MOUSE_ENTER:
      case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        impl_->pointer_inside = e.type == SDL_EVENT_WINDOW_MOUSE_ENTER;
        impl_->warp_check.left();
        break;
      case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
      case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        // Also when the window manager changes it.
        impl_->fullscreen = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        impl_->confine_mouse();
        impl_->limit_aspect();
        break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        impl_->fit_screen();
        impl_->confine_mouse();
        impl_->sync_panel_window();
        break;
      case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      case SDL_EVENT_DISPLAY_ORIENTATION:
      case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        impl_->sync_panel_window();
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
#ifndef SDL_PLATFORM_ANDROID
        if (e.type == SDL_EVENT_KEY_DOWN && is_fullscreen_toggle(e.key)) {
          if (!e.key.repeat) {
            impl_->set_fullscreen(!impl_->fullscreen);
            if (impl_->fullscreen_toggled) {
              impl_->fullscreen_toggled(impl_->fullscreen);
            }
          }
          impl_->toggle_key = e.key.key;
          break;
        }
        if (e.type == SDL_EVENT_KEY_UP && impl_->toggle_key == e.key.key) {
          impl_->toggle_key.reset();
          break;
        }
#endif
        out.type = e.type == SDL_EVENT_KEY_DOWN ? EventType::kKeyDown : EventType::kKeyUp;
        out.key = e.key.key;
        out.mods = e.key.mod;
        out.repeat = e.key.repeat;
        out.scancode = e.key.scancode;
        if (e.key.scancode != SDL_SCANCODE_UNKNOWN) {
          // The layout's own characters: the event keycode applies the
          // SDL_HINT_KEYCODE_OPTIONS (French layouts report their number
          // row as digits) and no modifiers.
          const auto level = static_cast<SDL_Keymod>(e.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CAPS));
          out.character = SDL_GetKeyFromScancode(e.key.scancode, level, false);
          const char32_t altgr =
              SDL_GetKeyFromScancode(e.key.scancode, static_cast<SDL_Keymod>(level | SDL_KMOD_MODE), false);
          out.altgr_character = altgr != out.character ? altgr : 0;
        }
        if (impl_->text_input && key_types_text(out)) {
          // Its character comes as SDL_EVENT_TEXT_INPUT (Android's soft
          // keyboard sends both for ASCII).
          break;
        }
        return out;
      case SDL_EVENT_MOUSE_MOTION:
        out.type = EventType::kMouseMove;
        // Rounded down: left of and above the frame are negative.
        out.x = static_cast<int>(std::floor(e.motion.x)) - impl_->frame_x();
        out.y = static_cast<int>(std::floor(e.motion.y));
        return out;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::kMouseDown : EventType::kMouseUp;
        out.x = static_cast<int>(std::floor(e.button.x)) - impl_->frame_x();
        out.y = static_cast<int>(std::floor(e.button.y));
        out.button = to_button(e.button.button);
        return out;
      case SDL_EVENT_FINGER_DOWN:
      case SDL_EVENT_FINGER_MOTION:
      case SDL_EVENT_FINGER_UP:
      case SDL_EVENT_FINGER_CANCELED:
        // In render coordinates (SDL_ConvertEventToRenderCoordinates).
        out.type = e.type == SDL_EVENT_FINGER_DOWN     ? EventType::kFingerDown
                   : e.type == SDL_EVENT_FINGER_MOTION ? EventType::kFingerMove
                                                       : EventType::kFingerUp;
        out.x = static_cast<int>(e.tfinger.x);
        out.y = static_cast<int>(e.tfinger.y);
        out.finger = static_cast<std::int64_t>(e.tfinger.fingerID);
        return out;
      case SDL_EVENT_MOUSE_WHEEL:
        out.type = EventType::kMouseWheel;
        // Whole notches (a touchpad's fractions accumulated by SDL), away
        // from the user positive.
        out.wheel = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.integer_y : e.wheel.integer_y;
        return out;
      default:
        break;
    }
  }
  return std::nullopt;
}

void Display::begin_text_input(const ScreenRect& area, TextInputKind kind) {
  // The field's corners through the letterbox to window coordinates.
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  const int fx = impl_->frame_x();
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(fx + area.x), static_cast<float>(area.y), &x0,
                                &y0);
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(fx + area.x + area.w),
                                static_cast<float>(area.y + area.h), &x1, &y1);
  const SDL_Rect rect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
  SDL_SetTextInputArea(impl_->window, &rect, 0);
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                        kind == TextInputKind::kDigits ? SDL_TEXTINPUT_TYPE_NUMBER : SDL_TEXTINPUT_TYPE_TEXT);
  // The game edits the text itself: no suggestions rewriting what was typed.
  SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
  if (!SDL_StartTextInputWithProperties(impl_->window, props)) {
    SDL_Log("%s: no text input: %s", edition::kExecutable, SDL_GetError());
  }
  SDL_DestroyProperties(props);
  impl_->text_input = true;
}

void Display::end_text_input() {
  SDL_StopTextInput(impl_->window);
  impl_->text_input = false;
  impl_->pending.clear();
}

void Display::set_command_panel(CommandPanel& panel, const CommandIcons& icons) {
  impl_->panel_view = std::make_unique<CommandPanelView>(impl_->renderer, icons);
  impl_->panel = &panel;
  impl_->sync_panel_window();
}

void Display::open_command_panel_above(const ScreenRect& area) {
  if (impl_->panel == nullptr) {
    return;
  }
  impl_->sync_panel_window();
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  // The frame's coordinates to the screen's.
  const int fx = impl_->frame_x();
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(fx + area.x), static_cast<float>(area.y), &x0,
                                &y0);
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(fx + area.x + area.w),
                                static_cast<float>(area.y + area.h), &x1, &y1);
  // Window points to output pixels.
  const float density = SDL_GetWindowPixelDensity(impl_->window);
  impl_->panel->open_above({x0 * density, y0 * density, (x1 - x0) * density, (y1 - y0) * density});
}

void Display::show_command_panel(bool shown) {
  if (impl_->panel != nullptr && !shown) {
    impl_->panel->close();
  }
  impl_->panel_shown = shown;
}

void Display::capture_next(std::filesystem::path file) { impl_->capture = std::move(file); }

int Display::screen_width() const {
  if (!impl_->wide || !impl_->wide_aspect) {
    return kScreenWidth;
  }
  int w = 0;
  int h = 0;
  SDL_GetWindowSize(impl_->window, &w, &h);
  return wide_screen_width(w, h);
}

void Display::set_side_texture(SideFill::Kind kind, const RgbaImage& picture) {
  if (kind == SideFill::Kind::kPicture || kind == SideFill::Kind::kBlack) {
    throw std::invalid_argument("Display::set_side_texture: not a texture kind");
  }
  SDL_Surface* surface = SDL_CreateSurfaceFrom(picture.width, picture.height, SDL_PIXELFORMAT_ARGB8888,
                                               const_cast<std::uint32_t*>(picture.pixels.data()), picture.width * 4);
  if (surface == nullptr) {
    sdl_fail("SDL_CreateSurfaceFrom");
  }
  SDL_Texture* texture = SDL_CreateTextureFromSurface(impl_->renderer, surface);
  SDL_DestroySurface(surface);
  if (texture == nullptr) {
    sdl_fail("SDL_CreateTextureFromSurface");
  }
  SDL_SetTextureScaleMode(texture, impl_->scale_mode);
  SDL_Texture*& slot = impl_->side_textures[static_cast<std::size_t>(kind)];
  if (slot != nullptr) {
    SDL_DestroyTexture(slot);
  }
  slot = texture;
}

void Display::set_side_fill(const SideFill& fill) { impl_->side_fill = fill; }

void Display::set_bottom_strip(const Image8* strip) { impl_->bottom_strip = strip; }

void Display::set_cursor_beyond(std::optional<DecodedFrame> cursor, int x, int y) {
  impl_->cursor = std::move(cursor);
  impl_->cursor_x = x;
  impl_->cursor_y = y;
}

bool Display::fullscreen() const { return impl_->fullscreen; }

std::optional<WindowRect> Display::mouse_confinement() const { return impl_->mouse_rect; }

bool Display::pointer_inside() const { return impl_->pointer_inside; }

bool Display::pointer_warp_refused() const { return impl_->warp_check.refused(); }

void Display::set_fullscreen(bool fullscreen) {
#ifndef SDL_PLATFORM_ANDROID
  impl_->set_fullscreen(fullscreen);
#else
  (void)fullscreen;
#endif
}

void Display::on_fullscreen_toggled(std::function<void(bool)> changed) {
  impl_->fullscreen_toggled = std::move(changed);
}

void Display::warp_pointer(int x, int y) {
#ifndef SDL_PLATFORM_ANDROID
  if (!impl_->pointer_inside || impl_->warp_check.refused()) {
    return;
  }
  // The pixel's centre, so that the motion back to it rounds to the pixel.
  float wx = 0;
  float wy = 0;
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(impl_->frame_x() + x) + 0.5f,
                                static_cast<float>(y) + 0.5f, &wx, &wy);
  // Where SDL last saw the pointer: the motions already queued are older.
  WindowPoint from;
  SDL_GetMouseState(&from.x, &from.y);
  impl_->warp_ns = SDL_GetTicksNS();
  SDL_WarpMouseInWindow(impl_->window, wx, wy);
  impl_->warp_check.warped(from, {wx, wy});
  impl_->log_warp_refusal();
#else
  (void)x;
  (void)y;
#endif
}

void Display::set_scaling(Scaling scaling) {
  // Every texture of the picture, and those made from now on.
  impl_->scale_mode = scaling == Scaling::kSharp ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
  const auto apply = [this](SDL_Texture* t) {
    if (t != nullptr) {
      SDL_SetTextureScaleMode(t, impl_->scale_mode);
    }
  };
  apply(impl_->texture);
  apply(impl_->bottom_texture);
  for (SDL_Texture* t : impl_->side_textures) {
    apply(t);
  }
  for (SDL_Texture* t : impl_->side_pictures) {
    apply(t);
  }
}

void Display::set_wide_aspect(bool wide_screen) {
  impl_->wide_aspect = wide_screen;
  impl_->limit_aspect();
  // In fullscreen the window keeps its size: the screen's width changes here.
  impl_->fit_screen();
}

std::uint64_t Display::ticks_ms() { return SDL_GetTicks(); }

void Display::delay_ms(std::uint32_t ms) { SDL_Delay(ms); }

}  // namespace sh

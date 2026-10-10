#include "app/viewer.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>

#include "core/edition.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr int kListTop = 20;  // below the panel title

bool iequals(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
  });
}

constexpr std::string_view kHelp[] = {
    "Arrows, PageUp/PageDown, Home/End: move",
    "Enter: open a category; play a sound or video",
    "Left/Right: previous/next frame or view",
    "Shift+arrows: scroll a large picture",
    "Tab: the item alone on the whole screen",
    "Esc: back, then quit",
};

}  // namespace

Viewer::Viewer(const ViewerServices& services) : services_(services) {
  categories_.push_back(make_pictures_category(services));
  categories_.push_back(make_sprites_category(services));
  categories_.push_back(make_maps_category(services));
  categories_.push_back(make_skies_category(services));
  categories_.push_back(make_fonts_category(services));
  categories_.push_back(make_sounds_category(services));
  categories_.push_back(make_messages_category(services));
  categories_.push_back(make_videos_category(services));
  item_cursors_.assign(categories_.size(), 0);
}

std::size_t Viewer::list_size() {
  return level_ == Level::kCategories ? categories_.size() : category().item_count();
}

std::size_t& Viewer::list_cursor() {
  return level_ == Level::kCategories ? category_index_ : item_cursors_[category_index_];
}

void Viewer::select_item(std::size_t index) {
  item_cursors_[category_index_] = index;
  pan_x_ = 0;
  pan_y_ = 0;
  error_.clear();
  item_ok_ = false;
  try {
    category().select(index);
    item_ok_ = true;
  } catch (const DataError& e) {
    error_ = e.what();
  }
}

void Viewer::move_cursor(std::ptrdiff_t delta) {
  const std::size_t n = list_size();
  if (n == 0) {
    return;
  }
  const auto target = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(list_cursor()) + delta, 0,
                                                  static_cast<std::ptrdiff_t>(n) - 1);
  if (level_ == Level::kCategories) {
    category_index_ = static_cast<std::size_t>(target);
  } else if (static_cast<std::size_t>(target) != list_cursor() || !item_ok_) {
    select_item(static_cast<std::size_t>(target));
  }
}

void Viewer::enter_category() {
  level_ = Level::kItems;
  list_top_ = 0;
  if (category().item_count() > 0) {
    select_item(item_cursors_[category_index_]);
  } else {
    item_ok_ = false;
    error_ = "no files of this kind in the install";
  }
}

void Viewer::step_frame(int delta) {
  if (!item_ok_) {
    return;
  }
  Category& c = category();
  c.set_frame(std::clamp(c.frame() + delta, 0, c.frame_count() - 1));
}

bool Viewer::handle(const InputEvent& event) {
  if (event.type == EventType::kQuit) {
    return false;
  }
  if (event.type != EventType::kKeyDown) {
    return true;
  }
  const bool shift = (event.mods & SDL_KMOD_SHIFT) != 0;
  const std::ptrdiff_t page = (kScreenHeight - kStatusHeight - kListTop) / services_.text.line_height() - 1;
  constexpr int kPanStep = 32;
  if (shift && level_ == Level::kItems) {
    switch (event.key) {
      case SDLK_LEFT:
        pan_x_ = std::max(0, pan_x_ - kPanStep);
        return true;
      case SDLK_RIGHT:
        pan_x_ += kPanStep;
        return true;
      case SDLK_UP:
        pan_y_ = std::max(0, pan_y_ - kPanStep);
        return true;
      case SDLK_DOWN:
        pan_y_ += kPanStep;
        return true;
      default:
        break;
    }
  }
  switch (event.key) {
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
      if (fullscreen_) {
        fullscreen_ = false;
      } else if (level_ == Level::kItems) {
        level_ = Level::kCategories;
        list_top_ = 0;
        error_.clear();
      } else {
        return event.key != SDLK_ESCAPE;
      }
      return true;
    case SDLK_UP:
      move_cursor(-1);
      return true;
    case SDLK_DOWN:
      move_cursor(1);
      return true;
    case SDLK_PAGEUP:
      move_cursor(-page);
      return true;
    case SDLK_PAGEDOWN:
      move_cursor(page);
      return true;
    case SDLK_HOME:
      move_cursor(-static_cast<std::ptrdiff_t>(list_size()));
      return true;
    case SDLK_END:
      move_cursor(static_cast<std::ptrdiff_t>(list_size()));
      return true;
    case SDLK_TAB:
      fullscreen_ = !fullscreen_;
      return true;
    default:
      break;
  }
  if (level_ == Level::kCategories) {
    if (event.key == SDLK_RETURN || event.key == SDLK_RIGHT) {
      enter_category();
    }
    return true;
  }
  if (!item_ok_) {
    return true;
  }
  switch (event.key) {
    case SDLK_LEFT:
      step_frame(-1);
      break;
    case SDLK_RIGHT:
      step_frame(1);
      break;
    case SDLK_RETURN:
    case SDLK_SPACE:
      category().activate();
      break;
    default:
      category().key(event);
      break;
  }
  return true;
}

void Viewer::update(std::uint64_t now_us) {
  if (level_ == Level::kItems && item_ok_) {
    category().update(now_us);
  }
}

void Viewer::open(std::string_view path) {
  const std::size_t slash = path.find('/');
  const std::string_view category_name = path.substr(0, slash);
  const auto it = std::ranges::find_if(categories_, [&](const auto& c) { return iequals(c->name(), category_name); });
  if (it == categories_.end()) {
    throw std::invalid_argument(std::format("no viewer category '{}'", category_name));
  }
  category_index_ = static_cast<std::size_t>(it - categories_.begin());
  if (slash == std::string_view::npos) {
    level_ = Level::kCategories;
    return;
  }
  std::string_view item = path.substr(slash + 1);
  std::optional<int> frame;
  if (const std::size_t hash = item.rfind('#'); hash != std::string_view::npos) {
    frame = std::stoi(std::string(item.substr(hash + 1)));
    item = item.substr(0, hash);
  }
  Category& c = category();
  for (std::size_t i = 0; i < c.item_count(); ++i) {
    if (iequals(c.item_name(i), item)) {
      level_ = Level::kItems;
      list_top_ = 0;
      item_cursors_[category_index_] = i;
      category().select(i);
      item_ok_ = true;
      error_.clear();
      if (frame) {
        c.set_frame(std::clamp(*frame, 0, c.frame_count() - 1));
      }
      return;
    }
  }
  throw std::invalid_argument(std::format("no item '{}' in {}", item, c.name()));
}

void Viewer::render(Image8& frame, Palette& palette) {
  palette = to_palette(services_.palettes.screen("MENUBGND.PCX"));
  frame.fill(0);
  const int x0 = fullscreen_ ? 0 : kPanelWidth;
  const int y1 = fullscreen_ ? kScreenHeight - 1 : kScreenHeight - kStatusHeight - 1;
  Canvas canvas{.area = {&frame, x0, 0, kScreenWidth - 1, y1},
                .width = kScreenWidth - x0,
                .height = y1 + 1,
                .pan_x = pan_x_,
                .pan_y = pan_y_,
                .palette = palette};
  if (level_ == Level::kItems && item_ok_) {
    category().draw(canvas);
    pan_x_ = std::clamp(pan_x_, 0, std::max(0, canvas.content_width - canvas.width));
    pan_y_ = std::clamp(pan_y_, 0, std::max(0, canvas.content_height - canvas.height));
  } else if (!fullscreen_) {
    draw_help(canvas);
  }
  if (!fullscreen_) {
    const UiColours colours = UiColours::pick(palette);
    draw_panel(frame, colours);
    draw_status(frame, colours);
  }
}

void Viewer::draw_help(Canvas& canvas) {
  const UiColours colours = UiColours::pick(canvas.palette);
  const UiText& text = services_.text;
  int y = 40;
  text.draw(canvas.area, 20, y, std::format("{} asset viewer", edition::kName), colours.accent);
  y += 2 * text.line_height();
  for (const std::string_view line : kHelp) {
    text.draw(canvas.area, 20, y, line, colours.text, canvas.width - 30);
    y += text.line_height() + 2;
  }
}

void Viewer::draw_panel(Image8& frame, const UiColours& colours) {
  const UiText& text = services_.text;
  const int line = text.line_height();
  frame.fill_rect(0, 0, kPanelWidth, kScreenHeight - kStatusHeight, colours.background);
  frame.fill_rect(kPanelWidth - 1, 0, 1, kScreenHeight - kStatusHeight, colours.dim);
  const Viewport vp{&frame, 0, 0, kPanelWidth - 2, kScreenHeight - kStatusHeight - 1};
  constexpr int kMargin = 4;
  const int max_width = kPanelWidth - 2 * kMargin - 2;

  const std::size_t n = list_size();
  if (level_ == Level::kCategories) {
    text.draw(vp, kMargin, 2, "CATEGORIES", colours.accent, max_width);
  } else {
    text.draw(vp, kMargin, 2, std::format("{} ({})", category().name(), n), colours.accent, max_width);
  }
  const auto visible = static_cast<std::size_t>((kScreenHeight - kStatusHeight - kListTop) / line);
  const std::size_t cursor = list_cursor();
  if (cursor < list_top_) {
    list_top_ = cursor;
  } else if (cursor >= list_top_ + visible) {
    list_top_ = cursor - visible + 1;
  }
  for (std::size_t i = list_top_; i < n && i < list_top_ + visible; ++i) {
    const int y = kListTop + static_cast<int>(i - list_top_) * line;
    const std::string name =
        level_ == Level::kCategories ? std::string(categories_[i]->name()) : category().item_name(i);
    if (i == cursor) {
      frame.fill_rect(1, y, kPanelWidth - 3, line, colours.select);
    }
    text.draw(vp, kMargin, y, name, i == cursor ? colours.accent : colours.text, max_width);
  }
}

std::string Viewer::status_text() {
  if (level_ == Level::kCategories) {
    return std::format("{} categories", categories_.size());
  }
  if (!error_.empty()) {
    return "Error: " + error_;
  }
  if (!item_ok_) {
    return {};
  }
  Category& c = category();
  std::string s = c.item_name(item_cursors_[category_index_]);
  if (c.frame_count() > 1) {
    s += std::format("  frame {}/{}", c.frame() + 1, c.frame_count());
  }
  const std::string details = c.status();
  if (!details.empty()) {
    s += "  " + details;
  }
  if (const std::string_view help = c.keys_help(); !help.empty()) {
    s += "  [";
    s += help;
    s += "]";
  }
  return s;
}

void Viewer::draw_status(Image8& frame, const UiColours& colours) {
  const int y = kScreenHeight - kStatusHeight;
  frame.fill_rect(0, y, kScreenWidth, kStatusHeight, colours.background);
  frame.fill_rect(0, y, kScreenWidth, 1, colours.dim);
  const Viewport vp{&frame, 0, y, kScreenWidth - 1, kScreenHeight - 1};
  services_.text.draw(vp, 4, 1, status_text(), error_.empty() ? colours.text : colours.accent, kScreenWidth - 8);
}

}  // namespace sh

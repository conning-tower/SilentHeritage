#include "platform/command_panel.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>

namespace sh {

namespace {

// The first line of the position file; the second holds the button
// centre's fractions of the window width and height.
constexpr std::string_view kPositionMagic = "SH-COMMAND-BUTTON 1";
constexpr float kDefaultRelX = 0.96f;
constexpr float kDefaultRelY = 0.62f;

// Parses "<x> <y>" fractions in 0..1. A stream in the classic locale,
// not std::from_chars: the libc++ of macOS and of older NDKs has no
// floating-point from_chars.
std::optional<std::array<float, 2>> parse_position(const std::string& text) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  std::array<float, 2> v{};
  for (float& f : v) {
    if (!(in >> f) || !(f >= 0 && f <= 1)) {
      return std::nullopt;
    }
  }
  return v;
}

// Returns an input taken by the widget.
PanelInput taken(bool redraw) {
  PanelInput out;
  out.consumed = true;
  out.redraw = redraw;
  return out;
}

}  // namespace

PanelKey panel_command_key(PanelCommand command) {
  switch (command) {
    case PanelCommand::kPeriscopeDepth:
      return {SDLK_P, 0};
    case PanelCommand::kRadarDepth:
      return {SDLK_R, 0};
    case PanelCommand::kSurface:
      return {SDLK_S, 0};
    case PanelCommand::kCrashDive:
      return {SDLK_C, 0};
    case PanelCommand::kBlowBallast:
      return {SDLK_B, 0};
    case PanelCommand::kCompressionUp:
      return {SDLK_PLUS, 0};
    case PanelCommand::kCompressionDown:
      return {SDLK_MINUS, 0};
    default:
      break;
  }
  // Tubes 1..9 on the digits, tube 10 on '0'.
  const int tube = static_cast<int>(command) - static_cast<int>(PanelCommand::kFireTube1) + 1;
  return {tube == 10 ? SDLK_0 : static_cast<std::uint32_t>(SDLK_0 + tube), SDL_KMOD_LALT};
}

CommandPanel::CommandPanel(std::filesystem::path position_file)
    : position_file_(std::move(position_file)), rel_x_(kDefaultRelX), rel_y_(kDefaultRelY) {
  if (position_file_.empty()) {
    return;
  }
  std::ifstream in(position_file_);
  std::string magic;
  std::string values;
  if (in && std::getline(in, magic) && magic == kPositionMagic && std::getline(in, values)) {
    // A missing or foreign file keeps the default place.
    if (const auto v = parse_position(values)) {
      rel_x_ = (*v)[0];
      rel_y_ = (*v)[1];
    }
  }
}

void CommandPanel::set_window(float width, float height, float scale) {
  width_ = width;
  height_ = height;
  scale_ = scale > 0 ? scale : 1;
}

PanelRect CommandPanel::group_rect() const {
  const float w = kGroupWidthDp * dp();
  const float h = (kOrdersHeightDp + kCompressionHeightDp) * dp();
  const float m = kMarginDp * dp();
  // The relative centre, kept inside the window.
  const float cx = std::clamp(rel_x_ * width_, w / 2 + m, std::max(w / 2 + m, width_ - w / 2 - m));
  const float cy = std::clamp(rel_y_ * height_, h / 2 + m, std::max(h / 2 + m, height_ - h / 2 - m));
  return {cx - w / 2, cy - h / 2, w, h};
}

PanelRect CommandPanel::button_rect() const {
  const PanelRect g = group_rect();
  return {g.x, g.y, g.w, kOrdersHeightDp * dp()};
}

std::array<PanelRect, 2> CommandPanel::compression_rects() const {
  const PanelRect g = group_rect();
  const float y = g.y + kOrdersHeightDp * dp();
  const float h = kCompressionHeightDp * dp();
  // "+" on the right, "-" on the left.
  return {{{g.x + g.w / 2, y, g.w / 2, h}, {g.x, y, g.w / 2, h}}};
}

std::optional<PanelCommand> CommandPanel::compression_pressed() const {
  return state_ == State::kButtonPress ? part_ : std::nullopt;
}

float CommandPanel::panel_scale() const {
  const float w = 2 * kPaddingDp + kColumns * kIconDp + (kColumns - 1) * kGapDp;
  const float h = 2 * kPaddingDp + 3 * kIconDp + 2 * kGapDp + kGroupGapDp;
  const float m = 2 * kMarginDp * dp();
  return std::min({dp(), (width_ - m) / w, (height_ - m) / h});
}

PanelRect CommandPanel::panel_rect() const {
  const float s = panel_scale();
  const float w = (2 * kPaddingDp + kColumns * kIconDp + (kColumns - 1) * kGapDp) * s;
  const float h = (2 * kPaddingDp + 3 * kIconDp + 2 * kGapDp + kGroupGapDp) * s;
  const float m = kMarginDp * dp();
  if (anchor_) {
    // Above the anchor, centred on it.
    const float ax = std::clamp(anchor_->x + anchor_->w / 2 - w / 2, 0.0f, std::max(0.0f, width_ - w));
    const float ay = std::clamp(anchor_->y - m - h, 0.0f, std::max(0.0f, height_ - h));
    return {ax, ay, w, h};
  }
  const PanelRect b = group_rect();
  // Beside the group, on the side with more room, then kept in the window.
  float x = b.x + b.w / 2 > width_ / 2 ? b.x - m - w : b.x + b.w + m;
  x = std::clamp(x, 0.0f, std::max(0.0f, width_ - w));
  const float y = std::clamp(b.y + b.h / 2 - h / 2, m, std::max(m, height_ - h - m));
  return {x, y, w, h};
}

std::array<PanelRect, kPanelCommandCount> CommandPanel::icon_rects() const {
  const float s = panel_scale();
  const PanelRect p = panel_rect();
  const float icon = kIconDp * s;
  const float step = (kIconDp + kGapDp) * s;
  std::array<PanelRect, kPanelCommandCount> out;
  for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
    // Row 0: the depth orders; rows 1 and 2: tubes 1-5 and 6-10.
    const auto row = static_cast<float>(i / kColumns);
    const auto col = static_cast<float>(i % kColumns);
    const float group = row > 0 ? kGroupGapDp * s : 0;
    out[i] = {p.x + kPaddingDp * s + col * step, p.y + kPaddingDp * s + row * step + group, icon, icon};
  }
  return out;
}

bool CommandPanel::set_status(std::optional<PanelStatus> status) {
  for (std::size_t t = 0; t < kPanelTubeCount; ++t) {
    if (status && status->tubes[t].state == TubeState::kReloading) {
      reload_total_[t] = std::max(reload_total_[t], status->tubes[t].reload_seconds);
    } else {
      reload_total_[t] = 0;
    }
  }
  if (status == status_) {
    return false;
  }
  status_ = std::move(status);
  return open_;
}

float CommandPanel::reload_progress(std::size_t tube) const {
  if (!status_ || status_->tubes.at(tube).state != TubeState::kReloading || reload_total_.at(tube) <= 0) {
    return 0;
  }
  const auto left = static_cast<float>(status_->tubes[tube].reload_seconds);
  return std::clamp(1.0f - left / static_cast<float>(reload_total_[tube]), 0.0f, 1.0f);
}

std::optional<PanelCommand> CommandPanel::icon_at(float x, float y) const {
  const auto rects = icon_rects();
  for (std::size_t i = 0; i < rects.size(); ++i) {
    const std::size_t first_tube = static_cast<std::size_t>(PanelCommand::kFireTube1);
    if (status_ && i >= first_tube && status_->tubes[i - first_tube].state == TubeState::kAbsent) {
      continue;
    }
    if (rects[i].contains(x, y)) {
      return static_cast<PanelCommand>(i);
    }
  }
  return std::nullopt;
}

std::optional<PanelCommand> CommandPanel::pressed() const {
  if (state_ == State::kIconPress && icon_hover_) {
    return icon_;
  }
  return std::nullopt;
}

PanelInput CommandPanel::press(float x, float y) {
  if (state_ != State::kIdle) {
    // A press during another (a second pointer): part of the first.
    return taken(false);
  }
  const PanelRect g = group_rect();
  if (button_shown_ && g.contains(x, y)) {
    state_ = State::kButtonPress;
    press_x_ = x;
    press_y_ = y;
    grab_dx_ = g.x + g.w / 2 - x;
    grab_dy_ = g.y + g.h / 2 - y;
    const auto parts = compression_rects();
    part_.reset();
    if (parts[0].contains(x, y)) {
      part_ = PanelCommand::kCompressionUp;
    } else if (parts[1].contains(x, y)) {
      part_ = PanelCommand::kCompressionDown;
    }
    return taken(part_.has_value());
  }
  if (!open_) {
    return {};
  }
  if (const std::optional<PanelCommand> icon = icon_at(x, y)) {
    state_ = State::kIconPress;
    icon_ = icon;
    icon_hover_ = true;
    return taken(true);
  }
  state_ = State::kSwallow;
  if (!panel_rect().contains(x, y)) {
    // A tap outside closes the panel and does not reach the game.
    open_ = false;
    return taken(true);
  }
  return taken(false);
}

PanelInput CommandPanel::move(float x, float y) {
  switch (state_) {
    case State::kIdle:
      return {};
    case State::kButtonPress: {
      const float threshold = kDragThresholdDp * dp();
      const float dx = x - press_x_;
      const float dy = y - press_y_;
      if (dx * dx + dy * dy <= threshold * threshold) {
        return taken(false);
      }
      state_ = State::kDrag;
      part_.reset();
      [[fallthrough]];
    }
    case State::kDrag:
      rel_x_ = std::clamp((x + grab_dx_) / width_, 0.0f, 1.0f);
      rel_y_ = std::clamp((y + grab_dy_) / height_, 0.0f, 1.0f);
      return taken(true);
    case State::kIconPress: {
      const bool hover = icon_at(x, y) == icon_;
      const bool changed = hover != icon_hover_;
      icon_hover_ = hover;
      return taken(changed);
    }
    case State::kSwallow:
      return taken(false);
  }
  return {};
}

PanelInput CommandPanel::release(float x, float y) {
  PanelInput out = taken(false);
  out.consumed = state_ != State::kIdle;
  switch (state_) {
    case State::kIdle:
      return out;
    case State::kButtonPress:
      if (part_) {
        out.command = part_;
      } else {
        open_ = !open_;
        anchor_.reset();
      }
      out.redraw = true;
      break;
    case State::kDrag:
      // The place kept is the one inside the window.
      {
        const PanelRect b = group_rect();
        rel_x_ = (b.x + b.w / 2) / width_;
        rel_y_ = (b.y + b.h / 2) / height_;
      }
      out.save_failed = !save();
      out.redraw = true;
      break;
    case State::kIconPress:
      if (icon_at(x, y) == icon_) {
        out.command = icon_;
      }
      out.redraw = true;
      break;
    case State::kSwallow:
      break;
  }
  state_ = State::kIdle;
  icon_.reset();
  icon_hover_ = false;
  part_.reset();
  return out;
}

PanelInput CommandPanel::other_button(float x, float y) const {
  const bool over = (button_shown_ && group_rect().contains(x, y)) || (open_ && panel_rect().contains(x, y));
  PanelInput out = taken(false);
  out.consumed = over || state_ != State::kIdle;
  return out;
}

void CommandPanel::set_button_shown(bool shown) {
  if (shown == button_shown_) {
    return;
  }
  button_shown_ = shown;
  if (state_ == State::kButtonPress || state_ == State::kDrag) {
    state_ = State::kIdle;
    part_.reset();
  }
}

void CommandPanel::open_above(PanelRect anchor) {
  anchor_ = anchor;
  open_ = true;
}

bool CommandPanel::close() {
  const bool changed = open_ || state_ != State::kIdle;
  open_ = false;
  anchor_.reset();
  state_ = State::kIdle;
  icon_.reset();
  icon_hover_ = false;
  part_.reset();
  return changed;
}

bool CommandPanel::save() const {
  if (position_file_.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(position_file_.parent_path(), ec);
  std::filesystem::path tmp = position_file_;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << kPositionMagic << '\n' << rel_x_ << ' ' << rel_y_ << '\n';
    if (!out.flush()) {
      return false;
    }
  }
  std::filesystem::rename(tmp, position_file_, ec);
  return !ec;
}

}  // namespace sh

#include "game/stations/view_drag.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/fixed_math.h"
#include "game/mission.h"
#include "game/stations/attack_services.h"
#include "sim/player_combat.h"
#include "ui/dialog.h"

namespace sh {

namespace {

// The focal length of the 3D views at 1x (render_list.h `kNearZ`).
constexpr double kFocal = 460;

bool inside(const ui::Rect& r, int x, int y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }

}  // namespace

double view_tenths_per_pixel(Fixed16 zoom) {
  const double z = std::max(zoom.to_double(), 1.0);
  return std::atan(1.0 / (kFocal * z)) * 1800.0 / std::numbers::pi;
}

double view_drag_acceleration(double px_per_s) { return std::clamp(std::abs(px_per_s) / kFlingSpeed, 1.0, 4.0); }

bool ViewDrag::event(const ui::Event& event, std::uint32_t tick) {
  if (event.is_key()) {
    // A key trains the view itself.
    rate_ = 0;
    return false;
  }
  const std::uint16_t flags = event.flags;
  if ((flags & ui::kEventLeftDown) != 0) {
    rate_ = 0;
    const auto hit = [&](const ui::Rect& r) { return inside(r, event.x, event.y); };
    if ((hooks_.enabled && !hooks_.enabled()) || !std::any_of(regions_.begin(), regions_.end(), hit) ||
        std::any_of(holes_.begin(), holes_.end(), hit)) {
      return false;
    }
    drag_.press(event.x, event.y, tick);
    across_ = 0;
    up_ = 0;
    hooks_.capture();
    return false;
  }
  if (!drag_.active()) {
    return false;
  }
  if ((flags & ui::kEventLeftUp) != 0 || (event.buttons & ui::kButtonLeft) == 0) {
    const bool was = drag_.dragging();
    if (was && hooks_.inertia) {
      const double speed = drag_.speed_x(tick);
      if (std::abs(speed) > kFlingSpeed) {
        rate_ = -speed * hooks_.tenths_per_px() * view_drag_acceleration(speed);
        rate_tick_ = tick;
      }
    }
    drag_.end();
    if (was && hooks_.cursor) {
      hooks_.cursor(false);
    }
    return was;
  }
  if ((flags & ui::kEventMoved) == 0) {
    return drag_.dragging();
  }
  const bool was = drag_.dragging();
  const int step = drag_.move(event.x, event.y, tick);
  if (!drag_.dragging()) {
    return false;
  }
  if (!was) {
    hooks_.release_dialog();
    if (hooks_.cursor) {
      hooks_.cursor(true);
    }
  }
  across_ -= step * hooks_.tenths_per_px() * view_drag_acceleration(drag_.speed_x(tick));
  up_ += drag_.step_dy() * hooks_.up_tenths_per_px;
  hooks_.turn(across_, up_);
  return true;
}

void ViewDrag::update(std::uint32_t tick) {
  if (drag_.dragging() && hooks_.cursor) {
    hooks_.cursor(true);
  }
  if (rate_ == 0 || tick == rate_tick_) {
    return;
  }
  const double dt = static_cast<double>(tick - rate_tick_) / 100.0;
  rate_tick_ = tick;
  // The exact integral of the decaying rate over the step.
  const double decay = std::exp(-dt / kFlingTau);
  across_ += rate_ * kFlingTau * (1 - decay);
  rate_ *= decay;
  hooks_.turn(across_, up_);
  if (std::abs(rate_) < kFlingStopRate) {
    rate_ = 0;
  }
}

void ViewDrag::stop() {
  rate_ = 0;
  drag_.end();
}

void release_dialog_press(ui::Dialog& dialog) {
  ui::Event up;
  up.flags = ui::kEventLeftUp;
  up.x = -0x4000;
  up.y = -0x4000;
  dialog.handle_event(up);
}

ViewDrag::Hooks view_bearing_hooks(AttackServices& services, const Fixed16* zoom,
                                   std::function<ui::Dialog*()> dialog) {
  auto start = std::make_shared<std::int32_t>(0);
  ViewDrag::Hooks h;
  h.tenths_per_px = [zoom] { return view_tenths_per_pixel(zoom != nullptr ? *zoom : Fixed16::from_int(1)); };
  h.capture = [&services, start] { *start = services.mission().player_combat().view_bearing.normalized().tenths(); };
  h.turn = [&services, start](double across, double) {
    PlayerCombat& combat = services.mission().player_combat();
    const auto t = static_cast<std::int32_t>(std::lround(across));
    combat.view_bearing = Angle10(*start + t).normalized();
    combat.tdc.toggle_bearing_follow(true);
  };
  h.release_dialog = [dialog = std::move(dialog)] {
    if (ui::Dialog* d = dialog()) {
      release_dialog_press(*d);
    }
  };
  h.cursor = [&services](bool on) { on ? services.set_cursor(ui::CursorId::kMove) : services.set_default_cursor(); };
  return h;
}

}  // namespace sh

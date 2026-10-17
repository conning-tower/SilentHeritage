#include "game/class_params.h"

#include <array>
#include <optional>
#include <string_view>

namespace sh {

namespace {

std::int32_t to_i32(std::uint32_t v) { return static_cast<std::int32_t>(v); }

// The Historical Realism Patch's depths by class, its periscope eye where
// it changes it.
struct RealisticDepths {
  std::string_view index_name;
  std::int32_t periscope_depth_ft;
  std::int32_t radar_depth_ft;
  std::optional<std::int32_t> periscope_eye_ft;
  std::int32_t bridge_eye_ft;
};
constexpr std::array<RealisticDepths, 9> kRealisticDepths{{
    {"S CLASS", 48, 36, 55, 25},
    {"BARRACUDA", 55, 40, 62, 25},
    {"NARWHAL", 60, 45, 67, 28},
    {"P CLASS", 55, 40, 62, 24},
    {"SALMON", 55, 40, 62, 24},
    {"T CLASS", 60, 45, std::nullopt, 24},
    {"GATO", 60, 45, std::nullopt, 26},
    {"BALAO", 60, 45, std::nullopt, 26},
    {"TENCH", 60, 45, std::nullopt, 26},
}};

}  // namespace

HullParams ship_hull_params(const ShipClass& ship) {
  HullParams h;
  h.length_ft = to_i32(ship.length_ft);
  h.beam_ft = to_i32(ship.beam_ft);
  h.draft_ft = to_i32(ship.draft_ft);
  h.displacement = to_i32(ship.displacement_tons);
  h.drag_cubic = ship.drag_cubic;
  h.drag_linear = ship.drag_linear;
  h.lateral_drag = ship.lateral_drag;
  h.astern_drag_factor = ship.astern_drag_factor;
  h.turn_factor = ship.turn_factor;
  return h;
}

ShipMotionParams ship_motion_params(const ShipClass& ship) {
  ShipMotionParams p;
  p.hull = ship_hull_params(ship);
  p.max_power = ship.max_power.raw();
  return p;
}

SubClassParams sub_class_params(const SubClass& sub) {
  SubClassParams s;
  s.length_ft = to_i32(sub.length_ft);
  s.beam_ft = to_i32(sub.beam_ft);
  s.draft_ft = to_i32(sub.draft_ft);
  s.test_depth_ft = to_i32(sub.test_depth_ft);
  s.periscope_depth_ft = to_i32(sub.periscope_depth_ft);
  s.radar_depth_ft = to_i32(sub.radar_depth_ft);
  s.periscope_height_ft = to_i32(sub.periscope_eye_ft);
  s.bridge_height_ft = to_i32(sub.bridge_eye_ft);
  s.awash_depth_ft = to_i32(sub.awash_depth_ft);
  s.noise_factor = to_i32(sub.noise_factor_pct);
  s.displacement_surfaced = to_i32(sub.displacement_surfaced);
  s.ballast_neutral = to_i32(sub.displacement_submerged);
  s.ballast_max = to_i32(sub.displacement_max);
  s.diesel_hp = to_i32(sub.diesel_hp);
  s.motor_hp = to_i32(sub.motor_hp);
  s.drag_cubic_surfaced = sub.drag_cubic[0];
  s.drag_cubic_submerged = sub.drag_cubic[1];
  s.drag_linear_surfaced = sub.drag_linear[0];
  s.drag_linear_submerged = sub.drag_linear[1];
  s.lateral_drag = sub.lateral_drag;
  s.astern_drag_factor = sub.astern_drag_factor;
  s.turn_factor = sub.turn_factor;
  s.pitch_factor = sub.pitch_factor;
  s.diesel_count = to_i32(sub.diesel_count);
  s.motor_count = to_i32(sub.motor_count);
  s.fuel_capacity = to_i32(sub.fuel_capacity);
  s.fuel_use = Fixed16::from_raw(to_i32(sub.fuel_use));
  s.power_surfaced = sub.power_surfaced;
  s.power_submerged = sub.power_submerged;
  return s;
}

SubClassParams sub_class_params(const SubClass& sub, const MissionRules& rules) {
  SubClassParams s = sub_class_params(sub);
  if (rules.realistic_depths) {
    const SubDepths d = realistic_sub_depths(sub);
    s.periscope_depth_ft = d.periscope_depth_ft;
    s.radar_depth_ft = d.radar_depth_ft;
    s.periscope_height_ft = d.periscope_eye_ft;
    s.bridge_height_ft = d.bridge_eye_ft;
  }
  return s;
}

SubDepths realistic_sub_depths(const SubClass& sub) {
  SubDepths d{to_i32(sub.periscope_depth_ft), to_i32(sub.radar_depth_ft), to_i32(sub.periscope_eye_ft),
              to_i32(sub.bridge_eye_ft)};
  for (const RealisticDepths& r : kRealisticDepths) {
    if (r.index_name == sub.index_name) {
      d.periscope_depth_ft = r.periscope_depth_ft;
      d.radar_depth_ft = r.radar_depth_ft;
      d.periscope_eye_ft = r.periscope_eye_ft.value_or(d.periscope_eye_ft);
      d.bridge_eye_ft = r.bridge_eye_ft;
    }
  }
  return d;
}

std::int32_t sub_deck_gun_rounds(const SubClass& sub) { return to_i32(sub.deck_gun_rounds); }

}  // namespace sh

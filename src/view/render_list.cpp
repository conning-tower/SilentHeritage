#include "view/render_list.h"

#include <algorithm>
#include <array>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "core/fixed_math.h"
#include "gfx/sprite.h"

namespace sh {

ViewCamera make_view_camera(Angle10 heading, std::int32_t x, std::int32_t y, std::int32_t z, Fixed16 zoom,
                            int screen_width, std::uint32_t scale_shift) {
  ViewCamera cam;
  cam.scale_shift = scale_shift;
  cam.x = x;
  cam.y = y >> scale_shift;
  cam.z = z;
  cam.heading = heading.normalized().tenths();
  cam.zoom = zoom.raw() < fx::kOne ? fx::kOne : zoom.raw();
  cam.inv_zoom = fx::div(fx::kOne, cam.zoom);
  // angle_sincos(-heading): the screen's right axis is (cos h, -sin h).
  const fx::SinCos right = fx::angle_sincos(Angle10(-cam.heading));
  cam.right_x = right.cos;
  cam.right_z = right.sin;
  const fx::Mat3 rotation = fx::mat3_from_euler(Angle10(cam.heading), Angle10(0), Angle10(0));
  cam.view = rotation;
  cam.inverse = fx::transpose(rotation);
  for (std::size_t c = 0; c < 3; ++c) {
    cam.view.m[6 + c] = fx::mul(cam.view.m[6 + c], cam.inv_zoom);
  }
  cam.screen_width = screen_width;
  cam.half_width = screen_width / 2;
  return cam;
}

void RenderList::add(const RenderObject& object, std::uint32_t sort_key, std::int32_t grid_width,
                     std::uint32_t scale_shift) {
  if (entries_.size() >= kMaxRenderEntries) {
    overflowed_ = true;
    return;
  }
  RenderEntry e;
  e.type = object.type;
  e.owner = &object;
  e.sprite = object.sprite;
  e.x = object.x;
  e.y = object.y;
  e.z = object.z;
  e.heading = object.heading.tenths();
  e.size = object.size;
  e.distance = sort_key;
  e.draw_unplaced = object.draw_unplaced;
  const std::int32_t cell = 0x10000 >> scale_shift;
  e.cell = fx::imul(object.z / cell, grid_width) + object.x / cell;
  entries_.push_back(e);
}

void RenderList::project_all(const ViewCamera& camera) {
  order_.clear();
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    project(i, camera);
  }
}

// render_list_project_entry (0x91bc8)
void RenderList::project(std::size_t index, const ViewCamera& camera) {
  RenderEntry& e = entries_[index];
  e.x -= camera.x;
  e.y -= camera.y;
  e.z -= camera.z;
  const fx::Vec3 v = fx::transform(camera.view, {e.x, 0, e.z});
  e.view_z = v.z;
  e.view_x = v.x;
  const std::uint32_t key_in = e.distance;
  if (key_in != 0xffffffffu) {
    if (e.view_z < kMinEntryZ || e.view_z > kMaxEntryZ) {
      return;
    }
    e.bearing = fx::heading_from_delta(e.view_x, e.view_z).tenths();
    if (e.bearing < 0xb54 && e.bearing > 700) {
      return;
    }
  }
  e.distance = fx::approx_distance(e.view_x, e.view_z);
  if (key_in != 0xffffffffu) {
    e.screen_x = fx::imul(e.view_x, kFocalX) / e.view_z + camera.half_width;
    e.screen_y = fx::imul(e.y, -kFocalY) / e.view_z + kHorizonRow;
    e.size = projected_size(e.size, e.distance, camera.scale_shift);
  }
  const std::uint32_t key = key_in == 0 ? e.distance : key_in;
  // Inserted before the first entry nearer than `key`, after the ties.
  auto it = order_.begin();
  while (it != order_.end() && entries_[*it].distance >= key) {
    ++it;
  }
  order_.insert(it, index);
}

std::uint32_t projected_size(std::uint32_t size, std::uint32_t distance, std::uint32_t scale_shift) {
  const std::uint64_t divisor = std::uint64_t{distance} << scale_shift;
  return static_cast<std::uint32_t>(size / std::max<std::uint64_t>(divisor, 1));
}

std::int32_t render_view_aspect_frame(const RenderEntry& entry) {
  // The frame of each degree (SH.EXE 0x10ff54, 360 entries): finer steps
  // near bow-on and stern-on.
  static const std::array<std::int32_t, 360> kFrameOfDegree = [] {
    constexpr std::uint32_t kAddress = 0x10ff54;
    const auto t = exe_i32_array<360>(game_exe(), kAddress);
    check_exe_table(t.front() == 0 && std::ranges::is_sorted(t) && t.back() < 64, "aspect frame", kAddress);
    return t;
  }();
  std::int32_t bearing = fx::heading_from_delta(-entry.x, -entry.z).tenths();
  // Near the screen edges the bearing is corrected by the angle off the
  // 25 deg cone, as if the object were seen from the view axis.
  if (entry.bearing <= 0xd16 && entry.bearing >= 0xfa) {
    const std::int32_t edge = entry.bearing < 0x708 ? 0xfa : 0xd16;
    bearing = fx::angle_sub(Angle10(bearing), fx::angle_sub(Angle10(entry.bearing), Angle10(edge))).tenths();
  }
  const std::int32_t degree = fx::angle_sub(Angle10(bearing), Angle10(entry.heading)).tenths() / 10;
  return kFrameOfDegree[static_cast<std::size_t>(std::clamp(degree, 0, 359))];
}

}  // namespace sh

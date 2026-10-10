#include <catch2/catch_test_macros.hpp>

#include "core/fixed_math.h"
#include "sim/geometry.h"

namespace sh {

using namespace geom;
using namespace fx;

TEST_CASE("approx_distance and bearing/range on known points", "[sim][geometry]") {
  CHECK(approx_distance(3000, 4000) == 4999);
  CHECK(approx_distance(-4000, 3000) == 4999);
  CHECK(approx_distance(0, 0) == 0);
  CHECK(approx_distance(1000, 0) == 1000);
  CHECK(approx_distance(1000, 1000) == 1414);
  const WorldXZ origin{100000, 200000};
  CHECK(bearing_to(origin, {100000 + 48 * 1000, 200000}).tenths() == 900);
  CHECK(range_yards(origin, {100000 + 48 * 1000, 200000}) == 1000);
  CHECK(bearing_to(origin, {100000 - 48 * 3000, 200000 - 48 * 4000}).tenths() == 1800 + 368);
  CHECK(range_yards(origin, {100000 - 48 * 3000, 200000 - 48 * 4000}) == 4999);
  CHECK(angle_diff(Angle10(100), Angle10(3500)) == 200);
  CHECK(angle_diff(Angle10(3500), Angle10(100)) == -200);
}

TEST_CASE("Orientation matrices point a heading's forward axis", "[sim][geometry]") {
  // Vehicles build their matrix from the negated heading.
  const fx::Mat3 east = mat3_from_euler(Angle10(-900), Angle10(0), Angle10(0));
  CHECK(fx::transform(east, {0, 0, fx::kOne}) == fx::Vec3{fx::kOne, 0, 0});
  const fx::Mat3 south = mat3_from_euler(Angle10(-1800), Angle10(0), Angle10(0));
  CHECK(fx::transform(south, {0, 0, fx::kOne}) == fx::Vec3{0, 0, -fx::kOne});
  const fx::Mat3 nose_up = mat3_from_euler(Angle10(0), Angle10(900), Angle10(0));
  CHECK(fx::transform(nose_up, {0, 0, fx::kOne}).y == fx::kOne);
  CHECK(fx::transform(fx::transpose(east), {fx::kOne, 0, 0}) == fx::Vec3{0, 0, fx::kOne});
  CHECK(tenths_to_radians16(Angle10(1800)) == 205887);
  CHECK(radians16_to_tenths(102943).tenths() == 899);
  CHECK(point_rotate_scale({0, 100}, {0, 0}, Angle10(900)) == Point2{-100, 0});
}

}  // namespace sh

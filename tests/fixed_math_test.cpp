#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "assets/exe_image.h"
#include "core/fixed.h"
#include "core/fixed_math.h"
#include "formula_tables.h"
#include "game_data.h"

namespace sh {

using namespace fx;

TEST_CASE("The trigonometry tables are SH.EXE's", "[fixedmath]") {
  SH_REQUIRE_INSTALL(install);
  const ExeImage exe = ExeImage::load(install);
  // The installed tables are SH.EXE's, and the formula tables of the runs
  // without SH_DATA equal them.
  const fx::Tables formula = test::formula_tables();
  for (std::uint32_t i = 0; i < formula.cos.size(); ++i) {
    INFO("cos " << i);
    REQUIRE(exe.i32(0xad5e7 + 4 * i) == formula.cos[i]);
    REQUIRE(angle_sincos(Angle10(static_cast<std::int32_t>(i))).cos == formula.cos[i]);
  }
  for (std::uint32_t i = 0; i < formula.atan_tenths.size(); ++i) {
    INFO("atan " << i);
    REQUIRE(static_cast<std::int16_t>(exe.u16(0x10b5ca + 2 * i)) == formula.atan_tenths[i]);
  }
  for (std::uint32_t i = 0; i < formula.hypot_excess.size(); ++i) {
    INFO("hypot " << i);
    REQUIRE(exe.u16(0x10b7cc + 2 * i) == formula.hypot_excess[i]);
    REQUIRE(approx_distance(0x10000, static_cast<std::int32_t>(i << 8)) == 0x10000u + formula.hypot_excess[i]);
  }
  // The arcsine table 0x1095c8 has 4097 entries, one per 16 raw units.
  for (std::uint32_t i = 0; i < formula.asin_tenths.size(); ++i) {
    INFO("asin " << i);
    REQUIRE(exe.u16(0x1095c8 + 2 * i) == formula.asin_tenths[i]);
    REQUIRE(fixed_asin(static_cast<std::int32_t>(i << 4)) == formula.asin_tenths[i]);
  }
}

TEST_CASE("fx arithmetic rounds and saturates like the original", "[fixedmath]") {
  CHECK(mul(0x18000, 0x8000) == 0xc000);
  CHECK(mul(1, 0x8000) == 1);    // 0.5 raw rounds up.
  CHECK(mul(-1, 0x8000) == 0);   // -0.5 raw rounds up too.
  CHECK(mul(INT32_MAX, INT32_MAX) == -0x10000);  // Wraps to 32 bits.
  CHECK(mul_floor(-1, 0x8000) == -1);
  CHECK(mul_floor(1, 0x8000) == 0);
  CHECK((Fixed16::from_raw(-3) * Fixed16::from_raw(0x8000)).raw() == mul_floor(-3, 0x8000));
  CHECK(div(3 << 16, 2 << 16) == 0x18000);
  CHECK(div(-7, 2 << 16) == -3);  // Truncates toward zero.
  CHECK(div(0x7fff0000, 0x10000) == 0x7fff0000);
  // The guard is conservative: it saturates from half the representable range.
  CHECK(div(0x10000000, 0x1000) == INT32_MAX);
  CHECK(div(-0x10000000, 0x1000) == INT32_MIN);
  CHECK(div(5, 0) == 0);
  CHECK(trunc(-0x18000) == -1);
  CHECK(trunc(0x18000) == 1);
  CHECK(wadd(INT32_MAX, 1) == INT32_MIN);
  CHECK(wsub(INT32_MIN, 1) == INT32_MAX);
  CHECK(imul(0x10000, 0x10000) == 0);
}

TEST_CASE("int64_div treats 0 as 1 and saturates on a non-zero high word", "[fixedmath]") {
  CHECK(int64_div(10, 0) == 10);
  CHECK(int64_div(-10, 5) == -2);
  // A small negative numerator has a high word of -1: it saturates when
  // |d| < 4, like the original.
  CHECK(int64_div(-10, 3) == INT32_MIN);
  CHECK(int64_div(-1, -1) == INT32_MAX);
  CHECK(int64_div(std::int64_t{1} << 32, 4) == 1 << 30);
  CHECK(int64_div(std::int64_t{1} << 32, 3) == INT32_MAX);
}

TEST_CASE("Angle sums and differences only fold one way", "[fixedmath]") {
  CHECK(angle_add(Angle10(3599), Angle10(1)) == Angle10(0));
  CHECK(angle_add(Angle10(3500), Angle10(3500)) == Angle10(3400));
  CHECK(angle_add(Angle10(-100), Angle10(50)) == Angle10(-50));
  CHECK(angle_sub(Angle10(100), Angle10(3500)) == Angle10(200));
  CHECK(angle_sub(Angle10(5000), Angle10(100)) == Angle10(4900));
  CHECK(angle_diff(Angle10(0), Angle10(1800)) == 1800);
  CHECK(angle_diff(Angle10(0), Angle10(1799)) == -1799);
}

TEST_CASE("angle_sincos reads the rounded cosine table", "[fixedmath]") {
  CHECK(angle_sincos(Angle10(0)).cos == 65536);
  CHECK(angle_sincos(Angle10(0)).sin == 0);
  CHECK(angle_sincos(Angle10(3)).cos == 65535);
  CHECK(angle_sincos(Angle10(900)).cos == 0);
  CHECK(angle_sincos(Angle10(900)).sin == 65536);
  CHECK(angle_sincos(Angle10(896)).cos == 458);
  CHECK(angle_sincos(Angle10(1800)).cos == -65536);
  CHECK(angle_sincos(Angle10(2700)).sin == -65536);
  CHECK(angle_sincos(Angle10(300)).cos == 56756);
  CHECK(angle_sincos(Angle10(300)).sin == 32768);
  CHECK(angle_sincos(Angle10(450)).cos == angle_sincos(Angle10(450)).sin);
  CHECK(angle_sincos(Angle10(-900)).sin == -65536);
  CHECK(angle_sincos(Angle10(3600)).cos == 65536);
  CHECK(angle_sincos(Angle10(3600 * 5 + 300)).sin == 32768);
  CHECK(angle_sincos(Angle10(-3600 * 5 - 300)).sin == -32768);
}

TEST_CASE("heading_from_delta gives compass headings with the original quirks", "[fixedmath]") {
  CHECK(heading_from_delta(0, 100).tenths() == 0);
  CHECK(heading_from_delta(100, 0).tenths() == 900);
  CHECK(heading_from_delta(0, -100).tenths() == 1800);
  CHECK(heading_from_delta(-100, 0).tenths() == 2700);
  CHECK(heading_from_delta(100, 100).tenths() == 450);
  CHECK(heading_from_delta(-100, -100).tenths() == 2250);
  // 100/173 -> table entry 147 = 29.8 deg.
  CHECK(heading_from_delta(100, 173).tenths() == 298);
  CHECK(heading_from_delta(-100, 173).tenths() == 3600 - 298);
  // Large deltas are halved until they fit 15 bits.
  CHECK(heading_from_delta(100 << 16, 173 << 16).tenths() == 298);
  CHECK(heading_from_delta(0, 0).tenths() == 450);
  CHECK(heading_from_delta(-1, 1000).tenths() == 3599);
  // INT32_MIN stays negative through the original's `abs`: no halving, and
  // the octant start (west) is clamped to 3599.
  CHECK(heading_from_delta(INT32_MIN, 0).tenths() == 3599);
}

TEST_CASE("approx_distance stays within the table's precision", "[fixedmath]") {
  CHECK(approx_distance(3000, 4000) == 4999);
  CHECK(approx_distance(1000, 1000) == 1414);
  CHECK(approx_distance(INT32_MIN, 0) == 0x80000000u);
  CHECK(approx_distance_3d(0, 0, -5) == 5);
  CHECK(approx_distance_3d(3000, 0, 4000) == 4999);
}

TEST_CASE("Vectors wrap and normalize like the original", "[fixedmath]") {
  CHECK(Vec3{INT32_MAX, 0, 0} + Vec3{1, 0, 0} == Vec3{INT32_MIN, 0, 0});
  CHECK(-Vec3{INT32_MIN, 1, 0} == Vec3{INT32_MIN, -1, 0});
  CHECK(cross({kOne, 0, 0}, {0, kOne, 0}) == Vec3{0, 0, kOne});
  CHECK(scale({kOne, -kOne, 3}, 0x8000) == Vec3{0x8000, -0x8000, 2});
  CHECK(divide({kOne, 0, 0}, 0) == Vec3{});
  CHECK(normalize({0, 0, 5 << 16}) == Vec3{0, 0, kOne});
  CHECK(normalize({}) == Vec3{});
  CHECK(dot({kOne, kOne, kOne}, {1, 2, 3}) == 6);
}

TEST_CASE("Rotations: Euler matrices and screen point rotation", "[fixedmath]") {
  CHECK(mat3_from_euler(Angle10(0), Angle10(0), Angle10(0)) == Mat3{});
  const Mat3 m = mat3_from_euler(Angle10(-300), Angle10(100), Angle10(50));
  CHECK(transpose(transpose(m)) == m);
  CHECK(m.m[5] == angle_sincos(Angle10(100)).sin);
  // Offsets are scaled with a floor (the +0x8000 lands in the low dword).
  CHECK(point_rotate_scale({10, 0}, {0, 0}, Angle10(0), 2 * kOne) == Point2{20, 0});
  CHECK(point_rotate_scale({10, 0}, {0, 0}, Angle10(0), -kOne) == Point2{-10, 0});
  CHECK(point_rotate_scale({0, 3}, {0, 0}, Angle10(0), kOne, 0x8000) == Point2{0, 1});
  CHECK(point_rotate_scale({0, -3}, {0, 0}, Angle10(0), kOne, 0x8000) == Point2{0, -2});
  // Clockwise on screen: up (-y) turns to the right (+x).
  CHECK(point_rotate_scale({5, -21}, {5, 0}, Angle10(900)) == Point2{26, 0});
}

}  // namespace sh

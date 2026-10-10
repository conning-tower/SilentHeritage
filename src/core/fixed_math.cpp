#include "core/fixed_math.h"

#include <stdexcept>

namespace sh::fx {

namespace {

std::uint32_t abs_u(std::int32_t v) { return static_cast<std::uint32_t>(abs(v)); }

Tables g_tables;
bool g_tables_installed = false;

const Tables& tables() {
  if (!g_tables_installed) {
    throw std::logic_error("fixed-point math used before its SH.EXE tables were loaded");
  }
  return g_tables;
}

std::int32_t table_cos(const Tables& t, std::int32_t tenths) { return t.cos[static_cast<std::size_t>(tenths)]; }
std::int32_t table_sin(const Tables& t, std::int32_t tenths) { return t.cos[static_cast<std::size_t>(900 - tenths)]; }

}  // namespace

void install_tables(const Tables& t) {
  g_tables = t;
  g_tables_installed = true;
}

bool tables_installed() { return g_tables_installed; }

std::int32_t int64_div(std::int64_t a, std::int32_t d) {
  if (d == 0) {
    d = 1;
  }
  const auto hi = static_cast<std::int32_t>(a >> 32);
  if (hi != 0 && abs_u(d) < abs_u(hi) * 2 + 2) {
    return (hi ^ d) < 0 ? INT32_MIN : INT32_MAX;
  }
  return static_cast<std::int32_t>(a / d);
}

Angle10 angle_add(Angle10 a, Angle10 b) {
  std::int32_t r = a.tenths() + b.tenths();
  while (r >= 3600) {
    r -= 3600;
  }
  return Angle10(r);
}

Angle10 angle_sub(Angle10 a, Angle10 b) {
  std::int32_t r = a.tenths() - b.tenths();
  while (r < 0) {
    r += 3600;
  }
  return Angle10(r);
}

std::int32_t angle_diff(Angle10 a, Angle10 b) {
  const std::int32_t d = angle_sub(a, b).tenths();
  return d > 1800 ? d - 3600 : d;
}

SinCos angle_sincos(Angle10 angle) {
  const Tables& t = tables();
  std::int32_t a = angle.tenths();
  while (a < 0) {
    a += 3600;
  }
  // 3600 itself is kept (and gives the same values as 0).
  while (a > 3600) {
    a -= 3600;
  }
  if (a < 1801) {
    if (a < 901) {
      return {table_cos(t, a), table_sin(t, a)};
    }
    return {-table_cos(t, 1800 - a), table_sin(t, 1800 - a)};
  }
  const std::int32_t u = 3600 - a;
  if (u < 901) {
    return {table_cos(t, u), -table_sin(t, u)};
  }
  return {-table_cos(t, 1800 - u), -table_sin(t, 1800 - u)};
}

Angle10 heading_from_delta(std::int32_t dx, std::int32_t dz) {
  // Octant bases at 0x10b9ce: the heading of the octant start, doubled, with
  // bit 0 set when the arctangent is subtracted.
  static constexpr std::uint16_t kOctantBase[8] = {0, 1801, 3601, 1800, 7201, 5400, 3600, 5401};
  int octant = 0;
  if (dz < 0) {
    octant = 2;
    dz = wsub(0, dz);
  }
  if (dx < 0) {
    dx = wsub(0, dx);
    octant += 4;
  }
  std::int32_t big = dz;
  std::int32_t small = dx;
  if (dz < dx) {
    ++octant;
    big = dx;
    small = dz;
  }
  // Signed compares of `abs`: INT32_MIN passes as is.
  while (abs(big) >= 0x8000 || abs(small) >= 0x8000) {
    big >>= 1;
    small >>= 1;
  }
  const std::uint16_t base = kOctantBase[octant];
  std::uint32_t ratio = 0x100;
  if (small != big) {
    ratio = 0;
    if (big != 0) {
      ratio = static_cast<std::uint32_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(small)) << 32) /
                                         static_cast<std::uint32_t>(big)) >>
              24;
    }
  }
  const std::int16_t atan = tables().atan_tenths[ratio];
  const std::int16_t delta = (base & 1) == 0 ? atan : static_cast<std::int16_t>(-atan);
  std::uint16_t result = static_cast<std::uint16_t>((base >> 1) + delta);
  if (result > 3599) {
    result = 3599;
  }
  return Angle10(result);
}

std::uint32_t approx_distance(std::int32_t a, std::int32_t b) {
  const std::uint32_t abs_a = abs_u(a);
  const std::uint32_t abs_b = abs_u(b);
  std::uint32_t big = abs_a;
  std::uint32_t ratio = 0x100;
  if (abs_a != abs_b) {
    big = abs_a < abs_b ? abs_b : abs_a;
    const std::uint32_t small = abs_a < abs_b ? abs_a : abs_b;
    ratio = static_cast<std::uint32_t>((static_cast<std::uint64_t>(small) << 32) / big) >> 24;
  }
  const std::uint64_t excess = static_cast<std::uint64_t>(tables().hypot_excess[ratio]) * big;
  return static_cast<std::uint32_t>(excess >> 16) + big;
}

std::uint32_t approx_distance_3d(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
  return approx_distance(static_cast<std::int32_t>(approx_distance(dx, dy)), dz);
}

std::int32_t fixed_asin(std::int32_t x) {
  const std::int32_t mag = abs(x);
  if (mag > kOne) {
    return 0;
  }
  const std::int32_t v = tables().asin_tenths[static_cast<std::size_t>(mag >> 4)];
  return x < 0 ? -v : v;
}

Point2 point_rotate_scale(Point2 src, Point2 centre, Angle10 angle, std::int32_t sx, std::int32_t sy) {
  const SinCos sc = angle_sincos(angle);
  // The offset is shifted into the high word, scaled, and the high dword of
  // the product (+0x8000) kept: a 16.16 multiply of an integer offset.
  const auto scale_offset = [](std::int32_t offset, std::int32_t factor) {
    const std::int64_t p = static_cast<std::int64_t>(imul(offset, 0x10000)) * factor;
    return static_cast<std::int32_t>((p + 0x8000) >> 32);
  };
  const std::int32_t x = scale_offset(src.x - centre.x, sx);
  const std::int32_t y = scale_offset(src.y - centre.y, sy);
  return {mul(x, sc.cos) - mul(y, sc.sin) + centre.x, mul(y, sc.cos) + mul(x, sc.sin) + centre.y};
}

Vec3 normalize(const Vec3& v) { return divide(v, static_cast<std::int32_t>(approx_distance_3d(v.x, v.y, v.z))); }

Mat3 mat3_from_euler(Angle10 heading, Angle10 pitch, Angle10 roll) {
  const SinCos h = angle_sincos(heading);
  const SinCos p = angle_sincos(pitch);
  const SinCos r = angle_sincos(roll);
  const std::int32_t sp_sr = mul(p.sin, r.sin);
  const std::int32_t sp_cr = mul(p.sin, r.cos);
  Mat3 m;
  m.m = {mul(h.cos, r.cos) - mul(h.sin, sp_sr),
         mul(h.cos, r.sin) + mul(h.sin, sp_cr),
         -mul(h.sin, p.cos),
         -mul(p.cos, r.sin),
         mul(p.cos, r.cos),
         p.sin,
         mul(h.sin, r.cos) + mul(h.cos, sp_sr),
         mul(h.sin, r.sin) - mul(h.cos, sp_cr),
         mul(h.cos, p.cos)};
  return m;
}

}  // namespace sh::fx

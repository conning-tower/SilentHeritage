#include "assets/game_exe.h"

#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

#include "assets/exe_tables.h"
#include "core/edition.h"
#include "core/error.h"
#include "core/fixed_math.h"

namespace sh {

namespace {

std::unique_ptr<const ExeImage> g_exe;

// Reads the trigonometry tables of `angle_sincos` (0xae3fb),
// `heading_from_delta` (0x5449a), `approx_distance` (0x54537) and
// `fixed_asin` (0x545b0), checking their end points.
fx::Tables read_fixed_math_tables(const ExeImage& exe) {
  constexpr std::uint32_t kCos = 0xad5e7;
  constexpr std::uint32_t kAtan = 0x10b5ca;
  constexpr std::uint32_t kHypot = 0x10b7cc;
  constexpr std::uint32_t kAsin = 0x1095c8;
  fx::Tables t;
  for (std::uint32_t i = 0; i < t.cos.size(); ++i) {
    t.cos[i] = exe.i32(kCos + 4 * i);
  }
  for (std::uint32_t i = 0; i < t.atan_tenths.size(); ++i) {
    t.atan_tenths[i] = static_cast<std::int16_t>(exe.u16(kAtan + 2 * i));
  }
  for (std::uint32_t i = 0; i < t.hypot_excess.size(); ++i) {
    t.hypot_excess[i] = exe.u16(kHypot + 2 * i);
  }
  for (std::uint32_t i = 0; i < t.asin_tenths.size(); ++i) {
    t.asin_tenths[i] = exe.u16(kAsin + 2 * i);
  }
  check_exe_table(t.cos.front() == fx::kOne && t.cos.back() == 0, "cosine", kCos);
  check_exe_table(t.atan_tenths.front() == 0 && t.atan_tenths.back() == 450, "arctangent", kAtan);
  check_exe_table(t.hypot_excess.front() == 0 && t.hypot_excess[128] == 7735, "hypotenuse", kHypot);
  check_exe_table(t.asin_tenths.front() == 0 && t.asin_tenths.back() == 900, "arcsine", kAsin);
  return t;
}

// Returns the image of `exe` with its math tables; throws `DataError` with
// the message of `check_game_exe`.
std::pair<ExeImage, fx::Tables> open_game_exe(std::span<const std::uint8_t> exe) {
  try {
    ExeImage image(exe);
    fx::Tables tables = read_fixed_math_tables(image);
    return {std::move(image), tables};
  } catch (const DataError& e) {
    throw DataError(std::format("SH.EXE is not that of Silent Hunter v1.31 (the version of the Commander's Edition), the "
                                "only version {} plays ({})",
                                edition::kName, e.what()));
  }
}

}  // namespace

void load_game_exe(const Install& install) {
  auto [image, tables] = open_game_exe(install.read("SH.EXE"));
  fx::install_tables(tables);
  g_exe = std::make_unique<const ExeImage>(std::move(image));
}

void check_game_exe(std::span<const std::uint8_t> exe) { open_game_exe(exe); }

void set_game_exe(ExeImage exe) {
  fx::install_tables(read_fixed_math_tables(exe));
  g_exe = std::make_unique<const ExeImage>(std::move(exe));
}

const ExeImage& game_exe() {
  if (!g_exe) {
    throw std::logic_error("SH.EXE data used before load_game_exe");
  }
  return *g_exe;
}

bool game_exe_loaded() { return g_exe != nullptr; }

}  // namespace sh

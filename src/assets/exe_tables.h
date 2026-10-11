// Helpers to read the original's constant tables out of an `ExeImage` (most
// often the process-wide one of assets/game_exe.h), by linear address.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

#include "assets/exe_image.h"
#include "assets/game_exe.h"
#include "core/error.h"

namespace sh {

// Returns the `N` 32-bit signed entries at `address`.
template <std::size_t N>
std::array<std::int32_t, N> exe_i32_array(const ExeImage& exe, std::uint32_t address) {
  std::array<std::int32_t, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = exe.i32(address + 4 * static_cast<std::uint32_t>(i));
  }
  return out;
}

// Returns the strings of the `N` `char*` entries at `address` (empty for a
// NULL pointer).
template <std::size_t N>
std::array<std::string, N> exe_string_array(const ExeImage& exe, std::uint32_t address) {
  std::array<std::string, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = std::string(exe.string_pointer(address + 4 * static_cast<std::uint32_t>(i)));
  }
  return out;
}

// Throws `DataError` naming the table `what` at `address` unless `ok`: the
// check that SH.EXE holds the table expected there.
inline void check_exe_table(bool ok, std::string_view what, std::uint32_t address) {
  if (!ok) {
    throw DataError(std::format("SH.EXE: unexpected {} table at {:#x}", what, address));
  }
}

// The month names of SH.EXE (`char*[12]`, "January".."December").
inline constexpr std::uint32_t kExeMonthNames = 0x10e910;

// Returns the name of `month` (0-based) from SH.EXE's month table, empty
// when out of range.
inline std::string exe_month_name(const ExeImage& exe, std::int32_t month) {
  return month >= 0 && month < 12
             ? std::string(exe.string_pointer(kExeMonthNames + 4 * static_cast<std::uint32_t>(month)))
             : std::string();
}

// `count` int32 entries at linear address `address` of the process-wide
// SH.EXE (`game_exe`), read on each access.
struct ExeIntTable {
  std::uint32_t address = 0;
  std::size_t count = 0;

  std::size_t size() const { return count; }

  // Returns entry `i`; throws `std::out_of_range` past the table.
  std::int32_t at(std::size_t i) const {
    if (i >= count) {
      throw std::out_of_range("SH.EXE table index out of range");
    }
    return game_exe().i32(address + 4 * static_cast<std::uint32_t>(i));
  }
};

}  // namespace sh

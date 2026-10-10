// Access to the original game files for tests that need them.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>

#include "core/install.h"

namespace sh::test {

// Returns the install named by the SH_DATA environment variable, or nothing.
inline std::optional<Install> game_install() {
  const char* dir = std::getenv("SH_DATA");
  if (dir == nullptr || *dir == '\0') {
    return std::nullopt;
  }
  return Install(dir);
}

}  // namespace sh::test

// Declares `install` (an `sh::Install`), or skips the test when SH_DATA is unset.
#define SH_REQUIRE_GAME_DATA(install)                             \
  const auto install##_opt = ::sh::test::game_install();          \
  if (!install##_opt) {                                           \
    SKIP("SH_DATA is not set to a Silent Hunter install");        \
  }                                                               \
  const ::sh::Install& install = *install##_opt

// Skips the test when SH_DATA is unset: for tests of code reading the
// process-wide SH.EXE (assets/game_exe.h, loaded by game_exe_setup.cpp).
#define SH_REQUIRE_GAME_EXE()                                      \
  if (!::sh::test::game_install()) {                               \
    SKIP("SH_DATA is not set to a Silent Hunter install");        \
  }

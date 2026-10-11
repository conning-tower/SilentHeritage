// The game files of the tests: the original's (SH_DATA) for the tests of its
// content, else the synthetic install for the tests of behaviour.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string_view>

#include "core/install.h"
#include "synthetic/install_files.h"
#include "synthetic/synthetic_install.h"

namespace sh::test {

// Returns the install named by the SH_DATA environment variable, or nothing.
// With SH_TRACE_READS, the paths of the files read are appended to the file
// it names (synthetic/install_files.h).
inline std::optional<Install> game_install() {
  const char* dir = std::getenv("SH_DATA");
  if (dir == nullptr || *dir == '\0') {
    return std::nullopt;
  }
  if (const char* log = std::getenv("SH_TRACE_READS"); log != nullptr && *log != '\0') {
    return traced_install(dir, log);
  }
  return Install(dir);
}

// Returns whether the tests run on the synthetic install even with SH_DATA
// set (SH_SYNTHETIC=1): the original's SH.EXE is then still the process-wide
// one, which a trace build uses to learn the addresses the synthetic data
// makes the code read (synthetic/exe_manifest.h).
inline bool synthetic_forced() {
  const char* v = std::getenv("SH_SYNTHETIC");
  return v != nullptr && std::string_view(v) == "1";
}

// Returns the install of SH_DATA, or the synthetic install
// (synthetic/synthetic_install.h) when it is unset or SH_SYNTHETIC=1.
inline Install any_install() {
  if (std::optional<Install> install = game_install(); install && !synthetic_forced()) {
    return *install;
  }
  return synthetic_install();
}

}  // namespace sh::test

// Declares `install` (an `sh::Install`): the original's install when
// SH_DATA is set, else the synthetic one. For tests that do not depend on
// the original's content.
#define SH_REQUIRE_INSTALL(install) const ::sh::Install install = ::sh::test::any_install()

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

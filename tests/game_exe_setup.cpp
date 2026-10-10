// Loads the process-wide SH.EXE (assets/game_exe.h) before the tests run,
// as the program does at startup. Without SH_DATA, it installs fixed-point
// math tables computed from their defining formulas instead (the same values
// as SH.EXE's, checked by "[fixedmath]"), and tests that need other SH.EXE
// data skip through SH_REQUIRE_GAME_DATA.
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "assets/game_exe.h"
#include "core/fixed_math.h"
#include "formula_tables.h"
#include "game_data.h"

namespace sh::test {

namespace {

class GameExeSetup : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo&) override {
    if (const auto install = game_install()) {
      load_game_exe(*install);
    } else {
      fx::install_tables(formula_tables());
    }
  }
};

}  // namespace

CATCH_REGISTER_LISTENER(GameExeSetup)

}  // namespace sh::test

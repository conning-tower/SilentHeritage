// Loads the process-wide SH.EXE (assets/game_exe.h) before the tests run,
// as the program does at startup. Without SH_DATA, it loads the synthetic
// install's (synthetic/synthetic_install.h), whose fixed-point math tables
// come from their defining formulas (the same values as SH.EXE's, checked
// by "[fixedmath]"); tests of the original's values skip through
// SH_REQUIRE_GAME_DATA. A build with SH_TRACE_EXE_READS
// writes the executables' addresses read to the file SH_TRACE_EXE names.
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <set>
#include <tuple>

#include "assets/exe_image.h"
#include "assets/game_exe.h"
#include "core/fixed_math.h"
#include "game_data.h"
#include "synthetic/exe_manifest.h"
#include "synthetic/le_writer.h"

namespace sh::test {

namespace {

#ifdef SH_TRACE_EXE_READS
// The reads of every executable image: (image end, address, size, kind).
std::set<std::tuple<std::uint32_t, std::uint32_t, std::size_t, ExeRead>>& exe_reads() {
  static std::set<std::tuple<std::uint32_t, std::uint32_t, std::size_t, ExeRead>> reads;
  return reads;
}
#endif

class GameExeSetup : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo&) override {
#ifdef SH_TRACE_EXE_READS
    ExeImage::set_read_observer([](const ExeImage& image, std::uint32_t address, std::size_t size, ExeRead kind) {
      exe_reads().emplace(image.end(), address, size, kind);
    });
#endif
    if (const auto install = game_install()) {
      load_game_exe(*install);
    } else {
      set_game_exe(ExeImage(write_le_executable(committed_exe_manifest("sh_exe"))));
    }
  }

#ifdef SH_TRACE_EXE_READS
  // Writes the reads to the file SH_TRACE_EXE names, one per line:
  // "<image end> <address> <size> <kind>" in hexadecimal.
  void testRunEnded(const Catch::TestRunStats&) override {
    ExeImage::set_read_observer(nullptr);
    const char* path = std::getenv("SH_TRACE_EXE");
    if (path == nullptr || *path == '\0') {
      return;
    }
    std::ofstream out(path, std::ios::app);
    for (const auto& [end, address, size, kind] : exe_reads()) {
      out << std::format("{:x} {:x} {:x} {}\n", end, address, size, static_cast<int>(kind));
    }
  }
#endif
};

}  // namespace

CATCH_REGISTER_LISTENER(GameExeSetup)

}  // namespace sh::test

// Gives each test run its own temporary directory: the tests' scratch
// directories are fixed names under `std::filesystem::temp_directory_path`,
// so two runs at once (several build directories or worktrees) would share
// them. The run's directory is made under the system's one, set as TMPDIR
// (TMP and TEMP on Windows) for the run, and removed afterwards.
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace sh::test {

namespace {

// Sets the variables `temp_directory_path` reads to `dir`.
void set_temp_dir(const std::filesystem::path& dir) {
#ifdef _WIN32
  _putenv_s("TMP", dir.string().c_str());
  _putenv_s("TEMP", dir.string().c_str());
#else
  setenv("TMPDIR", dir.c_str(), 1);
#endif
}

class TempDirSetup : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testRunStarting(const Catch::TestRunInfo&) override {
    dir_ = std::filesystem::temp_directory_path() / ("sh_tests_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir_);
    set_temp_dir(dir_);
  }

  void testRunEnded(const Catch::TestRunStats&) override {
    std::error_code ignored;
    std::filesystem::remove_all(dir_, ignored);
  }

 private:
  std::filesystem::path dir_;
};

}  // namespace

CATCH_REGISTER_LISTENER(TempDirSetup)

}  // namespace sh::test

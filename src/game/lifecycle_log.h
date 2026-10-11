// The diagnostic log of the program's lifecycle `SAVE\LIFECYCLE.LOG` (not
// in the original): the events the system sends (core/lifecycle.h) and
// what became of the resume snapshot (game/resume_keeper.h), one line each
// after the UTC date and time ("2026-10-11 12:34:56.789 "), for players to
// share when a game is not resumed.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace sh {

inline constexpr std::string_view kLifecycleLogName = "LIFECYCLE.LOG";
// The previous log, once the current one reached `LifecycleLog::kMaxSize`.
inline constexpr std::string_view kLifecycleOldLogName = "LIFECYCLE.OLD";

class LifecycleLog {
 public:
  // The size from which the log is moved to LIFECYCLE.OLD (replacing it)
  // before a line is added.
  static constexpr std::uintmax_t kMaxSize = 64 * 1024;

  // The log in directory `dir` (created at the first line).
  explicit LifecycleLog(std::filesystem::path dir);

  // Appends `message` as a line. The file is closed after each line, as
  // the process may be killed at any time; a line that cannot be written
  // is lost (the log is only a diagnostic).
  void write(std::string_view message) const;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path dir_;
  std::filesystem::path path_;
};

}  // namespace sh

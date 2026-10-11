#include "game/lifecycle_log.h"

#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace sh {

namespace {

// Returns the UTC date and time with milliseconds.
std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                     utc.tm_hour, utc.tm_min, utc.tm_sec, ms);
}

}  // namespace

LifecycleLog::LifecycleLog(std::filesystem::path dir) : dir_(std::move(dir)), path_(dir_ / kLifecycleLogName) {}

void LifecycleLog::write(std::string_view message) const {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (const std::uintmax_t size = std::filesystem::file_size(path_, ec); !ec && size >= kMaxSize) {
    const std::filesystem::path old = dir_ / kLifecycleOldLogName;
    std::filesystem::remove(old, ec);
    std::filesystem::rename(path_, old, ec);
  }
  std::ofstream out(path_, std::ios::binary | std::ios::app);
  out << timestamp() << ' ' << message << '\n';
}

}  // namespace sh

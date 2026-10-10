// A scratch directory for the editor tests, removed at the end of the test.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace sh::editor::test {

class TempDir {
 public:
  // Creates <temp>/<prefix><time stamp>.
  explicit TempDir(std::string_view prefix = "sh_editor_test_") {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / (std::string(prefix) + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace sh::editor::test

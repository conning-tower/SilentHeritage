#include "synthetic/install_files.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace sh::test {

namespace {

std::string index_of(const std::map<std::string, std::uint64_t>& sizes) {
  std::string index = "SH-BUNDLE 2\n";
  for (const auto& [path, size] : sizes) {
    index += std::to_string(size) + " " + path + "\n";
  }
  return index;
}

}  // namespace

Install install_from_files(std::shared_ptr<const InstallFiles> files, std::string name) {
  std::map<std::string, std::uint64_t> sizes;
  for (const auto& [path, data] : *files) {
    sizes.emplace(path, data.size());
  }
  return Install::packaged(
      index_of(sizes),
      [files](const std::string& path) -> std::optional<std::vector<std::uint8_t>> {
        const auto it = files->find(path);
        if (it == files->end()) {
          return std::nullopt;
        }
        return it->second;
      },
      std::move(name));
}

Install traced_install(const std::filesystem::path& root, const std::filesystem::path& log) {
  std::map<std::string, std::uint64_t> sizes;
  for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
    if (e.is_regular_file()) {
      sizes.emplace(std::filesystem::relative(e.path(), root).generic_string(), e.file_size());
    }
  }
  auto mutex = std::make_shared<std::mutex>();
  return Install::packaged(
      index_of(sizes),
      [root, log, mutex](const std::string& path) -> std::optional<std::vector<std::uint8_t>> {
        {
          const std::lock_guard<std::mutex> lock(*mutex);
          std::ofstream out(log, std::ios::app);
          std::string upper = path;
          std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
          out << upper << '\n';
        }
        std::ifstream in(root / path, std::ios::binary);
        if (!in) {
          return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
      },
      "traced " + root.string());
}

void write_install_files(const InstallFiles& files, const std::filesystem::path& dir) {
  for (const auto& [path, data] : files) {
    const std::filesystem::path out_path = dir / path;
    std::filesystem::create_directories(out_path.parent_path());
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
      throw std::runtime_error("cannot write " + out_path.string());
    }
  }
}

}  // namespace sh::test

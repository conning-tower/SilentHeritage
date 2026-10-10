#include "app/install_source.h"

#include <format>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "core/error.h"
#include "game/saves_options.h"

namespace sh {

namespace {

constexpr std::string_view kHeader = "SH-INSTALL 1";

struct KindName {
  InstallSource::Kind kind;
  std::string_view name;
};

constexpr KindName kKindNames[] = {
    {InstallSource::Kind::kFolder, "folder"},
    {InstallSource::Kind::kDocumentTree, "tree"},
    {InstallSource::Kind::kCopy, "copy"},
};

}  // namespace

std::optional<InstallSource> load_install_config(const std::filesystem::path& file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return std::nullopt;
  }
  const std::vector<std::uint8_t> data = read_file(file);
  std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
  const auto next_line = [&text] {
    const std::size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    return line;
  };
  const auto bad = [&file] { return DataError(std::format("{}: not an install configuration", file.string())); };
  if (next_line() != kHeader) {
    throw bad();
  }
  const std::string_view line = next_line();
  const std::size_t space = line.find(' ');
  if (space == std::string_view::npos || space + 1 == line.size()) {
    throw bad();
  }
  for (const KindName& k : kKindNames) {
    if (k.name == line.substr(0, space)) {
      return InstallSource{.kind = k.kind, .location = std::string(line.substr(space + 1))};
    }
  }
  throw bad();
}

void save_install_config(const std::filesystem::path& file, const InstallSource& source) {
  if (source.location.find_first_of("\r\n") != std::string::npos) {
    throw std::runtime_error(std::format("cannot remember the location '{}'", source.location));
  }
  std::string_view kind;
  for (const KindName& k : kKindNames) {
    if (k.kind == source.kind) {
      kind = k.name;
    }
  }
  const std::string text = std::format("{}\n{} {}\n", kHeader, kind, source.location);
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  if (!write_file_atomic(file, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))) {
    throw std::runtime_error(std::format("cannot write {}", file.string()));
  }
}

InstallCheck check_install(const std::function<Install()>& open) {
  try {
    Install install = open();
    if (!install.exists("SH.EXE")) {
      return {.install = std::nullopt, .problem = "SH.EXE is missing"};
    }
    if (!install.exists("DATA")) {
      return {.install = std::nullopt, .problem = "the DATA folder is missing"};
    }
    return {.install = std::move(install), .problem = {}};
  } catch (const DataError& e) {
    return {.install = std::nullopt, .problem = e.what()};
  }
}

InstallCheck check_install_folder(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return {.install = std::nullopt, .problem = std::format("{} is not a folder", dir.string())};
  }
  return check_install([&dir] { return Install(dir); });
}

Install open_folder_source(const InstallSource& source) {
  if (source.kind == InstallSource::Kind::kDocumentTree) {
    throw DataError(std::format("{}: document trees are only supported on Android", source.location));
  }
  return Install(std::filesystem::path(source.location));
}

StartupInstall choose_startup_install(const std::optional<std::filesystem::path>& data_dir, bool force_setup,
                                      const std::optional<InstallSource>& remembered, const InstallOpener& open) {
  StartupInstall out;
  if (force_setup) {
    return out;
  }
  if (data_dir) {
    InstallCheck check = check_install_folder(*data_dir);
    if (check.install) {
      out.install = std::move(check.install);
      return out;
    }
    out.problems.push_back(std::format("--data {}: {}", data_dir->string(), check.problem));
  }
  if (remembered) {
    InstallCheck check = check_install([&] { return open(*remembered); });
    if (check.install) {
      out.install = std::move(check.install);
      out.source = remembered;
      return out;
    }
    out.problems.push_back(std::format("The game files are no longer usable at {}: {}", remembered->location,
                                       check.problem));
  }
  return out;
}

void copy_document_files(const Install::DocumentTree& tree, const std::vector<DocumentFile>& files,
                         const std::filesystem::path& dest, const std::function<void(std::uint64_t)>& progress) {
  std::uint64_t done = 0;
  for (const DocumentFile& f : files) {
    const std::optional<std::vector<std::uint8_t>> data = tree.read(f.id);
    if (!data) {
      throw DataError(std::format("cannot read {} (in {})", f.path, tree.describe()));
    }
    if (data->size() != f.size) {
      throw DataError(
          std::format("{} (in {}): {} bytes, the listing says {}", f.path, tree.describe(), data->size(), f.size));
    }
    const std::filesystem::path target = dest / std::filesystem::path(f.path);
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data->data()), static_cast<std::streamsize>(data->size()));
    out.close();
    if (!out) {
      throw std::runtime_error(std::format("cannot write {}", target.string()));
    }
    done += f.size;
    progress(done);
  }
}

}  // namespace sh

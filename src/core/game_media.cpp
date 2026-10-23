#include "core/game_media.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <utility>

#include "core/arj.h"
#include "core/error.h"
#include "core/iso9660.h"

namespace sh {

namespace {

std::string upper(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(),
                         [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  return out;
}

std::string join(const std::string& dir, const std::string& name) {
  return dir.empty() ? name : std::format("{}/{}", dir, name);
}

// Deeper folders beside the volumes are not the CD's.
constexpr int kMaxLooseDepth = 8;

class HostMediaTree final : public MediaTree {
 public:
  explicit HostMediaTree(std::filesystem::path dir) : dir_(std::move(dir)) {}

  std::vector<Entry> list(const std::string& dir) const override {
    const std::filesystem::path path = dir_ / std::filesystem::path(dir);
    std::error_code ec;
    std::filesystem::directory_iterator it(path, ec);
    if (ec) {
      throw DataError(std::format("cannot list {}", path.string()));
    }
    std::vector<Entry> out;
    for (const auto& e : it) {
      std::error_code entry_ec;
      Entry entry{.name = e.path().filename().string(), .directory = e.is_directory(entry_ec), .size = 0};
      if (!entry.directory) {
        if (!e.is_regular_file(entry_ec)) {
          continue;  // special files are not the CD's
        }
        entry.size = e.file_size(entry_ec);
        if (entry_ec) {
          continue;
        }
      }
      out.push_back(std::move(entry));
    }
    return out;
  }

  std::shared_ptr<const ByteSource> open(const std::string& path) const override {
    return host_file_source(dir_ / std::filesystem::path(path));
  }

  std::string describe() const override { return dir_.string(); }

 private:
  std::filesystem::path dir_;
};

class IsoMediaTree final : public MediaTree {
 public:
  explicit IsoMediaTree(std::shared_ptr<const ByteSource> image) : image_(std::move(image)) {
    for (const IsoFile& f : image_.files()) {
      files_.emplace(f.path, &f);
      std::string dir;
      std::string_view rest = f.path;
      for (std::size_t slash = rest.find('/'); slash != std::string_view::npos; slash = rest.find('/')) {
        const std::string name(rest.substr(0, slash));
        dirs_[dir].emplace(name, true);
        dir = join(dir, name);
        rest.remove_prefix(slash + 1);
      }
      dirs_[dir].emplace(std::string(rest), false);
    }
  }

  std::vector<Entry> list(const std::string& dir) const override {
    std::vector<Entry> out;
    const auto it = dirs_.find(dir);
    if (it == dirs_.end()) {
      return out;
    }
    for (const auto& [name, directory] : it->second) {
      const auto file = files_.find(join(dir, name));
      out.push_back(Entry{.name = name,
                          .directory = directory,
                          .size = directory || file == files_.end() ? 0 : file->second->size});
    }
    return out;
  }

  std::shared_ptr<const ByteSource> open(const std::string& path) const override {
    const auto it = files_.find(path);
    if (it == files_.end()) {
      throw DataError(std::format("no {} in {}", path, image_.describe()));
    }
    return image_.open(*it->second);
  }

  std::string describe() const override { return image_.describe(); }

 private:
  IsoImage image_;
  std::map<std::string, const IsoFile*> files_;            // by path
  std::map<std::string, std::set<std::pair<std::string, bool>>> dirs_;  // names and whether directories
};

class DocumentMediaTree final : public MediaTree {
 public:
  explicit DocumentMediaTree(std::shared_ptr<const Install::DocumentTree> tree) : tree_(std::move(tree)) {}

  std::vector<Entry> list(const std::string& dir) const override {
    std::string id;
    {
      const std::lock_guard lock(mutex_);
      if (dir.empty()) {
        id = tree_->root();
      } else if (const auto it = ids_.find(dir); it != ids_.end()) {
        id = it->second;
      } else {
        throw DataError(std::format("cannot list {} (in {})", dir, tree_->describe()));
      }
    }
    std::vector<Entry> out;
    for (Install::DocumentTree::Entry& e : tree_->children(id)) {
      if (e.name.empty() || e.name == "." || e.name == ".." || e.name.find_first_of("/\\") != std::string::npos) {
        continue;
      }
      {
        const std::lock_guard lock(mutex_);
        ids_[join(dir, e.name)] = e.id;
      }
      out.push_back(Entry{.name = std::move(e.name), .directory = e.directory, .size = e.size});
    }
    return out;
  }

  std::shared_ptr<const ByteSource> open(const std::string& path) const override {
    std::string id;
    {
      const std::lock_guard lock(mutex_);
      const auto it = ids_.find(path);
      if (it == ids_.end()) {
        throw DataError(std::format("cannot open {} (in {})", path, tree_->describe()));
      }
      id = it->second;
    }
    return tree_->open(id, path);
  }

  std::string describe() const override { return tree_->describe(); }

 private:
  std::shared_ptr<const Install::DocumentTree> tree_;
  mutable std::mutex mutex_;
  mutable std::map<std::string, std::string> ids_;  // the ids of the paths listed
};

// Returns the entry of `entries` named `name` in any case.
const MediaTree::Entry* find_entry(const std::vector<MediaTree::Entry>& entries, std::string_view name,
                                   bool directory) {
  const std::string wanted = upper(name);
  for (const MediaTree::Entry& e : entries) {
    if (e.directory == directory && upper(e.name) == wanted) {
      return &e;
    }
  }
  return nullptr;
}

// Returns the names of the volumes in `entries` (SH.A00 or SH.ARJ, then
// SH.A01, SH.A02...), empty when there is no first one.
std::vector<std::string> volume_names(const std::vector<MediaTree::Entry>& entries) {
  std::vector<std::string> out;
  for (const char* first : {"SH.A00", "SH.ARJ"}) {
    if (const MediaTree::Entry* e = find_entry(entries, first, false)) {
      out.push_back(e->name);
      break;
    }
  }
  if (out.empty()) {
    return out;
  }
  for (int n = 1; n < 100; ++n) {
    const MediaTree::Entry* e = find_entry(entries, std::format("SH.A{:02}", n), false);
    if (e == nullptr) {
      break;
    }
    out.push_back(e->name);
  }
  return out;
}

// Returns `path` as an install path, or throws `DataError` naming
// `context` when it is absolute or climbs out.
std::string safe_path(std::string_view path, const std::string& context) {
  std::string out;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = std::min(path.find('/', start), path.size());
    const std::string_view part = path.substr(start, end - start);
    if (part == ".." || part.find(':') != std::string_view::npos || (part.empty() && start == 0 && end < path.size())) {
      throw DataError(std::format("{}: unsafe path {}", context, path));
    }
    if (!part.empty() && part != ".") {
      out = join(out, std::string(part));
    }
    start = end + 1;
  }
  return out;
}

// The media's files by upper-case path, in the order they were added.
class FileSet {
 public:
  // Adds `file`, replacing a file of the same path when `replace`.
  void add(GameMedia::File file, bool replace) {
    const auto [it, added] = index_.emplace(upper(file.path), files_.size());
    if (added) {
      files_.push_back(std::move(file));
    } else if (replace) {
      files_[it->second] = std::move(file);
    }
  }

  std::vector<GameMedia::File> take() { return std::move(files_); }

 private:
  std::map<std::string, std::size_t> index_;
  std::vector<GameMedia::File> files_;
};

// Adds the files of the volumes `names` of folder `dir`.
void add_volumes(const MediaTree& tree, const std::string& dir, const std::vector<std::string>& names,
                 FileSet& files) {
  struct Part {
    std::shared_ptr<const ArjArchive> archive;
    std::size_t entry;
  };
  // The file being continued into the next volume.
  std::optional<std::pair<GameMedia::File, std::vector<Part>>> open_file;
  const auto finish = [&files](std::pair<GameMedia::File, std::vector<Part>> pending) {
    pending.first.read = [parts = std::move(pending.second)] {
      std::vector<std::uint8_t> out;
      for (const Part& p : parts) {
        const std::vector<std::uint8_t> data = p.archive->extract(p.archive->entries()[p.entry]);
        out.insert(out.end(), data.begin(), data.end());
      }
      return out;
    };
    files.add(std::move(pending.first), true);
  };
  for (const std::string& name : names) {
    const auto archive = std::make_shared<const ArjArchive>(tree.open(join(dir, name)));
    const std::vector<ArjEntry>& entries = archive->entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const ArjEntry& e = entries[i];
      if (e.directory) {
        continue;
      }
      const std::string path = safe_path(e.path, archive->describe());
      if (path.empty()) {
        continue;
      }
      if (e.continuation) {
        if (!open_file || upper(open_file->first.path) != upper(path) || open_file->first.size != e.continued_at) {
          throw DataError(std::format("{}: {} continues a file the previous volume does not hold",
                                      archive->describe(), e.path));
        }
        open_file->first.size += e.original_size;
        open_file->second.push_back(Part{archive, i});
      } else {
        if (open_file) {
          throw DataError(std::format("{}: {} is not continued", archive->describe(), open_file->first.path));
        }
        open_file.emplace(GameMedia::File{.path = path, .size = e.original_size, .read = {}},
                          std::vector<Part>{Part{archive, i}});
      }
      if (!e.continues) {
        finish(std::move(*open_file));
        open_file.reset();
      }
    }
  }
  if (open_file) {
    throw DataError(std::format("{}: {} continues in a missing volume", tree.describe(), open_file->first.path));
  }
}

// Adds the files under folder `dir` (`path` in the install) that are not
// there yet.
void add_loose(const std::shared_ptr<const MediaTree>& tree, const std::string& dir, const std::string& path,
               int depth, FileSet& files) {
  for (const MediaTree::Entry& e : tree->list(dir)) {
    const std::string child = join(dir, e.name);
    if (e.directory) {
      if (depth < kMaxLooseDepth) {
        add_loose(tree, child, join(path, e.name), depth + 1, files);
      }
      continue;
    }
    files.add(GameMedia::File{.path = join(path, e.name),
                              .size = e.size,
                              .read = [tree, child] { return tree->open(child)->read_all(); }},
              false);
  }
}

// Returns the media of folder `dir` of `tree` when it holds the volumes.
std::optional<GameMedia> media_in(const std::shared_ptr<const MediaTree>& tree, const std::string& dir,
                                  const std::vector<MediaTree::Entry>& entries) {
  const std::vector<std::string> volumes = volume_names(entries);
  if (volumes.empty()) {
    return std::nullopt;
  }
  FileSet files;
  add_volumes(*tree, dir, volumes, files);
  for (const MediaTree::Entry& e : entries) {
    if (e.directory) {
      add_loose(tree, join(dir, e.name), e.name, 1, files);
    }
  }
  GameMedia media{.location = dir.empty() ? tree->describe() : std::format("{} (in {})", dir, tree->describe()),
                  .files = files.take()};
  if (media.find("SH.EXE") == nullptr) {
    throw DataError(std::format("{}: the archives hold no SH.EXE", media.location));
  }
  return media;
}

}  // namespace

std::shared_ptr<const MediaTree> host_media_tree(std::filesystem::path dir) {
  return std::make_shared<HostMediaTree>(std::move(dir));
}

std::shared_ptr<const MediaTree> iso_media_tree(std::shared_ptr<const ByteSource> image) {
  return std::make_shared<IsoMediaTree>(std::move(image));
}

std::shared_ptr<const MediaTree> document_media_tree(std::shared_ptr<const Install::DocumentTree> tree) {
  return std::make_shared<DocumentMediaTree>(std::move(tree));
}

std::uint64_t GameMedia::total_size() const {
  std::uint64_t total = 0;
  for (const File& f : files) {
    total += f.size;
  }
  return total;
}

const GameMedia::File* GameMedia::find(std::string_view path) const {
  const std::string wanted = upper(path);
  for (const File& f : files) {
    if (upper(f.path) == wanted) {
      return &f;
    }
  }
  return nullptr;
}

std::optional<GameMedia> find_game_media(const std::shared_ptr<const MediaTree>& tree) {
  const std::vector<MediaTree::Entry> root = tree->list("");
  if (std::optional<GameMedia> media = media_in(tree, "", root)) {
    return media;
  }
  for (const MediaTree::Entry& e : root) {
    if (e.directory) {
      if (std::optional<GameMedia> media = media_in(tree, e.name, tree->list(e.name))) {
        return media;
      }
    }
  }
  for (const MediaTree::Entry& e : root) {
    if (!e.directory && upper(e.name).ends_with(".ISO")) {
      const std::shared_ptr<const ByteSource> image = tree->open(e.name);
      if (!looks_like_iso(*image)) {
        continue;
      }
      if (std::optional<GameMedia> media = find_game_media(iso_media_tree(image))) {
        return media;
      }
    }
  }
  return std::nullopt;
}

void install_game_media(const GameMedia& media, const std::filesystem::path& dest,
                        const std::function<void(std::uint64_t)>& progress) {
  const std::filesystem::path temp = dest.string() + ".tmp";
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);
  try {
    std::uint64_t done = 0;
    for (const GameMedia::File& f : media.files) {
      const std::vector<std::uint8_t> data = f.read();
      if (data.size() != f.size) {
        throw DataError(std::format("{} (in {}): {} bytes, {} expected", f.path, media.location, data.size(), f.size));
      }
      const std::filesystem::path target = temp / std::filesystem::path(f.path);
      std::filesystem::create_directories(target.parent_path());
      std::ofstream out(target, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
      out.close();
      if (!out) {
        throw std::runtime_error(std::format("cannot write {}", target.string()));
      }
      done += f.size;
      progress(done);
    }
    std::filesystem::remove_all(dest);
    std::filesystem::rename(temp, dest);
  } catch (const std::exception&) {
    std::filesystem::remove_all(temp, ec);
    throw;
  }
}

}  // namespace sh

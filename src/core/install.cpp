#include "core/install.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <utility>

#include "core/byte_source.h"
#include "core/error.h"

namespace sh {

namespace {

std::string upper(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(),
                         [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  return out;
}

// Splits a DOS path into its components, dropping empty and "." ones.
std::vector<std::string> split_dos_path(std::string_view dos_path) {
  std::vector<std::string> parts;
  std::string component;
  const auto flush = [&] {
    if (!component.empty() && component != ".") {
      parts.push_back(std::move(component));
    }
    component.clear();
  };
  for (const char c : dos_path) {
    if (c == '/' || c == '\\') {
      flush();
    } else {
      component += c;
    }
  }
  flush();
  return parts;
}

// Returns the upper-case, '/'-joined key of path components.
std::string upper_key(const std::vector<std::string>& parts) {
  std::string key;
  for (const std::string& p : parts) {
    if (!key.empty()) {
      key += '/';
    }
    key += upper(p);
  }
  return key;
}

}  // namespace

class Install::Backend {
 public:
  virtual ~Backend() = default;

  // Each takes a path split by `split_dos_path`.
  virtual bool exists(const std::vector<std::string>& parts) const = 0;
  virtual std::optional<std::vector<std::uint8_t>> try_read(const std::vector<std::string>& parts) const = 0;
  // Returns the upper-case names of the files of a directory ending with
  // `upper_ext`, in any order.
  virtual std::vector<std::string> list(const std::vector<std::string>& dir, std::string_view upper_ext) const = 0;
  // Returns the install's description for error messages.
  virtual std::string describe() const = 0;
};

namespace {

// Finds the entry of `dir` whose name matches `name` case-insensitively.
std::optional<std::filesystem::path> find_entry(const std::filesystem::path& dir, std::string_view name) {
  std::error_code ec;
  const auto exact = dir / std::string(name);
  if (std::filesystem::exists(exact, ec)) {
    return exact;
  }
  const std::string wanted = upper(name);
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (upper(entry.path().filename().string()) == wanted) {
      return entry.path();
    }
  }
  return std::nullopt;
}

class DirectoryBackend final : public Install::Backend {
 public:
  explicit DirectoryBackend(std::filesystem::path root) : root_(std::move(root)) {}

  bool exists(const std::vector<std::string>& parts) const override { return find(parts).has_value(); }

  std::optional<std::vector<std::uint8_t>> try_read(const std::vector<std::string>& parts) const override {
    const auto path = find(parts);
    if (!path) {
      return std::nullopt;
    }
    return read_file(*path);
  }

  std::vector<std::string> list(const std::vector<std::string>& dir, std::string_view upper_ext) const override {
    std::vector<std::string> out;
    const auto path = find(dir);
    if (!path) {
      return out;
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*path, ec)) {
      std::string name = upper(entry.path().filename().string());
      if (entry.is_regular_file(ec) && name.ends_with(upper_ext)) {
        out.push_back(std::move(name));
      }
    }
    return out;
  }

  std::string describe() const override { return root_.string(); }

 private:
  // Returns the real path of `parts`, or nothing if it does not exist.
  std::optional<std::filesystem::path> find(const std::vector<std::string>& parts) const {
    std::filesystem::path current = root_;
    for (const std::string& name : parts) {
      auto next = find_entry(current, name);
      if (!next) {
        return std::nullopt;
      }
      current = std::move(*next);
    }
    return current;
  }

  std::filesystem::path root_;
};

// Returns the names of the files of directory `dir` ending with
// `upper_ext` in `files`, a map by upper-case '/'-joined path.
template <typename File>
std::vector<std::string> list_index(const std::map<std::string, File>& files, const std::vector<std::string>& dir,
                                    std::string_view upper_ext) {
  std::vector<std::string> out;
  std::string prefix = upper_key(dir);
  if (!prefix.empty()) {
    prefix += '/';
  }
  for (auto it = files.lower_bound(prefix); it != files.end() && it->first.starts_with(prefix); ++it) {
    const std::string_view name = std::string_view(it->first).substr(prefix.size());
    if (name.find('/') == std::string_view::npos && name.ends_with(upper_ext)) {
      out.emplace_back(name);
    }
  }
  return out;
}

// Returns whether `path` is relative, '/' separated, with no empty, "." or
// ".." components.
bool safe_relative(std::string_view path) {
  if (path.empty() || path.find('\\') != std::string_view::npos) {
    return false;
  }
  for (std::size_t start = 0; start <= path.size();) {
    const std::size_t end = std::min(path.find('/', start), path.size());
    const std::string_view part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") {
      return false;
    }
    start = end + 1;
  }
  return true;
}

class PackagedBackend final : public Install::Backend {
 public:
  PackagedBackend(std::string_view index, Install::PackageReader reader, std::string name)
      : reader_(std::move(reader)), name_(std::move(name)) {
    parse(index);
  }

  bool exists(const std::vector<std::string>& parts) const override {
    const std::string key = upper_key(parts);
    return key.empty() || files_.contains(key) || dirs_.contains(key);
  }

  std::optional<std::vector<std::uint8_t>> try_read(const std::vector<std::string>& parts) const override {
    const auto it = files_.find(upper_key(parts));
    if (it == files_.end()) {
      return std::nullopt;
    }
    const PackagedFile& f = it->second;
    std::optional<std::vector<std::uint8_t>> data = reader_(f.path);
    if (!data) {
      throw DataError(std::format("cannot read {} (in {})", f.path, name_));
    }
    if (data->size() != f.size) {
      throw DataError(std::format("{} (in {}): {} bytes, the index says {}", f.path, name_, data->size(), f.size));
    }
    return data;
  }

  std::vector<std::string> list(const std::vector<std::string>& dir, std::string_view upper_ext) const override {
    return list_index(files_, dir, upper_ext);
  }

  std::string describe() const override { return name_; }

 private:
  struct PackagedFile {
    std::string path;  // as spelled in the index
    std::uint64_t size = 0;
  };

  void parse(std::string_view text) {
    std::size_t line_no = 0;
    while (!text.empty()) {
      const std::size_t eol = text.find('\n');
      std::string_view line = text.substr(0, eol);
      text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }
      ++line_no;
      const auto bad = [&] { return DataError(std::format("{} index line {}: '{}'", name_, line_no, line)); };
      if (line_no == 1) {
        if (line != "SH-BUNDLE 2") {
          throw bad();
        }
        continue;
      }
      if (line.empty()) {
        continue;
      }
      const std::size_t space = line.find(' ');
      if (space == std::string_view::npos) {
        throw bad();
      }
      PackagedFile f;
      const std::string_view size = line.substr(0, space);
      const auto [end, ec] = std::from_chars(size.data(), size.data() + size.size(), f.size);
      f.path = line.substr(space + 1);
      if (ec != std::errc() || end != size.data() + size.size() || !safe_relative(f.path)) {
        throw bad();
      }
      std::string key = upper(f.path);
      for (std::size_t slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1)) {
        dirs_.insert(key.substr(0, slash));
      }
      if (!files_.emplace(std::move(key), std::move(f)).second) {
        throw bad();
      }
    }
    if (line_no == 0) {
      throw DataError(std::format("{} index: empty", name_));
    }
  }

  Install::PackageReader reader_;
  std::string name_;
  std::map<std::string, PackagedFile> files_;  // by upper-case path
  std::set<std::string> dirs_;                 // upper-case paths
};

class DocumentTreeBackend final : public Install::Backend {
 public:
  explicit DocumentTreeBackend(std::shared_ptr<const Install::DocumentTree> tree) : tree_(std::move(tree)) {
    for (DocumentFile& f : list_document_files(*tree_)) {
      std::string key = upper(f.path);
      for (std::size_t slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1)) {
        dirs_.insert(key.substr(0, slash));
      }
      files_.emplace(std::move(key), std::move(f));
    }
  }

  bool exists(const std::vector<std::string>& parts) const override {
    const std::string key = upper_key(parts);
    return key.empty() || files_.contains(key) || dirs_.contains(key);
  }

  std::optional<std::vector<std::uint8_t>> try_read(const std::vector<std::string>& parts) const override {
    const auto it = files_.find(upper_key(parts));
    if (it == files_.end()) {
      return std::nullopt;
    }
    const DocumentFile& f = it->second;
    std::optional<std::vector<std::uint8_t>> data = tree_->read(f.id);
    if (!data) {
      throw DataError(std::format("cannot read {} (in {})", f.path, tree_->describe()));
    }
    if (data->size() != f.size) {
      throw DataError(
          std::format("{} (in {}): {} bytes, the listing says {}", f.path, tree_->describe(), data->size(), f.size));
    }
    return data;
  }

  std::vector<std::string> list(const std::vector<std::string>& dir, std::string_view upper_ext) const override {
    return list_index(files_, dir, upper_ext);
  }

  std::string describe() const override { return tree_->describe(); }

 private:
  std::shared_ptr<const Install::DocumentTree> tree_;
  std::map<std::string, DocumentFile> files_;  // by upper-case path
  std::set<std::string> dirs_;                 // upper-case paths
};

}  // namespace

Install::Install(std::shared_ptr<const Backend> backend) : backend_(std::move(backend)) {
  if (!exists("SH.EXE") && !exists("DATA")) {
    throw DataError(std::format("{} is not a Silent Hunter install (no SH.EXE or DATA/)", backend_->describe()));
  }
}

Install::Install(std::filesystem::path root) : Install(std::make_shared<DirectoryBackend>(std::move(root))) {}

Install Install::packaged(std::string_view index, PackageReader reader, std::string name) {
  return Install(std::make_shared<PackagedBackend>(index, std::move(reader), std::move(name)));
}

Install Install::document_tree(std::shared_ptr<const DocumentTree> tree) {
  return Install(std::make_shared<DocumentTreeBackend>(std::move(tree)));
}

bool Install::exists(std::string_view dos_path) const { return backend_->exists(split_dos_path(dos_path)); }

std::optional<std::vector<std::uint8_t>> Install::try_read(std::string_view dos_path) const {
  return backend_->try_read(split_dos_path(dos_path));
}

std::vector<std::uint8_t> Install::read(std::string_view dos_path) const {
  std::optional<std::vector<std::uint8_t>> data = try_read(dos_path);
  if (!data) {
    throw DataError(std::format("missing game file {} (in {})", dos_path, backend_->describe()));
  }
  return std::move(*data);
}

std::vector<std::string> Install::list(std::string_view dos_dir, std::string_view extension) const {
  const std::vector<std::string> dir = split_dos_path(dos_dir);
  std::vector<std::string> out = backend_->list(dir, upper(extension));
  const std::string key = upper_key(dir);
  if (!key.empty()) {
    for (std::string& name : out) {
      name = std::format("{}/{}", key, name);
    }
  }
  std::ranges::sort(out);
  return out;
}

namespace {

// Deeper trees are not installs (and this guards against a provider's loops).
constexpr int kMaxDocumentDepth = 16;

// Appends the files under directory `id` (path `path`, "" for the root) to
// `out`; `seen` holds the upper-case paths taken.
void list_documents(const Install::DocumentTree& tree, const std::string& id, const std::string& path, int depth,
                    std::set<std::string>& seen, std::vector<DocumentFile>& out) {
  std::vector<Install::DocumentTree::Entry> entries = tree.children(id);
  std::ranges::sort(entries, {}, &Install::DocumentTree::Entry::name);
  for (Install::DocumentTree::Entry& e : entries) {
    if (e.name.empty() || e.name == "." || e.name == ".." || e.name.find_first_of("/\\") != std::string::npos) {
      continue;
    }
    std::string child = path.empty() ? e.name : std::format("{}/{}", path, e.name);
    if (!seen.insert(upper(child)).second) {
      continue;
    }
    if (!e.directory) {
      out.push_back(DocumentFile{.path = std::move(child), .id = std::move(e.id), .size = e.size});
    } else if (depth < kMaxDocumentDepth) {
      list_documents(tree, e.id, child, depth + 1, seen, out);
    }
  }
}

}  // namespace

std::shared_ptr<const ByteSource> Install::DocumentTree::open(const std::string& id, const std::string& name) const {
  std::optional<std::vector<std::uint8_t>> data = read(id);
  if (!data) {
    throw DataError(std::format("cannot read {} (in {})", name, describe()));
  }
  return memory_source(std::move(*data), std::format("{} (in {})", name, describe()));
}

std::vector<DocumentFile> list_document_files(const Install::DocumentTree& tree) {
  std::set<std::string> seen;
  std::vector<DocumentFile> out;
  list_documents(tree, tree.root(), "", 0, seen, out);
  return out;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw DataError(std::format("cannot open {}", path.string()));
  }
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    // A directory opens, with no meaningful size.
    throw DataError(std::format("cannot read {}", path.string()));
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::size_t>(in.tellg());
  in.seekg(0);
  std::vector<std::uint8_t> data(size);
  if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
    throw DataError(std::format("cannot read {}", path.string()));
  }
  return data;
}

}  // namespace sh

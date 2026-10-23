// Access to the player's original Silent Hunter install.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/byte_source.h"

namespace sh {

// The original game install (SH.EXE, DATA/, ART/...): a host directory, a
// file tree packaged with the program (the Android APK's assets) or a tree
// of documents (a folder opened through Android's Storage Access
// Framework). Paths are given in the game's own DOS spelling ("DATA/SUBS.DAT",
// '\\' or '/' separators, empty and "." components ignored) and matched
// case-insensitively, since installs copied from the CD may be lower- or
// mixed-case. An install is immutable and cheap to copy.
class Install {
 public:
  // Reads one file of a packaged tree by its path as spelled in the index;
  // returns nothing when it cannot be opened. Called from any thread.
  using PackageReader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string& path)>;

  // The install in host directory `root`. Throws `DataError` if `root`
  // does not look like an install (no SH.EXE and no DATA directory).
  explicit Install(std::filesystem::path root);

  // The packaged install whose files `index` lists, read through `reader`;
  // `name` describes it in error messages. The index is text:
  //
  //   SH-BUNDLE 2
  //   <size in bytes> <path relative to the tree, '/' separated>
  //   ...
  //
  // Throws `DataError` on a malformed index (bad header or line, a path
  // with "..", '\\' or empty components, two paths equal ignoring case) or
  // one that lists neither SH.EXE nor a DATA directory.
  static Install packaged(std::string_view index, PackageReader reader, std::string name);

  // A tree of documents, directories and files named by opaque ids.
  class DocumentTree {
   public:
    struct Entry {
      std::string name;  // the file or directory name
      std::string id;    // the document id
      bool directory = false;
      std::uint64_t size = 0;  // of a file
    };

    virtual ~DocumentTree() = default;

    // Returns the id of the root directory.
    virtual std::string root() const = 0;
    // Returns the entries of directory `id`, in any order; throws
    // `DataError` if it cannot be listed.
    virtual std::vector<Entry> children(const std::string& id) const = 0;
    // Returns the whole content of file `id`, nothing if it cannot be
    // opened. Called from any thread.
    virtual std::optional<std::vector<std::uint8_t>> read(const std::string& id) const = 0;
    // Returns random access to file `id` (`name` describing it), for the
    // files too large to read whole (a CD image); throws `DataError` when it
    // cannot be opened. By default, `read`'s content.
    virtual std::shared_ptr<const ByteSource> open(const std::string& id, const std::string& name) const;
    // Returns the tree's description for error messages.
    virtual std::string describe() const = 0;
  };

  // The install in document tree `tree`, whose files are all listed once
  // here by `list_document_files`. Throws `DataError` when the tree cannot
  // be listed or holds neither SH.EXE nor a DATA directory; reading a file
  // throws it when the document cannot be read or its size differs from
  // the listed one.
  static Install document_tree(std::shared_ptr<const DocumentTree> tree);

  // Returns whether `dos_path` names a file or a directory of the install.
  bool exists(std::string_view dos_path) const;

  // Returns the whole content of file `dos_path`, nothing if it is missing;
  // throws `DataError` if it exists but cannot be read.
  std::optional<std::vector<std::uint8_t>> try_read(std::string_view dos_path) const;

  // Returns the whole content of file `dos_path`; throws `DataError` if it
  // is missing or cannot be read.
  std::vector<std::uint8_t> read(std::string_view dos_path) const;

  // Returns the DOS-spelled paths (upper case, '/' separated) of the files in
  // directory `dos_dir` whose name ends with `extension` (e.g. ".SCN"),
  // sorted. An empty `dos_dir` means the root.
  std::vector<std::string> list(std::string_view dos_dir, std::string_view extension) const;

  // The storage of the files: a host directory or a packaged tree.
  class Backend;

 private:
  explicit Install(std::shared_ptr<const Backend> backend);

  std::shared_ptr<const Backend> backend_;
};

// A file of a document tree, by its '/' separated path from the root.
struct DocumentFile {
  std::string path;
  std::string id;
  std::uint64_t size = 0;
};

// Returns every file of `tree`, depth-first in byte order of the names.
// Of names equal ignoring case only the first is kept (a file or a whole
// directory); names that cannot be part of a path ("", ".", "..", with a
// '/' or '\\') are left out, as are directories deeper than 16 levels.
// Throws `DataError` when a directory cannot be listed.
std::vector<DocumentFile> list_document_files(const Install::DocumentTree& tree);

// Reads a whole file from the host file system; throws `DataError` on failure.
std::vector<std::uint8_t> read_file(const std::filesystem::path& path);

}  // namespace sh

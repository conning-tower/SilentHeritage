// The game as the original CDs hold it, before INSTALL.EXE: the ARJ volumes
// SH.A00, SH.A01... (core/arj.h) in the CD's SH folder, with the folders the
// game reads from the CD (BUD, CINES, SPEECH) beside them. They are found in
// a folder, a document tree or a CD image (core/iso9660.h) and unpacked
// into a host folder, which is then an install (core/install.h).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/byte_source.h"
#include "core/install.h"

namespace sh {

// A tree of files, read by '/' separated paths of their real names.
class MediaTree {
 public:
  struct Entry {
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;  // of a file
  };

  virtual ~MediaTree() = default;

  // Returns the entries of directory `dir` ("" for the root), in any order;
  // throws `DataError` when it cannot be listed.
  virtual std::vector<Entry> list(const std::string& dir) const = 0;
  // Returns file `path`; throws `DataError` when it cannot be opened.
  virtual std::shared_ptr<const ByteSource> open(const std::string& path) const = 0;
  // Returns the tree's description for messages.
  virtual std::string describe() const = 0;
};

// The tree of host folder `dir`.
std::shared_ptr<const MediaTree> host_media_tree(std::filesystem::path dir);
// The tree of CD image `image`; throws `DataError` when it is not one.
std::shared_ptr<const MediaTree> iso_media_tree(std::shared_ptr<const ByteSource> image);
// The tree of document tree `tree`.
std::shared_ptr<const MediaTree> document_media_tree(std::shared_ptr<const Install::DocumentTree> tree);

// The files of an install to be unpacked from the media.
struct GameMedia {
  struct File {
    std::string path;  // in the install, '/' separated
    std::uint64_t size = 0;
    // Returns the content; throws `DataError` when it cannot be read.
    std::function<std::vector<std::uint8_t>()> read;
  };

  std::string location;  // where the media were found, for messages
  std::vector<File> files;

  std::uint64_t total_size() const;
  // Returns the file at `path` (any case), nothing if there is none.
  const File* find(std::string_view path) const;
};

// Looks for the media in `tree`: the folder holding SH.A00 (or SH.ARJ) at
// the root or in a folder of the root (the CD's root chosen), else the CD
// images (.iso) of the root. The files are those of the volumes, files
// continued from one volume to the next joined, a later volume's file
// replacing an earlier one, then the files of the subfolders beside the
// volumes that the volumes do not hold. Returns nothing when there are no
// media; throws `DataError` when they are corrupt (an archive's headers,
// an unsafe path) or hold no SH.EXE.
std::optional<GameMedia> find_game_media(const std::shared_ptr<const MediaTree>& tree);

// Unpacks `media` into host folder `dest` through a temporary folder
// beside it, replacing any previous install there, and calls `progress`
// with the bytes written so far after each file. Throws `DataError` when
// a file cannot be read or unpacked, `std::runtime_error` when it cannot
// be written, leaving no temporary folder.
void install_game_media(const GameMedia& media, const std::filesystem::path& dest,
                        const std::function<void(std::uint64_t)>& progress);

}  // namespace sh

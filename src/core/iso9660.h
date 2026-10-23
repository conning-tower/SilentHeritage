// ISO 9660 CD images (.iso), read through their primary volume descriptor:
// the 8.3 names of a DOS-era CD ("SH/SH.A00"), without the ";1" version.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/byte_source.h"

namespace sh {

// One file of an image.
struct IsoFile {
  std::string path;  // '/' separated, from the root
  std::uint64_t offset = 0;  // in the image
  std::uint32_t size = 0;
};

class IsoImage {
 public:
  // Reads the directories of `image`. Throws `DataError` when it is not an
  // ISO 9660 image or its directories are corrupt.
  explicit IsoImage(std::shared_ptr<const ByteSource> image);

  // The files, those of each directory before its subdirectories'.
  const std::vector<IsoFile>& files() const { return files_; }

  // Returns the source of `file` (one of `files()`).
  std::shared_ptr<const ByteSource> open(const IsoFile& file) const;

  std::string describe() const { return image_->describe(); }

 private:
  void read_directory(std::uint64_t extent, std::uint32_t size, const std::string& path, int depth);

  std::shared_ptr<const ByteSource> image_;
  std::vector<IsoFile> files_;
};

// Returns whether `image` starts like an ISO 9660 image (a volume
// descriptor at sector 16).
bool looks_like_iso(const ByteSource& image);

}  // namespace sh

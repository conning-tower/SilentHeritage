#include "core/iso9660.h"

#include <format>
#include <utility>

#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr std::uint64_t kSector = 2048;
constexpr std::uint64_t kFirstDescriptor = 16;
// Descriptors read before giving up on a primary one.
constexpr std::uint64_t kMaxDescriptors = 32;
// Deeper directories are not those of a game CD (and this guards against
// loops).
constexpr int kMaxDepth = 16;

constexpr std::uint8_t kTypePrimary = 1;
constexpr std::uint8_t kTypeTerminator = 255;
constexpr std::uint8_t kFlagDirectory = 0x02;
constexpr std::uint8_t kFlagMultiExtent = 0x80;

bool is_descriptor(std::span<const std::uint8_t> sector) {
  return sector.size() >= 7 && sector[1] == 'C' && sector[2] == 'D' && sector[3] == '0' && sector[4] == '0' &&
         sector[5] == '1';
}

}  // namespace

bool looks_like_iso(const ByteSource& image) {
  if (image.size() < (kFirstDescriptor + 1) * kSector) {
    return false;
  }
  return is_descriptor(image.read_bytes(kFirstDescriptor * kSector, 7));
}

IsoImage::IsoImage(std::shared_ptr<const ByteSource> image) : image_(std::move(image)) {
  for (std::uint64_t n = kFirstDescriptor; n < kFirstDescriptor + kMaxDescriptors; ++n) {
    if ((n + 1) * kSector > image_->size()) {
      break;
    }
    const std::vector<std::uint8_t> sector = image_->read_bytes(n * kSector, kSector);
    if (!is_descriptor(sector) || sector[0] == kTypeTerminator) {
      break;
    }
    if (sector[0] == kTypePrimary) {
      // The root directory's record.
      const std::span<const std::uint8_t> root = std::span(sector).subspan(156, 34);
      read_directory(read_u32le(root, 2) * kSector, read_u32le(root, 10), "", 0);
      return;
    }
  }
  throw DataError(std::format("{} is not an ISO 9660 CD image", image_->describe()));
}

void IsoImage::read_directory(std::uint64_t extent, std::uint32_t size, const std::string& path, int depth) {
  const std::vector<std::uint8_t> data = image_->read_bytes(extent, size);
  std::vector<std::pair<std::string, std::pair<std::uint64_t, std::uint32_t>>> subdirs;
  std::size_t pos = 0;
  while (pos < data.size()) {
    const std::uint8_t length = data[pos];
    if (length == 0) {
      // Records do not cross sectors: the rest of this one is padding.
      pos = (pos / kSector + 1) * kSector;
      continue;
    }
    if (length < 34 || pos + length > data.size() || 33u + data[pos + 32] > length) {
      throw DataError(std::format("{}: corrupt directory /{}", image_->describe(), path));
    }
    const std::span<const std::uint8_t> record = std::span(data).subspan(pos, length);
    pos += length;
    const std::uint8_t flags = record[25];
    std::string name(reinterpret_cast<const char*>(record.data() + 33), record[32]);
    if (name.size() == 1 && (name[0] == '\0' || name[0] == '\1')) {
      continue;  // "." and ".."
    }
    if (const std::size_t version = name.find(';'); version != std::string::npos) {
      name.resize(version);
    }
    if (!name.empty() && name.back() == '.') {
      name.pop_back();  // a name without extension
    }
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos) {
      continue;
    }
    std::string child = path.empty() ? name : std::format("{}/{}", path, name);
    const std::uint64_t child_extent = read_u32le(record, 2) * kSector;
    const std::uint32_t child_size = read_u32le(record, 10);
    if ((flags & kFlagDirectory) != 0) {
      subdirs.emplace_back(std::move(child), std::pair(child_extent, child_size));
    } else if ((flags & kFlagMultiExtent) != 0) {
      throw DataError(std::format("{}: /{} is in several parts (not supported)", image_->describe(), child));
    } else {
      if (child_extent + child_size > image_->size()) {
        throw DataError(std::format("{}: /{} is past the end of the image", image_->describe(), child));
      }
      files_.push_back(IsoFile{.path = std::move(child), .offset = child_extent, .size = child_size});
    }
  }
  if (depth >= kMaxDepth) {
    return;
  }
  for (const auto& [child, where] : subdirs) {
    read_directory(where.first, where.second, child, depth + 1);
  }
}

std::shared_ptr<const ByteSource> IsoImage::open(const IsoFile& file) const {
  return byte_range(image_, file.offset, file.size, std::format("{} (in {})", file.path, image_->describe()));
}

}  // namespace sh

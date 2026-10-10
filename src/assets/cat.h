// `.CAT` archives: flat bundles of `.SHP` members (3DSHIPS, 3DAIR, IDBOOK).
// Layout in docs/formats.md.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh {

class Install;

// One directory entry of a `.CAT` archive.
struct CatEntry {
  std::string name;          // e.g. "ABOAS.SHP" (padding removed)
  std::uint16_t flags = 0;   // observed 0x2000
  std::uint32_t timestamp = 0;  // packed, opaque
  std::uint32_t size = 0;
  std::uint32_t offset = 0;  // absolute offset of the data in the archive
};

// A loaded `.CAT` archive; owns the file content and hands out member views.
class CatArchive {
 public:
  // Parses the archive in `data`; throws `DataError` (naming `context`) if
  // the directory is truncated or a member lies outside the data.
  static CatArchive parse(std::vector<std::uint8_t> data, std::string_view context);
  // Reads and parses the archive at `dos_path` of `install`.
  static CatArchive load(const Install& install, std::string_view dos_path);

  const std::vector<CatEntry>& entries() const { return entries_; }

  // Returns the index of the member named `name` (case-insensitive), if any.
  std::optional<std::size_t> find(std::string_view name) const;

  // Returns the bytes of member `index` (valid while the archive lives).
  std::span<const std::uint8_t> member(std::size_t index) const;
  // Returns the bytes of the member named `name`; throws `DataError` if absent.
  std::span<const std::uint8_t> member(std::string_view name) const;

 private:
  std::vector<std::uint8_t> data_;
  std::vector<CatEntry> entries_;
  std::string context_;
};

}  // namespace sh

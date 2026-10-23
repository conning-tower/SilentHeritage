// Random access to the bytes of a file that may be too large to read whole
// (a CD image): a host file, a document, a part of another source or a
// buffer.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sh {

class ByteSource {
 public:
  virtual ~ByteSource() = default;

  virtual std::uint64_t size() const = 0;
  // Fills `out` with the bytes at `offset`; throws `DataError` when they
  // cannot be read, past the end included. Called from one thread at a time.
  virtual void read(std::uint64_t offset, std::span<std::uint8_t> out) const = 0;
  // Returns the source's description for error messages.
  virtual std::string describe() const = 0;

  // Returns the `size` bytes at `offset`.
  std::vector<std::uint8_t> read_bytes(std::uint64_t offset, std::size_t size) const;
  // Returns the whole content.
  std::vector<std::uint8_t> read_all() const;
};

// Returns the source of host file `path`; throws `DataError` when it cannot
// be opened.
std::shared_ptr<const ByteSource> host_file_source(const std::filesystem::path& path);

// Returns the `size` bytes of `source` at `offset`, described as `name`;
// throws `DataError` when they are not all within `source`.
std::shared_ptr<const ByteSource> byte_range(std::shared_ptr<const ByteSource> source, std::uint64_t offset,
                                             std::uint64_t size, std::string name);

// Returns the source of `data`, described as `name`.
std::shared_ptr<const ByteSource> memory_source(std::vector<std::uint8_t> data, std::string name);

}  // namespace sh

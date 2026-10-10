// Bounds-checked little-endian reader over an in-memory original file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "core/error.h"

namespace sh {

// Reads little-endian integers and fixed-size strings sequentially from a
// byte buffer. Every read past the end throws `DataError` naming `context`
// (usually the file name). The buffer must outlive the reader.
class ByteReader {
 public:
  explicit ByteReader(std::span<const std::uint8_t> data, std::string_view context = "data")
      : data_(data), context_(context) {}

  std::size_t pos() const { return pos_; }
  std::size_t size() const { return data_.size(); }
  std::size_t remaining() const { return data_.size() - pos_; }
  bool at_end() const { return pos_ == data_.size(); }

  // Moves to absolute offset `pos` (may equal `size()`).
  void seek(std::size_t pos);
  void skip(std::size_t n);

  std::uint8_t u8();
  std::int8_t i8() { return static_cast<std::int8_t>(u8()); }
  std::uint16_t u16();
  std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
  std::uint32_t u32();
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

  // Returns the next `n` bytes without copying.
  std::span<const std::uint8_t> bytes(std::size_t n);

  // Reads an `n`-byte field holding a NUL-terminated (or NUL-padded) string.
  std::string fixed_string(std::size_t n);

  // Returns a reader over the next `n` bytes and skips them here.
  ByteReader sub(std::size_t n);

  // Throws `DataError` with `message`, prefixed by the context and position.
  [[noreturn]] void fail(std::string_view message) const;

 private:
  void require(std::size_t n) const;

  std::span<const std::uint8_t> data_;
  std::string context_;
  std::size_t pos_ = 0;
};

// Reads little-endian values at absolute offsets of a buffer (bounds-checked).
std::uint16_t read_u16le(std::span<const std::uint8_t> data, std::size_t offset);
std::uint32_t read_u32le(std::span<const std::uint8_t> data, std::size_t offset);

}  // namespace sh

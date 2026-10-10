#include "core/byte_reader.h"

#include <algorithm>
#include <format>

namespace sh {

void ByteReader::require(std::size_t n) const {
  if (n > remaining()) {
    fail(std::format("read of {} bytes past the end ({} bytes)", n, data_.size()));
  }
}

void ByteReader::fail(std::string_view message) const {
  throw DataError(std::format("{} @0x{:x}: {}", context_, pos_, message));
}

void ByteReader::seek(std::size_t pos) {
  if (pos > data_.size()) {
    fail(std::format("seek to 0x{:x} past the end", pos));
  }
  pos_ = pos;
}

void ByteReader::skip(std::size_t n) {
  require(n);
  pos_ += n;
}

std::uint8_t ByteReader::u8() {
  require(1);
  return data_[pos_++];
}

std::uint16_t ByteReader::u16() {
  require(2);
  const auto v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
  pos_ += 2;
  return v;
}

std::uint32_t ByteReader::u32() {
  require(4);
  const std::uint32_t v = static_cast<std::uint32_t>(data_[pos_]) |
                          (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
                          (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
                          (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
  pos_ += 4;
  return v;
}

std::span<const std::uint8_t> ByteReader::bytes(std::size_t n) {
  require(n);
  auto out = data_.subspan(pos_, n);
  pos_ += n;
  return out;
}

std::string ByteReader::fixed_string(std::size_t n) {
  const auto raw = bytes(n);
  const auto end = std::find(raw.begin(), raw.end(), std::uint8_t{0});
  return std::string(raw.begin(), end);
}

ByteReader ByteReader::sub(std::size_t n) {
  ByteReader out(bytes(n), context_);
  return out;
}

std::uint16_t read_u16le(std::span<const std::uint8_t> data, std::size_t offset) {
  ByteReader r(data);
  r.seek(offset);
  return r.u16();
}

std::uint32_t read_u32le(std::span<const std::uint8_t> data, std::size_t offset) {
  ByteReader r(data);
  r.seek(offset);
  return r.u32();
}

}  // namespace sh

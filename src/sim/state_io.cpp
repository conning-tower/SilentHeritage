#include "sim/state_io.h"

#include <algorithm>
#include <cstring>

#include "core/error.h"
#include "sim/vehicle.h"

namespace sh {

void ByteWriter::u32(std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    data_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
}

void ByteWriter::i64(std::int64_t v) {
  const auto u = static_cast<std::uint64_t>(v);
  u32(static_cast<std::uint32_t>(u));
  u32(static_cast<std::uint32_t>(u >> 32));
}

void ByteWriter::fixed_string(std::string_view s, std::size_t n) {
  const std::size_t len = std::min(s.size(), n == 0 ? 0 : n - 1);
  data_.insert(data_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
  data_.insert(data_.end(), n - len, 0);
}

std::span<const std::uint8_t> StateImage::bytes(std::size_t offset, std::size_t n) const {
  if (offset > data_.size() || n > data_.size() - offset) {
    throw DataError("state image: field out of range");
  }
  return std::span<const std::uint8_t>(data_).subspan(offset, n);
}

std::span<std::uint8_t> StateImage::bytes(std::size_t offset, std::size_t n) {
  if (offset > data_.size() || n > data_.size() - offset) {
    throw DataError("state image: field out of range");
  }
  return std::span<std::uint8_t>(data_).subspan(offset, n);
}

std::int32_t StateImage::i32(std::size_t offset) const {
  return static_cast<std::int32_t>(read_u32le(bytes(offset, 4), 0));
}

void StateImage::set_i32(std::size_t offset, std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  std::span<std::uint8_t> b = bytes(offset, 4);
  for (std::size_t i = 0; i < 4; ++i) {
    b[i] = static_cast<std::uint8_t>(u >> (8 * i));
  }
}

std::int64_t StateImage::i64(std::size_t offset) const {
  const std::uint64_t lo = static_cast<std::uint32_t>(i32(offset));
  const std::uint64_t hi = static_cast<std::uint32_t>(i32(offset + 4));
  return static_cast<std::int64_t>(lo | hi << 32);
}

void StateImage::set_i64(std::size_t offset, std::int64_t v) {
  const auto u = static_cast<std::uint64_t>(v);
  set_i32(offset, static_cast<std::int32_t>(static_cast<std::uint32_t>(u)));
  set_i32(offset + 4, static_cast<std::int32_t>(static_cast<std::uint32_t>(u >> 32)));
}

std::string StateImage::string(std::size_t offset, std::size_t n) const {
  const std::span<const std::uint8_t> b = bytes(offset, n);
  const auto end = std::find(b.begin(), b.end(), std::uint8_t{0});
  return std::string(b.begin(), end);
}

void StateImage::set_string(std::size_t offset, std::size_t n, std::string_view s) {
  std::span<std::uint8_t> b = bytes(offset, n);
  std::fill(b.begin(), b.end(), std::uint8_t{0});
  const std::size_t len = std::min(s.size(), n == 0 ? 0 : n - 1);
  std::memcpy(b.data(), s.data(), len);
}

void write_ranges(ByteWriter& out, const StateImage& image, std::span<const StateRange> ranges) {
  for (const StateRange& r : ranges) {
    out.bytes(image.bytes(r.offset, r.size));
  }
}

void read_ranges(ByteReader& in, StateImage& image, std::span<const StateRange> ranges) {
  for (const StateRange& r : ranges) {
    const std::span<const std::uint8_t> src = in.bytes(r.size);
    std::span<std::uint8_t> dst = image.bytes(r.offset, r.size);
    std::copy(src.begin(), src.end(), dst.begin());
  }
}

VehicleIds::VehicleIds(const std::vector<std::unique_ptr<Vehicle>>& vehicles) {
  for (std::size_t i = 0; i < vehicles.size(); ++i) {
    ids_[vehicles[i].get()] = static_cast<std::int32_t>(i);
  }
}

std::int32_t VehicleIds::id(const Vehicle* v) const {
  if (v == nullptr) {
    return -1;
  }
  const auto it = ids_.find(v);
  return it == ids_.end() ? -1 : it->second;
}

void VehicleLinks::resolve(const std::vector<std::unique_ptr<Vehicle>>& vehicles) {
  for (const auto& fixup : fixups_) {
    fixup(vehicles);
  }
  fixups_.clear();
}

void StateOut::vec3(std::uint32_t off, const fx::Vec3& v) {
  image_.set_i32(off, v.x);
  image_.set_i32(off + 4, v.y);
  image_.set_i32(off + 8, v.z);
}

void StateOut::mat3(std::uint32_t off, const fx::Mat3& m) {
  for (std::uint32_t i = 0; i < 9; ++i) {
    image_.set_i32(off + i * 4, m.m[i]);
  }
}

void StateOut::x_vec3(const fx::Vec3& v) {
  ext_.i32(v.x);
  ext_.i32(v.y);
  ext_.i32(v.z);
}

void StateOut::x_string(const std::string& s) {
  ext_.u32(static_cast<std::uint32_t>(s.size()));
  ext_.bytes(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

void StateIn::vec3(std::uint32_t off, fx::Vec3& v) const {
  v = {image_.i32(off), image_.i32(off + 4), image_.i32(off + 8)};
}

void StateIn::mat3(std::uint32_t off, fx::Mat3& m) const {
  for (std::uint32_t i = 0; i < 9; ++i) {
    m.m[i] = image_.i32(off + i * 4);
  }
}

void StateIn::x_i64(std::int64_t& v) {
  if (ext_ != nullptr) {
    const std::uint64_t lo = ext_->u32();
    const std::uint64_t hi = ext_->u32();
    v = static_cast<std::int64_t>(lo | hi << 32);
  }
}

void StateIn::x_vec3(fx::Vec3& v) {
  if (ext_ != nullptr) {
    v.x = ext_->i32();
    v.y = ext_->i32();
    v.z = ext_->i32();
  }
}

void StateIn::x_string(std::string& s) {
  if (ext_ != nullptr) {
    const std::uint32_t n = ext_->u32();
    const std::span<const std::uint8_t> b = ext_->bytes(n);
    s.assign(b.begin(), b.end());
  }
}

}  // namespace sh

// The building blocks of the original's state files: the simulation state
// lists of `sim_write_state` / `sim_read_state` (0x19061 / 0x1873b), which
// follow the MISSn.SAV header and the ROSTER of a career saved on patrol.
//
// The original writes each object as ranges of its own memory layout
// (`write(fd, obj + offset, size)`), and every class's `*_write_state` /
// `*_read_state` pair lists the same ranges. Here an object is captured
// into a `StateImage` of the original's layout, written and read by its
// `StateRange` list, and the classes map their fields to the original
// offsets in one `bind` per class used both ways (`StateOut` fills the
// image, `StateIn` reads it). Pointers between vehicles are stored as the
// vehicles' list indices (the original's +0xc4 ids) and set once every
// vehicle exists (`VehicleLinks`, the original's slot +0xe4).
//
// The state the original does not save (the clock's hundredths, the sky,
// the sensors' timers, the random generator...) goes to an extension
// stream appended after the original lists, which the original's reader
// never reaches: with it a restored mission continues exactly as the saved
// one; a file without it (an original save) restores what the original
// restores.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "core/byte_reader.h"
#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh {

class Vehicle;

// Little-endian byte sink of the state files.
class ByteWriter {
 public:
  void u8(std::uint8_t v) { data_.push_back(v); }
  void u32(std::uint32_t v);
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void i64(std::int64_t v);
  void bytes(std::span<const std::uint8_t> b) { data_.insert(data_.end(), b.begin(), b.end()); }
  // Writes `s` in an `n`-byte field, NUL-padded (cut to n - 1 characters).
  void fixed_string(std::string_view s, std::size_t n);
  void zeros(std::size_t n) { data_.insert(data_.end(), n, 0); }

  std::size_t size() const { return data_.size(); }
  const std::vector<std::uint8_t>& data() const { return data_; }
  std::vector<std::uint8_t> take() { return std::move(data_); }

 private:
  std::vector<std::uint8_t> data_;
};

// The bytes of an object in the original's memory layout (zero where
// nothing is known), addressed by the original's offsets.
class StateImage {
 public:
  explicit StateImage(std::size_t size = 0) : data_(size, 0) {}

  std::size_t size() const { return data_.size(); }
  std::span<const std::uint8_t> bytes(std::size_t offset, std::size_t n) const;
  std::span<std::uint8_t> bytes(std::size_t offset, std::size_t n);
  std::int32_t i32(std::size_t offset) const;
  void set_i32(std::size_t offset, std::int32_t v);
  std::int64_t i64(std::size_t offset) const;
  void set_i64(std::size_t offset, std::int64_t v);
  std::uint8_t u8(std::size_t offset) const { return bytes(offset, 1)[0]; }
  void set_u8(std::size_t offset, std::uint8_t v) { bytes(offset, 1)[0] = v; }
  // A NUL-terminated string in an `n`-byte field.
  std::string string(std::size_t offset, std::size_t n) const;
  void set_string(std::size_t offset, std::size_t n, std::string_view s);

  bool operator==(const StateImage&) const = default;

 private:
  std::vector<std::uint8_t> data_;
};

// One `write(fd, obj + offset, size)` of an object's writer.
struct StateRange {
  std::uint32_t offset = 0;
  std::uint32_t size = 0;
};

// Writes the ranges of `image` in order.
void write_ranges(ByteWriter& out, const StateImage& image, std::span<const StateRange> ranges);
// Reads the ranges into `image` in order; throws `DataError` on a short read.
void read_ranges(ByteReader& in, StateImage& image, std::span<const StateRange> ranges);

// The ids the state files refer to the vehicles by: their index in the
// simulation's vehicle list (the original sets every vehicle's +0xc4 to
// it before writing), -1 for none.
class VehicleIds {
 public:
  VehicleIds() = default;
  explicit VehicleIds(const std::vector<std::unique_ptr<Vehicle>>& vehicles);
  std::int32_t id(const Vehicle* v) const;

 private:
  std::unordered_map<const Vehicle*, std::int32_t> ids_;
};

// The pointers read as vehicle ids, set once every vehicle exists (slot
// +0xe4, `*_resolve_refs`). An id outside the list (e.g. a raw pointer of
// an original save) gives null.
class VehicleLinks {
 public:
  template <class T>
  void add(T*& slot, std::int32_t id) {
    fixups_.push_back([&slot, id](const std::vector<std::unique_ptr<Vehicle>>& vehicles) {
      slot = id >= 0 && static_cast<std::size_t>(id) < vehicles.size()
                 ? dynamic_cast<T*>(vehicles[static_cast<std::size_t>(id)].get())
                 : nullptr;
    });
  }
  // Sets the pointers from `vehicles` (in list order) and forgets them.
  void resolve(const std::vector<std::unique_ptr<Vehicle>>& vehicles);

 private:
  std::vector<std::function<void(const std::vector<std::unique_ptr<Vehicle>>&)>> fixups_;
};

// The original's flag values: most are set to -1.
inline constexpr std::int32_t kFlagOn = -1;

// Fills an image (and the extension stream) from an object's fields.
class StateOut {
 public:
  static constexpr bool kReading = false;

  StateOut(StateImage& image, ByteWriter& ext, const VehicleIds& ids) : image_(image), ext_(ext), ids_(ids) {}

  StateImage& image() { return image_; }
  const VehicleIds& ids() const { return ids_; }

  void i32(std::uint32_t off, std::int32_t v) { image_.set_i32(off, v); }
  void u32(std::uint32_t off, std::uint32_t v) { image_.set_i32(off, static_cast<std::int32_t>(v)); }
  void u8(std::uint32_t off, std::uint8_t v) { image_.set_u8(off, v); }
  void i64(std::uint32_t off, std::int64_t v) { image_.set_i64(off, v); }
  void flag(std::uint32_t off, bool v, std::int32_t on = kFlagOn) { image_.set_i32(off, v ? on : 0); }
  void angle(std::uint32_t off, Angle10 v) { image_.set_i32(off, v.tenths()); }
  void fixed(std::uint32_t off, Fixed16 v) { image_.set_i32(off, v.raw()); }
  void vec3(std::uint32_t off, const fx::Vec3& v);
  void mat3(std::uint32_t off, const fx::Mat3& m);
  template <class E>
  void enumeration(std::uint32_t off, E v) {
    image_.set_i32(off, static_cast<std::int32_t>(v));
  }
  void string(std::uint32_t off, std::size_t n, const std::string& s) { image_.set_string(off, n, s); }
  template <class T>
  void ref(std::uint32_t off, T* v) {
    image_.set_i32(off, ids_.id(v));
  }

  // The extension stream.
  void x_i32(std::int32_t v) { ext_.i32(v); }
  void x_u32(std::uint32_t v) { ext_.u32(v); }
  void x_i64(std::int64_t v) { ext_.i64(v); }
  void x_flag(bool v) { ext_.u8(v ? 1 : 0); }
  void x_angle(Angle10 v) { ext_.i32(v.tenths()); }
  void x_fixed(Fixed16 v) { ext_.i32(v.raw()); }
  void x_vec3(const fx::Vec3& v);
  template <class E>
  void x_enum(E v) {
    ext_.i32(static_cast<std::int32_t>(v));
  }
  void x_string(const std::string& s);
  template <class T>
  void x_ref(T* v) {
    ext_.i32(ids_.id(v));
  }
  // A count of the extension: written, and returned as is.
  std::size_t x_count(std::size_t n) {
    ext_.u32(static_cast<std::uint32_t>(n));
    return n;
  }

 private:
  StateImage& image_;
  ByteWriter& ext_;
  const VehicleIds& ids_;
};

// Sets an object's fields from an image (and the extension stream, when the
// file has one: else the `x_` calls leave the fields as they are).
class StateIn {
 public:
  static constexpr bool kReading = true;

  StateIn(const StateImage& image, ByteReader* ext, VehicleLinks& links) : image_(image), ext_(ext), links_(links) {}

  const StateImage& image() const { return image_; }
  bool has_extension() const { return ext_ != nullptr; }
  VehicleLinks& links() { return links_; }

  void i32(std::uint32_t off, std::int32_t& v) const { v = image_.i32(off); }
  void u32(std::uint32_t off, std::uint32_t& v) const { v = static_cast<std::uint32_t>(image_.i32(off)); }
  void u8(std::uint32_t off, std::uint8_t& v) const { v = image_.u8(off); }
  void i64(std::uint32_t off, std::int64_t& v) const { v = image_.i64(off); }
  void flag(std::uint32_t off, bool& v, std::int32_t /*on*/ = kFlagOn) const { v = image_.i32(off) != 0; }
  void angle(std::uint32_t off, Angle10& v) const { v = Angle10(image_.i32(off)); }
  void fixed(std::uint32_t off, Fixed16& v) const { v = Fixed16::from_raw(image_.i32(off)); }
  void vec3(std::uint32_t off, fx::Vec3& v) const;
  void mat3(std::uint32_t off, fx::Mat3& m) const;
  template <class E>
  void enumeration(std::uint32_t off, E& v) const {
    v = static_cast<E>(image_.i32(off));
  }
  void string(std::uint32_t off, std::size_t n, std::string& s) const { s = image_.string(off, n); }
  template <class T>
  void ref(std::uint32_t off, T*& v) {
    links_.add(v, image_.i32(off));
  }

  void x_i32(std::int32_t& v) {
    if (ext_ != nullptr) {
      v = ext_->i32();
    }
  }
  void x_u32(std::uint32_t& v) {
    if (ext_ != nullptr) {
      v = ext_->u32();
    }
  }
  void x_i64(std::int64_t& v);
  void x_flag(bool& v) {
    if (ext_ != nullptr) {
      v = ext_->u8() != 0;
    }
  }
  void x_angle(Angle10& v) {
    if (ext_ != nullptr) {
      v = Angle10(ext_->i32());
    }
  }
  void x_fixed(Fixed16& v) {
    if (ext_ != nullptr) {
      v = Fixed16::from_raw(ext_->i32());
    }
  }
  void x_vec3(fx::Vec3& v);
  template <class E>
  void x_enum(E& v) {
    if (ext_ != nullptr) {
      v = static_cast<E>(ext_->i32());
    }
  }
  void x_string(std::string& s);
  template <class T>
  void x_ref(T*& v) {
    if (ext_ != nullptr) {
      links_.add(v, ext_->i32());
    }
  }
  // A count of the extension: read, or `n` (the current one) without one.
  // Throws `DataError` for a count above the bytes left (every counted item
  // takes at least one).
  std::size_t x_count(std::size_t n) {
    if (ext_ == nullptr) {
      return n;
    }
    const std::uint32_t count = ext_->u32();
    if (count > ext_->remaining()) {
      ext_->fail("extension count past the end");
    }
    return count;
  }

 private:
  const StateImage& image_;
  ByteReader* ext_;
  VehicleLinks& links_;
};

// Binds a vector of the extension: its size, then `element(io, item)` for
// each item (resized when reading).
template <class Io, class V, class F>
void bind_x_vector(Io& io, V& items, F element) {
  const std::size_t n = io.x_count(items.size());
  if constexpr (Io::kReading) {
    items.resize(n);
  }
  for (auto& item : items) {
    element(io, item);
  }
}

}  // namespace sh

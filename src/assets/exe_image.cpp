#include "assets/exe_image.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>

#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr std::uint32_t kPageSize = 4096;
constexpr double kMinFixupConsistency = 0.9;

// LE fixup source types (low nibble of the source byte).
enum class FixupSource : std::uint8_t {
  kSelector16 = 0x02,
  kOffset16 = 0x05,
  kPointer16_32 = 0x06,
  kOffset32 = 0x07,
  kSelfRelative32 = 0x08,
};

// One internal relocation: the value at linear `source` refers to linear
// `target`, in object `target_object` (0-based).
struct Fixup {
  std::uint32_t source;
  std::uint8_t type;
  std::uint32_t target;
  std::size_t target_object;
};

// The LE header fields the loader needs.
struct LeHeader {
  std::size_t le_offset = 0;   // file offset of the "LE" signature
  std::size_t exe_offset = 0;  // file offset of the bound program's MZ stub
  std::uint32_t num_pages = 0;
  std::uint32_t data_pages_offset = 0;  // stub-relative
  std::uint32_t fixup_page_table = 0;   // LE-relative
  std::uint32_t fixup_record_table = 0;  // LE-relative
  std::vector<LeObject> objects;
};

// Returns the file offset of the LE header: an "LE" signature whose fields
// are self-consistent (byte/word order 0, 386 CPU family, 4 KiB pages, object
// table at 0xc4 as Watcom writes it), unlike incidental "LE" pairs in code.
std::size_t find_le_header(std::span<const std::uint8_t> data) {
  for (std::size_t pos = 0; pos + 0xc8 <= data.size(); ++pos) {
    if (data[pos] != 'L' || data[pos + 1] != 'E' || data[pos + 2] != 0 || data[pos + 3] != 0) {
      continue;
    }
    const std::uint16_t cpu = read_u16le(data, pos + 0x08);
    if (cpu >= 1 && cpu <= 4 && read_u32le(data, pos + 0x28) == kPageSize && read_u32le(data, pos + 0x40) == 0xc4) {
      return pos;
    }
  }
  throw DataError("SH.EXE: no LE header found");
}

// Returns the file offset of the MZ stub whose `e_lfanew` points at the LE
// header at `le_offset` (the base of the page data offset).
std::size_t find_bound_stub(std::span<const std::uint8_t> data, std::size_t le_offset) {
  for (std::size_t pos = le_offset; pos-- > 0;) {
    if (data[pos] == 'M' && pos + 1 < le_offset && data[pos + 1] == 'Z' && pos + 0x40 <= le_offset &&
        pos + read_u32le(data, pos + 0x3c) == le_offset) {
      return pos;
    }
  }
  throw DataError(std::format("SH.EXE: no MZ stub points at the LE header at {:#x}", le_offset));
}

LeHeader parse_le(std::span<const std::uint8_t> data) {
  LeHeader le;
  le.le_offset = find_le_header(data);
  le.exe_offset = find_bound_stub(data, le.le_offset);
  const auto field = [&](std::size_t off) { return read_u32le(data, le.le_offset + off); };
  le.num_pages = field(0x14);
  le.fixup_page_table = field(0x68);
  le.fixup_record_table = field(0x6c);
  le.data_pages_offset = field(0x80);
  ByteReader r(data, "SH.EXE");
  r.seek(le.le_offset + field(0x40));
  const std::uint32_t count = field(0x44);
  for (std::uint32_t i = 0; i < count; ++i) {
    LeObject obj;
    obj.virtual_size = r.u32();
    obj.base_address = r.u32();
    obj.flags = r.u32();
    obj.first_page = r.u32();
    obj.num_pages = r.u32();
    r.skip(4);  // reserved
    le.objects.push_back(obj);
  }
  if (le.objects.empty()) {
    throw DataError("SH.EXE: the LE module has no objects");
  }
  return le;
}

// Maps each 1-based logical page number to its linear base address.
std::map<std::uint32_t, std::uint32_t> page_linear_bases(const LeHeader& le) {
  std::map<std::uint32_t, std::uint32_t> bases;
  for (const LeObject& obj : le.objects) {
    for (std::uint32_t p = 0; p < obj.num_pages; ++p) {
      bases[obj.first_page + p] = obj.base_address + p * kPageSize;
    }
  }
  return bases;
}

// Decodes the internal fixup records (the fixup page table bounds the records
// of each logical page; import fixups are skipped, a DOS/4GW game has none).
std::vector<Fixup> read_fixups(std::span<const std::uint8_t> data, const LeHeader& le) {
  const auto pages = page_linear_bases(le);
  const std::size_t page_table = le.le_offset + le.fixup_page_table;
  const std::size_t records = le.le_offset + le.fixup_record_table;
  std::vector<Fixup> out;
  ByteReader r(data, "SH.EXE fixups");
  for (std::uint32_t i = 0; i < le.num_pages; ++i) {
    const auto page = pages.find(i + 1);
    const std::size_t begin = records + read_u32le(data, page_table + 4 * i);
    const std::size_t end = records + read_u32le(data, page_table + 4 * (i + 1));
    if (page == pages.end()) {
      continue;
    }
    r.seek(begin);
    while (r.pos() < end) {
      const std::uint8_t source = r.u8();
      const std::uint8_t flags = r.u8();
      const std::uint8_t type = source & 0x0f;
      std::vector<std::int16_t> offsets;
      if ((source & 0x10) != 0) {  // source list
        const std::uint8_t n = r.u8();
        for (std::uint8_t k = 0; k < n; ++k) {
          offsets.push_back(r.i16());
        }
      } else {
        offsets.push_back(r.i16());
      }
      const bool internal = (flags & 0x03) == 0;
      const std::uint32_t object = (flags & 0x40) != 0 ? r.u16() : r.u8();
      std::uint32_t target_offset = 0;
      if (type != static_cast<std::uint8_t>(FixupSource::kSelector16)) {
        target_offset = (flags & 0x10) != 0 ? r.u32() : r.u16();
      }
      if ((flags & 0x04) != 0) {  // additive value (not used by Watcom here)
        r.skip((flags & 0x20) != 0 ? 4 : 2);
      }
      if (!internal || object < 1 || object > le.objects.size()) {
        continue;
      }
      const std::uint32_t target = le.objects[object - 1].base_address + target_offset;
      for (const std::int16_t so : offsets) {
        out.push_back(Fixup{static_cast<std::uint32_t>(static_cast<std::int64_t>(page->second) + so), type,
                            target, object - 1});
      }
    }
  }
  return out;
}

// Returns the fraction of 32-bit offset fixups whose source dword on disk
// holds the target's object-relative offset (Watcom's linker leaves it there).
double measure_fixup_consistency(std::span<const std::uint8_t> data, const LeHeader& le, const std::vector<Fixup>& fixups) {
  const std::size_t page_base = le.exe_offset + le.data_pages_offset;
  std::map<std::uint32_t, std::uint32_t> linear_to_page;
  for (const auto& [num, linear] : page_linear_bases(le)) {
    linear_to_page[linear] = num;
  }
  std::size_t total = 0;
  std::size_t ok = 0;
  for (const Fixup& fx : fixups) {
    if (fx.type != static_cast<std::uint8_t>(FixupSource::kOffset32)) {
      continue;
    }
    const std::uint32_t page_linear = fx.source & ~(kPageSize - 1);
    const auto page = linear_to_page.find(page_linear);
    if (page == linear_to_page.end()) {
      continue;
    }
    const std::size_t off = page_base + std::size_t{page->second - 1} * kPageSize + (fx.source - page_linear);
    if (off + 4 > data.size()) {
      continue;
    }
    ++total;
    ok += read_u32le(data, off) == fx.target - le.objects[fx.target_object].base_address ? 1 : 0;
  }
  return total == 0 ? 0.0 : static_cast<double>(ok) / static_cast<double>(total);
}

}  // namespace

ExeImage::ExeImage(std::span<const std::uint8_t> exe) {
  const LeHeader le = parse_le(exe);
  objects_ = le.objects;
  const auto lo = std::ranges::min_element(objects_, {}, &LeObject::base_address)->base_address;
  std::uint32_t hi = 0;
  for (const LeObject& obj : objects_) {
    hi = std::max(hi, obj.base_address + obj.virtual_size);
  }
  if (lo != kExpectedBase) {
    throw DataError(std::format("SH.EXE: image based at {:#x}, expected {:#x}", lo, kExpectedBase));
  }
  base_ = lo;
  image_.assign(hi - lo, 0);

  // Pages are stored back to back from the stub-relative page data offset.
  const std::size_t page_base = le.exe_offset + le.data_pages_offset;
  for (const LeObject& obj : objects_) {
    for (std::uint32_t p = 0; p < obj.num_pages; ++p) {
      const std::size_t src = page_base + std::size_t{obj.first_page - 1 + p} * kPageSize;
      const std::size_t dst = obj.base_address - base_ + std::size_t{p} * kPageSize;
      if (src >= exe.size() || dst >= image_.size()) {
        continue;
      }
      const std::size_t n = std::min({std::size_t{kPageSize}, exe.size() - src, image_.size() - dst});
      std::memcpy(image_.data() + dst, exe.data() + src, n);
    }
  }

  const std::vector<Fixup> fixups = read_fixups(exe, le);
  fixup_consistency_ = measure_fixup_consistency(exe, le, fixups);
  if (fixup_consistency_ <= kMinFixupConsistency) {
    throw DataError(std::format("SH.EXE: fixup consistency {:.1f} %, the page data looks mislocated",
                                fixup_consistency_ * 100.0));
  }

  const auto put = [&](std::uint32_t linear, std::uint32_t value, std::size_t size) {
    if (linear < base_ || linear - base_ + size > image_.size()) {
      return;
    }
    for (std::size_t k = 0; k < size; ++k) {
      image_[linear - base_ + k] = static_cast<std::uint8_t>(value >> (8 * k));
    }
    ++fixups_applied_;
  };
#ifdef SH_TRACE_EXE_READS
  for (const Fixup& fx : fixups) {
    if (static_cast<FixupSource>(fx.type) == FixupSource::kOffset32) {
      pointer_sources_.push_back(fx.source);
    }
  }
  std::ranges::sort(pointer_sources_);
#endif
  for (const Fixup& fx : fixups) {
    switch (static_cast<FixupSource>(fx.type)) {
      case FixupSource::kOffset32:
      case FixupSource::kPointer16_32:
        put(fx.source, fx.target, 4);
        break;
      case FixupSource::kOffset16:
        put(fx.source, fx.target & 0xffff, 2);
        break;
      case FixupSource::kSelfRelative32:
        put(fx.source, fx.target - (fx.source + 4), 4);
        break;
      default:
        break;  // selector fixups: flat model, nothing to patch
    }
  }
}

ExeImage ExeImage::load(const Install& install) {
  const std::vector<std::uint8_t> exe = install.read("SH.EXE");
  return ExeImage(exe);
}

std::size_t ExeImage::offset_of(std::uint32_t address, std::size_t n) const {
  if (address < base_ || address - base_ > image_.size() || image_.size() - (address - base_) < n) {
    throw DataError(std::format("SH.EXE: {} bytes at {:#x} are outside the image", n, address));
  }
  return address - base_;
}

#ifdef SH_TRACE_EXE_READS
namespace {
ExeImage::ReadObserver g_read_observer = nullptr;
}  // namespace

void ExeImage::set_read_observer(ReadObserver observer) { g_read_observer = observer; }

#define SH_EXE_READ(address, size, kind)                     \
  do {                                                       \
    if (g_read_observer != nullptr) {                        \
      g_read_observer(*this, (address), (size), (kind));     \
    }                                                        \
  } while (false)
#else
#define SH_EXE_READ(address, size, kind) \
  do {                                   \
  } while (false)
#endif

std::uint8_t ExeImage::u8(std::uint32_t address) const {
  SH_EXE_READ(address, 1, ExeRead::kU8);
  return image_[offset_of(address, 1)];
}

std::uint16_t ExeImage::u16(std::uint32_t address) const {
  SH_EXE_READ(address, 2, ExeRead::kU16);
  return read_u16le(image_, offset_of(address, 2));
}

std::uint32_t ExeImage::u32(std::uint32_t address) const {
  SH_EXE_READ(address, 4, ExeRead::kU32);
  return read_u32le(image_, offset_of(address, 4));
}

std::span<const std::uint8_t> ExeImage::bytes(std::uint32_t address, std::size_t n) const {
  SH_EXE_READ(address, n, ExeRead::kBytes);
  return std::span<const std::uint8_t>(image_).subspan(offset_of(address, n), n);
}

std::string_view ExeImage::c_string(std::uint32_t address) const {
  const std::size_t start = offset_of(address, 1);
  const auto nul = std::find(image_.begin() + static_cast<std::ptrdiff_t>(start), image_.end(), std::uint8_t{0});
  if (nul == image_.end()) {
    throw DataError(std::format("SH.EXE: unterminated string at {:#x}", address));
  }
  const std::size_t length = static_cast<std::size_t>(nul - image_.begin()) - start;
  SH_EXE_READ(address, length + 1, ExeRead::kString);
  return std::string_view(reinterpret_cast<const char*>(image_.data() + start), length);
}

std::string_view ExeImage::string_pointer(std::uint32_t pointer_address) const {
  SH_EXE_READ(pointer_address, 4, ExeRead::kStringPointer);
  const std::uint32_t pointer = read_u32le(image_, offset_of(pointer_address, 4));
  return pointer == 0 ? std::string_view() : c_string(pointer);
}

#undef SH_EXE_READ

}  // namespace sh

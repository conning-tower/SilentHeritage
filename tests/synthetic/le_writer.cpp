#include "synthetic/le_writer.h"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "synthetic/encoders.h"
#include "synthetic/exe_rules.h"

namespace sh::test {

namespace {

constexpr std::uint32_t kPageSize = 4096;
constexpr std::size_t kStubSize = 0x40;
constexpr std::size_t kHeaderSize = 0xc4;  // the object table follows

}  // namespace

std::vector<std::uint8_t> write_le_executable(const ExeManifest& m) {
  if (m.objects.empty()) {
    throw std::runtime_error("exe manifest without objects");
  }
  const std::uint32_t base = std::ranges::min_element(m.objects, {}, &ExeManifest::Object::base)->base;
  std::uint32_t end = 0;
  for (const auto& o : m.objects) {
    end = std::max(end, o.base + o.size);
  }
  std::vector<std::uint8_t> image(end - base, 0);
  const auto put = [&](std::uint32_t address, std::uint32_t value, std::size_t size) {
    if (address < base || address - base + size > image.size()) {
      throw std::runtime_error("exe manifest entry outside the image");
    }
    for (std::size_t k = 0; k < size; ++k) {
      image[address - base + k] = static_cast<std::uint8_t>(value >> (8 * k));
    }
  };
  for (const auto& v : m.values) {
    put(v.address, v.value, v.size);
  }
  for (const auto& s : m.strings) {
    for (std::size_t i = 0; i < s.text.size(); ++i) {
      put(s.address + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(s.text[i]), 1);
    }
    put(s.address + static_cast<std::uint32_t>(s.text.size()), 0, 1);
  }
  for (const auto& r : m.rules) {
    if (r.address < base || r.address - base + r.size > image.size()) {
      throw std::runtime_error("exe rule outside the image");
    }
    fill_exe_rule(r.name, std::span(image).subspan(r.address - base, r.size));
  }
  // The pointers hold their target's object offset on disk; the fixups
  // add the object's base.
  const auto object_of = [&m](std::uint32_t address) -> std::size_t {
    for (std::size_t i = 0; i < m.objects.size(); ++i) {
      if (address >= m.objects[i].base && address < m.objects[i].base + m.objects[i].size) {
        return i;
      }
    }
    throw std::runtime_error("exe pointer target outside the objects");
  };
  struct FixupRecord {
    std::uint32_t source;
    std::size_t object;
    std::uint32_t offset;
  };
  std::vector<FixupRecord> fixups;
  for (const auto& p : m.pointers) {
    const std::size_t object = object_of(p.target);
    const std::uint32_t offset = p.target - m.objects[object].base;
    put(p.address, offset, 4);
    fixups.push_back({p.address, object, offset});
  }

  // The pages, object by object.
  struct PageInfo {
    std::uint32_t linear;
  };
  std::vector<PageInfo> pages;
  std::vector<std::uint32_t> first_page(m.objects.size());
  std::vector<std::uint32_t> page_count(m.objects.size());
  for (std::size_t i = 0; i < m.objects.size(); ++i) {
    first_page[i] = static_cast<std::uint32_t>(pages.size() + 1);
    page_count[i] = (m.objects[i].size + kPageSize - 1) / kPageSize;
    for (std::uint32_t p = 0; p < page_count[i]; ++p) {
      pages.push_back({m.objects[i].base + p * kPageSize});
    }
  }
  // The fixup records of each page.
  std::vector<Bytes> records(pages.size());
  for (const FixupRecord& f : fixups) {
    for (std::size_t p = 0; p < pages.size(); ++p) {
      if (f.source >= pages[p].linear && f.source < pages[p].linear + kPageSize) {
        Bytes& r = records[p];
        r.u8(0x07);  // 32-bit offset
        r.u8(0x10);  // internal reference, 32-bit target offset
        r.u16(static_cast<std::uint16_t>(f.source - pages[p].linear));
        r.u8(static_cast<std::uint8_t>(f.object + 1));
        r.u32(f.offset);
        break;
      }
    }
  }

  Bytes le;
  le.zeros(kHeaderSize);
  const std::size_t object_table = le.size();
  for (std::size_t i = 0; i < m.objects.size(); ++i) {
    le.u32(m.objects[i].size);
    le.u32(m.objects[i].base);
    le.u32(m.objects[i].flags);
    le.u32(first_page[i]);
    le.u32(page_count[i]);
    le.u32(0);
  }
  const std::size_t page_table = le.size();
  for (std::size_t p = 0; p < pages.size(); ++p) {
    le.u32(static_cast<std::uint32_t>(p + 1) << 8);
  }
  const std::size_t fixup_page_table = le.size();
  std::uint32_t at = 0;
  for (std::size_t p = 0; p < pages.size(); ++p) {
    le.u32(at);
    at += static_cast<std::uint32_t>(records[p].size());
  }
  le.u32(at);
  const std::size_t fixup_records = le.size();
  for (Bytes& r : records) {
    le.bytes(r.data());
  }
  const std::size_t data_pages = kStubSize + le.size();
  for (const PageInfo& p : pages) {
    const std::size_t from = p.linear - base;
    const std::size_t n = std::min<std::size_t>(kPageSize, image.size() - from);
    le.bytes(std::span(image).subspan(from, n));
    le.zeros(kPageSize - n);
  }
  std::vector<std::uint8_t>& d = le.data();
  const auto set16 = [&d](std::size_t off, std::uint16_t v) {
    d[off] = static_cast<std::uint8_t>(v);
    d[off + 1] = static_cast<std::uint8_t>(v >> 8);
  };
  d[0] = 'L';
  d[1] = 'E';
  set16(0x08, 3);  // 386
  set16(0x0a, 1);  // OS/2-style flat
  le.patch_u32(0x14, static_cast<std::uint32_t>(pages.size()));
  le.patch_u32(0x28, kPageSize);
  le.patch_u32(0x40, static_cast<std::uint32_t>(object_table));
  le.patch_u32(0x44, static_cast<std::uint32_t>(m.objects.size()));
  le.patch_u32(0x48, static_cast<std::uint32_t>(page_table));
  le.patch_u32(0x68, static_cast<std::uint32_t>(fixup_page_table));
  le.patch_u32(0x6c, static_cast<std::uint32_t>(fixup_records));
  le.patch_u32(0x80, static_cast<std::uint32_t>(data_pages));

  Bytes out;
  out.u8('M');
  out.u8('Z');
  out.zeros(kStubSize - 2);
  out.patch_u32(0x3c, static_cast<std::uint32_t>(kStubSize));
  out.bytes(le.data());
  return out.take();
}

}  // namespace sh::test

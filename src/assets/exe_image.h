// The relocated memory image of the original SH.EXE, for the data the game
// keeps in its own executable (crew messages, text, constant tables).
//
// SH.EXE is a DOS/4GW extender (MZ) followed by the bound program: a second MZ
// stub whose `e_lfanew` points at a Watcom LE (Linear Executable) module. The
// image is built like the DOS/4GW loader does: each LE object placed at its
// linear base address (code at 0x10000, data at 0xf0000), BSS zero-filled, and
// the internal fixups applied, so pointers in the image hold the linear
// addresses used throughout `docs/` and the code comments.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/install.h"

namespace sh {

// One LE object-table entry (a loadable segment).
struct LeObject {
  std::uint32_t virtual_size = 0;
  std::uint32_t base_address = 0;  // linear address of the object
  std::uint32_t flags = 0;         // 0x1 read, 0x2 write, 0x4 exec, 0x2000 32-bit
  std::uint32_t first_page = 0;    // 1-based index into the page map
  std::uint32_t num_pages = 0;

  bool is_exec() const { return (flags & 0x4) != 0; }
};

// What an `ExeImage` read returned, for the read trace of the test builds.
enum class ExeRead : std::uint8_t { kU8, kU16, kU32, kBytes, kString, kStringPointer };

// The flat, relocated image of SH.EXE's LE module. Reads are by linear address
// and bounds-checked (`DataError` outside the image).
class ExeImage {
 public:
  // Linear address of the first object of the original SH.EXE.
  static constexpr std::uint32_t kExpectedBase = 0x10000;

  // Builds the image from the bytes of a DOS/4GW-bound executable. Throws
  // `DataError` if no LE module is found, or if it does not look like the
  // original SH.EXE (objects not based at `kExpectedBase`, or fewer than 90 %
  // of the 32-bit fixup sources holding their target's object offset on disk,
  // the sign of a mislocated page area).
  explicit ExeImage(std::span<const std::uint8_t> exe);

  // Loads SH.EXE from `install`.
  static ExeImage load(const Install& install);

  std::uint32_t base() const { return base_; }
  // Returns one past the last linear address of the image.
  std::uint32_t end() const { return base_ + static_cast<std::uint32_t>(image_.size()); }
  const std::vector<LeObject>& objects() const { return objects_; }
  std::size_t fixups_applied() const { return fixups_applied_; }
  // Returns the fraction of 32-bit offset fixups whose on-disk source dword
  // already held the target's object-relative offset (1.0 on SH.EXE).
  double fixup_consistency() const { return fixup_consistency_; }

  std::uint8_t u8(std::uint32_t address) const;
  std::uint16_t u16(std::uint32_t address) const;
  std::uint32_t u32(std::uint32_t address) const;
  std::int32_t i32(std::uint32_t address) const { return static_cast<std::int32_t>(u32(address)); }
  // Returns `n` bytes starting at `address`.
  std::span<const std::uint8_t> bytes(std::uint32_t address, std::size_t n) const;

  // Returns the NUL-terminated string at `address` (without the NUL). Throws
  // `DataError` if it runs off the image.
  std::string_view c_string(std::uint32_t address) const;
  // Returns the string a `char*` stored at `pointer_address` points to, or an
  // empty string for a NULL pointer.
  std::string_view string_pointer(std::uint32_t pointer_address) const;

#ifdef SH_TRACE_EXE_READS
  // Not in the original. Called on every read of any image, with the image,
  // the address, the size read (the string's length with its NUL) and the
  // kind of read: the test builds' trace of the addresses the code reads
  // (CMake option SH_TRACE_EXE_READS; nothing in other builds).
  using ReadObserver = void (*)(const ExeImage& image, std::uint32_t address, std::size_t size, ExeRead kind);
  static void set_read_observer(ReadObserver observer);
  // Returns the linear addresses holding a 32-bit pointer (the sources of
  // the 32-bit offset fixups), sorted.
  const std::vector<std::uint32_t>& pointer_sources() const { return pointer_sources_; }
#endif

 private:
  std::size_t offset_of(std::uint32_t address, std::size_t n) const;

  std::vector<std::uint8_t> image_;
  std::vector<LeObject> objects_;
  std::uint32_t base_ = 0;
  std::size_t fixups_applied_ = 0;
  double fixup_consistency_ = 0.0;
#ifdef SH_TRACE_EXE_READS
  std::vector<std::uint32_t> pointer_sources_;
#endif
};

}  // namespace sh

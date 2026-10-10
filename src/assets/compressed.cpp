#include "assets/compressed.h"

#include <algorithm>
#include <array>

#include "core/error.h"

namespace sh {
namespace {

constexpr std::uint16_t kRoot = 0xffff;
constexpr std::size_t kTableSize = 0x800;  // 2**11: the game's fixed dictionary
constexpr std::uint32_t kFirstFree = 0x100;
constexpr std::uint32_t kStartBits = 9;

// Reads LZW strings with the game's prefix/suffix tables, including the stale
// suffixes kept across a dictionary reset (a string dictionary would differ).
class LzwReader {
 public:
  explicit LzwReader(std::span<const std::uint8_t> data) : data_(data) {
    if (data.size() < 2) {
      throw DataError("compressed block: missing LZW header");
    }
    max_bits_ = std::min<std::uint32_t>(data[0], 11);
    // The header word's high byte is the first 8 bits of the code stream.
    bitbuf_ = data[1];
    avail_ = 8;
    pos_ = 2;
    prefix_.fill(kRoot);
    for (std::size_t i = 0; i < kTableSize; ++i) {
      suffix_[i] = static_cast<std::uint8_t>(i & 0xff);
    }
  }

  std::size_t pos() const { return pos_; }

  // Appends the string of the next code to `out` (in order).
  void next_string(std::vector<std::uint8_t>& out) {
    while (avail_ < bits_) {
      if (pos_ + 2 > data_.size()) {
        throw DataError("compressed block: stream ends before the block does");
      }
      bitbuf_ |= static_cast<std::uint64_t>(data_[pos_] | (data_[pos_ + 1] << 8)) << avail_;
      pos_ += 2;
      avail_ += 16;
    }
    std::uint32_t code = static_cast<std::uint32_t>(bitbuf_ & mask_);
    bitbuf_ >>= bits_;
    avail_ -= bits_;

    const std::size_t start = out.size();
    std::uint32_t cur = code;
    if (code >= next_) {  // the KwKwK case: the code being defined right now
      out.push_back(finchar_);
      cur = prev_;
      code = next_;
    }
    std::size_t steps = 0;
    while (prefix_[cur] != kRoot) {
      if (++steps > kTableSize) {
        throw DataError("compressed block: cyclic LZW dictionary");
      }
      out.push_back(suffix_[cur]);
      cur = prefix_[cur];
    }
    finchar_ = suffix_[cur];
    out.push_back(finchar_);
    std::reverse(out.begin() + static_cast<std::ptrdiff_t>(start), out.end());

    suffix_[next_] = finchar_;
    prefix_[next_] = static_cast<std::uint16_t>(prev_);
    ++next_;
    if (next_ > mask_) {
      ++bits_;
      mask_ = mask_ << 1 | 1;
      if (bits_ > max_bits_) {  // silent reset, no clear code
        bits_ = kStartBits;
        mask_ = 0x1ff;
        next_ = kFirstFree;
        prefix_.fill(kRoot);
      }
    }
    prev_ = code;
  }

 private:
  std::span<const std::uint8_t> data_;
  std::uint32_t max_bits_ = 0;
  std::uint64_t bitbuf_ = 0;
  std::uint32_t avail_ = 0;
  std::size_t pos_ = 0;
  std::uint32_t bits_ = kStartBits;
  std::uint32_t mask_ = 0x1ff;
  std::uint32_t next_ = kFirstFree;
  std::array<std::uint16_t, kTableSize> prefix_{};
  std::array<std::uint8_t, kTableSize> suffix_{};
  std::uint32_t prev_ = 0;
  std::uint8_t finchar_ = 0;
};

}  // namespace

DecodedStream lzw_rle90_decode(std::span<const std::uint8_t> stream, std::size_t size) {
  LzwReader reader(stream);
  DecodedStream result;
  // No reserve: `size` comes from the file and only the stream bounds the
  // output.
  auto& out = result.bytes;
  std::vector<std::uint8_t> chunk;
  std::uint8_t last = 0;
  bool pending_mark = false;
  while (out.size() < size) {
    chunk.clear();
    reader.next_string(chunk);
    for (const std::uint8_t c : chunk) {
      if (pending_mark) {
        pending_mark = false;
        if (c == 0) {
          last = kRle90Mark;
          out.push_back(kRle90Mark);
        } else {
          out.insert(out.end(), c - 1u, last);
        }
      } else if (c == kRle90Mark) {
        pending_mark = true;
      } else {
        last = c;
        out.push_back(c);
      }
    }
  }
  out.resize(size);
  result.consumed = reader.pos();
  return result;
}

std::vector<std::uint8_t> read_compressed(ByteReader& reader) {
  const std::uint32_t size = reader.u32();
  if (size == 0) {
    return {};
  }
  const std::size_t start = reader.pos();
  const auto rest = reader.bytes(reader.remaining());
  DecodedStream decoded = [&] {
    try {
      return lzw_rle90_decode(rest, size);
    } catch (const DataError& e) {
      reader.seek(start);
      reader.fail(e.what());
    }
  }();
  reader.seek(start + decoded.consumed);
  return std::move(decoded.bytes);
}

std::vector<std::uint8_t> rle90_decode(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out;
  for (std::size_t i = 0; i < data.size();) {
    const std::uint8_t c = data[i++];
    if (c != kRle90Mark) {
      out.push_back(c);
      continue;
    }
    if (i == data.size()) {
      throw DataError("RLE90: escape byte at the end of the data");
    }
    const std::uint8_t n = data[i++];
    if (n == 0) {
      out.push_back(kRle90Mark);
    } else if (out.empty()) {
      throw DataError("RLE90: run with no byte to repeat");
    } else {
      const std::uint8_t previous = out.back();
      out.insert(out.end(), n - 1u, previous);
    }
  }
  return out;
}

}  // namespace sh

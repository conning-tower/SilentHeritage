#include "core/arj.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "core/byte_reader.h"
#include "core/crc32.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr std::uint8_t kId0 = 0x60;
constexpr std::uint8_t kId1 = 0xea;
constexpr std::size_t kMaxHeader = 2600;
// How far into a file the first header is looked for (past a
// self-extractor's code).
constexpr std::uint64_t kMaxPrefix = 256 * 1024;

constexpr std::uint8_t kFlagGarbled = 0x01;
constexpr std::uint8_t kFlagVolume = 0x04;
constexpr std::uint8_t kFlagExtFile = 0x08;

constexpr std::uint8_t kTypeMain = 2;
constexpr std::uint8_t kTypeDirectory = 3;
constexpr std::uint8_t kTypeVolumeLabel = 4;

// The bit stream of the decoders, most significant bit first, as ARJ's own
// `fillbuf`: `bits` holds the next 16 bits; past the data, zeros.
class BitReader {
 public:
  explicit BitReader(std::span<const std::uint8_t> data) : data_(data) { fill(16); }

  std::uint16_t peek() const { return bits_; }

  // Drops `n` (at most 16) bits.
  void fill(int n) {
    bits_ = static_cast<std::uint16_t>(bits_ << n);
    while (n > count_) {
      n -= count_;
      bits_ = static_cast<std::uint16_t>(bits_ | (sub_ << n));
      sub_ = pos_ < data_.size() ? data_[pos_] : 0;
      ++pos_;
      count_ = 8;
    }
    count_ -= n;
    bits_ = static_cast<std::uint16_t>(bits_ | (sub_ >> count_));
  }

  // Returns the next `n` (1 to 16) bits.
  std::uint16_t get(int n) {
    const auto x = static_cast<std::uint16_t>(bits_ >> (16 - n));
    fill(n);
    return x;
  }

  // Returns whether more bits were consumed than the data holds.
  bool overrun() const { return pos_ > data_.size() + 2; }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
  std::uint16_t bits_ = 0;
  std::uint16_t sub_ = 0;
  int count_ = 0;
};

// The decoder of methods 1 to 3.
class HuffmanDecoder {
 public:
  HuffmanDecoder(std::span<const std::uint8_t> packed, const std::string& context) : in_(packed), context_(context) {}

  void decode(std::vector<std::uint8_t>& out) {
    while (out.size() < out.capacity()) {
      const std::uint16_t c = decode_c();
      if (c < 256) {
        out.push_back(static_cast<std::uint8_t>(c));
        continue;
      }
      const std::size_t length = c - (256 - kThreshold);
      const std::size_t distance = decode_p() + 1u;
      copy_match(out, distance, length);
      if (in_.overrun()) {
        fail();
      }
    }
  }

 private:
  static constexpr int kThreshold = 3;
  static constexpr int kNc = 255 + 256 + 2 - kThreshold;  // literals and lengths
  static constexpr int kNp = 16 + 1;                      // distance bit counts
  static constexpr int kNt = 16 + 3;                      // code length codes
  static constexpr int kCBit = 9;
  static constexpr int kPBit = 5;
  static constexpr int kTBit = 5;
  static constexpr int kNpt = kNt;

  [[noreturn]] void fail() const { throw DataError(std::format("{}: corrupt compressed data", context_)); }

  void copy_match(std::vector<std::uint8_t>& out, std::size_t distance, std::size_t length) const {
    if (distance > out.size() || length > out.capacity() - out.size()) {
      fail();
    }
    for (std::size_t k = 0; k < length; ++k) {
      out.push_back(out[out.size() - distance]);
    }
  }

  // Builds the lookup `table` (`table_bits` wide) of the canonical code of
  // `lengths`, longer codes continuing in `left_` / `right_`.
  void make_table(std::span<const std::uint8_t> lengths, int table_bits, std::span<std::uint16_t> table) {
    std::array<std::uint32_t, 17> count{};
    std::array<std::uint32_t, 18> start{};
    std::array<std::uint32_t, 17> weight{};
    for (const std::uint8_t len : lengths) {
      if (len > 16) {
        fail();
      }
      ++count[len];
    }
    for (int i = 1; i <= 16; ++i) {
      start[i + 1] = start[i] + (count[i] << (16 - i));
    }
    if (start[17] != (1u << 16)) {
      fail();
    }
    const int jut = 16 - table_bits;
    for (int i = 1; i <= table_bits; ++i) {
      start[i] >>= jut;
      weight[i] = 1u << (table_bits - i);
    }
    for (int i = table_bits + 1; i <= 16; ++i) {
      weight[i] = 1u << (16 - i);
    }
    for (std::uint32_t i = start[table_bits + 1] >> jut; i < (1u << table_bits); ++i) {
      table[i] = 0;
    }
    auto avail = static_cast<std::uint16_t>(lengths.size());
    const std::uint32_t mask = 1u << (15 - table_bits);
    for (std::size_t ch = 0; ch < lengths.size(); ++ch) {
      const int len = lengths[ch];
      if (len == 0) {
        continue;
      }
      std::uint32_t k = start[len];
      const std::uint32_t next = k + weight[len];
      if (len <= table_bits) {
        if (next > table.size()) {
          fail();
        }
        std::fill(table.begin() + k, table.begin() + next, static_cast<std::uint16_t>(ch));
      } else {
        std::uint16_t* p = &table[k >> jut];
        for (int i = len - table_bits; i != 0; --i) {
          if (*p == 0) {
            if (avail >= left_.size()) {
              fail();
            }
            left_[avail] = right_[avail] = 0;
            *p = avail++;
          }
          p = (k & mask) != 0 ? &right_[*p] : &left_[*p];
          k <<= 1;
        }
        *p = static_cast<std::uint16_t>(ch);
      }
      start[len] = next;
    }
  }

  // Follows the tree below table entry `j` (bits from `mask` down) to a
  // symbol below `n`.
  std::uint16_t walk(std::uint16_t j, int n, std::uint16_t mask) {
    while (j >= n) {
      if (j >= left_.size() || mask == 0) {
        fail();
      }
      j = (in_.peek() & mask) != 0 ? right_[j] : left_[j];
      mask = static_cast<std::uint16_t>(mask >> 1);
    }
    return j;
  }

  void read_pt_len(int nn, int nbit, int special) {
    const int n = in_.get(nbit);
    if (n == 0) {
      const std::uint16_t c = in_.get(nbit);
      std::fill_n(pt_len_.begin(), nn, 0);
      pt_table_.fill(c);
      return;
    }
    if (n > nn) {
      fail();
    }
    int i = 0;
    while (i < n) {
      int c = in_.peek() >> 13;
      if (c == 7) {
        for (std::uint16_t mask = 1 << 12; (in_.peek() & mask) != 0; mask = static_cast<std::uint16_t>(mask >> 1)) {
          ++c;
        }
        if (c > 16) {
          fail();
        }
      }
      in_.fill(c < 7 ? 3 : c - 3);
      pt_len_[i++] = static_cast<std::uint8_t>(c);
      if (i == special) {
        for (int z = in_.get(2); z > 0 && i < nn; --z) {
          pt_len_[i++] = 0;
        }
      }
    }
    std::fill(pt_len_.begin() + i, pt_len_.begin() + nn, 0);
    make_table(std::span(pt_len_).first(nn), 8, pt_table_);
  }

  void read_c_len() {
    const int n = in_.get(kCBit);
    if (n == 0) {
      const std::uint16_t c = in_.get(kCBit);
      c_len_.fill(0);
      c_table_.fill(c);
      return;
    }
    if (n > kNc) {
      fail();
    }
    int i = 0;
    while (i < n) {
      const std::uint16_t c = walk(pt_table_[in_.peek() >> 8], kNt, 1 << 7);
      in_.fill(pt_len_[c]);
      if (c <= 2) {
        const int zeros = c == 0 ? 1 : c == 1 ? in_.get(4) + 3 : in_.get(kCBit) + 20;
        if (zeros > n - i) {
          fail();
        }
        std::fill_n(c_len_.begin() + i, zeros, 0);
        i += zeros;
      } else {
        c_len_[i++] = static_cast<std::uint8_t>(c - 2);
      }
    }
    std::fill(c_len_.begin() + i, c_len_.end(), 0);
    make_table(c_len_, 12, c_table_);
  }

  std::uint16_t decode_c() {
    if (block_left_ == 0) {
      block_left_ = in_.get(16);
      read_pt_len(kNt, kTBit, 3);
      read_c_len();
      read_pt_len(kNp, kPBit, -1);
      if (block_left_ == 0 || in_.overrun()) {
        fail();
      }
    }
    --block_left_;
    const std::uint16_t j = walk(c_table_[in_.peek() >> 4], kNc, 1 << 3);
    in_.fill(c_len_[j]);
    return j;
  }

  std::uint16_t decode_p() {
    std::uint16_t j = walk(pt_table_[in_.peek() >> 8], kNp, 1 << 7);
    in_.fill(pt_len_[j]);
    if (j != 0) {
      --j;
      j = static_cast<std::uint16_t>((1u << j) + (j == 0 ? 0 : in_.get(j)));
    }
    return j;
  }

  BitReader in_;
  const std::string& context_;
  std::uint32_t block_left_ = 0;
  std::array<std::uint8_t, kNc> c_len_{};
  std::array<std::uint8_t, kNpt> pt_len_{};
  std::array<std::uint16_t, 4096> c_table_{};
  std::array<std::uint16_t, 256> pt_table_{};
  std::array<std::uint16_t, 2 * kNc - 1> left_{};
  std::array<std::uint16_t, 2 * kNc - 1> right_{};
};

// Method 4: a length (0 for a literal) and a distance in a unary-prefixed
// code each.
void decode_fastest(std::span<const std::uint8_t> packed, std::vector<std::uint8_t>& out,
                    const std::string& context) {
  BitReader in(packed);
  // A unary count of `width`s from `first` (at most `last`), then `width`
  // bits, added to the values the shorter widths cover.
  const auto read_code = [&in](int first, int last) {
    int plus = 0;
    int width = first;
    for (; width < last; ++width) {
      if (in.get(1) == 0) {
        break;
      }
      plus += 1 << width;
    }
    return plus + (width != 0 ? in.get(width) : 0);
  };
  while (out.size() < out.capacity()) {
    const int c = read_code(0, 7);
    if (c == 0) {
      out.push_back(static_cast<std::uint8_t>(in.get(8)));
      continue;
    }
    const std::size_t length = static_cast<std::size_t>(c) + 2;
    const std::size_t distance = static_cast<std::size_t>(read_code(9, 13)) + 1;
    if (distance > out.size() || length > out.capacity() - out.size() || in.overrun()) {
      throw DataError(std::format("{}: corrupt compressed data", context));
    }
    for (std::size_t k = 0; k < length; ++k) {
      out.push_back(out[out.size() - distance]);
    }
  }
}

// Returns the offset of the main header (the first header whose CRC holds)
// within the first `kMaxPrefix` bytes.
std::uint64_t find_main_header(const ByteSource& source) {
  const std::uint64_t limit = std::min<std::uint64_t>(source.size(), kMaxPrefix);
  const std::vector<std::uint8_t> head = source.read_bytes(0, static_cast<std::size_t>(limit));
  for (std::size_t i = 0; i + 4 <= head.size(); ++i) {
    if (head[i] != kId0 || head[i + 1] != kId1) {
      continue;
    }
    const std::size_t size = read_u16le(head, i + 2);
    if (size == 0 || size > kMaxHeader || i + 4 + size + 4 > head.size()) {
      continue;
    }
    const std::span<const std::uint8_t> header = std::span(head).subspan(i + 4, size);
    if (crc32(header) == read_u32le(head, i + 4 + size) && header[6] == kTypeMain) {
      return i;
    }
  }
  throw DataError(std::format("{} is not an ARJ archive", source.describe()));
}

}  // namespace

std::vector<std::uint8_t> arj_decompress(std::span<const std::uint8_t> packed, std::uint8_t method,
                                         std::size_t original_size, const std::string& context) {
  std::vector<std::uint8_t> out;
  out.reserve(original_size);
  switch (method) {
    case 0:
      if (packed.size() < original_size) {
        throw DataError(std::format("{}: truncated stored data", context));
      }
      out.assign(packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(original_size));
      break;
    case 1:
    case 2:
    case 3:
      HuffmanDecoder(packed, context).decode(out);
      break;
    case 4:
      decode_fastest(packed, out, context);
      break;
    default:
      throw DataError(std::format("{}: unknown ARJ method {}", context, method));
  }
  return out;
}

ArjArchive::ArjArchive(std::shared_ptr<const ByteSource> source) : source_(std::move(source)) {
  std::uint64_t pos = find_main_header(*source_);
  bool main = true;
  for (;;) {
    const std::vector<std::uint8_t> id = source_->read_bytes(pos, 4);
    if (id[0] != kId0 || id[1] != kId1) {
      throw DataError(std::format("{}: no ARJ header at {}", source_->describe(), pos));
    }
    const std::size_t size = read_u16le(id, 2);
    pos += 4;
    if (size == 0) {
      break;  // the end of the archive
    }
    if (size < 30 || size > kMaxHeader) {
      throw DataError(std::format("{}: bad ARJ header at {}", source_->describe(), pos - 4));
    }
    const std::vector<std::uint8_t> header = source_->read_bytes(pos, size + 4);
    const std::span<const std::uint8_t> basic = std::span(header).first(size);
    if (crc32(basic) != read_u32le(header, size)) {
      throw DataError(std::format("{}: ARJ header CRC mismatch at {}", source_->describe(), pos - 4));
    }
    pos += size + 4;
    // Extended headers: skipped.
    for (;;) {
      const std::size_t extended = read_u16le(source_->read_bytes(pos, 2), 0);
      pos += 2;
      if (extended == 0) {
        break;
      }
      pos += extended + 4;
    }
    ByteReader r(basic, source_->describe());
    const std::uint8_t first_size = r.u8();
    r.skip(3);  // archiver version, minimum version, host OS
    const std::uint8_t flags = r.u8();
    ArjEntry e;
    e.method = r.u8();
    const std::uint8_t type = r.u8();
    r.skip(1 + 4);  // reserved, date and time
    e.packed_size = r.u32();
    e.original_size = r.u32();
    e.crc = r.u32();
    if (first_size >= 34 && size >= 34) {
      r.seek(30);
      e.continued_at = r.u32();
    }
    if (first_size > size) {
      throw DataError(std::format("{}: bad ARJ header at {}", source_->describe(), pos));
    }
    r.seek(first_size);
    while (!r.at_end()) {
      const char c = static_cast<char>(r.u8());
      if (c == '\0') {
        break;
      }
      e.path += c == '\\' ? '/' : c;
    }
    if (main) {
      main = false;  // its sizes are dates
      continue;
    }
    e.data_offset = pos;
    pos += e.packed_size;
    if (type == kTypeVolumeLabel) {
      continue;
    }
    e.directory = type == kTypeDirectory;
    e.garbled = (flags & kFlagGarbled) != 0;
    e.continues = (flags & kFlagVolume) != 0;
    e.continuation = (flags & kFlagExtFile) != 0;
    if (pos > source_->size()) {
      throw DataError(std::format("{}: {} is truncated", source_->describe(), e.path));
    }
    entries_.push_back(std::move(e));
  }
}

std::vector<std::uint8_t> ArjArchive::extract(const ArjEntry& entry) const {
  const std::string context = std::format("{} (in {})", entry.path, source_->describe());
  if (entry.directory) {
    return {};
  }
  if (entry.garbled) {
    throw DataError(std::format("{}: password-protected", context));
  }
  const std::vector<std::uint8_t> packed = source_->read_bytes(entry.data_offset, entry.packed_size);
  std::vector<std::uint8_t> data = arj_decompress(packed, entry.method, entry.original_size, context);
  if (crc32(data) != entry.crc) {
    throw DataError(std::format("{}: CRC mismatch", context));
  }
  return data;
}

}  // namespace sh

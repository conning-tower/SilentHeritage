#include "assets/smacker.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <utility>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"

namespace sh {

using smacker_detail::BigTree;

namespace {

constexpr std::size_t kHeaderSize = 104;
constexpr std::size_t kMaxTracks = 7;
constexpr int kMaxDimension = 4096;

// Header flags.
constexpr std::uint32_t kFlagRingFrame = 0x01;
constexpr std::uint32_t kFlagYInterlace = 0x02;
constexpr std::uint32_t kFlagYDouble = 0x04;

// Audio rate field bits.
constexpr std::uint32_t kAudioCompressed = 0x80000000;
constexpr std::uint32_t kAudioPresent = 0x40000000;
constexpr std::uint32_t kAudio16Bit = 0x20000000;
constexpr std::uint32_t kAudioStereo = 0x10000000;
constexpr std::uint32_t kAudioBink = 0x0c000000;
constexpr std::uint32_t kAudioRateMask = 0x00ffffff;

// Frame type bits: palette chunk, then one audio chunk per track (bit 1 + track).
constexpr std::uint8_t kFramePalette = 0x01;

// Flattened trees: a node entry holds kNode | the entry count of its "0"
// subtree, which follows it; its "1" subtree comes right after that one.
constexpr std::uint32_t kNode = 0x80000000;
constexpr int kMaxTreeDepth = 64;

enum class BlockType : std::uint8_t { kTwoColor = 0, kFull = 1, kSkip = 2, kSolid = 3 };

// Run lengths of the block type codes (bits 2..7).
constexpr std::array<std::uint32_t, 64> kBlockRuns = [] {
  std::array<std::uint32_t, 64> runs{};
  for (std::uint32_t i = 0; i < 59; ++i) {
    runs[i] = i + 1;
  }
  runs[59] = 128;
  runs[60] = 256;
  runs[61] = 512;
  runs[62] = 1024;
  runs[63] = 2048;
  return runs;
}();

// Reads bits least significant first, as the Smacker bit streams store them.
class BitReader {
 public:
  BitReader(std::span<const std::uint8_t> data, const std::string& context) : data_(data), context_(context) {}

  bool bit() {
    if (pos_ >= data_.size() * 8) {
      throw DataError(context_ + ": bit stream truncated");
    }
    const bool b = ((data_[pos_ >> 3] >> (pos_ & 7)) & 1) != 0;
    ++pos_;
    return b;
  }

  std::uint32_t bits(int n) {
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      v |= static_cast<std::uint32_t>(bit()) << i;
    }
    return v;
  }

  const std::string& context() const { return context_; }

 private:
  std::span<const std::uint8_t> data_;
  const std::string& context_;
  std::size_t pos_ = 0;
};

// Returns the leaf value reached by following the stream's bits from the root.
std::uint32_t walk(const std::vector<std::uint32_t>& entries, BitReader& br) {
  std::size_t i = 0;
  while ((entries[i] & kNode) != 0) {
    if (br.bit()) {
      i += entries[i] & ~kNode;
    }
    ++i;
  }
  return entries[i];
}

// A Huffman tree of byte values; an absent tree decodes 0 without reading bits.
class ByteTree {
 public:
  // Reads the presence bit and, when set, the tree and its terminating bit.
  static ByteTree read(BitReader& br) {
    ByteTree tree;
    if (br.bit()) {
      tree.read_node(br, 0);
      br.bit();
    }
    return tree;
  }

  std::uint8_t decode(BitReader& br) const {
    return entries_.empty() ? 0 : static_cast<std::uint8_t>(walk(entries_, br));
  }

 private:
  // Appends the subtree at the stream position; returns its entry count.
  std::uint32_t read_node(BitReader& br, int depth) {
    if (depth > kMaxTreeDepth) {
      throw DataError(br.context() + ": Huffman tree too deep");
    }
    if (!br.bit()) {
      if (++leaves_ > 256) {
        throw DataError(br.context() + ": byte Huffman tree has more than 256 leaves");
      }
      entries_.push_back(br.bits(8));
      return 1;
    }
    const std::size_t at = entries_.size();
    entries_.push_back(0);
    const std::uint32_t zero = read_node(br, depth + 1);
    entries_[at] = kNode | zero;
    return 1 + zero + read_node(br, depth + 1);
  }

  std::vector<std::uint32_t> entries_;
  int leaves_ = 0;
};

// Reads one of the four header trees; `size` is its size field in the header.
BigTree read_big_tree(BitReader& br, std::uint32_t size) {
  BigTree tree;
  if (!br.bit()) {
    // Absent: a single leaf 0 plus a dummy cache leaf.
    tree.entries = {0, 0};
    tree.cache = {1, 1, 1};
    return tree;
  }
  const ByteTree low = ByteTree::read(br);
  const ByteTree high = ByteTree::read(br);
  std::array<std::uint32_t, 3> escapes{};
  for (std::uint32_t& e : escapes) {
    e = br.bits(16);
  }
  constexpr std::size_t kUnset = static_cast<std::size_t>(-1);
  tree.cache = {kUnset, kUnset, kUnset};
  const std::size_t max_entries = ((static_cast<std::size_t>(size) + 3) >> 2) + 4;

  // Leaves whose value is one of the escapes become the cache entries.
  auto read_node = [&](auto& self, int depth) -> std::uint32_t {
    if (depth > kMaxTreeDepth || tree.entries.size() + 3 >= max_entries) {
      throw DataError(br.context() + ": malformed Huffman tree");
    }
    if (!br.bit()) {
      std::uint32_t v = low.decode(br) | static_cast<std::uint32_t>(high.decode(br)) << 8;
      for (std::size_t k = 0; k < escapes.size(); ++k) {
        if (v == escapes[k]) {
          tree.cache[k] = tree.entries.size();
          v = 0;
          break;
        }
      }
      tree.entries.push_back(v);
      return 1;
    }
    const std::size_t at = tree.entries.size();
    tree.entries.push_back(0);
    const std::uint32_t zero = self(self, depth + 1);
    tree.entries[at] = kNode | zero;
    return 1 + zero + self(self, depth + 1);
  };
  read_node(read_node, 0);
  br.bit();
  for (std::size_t& c : tree.cache) {
    if (c == kUnset) {
      c = tree.entries.size();
      tree.entries.push_back(0);
    }
  }
  return tree;
}

void reset_cache(BigTree& tree) {
  for (const std::size_t c : tree.cache) {
    tree.entries[c] = 0;
  }
}

// Decodes a value and moves it to the front of the cache.
std::uint16_t decode(BigTree& tree, BitReader& br) {
  const std::uint32_t v = walk(tree.entries, br);
  auto& e = tree.entries;
  const auto& c = tree.cache;
  if (v != e[c[0]]) {
    e[c[2]] = e[c[1]];
    e[c[1]] = e[c[0]];
    e[c[0]] = v;
  }
  return static_cast<std::uint16_t>(v);
}

// Returns the PCM bytes of one frame's audio chunk.
std::vector<std::uint8_t> decode_audio(std::span<const std::uint8_t> chunk, const SmackerAudioFormat& format,
                                       const std::string& context) {
  if (!format.compressed) {
    return {chunk.begin(), chunk.end()};
  }
  ByteReader r(chunk, context);
  const std::uint32_t unpacked = r.u32();
  // Bounds the output (a stream of one-leaf trees reads no bits at all).
  if (unpacked > format.max_chunk_size) {
    r.fail(std::format("audio chunk of {} bytes, the header's largest is {}", unpacked, format.max_chunk_size));
  }
  BitReader br(chunk.subspan(4), context);
  if (!br.bit()) {
    return {};
  }
  const bool stereo = br.bit();
  const bool sixteen = br.bit();
  if (stereo != (format.channels == 2) || sixteen != (format.bits == 16)) {
    throw DataError(context + ": audio chunk format differs from the header");
  }
  const std::size_t channels = stereo ? 2 : 1;
  const std::size_t sample_size = sixteen ? 2 : 1;
  const std::size_t samples = unpacked / sample_size;
  if (samples < channels) {
    throw DataError(context + ": audio chunk too short");
  }
  std::array<ByteTree, 4> trees;
  for (std::size_t i = 0; i < (std::size_t{1} << (channels - 1 + sample_size - 1)); ++i) {
    trees[i] = ByteTree::read(br);
  }

  // Each channel starts from a raw sample (right channel first), then adds
  // decoded deltas with wrap-around.
  std::vector<std::uint8_t> out(samples * sample_size);
  const std::size_t odd_mask = channels - 1;
  if (sixteen) {
    std::array<std::uint16_t, 2> pred{};
    for (std::size_t c = channels; c-- > 0;) {
      const std::uint32_t hi = br.bits(8);
      pred[c] = static_cast<std::uint16_t>(hi << 8 | br.bits(8));
    }
    for (std::size_t i = 0; i < samples; ++i) {
      const std::size_t c = i & odd_mask;
      if (i >= channels) {
        const std::uint32_t lo = trees[2 * c].decode(br);
        pred[c] = static_cast<std::uint16_t>(pred[c] + (lo | static_cast<std::uint32_t>(trees[2 * c + 1].decode(br)) << 8));
      }
      out[2 * i] = static_cast<std::uint8_t>(pred[c]);
      out[2 * i + 1] = static_cast<std::uint8_t>(pred[c] >> 8);
    }
  } else {
    std::array<std::uint8_t, 2> pred{};
    for (std::size_t c = channels; c-- > 0;) {
      pred[c] = static_cast<std::uint8_t>(br.bits(8));
    }
    for (std::size_t i = 0; i < samples; ++i) {
      const std::size_t c = i & odd_mask;
      if (i >= channels) {
        pred[c] = static_cast<std::uint8_t>(pred[c] + trees[c].decode(br));
      }
      out[i] = pred[c];
    }
  }
  return out;
}

}  // namespace

SmackerVideo::SmackerVideo(std::vector<std::uint8_t> data, std::string_view name)
    : data_(std::move(data)), name_(name) {
  ByteReader r(data_, name_);
  const auto signature = r.bytes(4);
  if (std::memcmp(signature.data(), "SMK2", 4) != 0) {
    r.fail(std::memcmp(signature.data(), "SMK4", 4) == 0 ? "Smacker 4 videos are not supported" : "not a Smacker file");
  }
  const std::uint32_t width = r.u32();
  const std::uint32_t height = r.u32();
  frame_count_ = r.u32();
  const std::int32_t rate = r.i32();
  const std::uint32_t flags = r.u32();
  if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension || frame_count_ == 0 ||
      frame_count_ > 0x100000) {
    r.fail("bad video size or frame count");
  }
  width_ = static_cast<int>(width);
  stored_height_ = static_cast<int>(height);
  // Positive: milliseconds; negative: tens of microseconds; zero: 10 fps.
  if (rate > 0) {
    frame_duration_us_ = static_cast<std::uint32_t>(rate) * 1000;
  } else if (rate < 0) {
    frame_duration_us_ = static_cast<std::uint32_t>(-static_cast<std::int64_t>(rate)) * 10;
  } else {
    frame_duration_us_ = 100000;
  }
  if ((flags & kFlagYDouble) != 0) {
    y_scale_ = SmackerYScale::kDoubled;
  } else if ((flags & kFlagYInterlace) != 0) {
    y_scale_ = SmackerYScale::kInterlaced;
  }
  std::array<std::uint32_t, kMaxTracks> max_chunk_sizes{};
  for (std::uint32_t& size : max_chunk_sizes) {
    size = r.u32();
  }
  const std::uint32_t trees_size = r.u32();
  const std::uint32_t mmap_size = r.u32();
  const std::uint32_t mclr_size = r.u32();
  const std::uint32_t full_size = r.u32();
  const std::uint32_t type_size = r.u32();
  for (std::uint8_t track = 0; track < kMaxTracks; ++track) {
    const std::uint32_t field = r.u32();
    if ((field & kAudioPresent) == 0) {
      continue;
    }
    if ((field & kAudioBink) != 0) {
      r.fail("Bink audio is not supported");
    }
    audio_tracks_.push_back({
        .track = track,
        .rate = field & kAudioRateMask,
        .channels = static_cast<std::uint8_t>((field & kAudioStereo) != 0 ? 2 : 1),
        .bits = static_cast<std::uint8_t>((field & kAudio16Bit) != 0 ? 16 : 8),
        .compressed = (field & kAudioCompressed) != 0,
        .max_chunk_size = max_chunk_sizes[track],
    });
  }
  r.skip(4);
  if (r.pos() != kHeaderSize) {
    r.fail("bad header size");
  }

  // The ring frame (a copy of the first transition, for looping) has table
  // entries but is never decoded.
  const std::uint32_t table_frames = frame_count_ + ((flags & kFlagRingFrame) != 0 ? 1 : 0);
  std::vector<std::uint32_t> sizes(table_frames);
  for (std::uint32_t& s : sizes) {
    s = r.u32() & ~std::uint32_t{3};  // Bit 0 marks key frames.
  }
  frame_types_.resize(table_frames);
  for (std::uint8_t& t : frame_types_) {
    t = r.u8();
  }
  BitReader br(r.bytes(trees_size), name_);
  mmap_tree_ = read_big_tree(br, mmap_size);
  mclr_tree_ = read_big_tree(br, mclr_size);
  full_tree_ = read_big_tree(br, full_size);
  type_tree_ = read_big_tree(br, type_size);

  std::size_t offset = r.pos();
  for (std::uint32_t i = 0; i <= frame_count_; ++i) {
    frame_offsets_.push_back(offset);
    if (i < frame_count_) {
      offset += sizes[i];
    }
  }
  if (offset > data_.size()) {
    r.fail("frames extend past the end of the file");
  }
  rewind();
}

SmackerVideo SmackerVideo::load(const Install& install, std::string_view dos_path) {
  return SmackerVideo(install.read(dos_path), dos_path);
}

void SmackerVideo::rewind() {
  next_frame_ = 0;
  stored_ = Image8(width_, stored_height_);
  scaled_ = y_scale_ == SmackerYScale::kNone ? Image8() : Image8(width_, 2 * stored_height_);
  palette_ = Palette{};
  palette_changed_ = false;
  audio_.assign(audio_tracks_.size(), {});
}

bool SmackerVideo::decode_next() {
  if (next_frame_ >= frame_count_) {
    return false;
  }
  const std::string context = name_ + " frame " + std::to_string(next_frame_);
  const std::size_t begin = frame_offsets_[next_frame_];
  ByteReader r(std::span(data_).subspan(begin, frame_offsets_[next_frame_ + 1] - begin), context);
  const std::uint8_t type = frame_types_[next_frame_];

  palette_changed_ = (type & kFramePalette) != 0;
  if (palette_changed_) {
    // The length byte counts 4-byte units, itself included.
    const std::size_t length = std::size_t{r.u8()} * 4;
    if (length == 0) {
      r.fail("empty palette chunk");
    }
    decode_palette(r.bytes(length - 1));
  }
  for (std::size_t i = 0; i < audio_tracks_.size(); ++i) {
    audio_[i].clear();
  }
  for (std::uint8_t track = 0; track < kMaxTracks; ++track) {
    if ((type & (2 << track)) == 0) {
      continue;
    }
    const std::uint32_t length = r.u32();
    if (length < 4) {
      r.fail("bad audio chunk length");
    }
    const auto chunk = r.bytes(length - 4);
    const auto it = std::find_if(audio_tracks_.begin(), audio_tracks_.end(),
                                 [&](const SmackerAudioFormat& f) { return f.track == track; });
    if (it == audio_tracks_.end()) {
      r.fail("audio chunk for a track absent from the header");
    }
    audio_[static_cast<std::size_t>(it - audio_tracks_.begin())] = decode_audio(chunk, *it, context);
  }
  decode_video(r.bytes(r.remaining()));

  if (y_scale_ != SmackerYScale::kNone) {
    for (int y = 0; y < stored_height_; ++y) {
      std::ranges::copy(stored_.row(y), scaled_.row(2 * y).begin());
      if (y_scale_ == SmackerYScale::kDoubled) {
        std::ranges::copy(stored_.row(y), scaled_.row(2 * y + 1).begin());
      }
    }
  }
  ++next_frame_;
  return true;
}

void SmackerVideo::decode_palette(std::span<const std::uint8_t> chunk) {
  const Palette previous = palette_;
  ByteReader r(chunk, name_ + " palette");
  // 6-bit components are expanded as (v << 2) | (v >> 4).
  auto expand = [](std::uint8_t v) { return static_cast<std::uint8_t>((v & 0x3f) << 2 | (v & 0x3f) >> 4); };
  std::size_t index = 0;
  while (index < 256 && !r.at_end()) {
    const std::uint8_t op = r.u8();
    if ((op & 0x80) != 0) {
      // Keeps the next entries.
      index += (op & 0x7fu) + 1;
    } else if ((op & 0x40) != 0) {
      // Copies a run of the previous frame's palette.
      const std::size_t count = (op & 0x3fu) + 1;
      const std::size_t source = r.u8();
      if (source + count > 256 || index + count > 256) {
        r.fail("palette copy out of range");
      }
      std::copy_n(previous.colors.begin() + static_cast<std::ptrdiff_t>(source), count,
                  palette_.colors.begin() + static_cast<std::ptrdiff_t>(index));
      index += count;
    } else {
      const std::uint8_t g = r.u8();
      const std::uint8_t b = r.u8();
      palette_.colors[index++] = {expand(op), expand(g), expand(b)};
    }
  }
}

void SmackerVideo::decode_video(std::span<const std::uint8_t> chunk) {
  BitReader br(chunk, name_);
  for (BigTree* tree : {&mmap_tree_, &mclr_tree_, &full_tree_, &type_tree_}) {
    reset_cache(*tree);
  }
  // The picture is a grid of 4x4 blocks in row order.
  const std::size_t blocks_wide = static_cast<std::size_t>(width_) / 4;
  const std::size_t blocks = blocks_wide * (static_cast<std::size_t>(stored_height_) / 4);
  const std::size_t stride = static_cast<std::size_t>(width_);
  std::uint8_t* const pixels = stored_.pixels().data();
  auto block_origin = [&](std::size_t block) {
    return pixels + (block / blocks_wide) * 4 * stride + (block % blocks_wide) * 4;
  };

  std::size_t block = 0;
  while (block < blocks) {
    const std::uint16_t code = decode(type_tree_, br);
    const std::size_t end = std::min<std::size_t>(blocks, block + kBlockRuns[(code >> 2) & 0x3f]);
    switch (static_cast<BlockType>(code & 3)) {
      case BlockType::kTwoColor:
        for (; block < end; ++block) {
          const std::uint16_t colors = decode(mclr_tree_, br);
          std::uint16_t mask = decode(mmap_tree_, br);
          const auto lo = static_cast<std::uint8_t>(colors);
          const auto hi = static_cast<std::uint8_t>(colors >> 8);
          std::uint8_t* out = block_origin(block);
          for (int y = 0; y < 4; ++y, out += stride, mask = static_cast<std::uint16_t>(mask >> 4)) {
            for (int x = 0; x < 4; ++x) {
              out[x] = (mask & (1 << x)) != 0 ? hi : lo;
            }
          }
        }
        break;
      case BlockType::kFull:
        // Each row is two pixel pairs, the right pair first.
        for (; block < end; ++block) {
          std::uint8_t* out = block_origin(block);
          for (int y = 0; y < 4; ++y, out += stride) {
            for (const int x : {2, 0}) {
              const std::uint16_t pair = decode(full_tree_, br);
              out[x] = static_cast<std::uint8_t>(pair);
              out[x + 1] = static_cast<std::uint8_t>(pair >> 8);
            }
          }
        }
        break;
      case BlockType::kSkip:
        block = end;
        break;
      case BlockType::kSolid: {
        const auto color = static_cast<std::uint8_t>(code >> 8);
        for (; block < end; ++block) {
          std::uint8_t* out = block_origin(block);
          for (int y = 0; y < 4; ++y, out += stride) {
            std::fill_n(out, 4, color);
          }
        }
        break;
      }
    }
  }
}

}  // namespace sh

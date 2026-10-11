// The sounds and videos of the synthetic install: tones and noise bursts
// in the sound effects' formats and lengths (MEL, BNK, the speech RAW
// clips), and short Smacker videos of solid colours with a tone track.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "assets/mel.h"
#include "synthetic/encoders.h"
#include "synthetic/generators.h"
#include "synthetic/table_encoders.h"

namespace sh::test::synth {

namespace {

// The frames of a synthetic video at most: they are only coloured screens.
constexpr std::int64_t kMaxVideoFrames = 40;

// Returns `n` unsigned 8-bit samples of a tone at `hz` (at `rate` Hz),
// faded in and out, with noise when `noisy`.
std::vector<std::uint8_t> tone(std::size_t n, double hz, std::int32_t rate, bool noisy, std::uint32_t seed) {
  std::vector<std::uint8_t> out(n);
  SynthRandom random(seed);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / std::max(1, rate);
    const double fade = std::min({1.0, static_cast<double>(i) / 200.0, static_cast<double>(n - i) / 200.0});
    double v = std::sin(2 * std::numbers::pi * hz * t);
    if (noisy) {
      v = 0.4 * v + 0.6 * (random.unit() * 2 - 1);
    }
    out[i] = static_cast<std::uint8_t>(128 + std::lround(fade * 90 * v));
  }
  return out;
}

// A little-endian bit sink for the Smacker trees and streams (least
// significant bit first).
class SmkBits {
 public:
  void bit(bool b) {
    if (count_ % 8 == 0) {
      bytes_.push_back(0);
    }
    if (b) {
      bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (1u << (count_ % 8)));
    }
    ++count_;
  }
  void bits(std::uint32_t v, int n) {
    for (int i = 0; i < n; ++i) {
      bit(((v >> i) & 1) != 0);
    }
  }
  std::vector<std::uint8_t> take() {
    bytes_.resize((bytes_.size() + 3) & ~std::size_t{3}, 0);
    return std::move(bytes_);
  }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t count_ = 0;
};

// Writes a balanced tree over `leaves` (the node bits, `leaf` writing each
// leaf's payload) and returns each leaf's path, its bits from the root.
template <typename Leaf>
void balanced_tree(SmkBits& out, std::size_t first, std::size_t count, std::vector<std::vector<bool>>& paths,
                   std::vector<bool>& path, Leaf&& leaf) {
  if (count == 1) {
    out.bit(false);
    leaf(first);
    paths[first] = path;
    return;
  }
  out.bit(true);
  const std::size_t half = count / 2;
  path.push_back(false);
  balanced_tree(out, first, half, paths, path, leaf);
  path.back() = true;
  balanced_tree(out, first + half, count - half, paths, path, leaf);
  path.pop_back();
}

// One synthetic video: solid frames cycling through `colours` palette
// entries, a full palette on the first frame, and a tone track per
// audio track of the manifest.
std::vector<std::uint8_t> smacker_file(const FileManifest& m) {
  const auto& n = m.numbers;
  if (n.size() < 5) {
    throw std::runtime_error(m.path + ": video manifest too short");
  }
  const auto width = static_cast<std::uint32_t>(n[0]);
  const auto y_scale = static_cast<std::uint32_t>(n[2]);
  const auto height = static_cast<std::uint32_t>(y_scale == 0 ? n[1] : n[1] / 2);
  const auto frames = static_cast<std::uint32_t>(std::min(n[3], kMaxVideoFrames));
  const auto frame_us = static_cast<std::uint32_t>(n[4]);
  struct Track {
    std::uint32_t rate, channels, bits, chunk;
  };
  std::vector<Track> tracks;
  for (std::size_t i = 5; i + 2 < n.size() + 0 && tracks.size() < 7; i += 3) {
    const auto rate = static_cast<std::uint32_t>(n[i]);
    const auto channels = static_cast<std::uint32_t>(n[i + 1]);
    const auto bits = static_cast<std::uint32_t>(n[i + 2]);
    const auto chunk = static_cast<std::uint32_t>(std::uint64_t{rate} * frame_us / 1000000) * channels * (bits / 8);
    tracks.push_back({rate, channels, bits, chunk});
  }

  // The type tree: one solid code of the longest run per colour.
  constexpr std::size_t kColours = 16;
  constexpr std::uint32_t kLongestRun = 63;
  SmkBits trees;
  trees.bit(false);  // MMAP absent
  trees.bit(false);  // MCLR absent
  trees.bit(false);  // FULL absent
  trees.bit(true);   // TYPE
  // Its low-byte tree: the one value (longest run, solid).
  trees.bit(true);
  trees.bit(false);
  trees.bits(kLongestRun << 2 | 3, 8);
  trees.bit(false);
  // Its high-byte tree: the colours.
  std::vector<std::vector<bool>> colour_paths(kColours);
  std::vector<bool> path;
  trees.bit(true);
  balanced_tree(trees, 0, kColours, colour_paths, path,
                [&trees](std::size_t i) { trees.bits(static_cast<std::uint32_t>(16 + 16 * i + 8), 8); });
  trees.bit(false);
  for (const std::uint32_t escape : {0xfff0u, 0xfff4u, 0xfff8u}) {  // two-colour codes, never used
    trees.bits(escape, 16);
  }
  // The big tree: a leaf per colour (its high byte's path; the low tree
  // has a single leaf and reads no bits).
  std::vector<std::vector<bool>> code_paths(kColours);
  balanced_tree(trees, 0, kColours, code_paths, path, [&trees, &colour_paths](std::size_t i) {
    for (const bool b : colour_paths[i]) {
      trees.bit(b);
    }
  });
  trees.bit(false);
  const std::vector<std::uint8_t> tree_bytes = trees.take();

  // The frames.
  const std::uint32_t blocks = (width / 4) * (height / 4);
  const std::uint32_t codes = (blocks + 2047) / 2048;
  const VgaPalette palette = synthetic_palette(name_hash(m.path));
  std::vector<std::vector<std::uint8_t>> chunks;
  std::vector<std::uint8_t> types;
  for (std::uint32_t f = 0; f < frames; ++f) {
    Bytes chunk;
    std::uint8_t type = 0;
    if (f == 0) {
      type |= 1;
      chunk.u8(193);  // 772 bytes in 4-byte units: the length byte and 256 colours
      chunk.bytes(palette);
      chunk.zeros(3);
    }
    for (std::size_t t = 0; t < tracks.size(); ++t) {
      type = static_cast<std::uint8_t>(type | (2u << t));
      chunk.u32(tracks[t].chunk + 4);
      std::vector<std::uint8_t> pcm =
          tone(tracks[t].chunk, 220.0 * static_cast<double>(t + 1), static_cast<std::int32_t>(tracks[t].rate), false, f);
      if (tracks[t].bits == 16) {
        for (std::size_t i = 0; i < pcm.size(); ++i) {
          pcm[i] = i % 2 == 0 ? 0 : static_cast<std::uint8_t>(pcm[i] - 128);
        }
      }
      chunk.bytes(pcm);
    }
    SmkBits video;
    for (std::uint32_t c = 0; c < codes; ++c) {
      for (const bool b : code_paths[(f / 4) % kColours]) {
        video.bit(b);
      }
    }
    chunk.bytes(video.take());
    // The size table keeps the low two bits for flags.
    chunk.zeros((4 - chunk.size() % 4) % 4);
    types.push_back(type);
    chunks.push_back(chunk.take());
  }

  Bytes out;
  out.bytes(std::span(reinterpret_cast<const std::uint8_t*>("SMK2"), 4));
  out.u32(width);
  out.u32(height);
  out.u32(frames);
  out.i32(frame_us % 1000 == 0 ? static_cast<std::int32_t>(frame_us / 1000) : -static_cast<std::int32_t>(frame_us / 10));
  out.u32(y_scale == 2 ? 4 : y_scale == 1 ? 2 : 0);
  for (std::size_t t = 0; t < 7; ++t) {
    out.u32(t < tracks.size() ? tracks[t].chunk : 0);
  }
  out.u32(static_cast<std::uint32_t>(tree_bytes.size()));
  for (int i = 0; i < 3; ++i) {
    out.u32(16);  // the absent trees
  }
  out.u32(static_cast<std::uint32_t>((2 * kColours + 8) * 4));
  for (std::size_t t = 0; t < 7; ++t) {
    if (t < tracks.size()) {
      out.u32(0x40000000u | (tracks[t].channels == 2 ? 0x10000000u : 0u) | (tracks[t].bits == 16 ? 0x20000000u : 0u) |
              tracks[t].rate);
    } else {
      out.u32(0);
    }
  }
  out.u32(0);
  for (std::uint32_t f = 0; f < frames; ++f) {
    out.u32(static_cast<std::uint32_t>(chunks[f].size()) | (f == 0 ? 1u : 0u));
  }
  out.bytes(types);
  out.bytes(tree_bytes);
  for (const auto& c : chunks) {
    out.bytes(c);
  }
  return out.take();
}

}  // namespace

void generate_sounds(Context& context) {
  std::map<std::int32_t, MelHeader> headers;
  for (const FileManifest* m : context.entries("SFX/", "mel")) {
    const auto& n = m->numbers;
    MelSample s;
    s.header = MelHeader{.rate = static_cast<std::int32_t>(n.at(0)),
                         .rate_max = static_cast<std::int32_t>(n.at(1)),
                         .rate_min = static_cast<std::int32_t>(n.at(2)),
                         .loop_count = static_cast<std::int32_t>(n.at(3)),
                         .volume_offset = static_cast<std::int32_t>(n.at(4))};
    const std::uint32_t h = name_hash(m->path);
    s.pcm = tone(static_cast<std::size_t>(n.at(5)), 150.0 + h % 900, s.header.nominal_rate(), h % 3 == 0, h);
    context.files[m->path] = encode_mel(s);
    const std::string name = m->path.substr(4);
    headers[std::stoi(name)] = s.header;
  }
  for (const FileManifest* m : context.entries("SFX/", "bnk")) {
    std::vector<BankEntry> entries;
    for (const std::int64_t id : m->numbers) {
      const auto it = headers.find(static_cast<std::int32_t>(id));
      entries.push_back(BankEntry{.sample_id = static_cast<std::int32_t>(id),
                                  .size = 0,
                                  .pointer = 0,
                                  .header = it == headers.end() ? MelHeader{.rate = 11025} : it->second});
    }
    context.files[m->path] = encode_bnk(entries);
  }
  // The speech and radio clips: a voice-like warble of each clip's length.
  for (const std::string_view dir : {"SPEECH/", "ALT/"}) {
    for (const FileManifest* m : context.entries(dir, "size")) {
      if (!m->path.ends_with(".RAW")) {
        continue;
      }
      const std::uint32_t h = name_hash(m->path);
      context.files[m->path] = tone(static_cast<std::size_t>(m->numbers.at(0)), 120.0 + h % 120, 22050, true, h);
    }
  }
}

void generate_videos(Context& context) {
  for (const FileManifest* m : context.entries("", "smk")) {
    context.files[m->path] = smacker_file(*m);
  }
}

}  // namespace sh::test::synth

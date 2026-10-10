#include "assets/mel.h"

#include <format>

namespace sh {

MelHeader read_mel_header(ByteReader& r) {
  MelHeader h;
  r.skip(4);  // +0: always 0; no code reads it (`sfx_play_on_channel` 0xb602b reads +4..+0x14)
  h.rate = r.i32();
  h.rate_max = r.i32();
  h.rate_min = r.i32();
  h.loop_count = r.i32();
  h.volume_offset = r.i32();
  if (h.random_rate()) {
    if (!(0 < h.rate_min && h.rate_min < h.rate_max)) {
      r.fail(std::format("bad random rate range {}..{}", h.rate_min, h.rate_max));
    }
  } else if (h.rate < 4000 || h.rate > 48000) {
    r.fail(std::format("bad playback rate {}", h.rate));
  }
  return h;
}

MelSample parse_mel(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  MelSample s;
  s.header = read_mel_header(r);
  const auto pcm = r.bytes(r.remaining());
  s.pcm.assign(pcm.begin(), pcm.end());
  return s;
}

std::vector<BankEntry> parse_bnk(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  if (data.size() % kBankEntrySize != 0) {
    r.fail(std::format("size {} is not a multiple of {}", data.size(), kBankEntrySize));
  }
  std::vector<BankEntry> entries;
  while (!r.at_end()) {
    BankEntry e;
    e.sample_id = r.i32();
    if (e.sample_id == -1) {
      if (r.remaining() != kBankEntrySize - 4) {
        r.fail("free record before the end");
      }
      return entries;
    }
    e.size = r.u32();
    e.pointer = r.u32();
    e.header = read_mel_header(r);
    entries.push_back(e);
  }
  return entries;
}

std::string mel_path(std::int32_t id, std::string_view sfx_dir) { return std::format("{}{}.MEL", sfx_dir, id); }

std::string bnk_path(std::int32_t id, std::string_view sfx_dir) { return std::format("{}{}.BNK", sfx_dir, id); }

}  // namespace sh

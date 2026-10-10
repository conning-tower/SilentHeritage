// The MEL effect sample table: samples preloaded from a `.BNK` list and
// loaded on demand by id (mel_load_sfx_bank 0xb6e98, sfx_lookup_or_load
// 0xb3a35).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "assets/mel.h"
#include "core/install.h"

namespace sh {

// Effect samples by id. Samples are never removed, so the pointers returned
// stay valid for the table's lifetime. Not thread-safe.
class SampleTable {
 public:
  // A table loading from `install` (nullable: then only `add` fills it),
  // reading `.MEL`/`.BNK` files from `sfx_dir` (the SHPATH.INI `SFX` entry).
  explicit SampleTable(const Install* install, std::string_view sfx_dir = "SFX/");

  // Loads bank `bank_id` (init_sound loads 1) and every `.MEL` it lists; the
  // `.MEL` headers replace the bank's stale copies. Does nothing without an
  // install. Throws `DataError` if a file is missing or malformed.
  void load_bank(std::int32_t bank_id);

  // Stores `sample` under `id`, replacing any previous one.
  const MelSample& add(std::int32_t id, MelSample sample);

  // Returns sample `id`, loading `<sfx dir><id>.MEL` on a miss; returns
  // nullptr when that file does not exist. Throws `DataError` if malformed.
  const MelSample* find_or_load(std::int32_t id);

  std::size_t size() const { return samples_.size(); }
  bool contains(std::int32_t id) const { return samples_.contains(id); }

 private:
  const Install* install_;
  std::string sfx_dir_;
  std::map<std::int32_t, MelSample> samples_;
};

}  // namespace sh

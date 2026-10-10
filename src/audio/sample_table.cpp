#include "audio/sample_table.h"

#include <utility>

namespace sh {

SampleTable::SampleTable(const Install* install, std::string_view sfx_dir) : install_(install), sfx_dir_(sfx_dir) {}

void SampleTable::load_bank(std::int32_t bank_id) {
  if (install_ == nullptr) {
    return;
  }
  const std::string path = bnk_path(bank_id, sfx_dir_);
  for (const BankEntry& e : parse_bnk(install_->read(path), path)) {
    const std::string mel = mel_path(e.sample_id, sfx_dir_);
    add(e.sample_id, parse_mel(install_->read(mel), mel));
  }
}

const MelSample& SampleTable::add(std::int32_t id, MelSample sample) {
  return samples_.insert_or_assign(id, std::move(sample)).first->second;
}

const MelSample* SampleTable::find_or_load(std::int32_t id) {
  if (const auto it = samples_.find(id); it != samples_.end()) {
    return &it->second;
  }
  if (install_ == nullptr) {
    return nullptr;
  }
  const std::string path = mel_path(id, sfx_dir_);
  const std::optional<std::vector<std::uint8_t>> data = install_->try_read(path);
  if (!data) {
    return nullptr;
  }
  return &add(id, parse_mel(*data, path));
}

}  // namespace sh

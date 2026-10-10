#include "editor/data.h"

#include <algorithm>
#include <string>

namespace sh::editor {
namespace {

std::optional<std::uint32_t> find_entry(const std::vector<CatEntry>& entries, std::string_view name) {
  const auto it =
      std::ranges::find_if(entries, [&](const CatEntry& e) { return equals_ignore_case(e.name, name); });
  if (it == entries.end()) {
    return std::nullopt;
  }
  return it->size;
}

template <class Record>
std::optional<std::size_t> find_by_name(const std::vector<Record>& records, std::string_view name) {
  for (std::size_t i = 0; i < records.size(); ++i) {
    if (equals_ignore_case(records[i].name, name)) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace

ModelCatalog ModelCatalog::load(const Install& install) {
  return ModelCatalog(CatArchive::load(install, "ART/3DSHIPS.CAT").entries(),
                      CatArchive::load(install, "ART/3DAIR.CAT").entries());
}

std::optional<std::uint32_t> ModelCatalog::size_of(std::string_view name) const {
  if (const auto size = find_entry(ships_, name)) {
    return size;
  }
  return find_entry(air_, name);
}

TorpedoChoices TorpedoChoices::from_weapons(const std::vector<Torpedo>& weapons) {
  const auto listed = [](const Torpedo& t) { return t.nation == Nation::kUs && t.id != 4; };
  TorpedoChoices c;
  c.count = 1 + static_cast<std::size_t>(std::ranges::count_if(weapons, listed));
  for (std::size_t i = 0; i < weapons.size() && listed(weapons[i]); ++i) {
    c.torpedoes.push_back(i);
  }
  return c;
}

EditorData EditorData::load(const Install& install) {
  GameTables tables = GameTables::load(install);
  TorpedoChoices choices = TorpedoChoices::from_weapons(tables.weapons.torpedoes);
  return EditorData{
      .tables = std::move(tables),
      .zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "DATA/PZ.DAT"),
      .models = ModelCatalog::load(install),
      .torpedo_choices = std::move(choices),
      .se = SeExe::load(install),
      .ephemeris = Ephemeris::load(install),
  };
}

std::optional<std::size_t> EditorData::find_ship(std::string_view name) const {
  return find_by_name(tables.ships, name);
}

std::optional<std::size_t> EditorData::find_sub(std::string_view name) const { return find_by_name(tables.subs, name); }

std::optional<std::size_t> EditorData::find_aircraft(std::string_view name) const {
  return find_by_name(tables.aircraft, name);
}

std::optional<std::size_t> EditorData::find_listed_torpedo(std::string_view short_name) const {
  const std::vector<Torpedo>& torpedoes = tables.weapons.torpedoes;
  const std::size_t n = std::min(torpedo_choices.count, torpedoes.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (equals_ignore_case(torpedoes[i].short_name, short_name)) {
      return i;
    }
  }
  return std::nullopt;
}

std::unique_ptr<WorldData> WorldData::load(const Install& install, const EditorData& data,
                                           std::string_view world_name) {
  const std::string path = "DATA/" + std::string(world_name);
  std::unique_ptr<WorldData> w(new WorldData(parse_world(install.read(path), path)));
  // sb_setup_world: the PZ.DAT record of the world, else no bases.
  const auto zone =
      std::ranges::find_if(data.zones, [&](const PatrolZone& z) { return equals_ignore_case(z.world, world_name); });
  if (zone != data.zones.end()) {
    w->zone_ = ZoneData::load(install, *zone);
  }
  return w;
}

const std::vector<BaseRecord>& WorldData::bases() const {
  static const std::vector<BaseRecord> kNone;
  return zone_ ? zone_->bases : kNone;
}

void WorldData::update_owners(std::int32_t day, std::int32_t month, std::int32_t year) {
  airstrike_targets_ = 0;
  photo_recon_targets_ = 0;
  if (!zone_) {
    return;
  }
  zone_->update_owners(day, month, year);
  for (std::size_t i = 0; i < zone_->bases.size(); ++i) {
    const BaseRecord& b = zone_->bases[i];
    if (b.has(BaseFlag::kActive) && zone_->owners[i].side == BaseSide::kJapanese) {
      if (b.pick_weight > 0) {
        ++airstrike_targets_;
      }
      if (!b.has(BaseFlag::kNoRecon)) {
        ++photo_recon_targets_;
      }
    }
  }
}

std::int32_t WorldData::memory_size() const {
  // 0x2d8ba: chart block 0xf3420, icons 0xf342c, grid 0xf340c x 0xf3410.
  return static_cast<std::int32_t>(world_.chart_block_size) + static_cast<std::int32_t>(world_.icons_shp.size()) +
         world_.grid.width * world_.grid.height;
}

std::int32_t memory_budget(std::int32_t world_memory_size, std::int32_t free_memory) {
  // 0x2d7bb (argument 1) then 0x2d8ba.
  const std::int32_t basmem = free_memory + 0x183000;
  constexpr std::int32_t kMaxSfxbuf = 0x1ab3f0;
  std::int32_t sfxbuf = kMaxSfxbuf;
  if (basmem - 0x230000 <= 0x698f6f) {
    sfxbuf = std::min((basmem - 0x6d4f70) / 1024 * 0x4a7 / 2000 * 0x400 + 530000, kMaxSfxbuf);
  }
  return basmem - (world_memory_size + sfxbuf + 0x4b6f90);
}

}  // namespace sh::editor

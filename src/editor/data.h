// The editor's data (`sb_load_data`, SE 0x44e0b, and `sb_setup_world`,
// 0x45259): the game tables, the TORPEDOES list, the 3D-model catalogue
// sizes of the memory estimate, `PZ.DAT`, `SE.EXE`'s strings, and the
// patrol map of the current scenario with its bases.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "assets/cat.h"
#include "assets/dat_tables.h"
#include "assets/ephemeris.h"
#include "assets/patrol_zones.h"
#include "assets/world.h"
#include "core/install.h"
#include "editor/se_strings.h"
#include "game/scengen.h"
#include "sim/line_of_sight.h"
#include "sim/terrain.h"

namespace sh::editor {

// The file sizes of the 3D models, the catalogue sizes `model_cache_add`
// (SE 0x4629f) adds to the memory estimate.
class ModelCatalog {
 public:
  ModelCatalog() = default;
  // `ships` and `air` are the directories of ART\3DSHIPS.CAT and
  // ART\3DAIR.CAT.
  ModelCatalog(std::vector<CatEntry> ships, std::vector<CatEntry> air)
      : ships_(std::move(ships)), air_(std::move(air)) {}
  static ModelCatalog load(const Install& install);

  // Returns the size of model file `name` in 3DSHIPS.CAT, else in 3DAIR.CAT
  // (case-insensitive), or nothing when neither holds it.
  std::optional<std::uint32_t> size_of(std::string_view name) const;

 private:
  std::vector<CatEntry> ships_;
  std::vector<CatEntry> air_;
};

// The TORPEDOES list (`load_weapons_dat`, SE 0x45ae2): "EMPTY", then the
// leading WEAPONS.DAT torpedoes that are American and not id 4 (MARK10,
// MARK14, MARK18-1).
struct TorpedoChoices {
  // WEAPONS.DAT torpedo indexes of the list entries after "EMPTY" (0xa50ac).
  std::vector<std::size_t> torpedoes;
  // The list size 0xa50b0: "EMPTY" plus every American torpedo that is not
  // id 4, leading or not; also the size of the per-torpedo reload counts.
  std::size_t count = 1;

  static TorpedoChoices from_weapons(const std::vector<Torpedo>& weapons);
};

// The tables loaded once at start-up.
struct EditorData {
  GameTables tables;
  std::vector<PatrolZone> zones;
  ModelCatalog models;
  TorpedoChoices torpedo_choices;
  SeExe se;
  // SUN.DAT / EARTH.DAT (`load_ephemeris`): the ENVIRONMENT sky.
  Ephemeris ephemeris;

  // Throws `DataError` if a file is missing or malformed.
  static EditorData load(const Install& install);

  // Return the index of the class whose record name (+0) is `name`
  // (case-insensitive, first match), as the editor's linear searches do.
  std::optional<std::size_t> find_ship(std::string_view name) const;
  std::optional<std::size_t> find_sub(std::string_view name) const;
  std::optional<std::size_t> find_aircraft(std::string_view name) const;
  // Returns the index into WEAPONS.DAT's torpedoes of `short_name` among the
  // first `torpedo_choices.count` torpedoes (`sb_reset_torpedo_load`).
  std::optional<std::size_t> find_listed_torpedo(std::string_view short_name) const;
};

// The patrol map of the scenario (`scenario_reload_world` + `sb_setup_world`):
// the `.WLD` world, its sea floor and grid, and the `PZ.DAT` zone with its
// bases when one names the world.
class WorldData {
 public:
  // Loads DATA\<`world_name`> and the bases of the zone whose world it is.
  // Throws `DataError` if the world cannot be read.
  static std::unique_ptr<WorldData> load(const Install& install, const EditorData& data, std::string_view world_name);

  WorldData(const WorldData&) = delete;
  WorldData& operator=(const WorldData&) = delete;

  const World& world() const { return world_; }
  const WorldSeaFloor& floor() const { return floor_; }
  const TerrainGrid& grid() const { return grid_; }
  // The zone with its bases, or nullptr when no `PZ.DAT` record names the
  // world (the editor then has no bases).
  const ZoneData* zone() const { return zone_ ? &*zone_ : nullptr; }
  // The bases of the zone (empty without one).
  const std::vector<BaseRecord>& bases() const;

  // Sets the base owners on a date (`bases_update_owners`, `month` 0-based,
  // `year` since 1930) and counts the valid targets (`sb_build_base_list`,
  // 0x45f25).
  void update_owners(std::int32_t day, std::int32_t month, std::int32_t year);
  // The active Japanese-held bases with an air-raid weight (0xa50dc) and
  // those not excluded from photo recon (0xa50e0).
  std::int32_t airstrike_targets() const { return airstrike_targets_; }
  std::int32_t photo_recon_targets() const { return photo_recon_targets_; }

  // Returns the bytes the world holds in memory (chart block, chart icons,
  // terrain grid), part of the memory budget (`0x2d8ba`).
  std::int32_t memory_size() const;

 private:
  explicit WorldData(World world) : world_(std::move(world)), floor_(world_), grid_(world_) {}

  World world_;
  WorldSeaFloor floor_;
  WorldTerrainGrid grid_;
  std::optional<ZoneData> zone_;
  std::int32_t airstrike_targets_ = 0;
  std::int32_t photo_recon_targets_ = 0;
};

// The free-memory figure the memory budget assumes (see `memory_budget`).
inline constexpr std::int32_t kAssumedFreeMemory = 0xf00000;

// Returns the memory budget the scenario size is compared with (0xa5108):
// BASMEM - (world sizes + SFXBUF + 0x4b6f90), with SE's own BASMEM and
// SFXBUF of a start without `/BASMEM=` and `/SFXBUF=` (0x2d7bb with the
// argument 1 of `se_main`): BASMEM = free memory + 0x183000; SFXBUF = 530000
// plus a share of the memory above 0x6d4f70, at most 0x1ab3f0.
// Limitation: the free memory is the fixed `kAssumedFreeMemory` (a DOSBox
// 16 MB machine), not SH's command line as when SH starts SE.
std::int32_t memory_budget(std::int32_t world_memory_size, std::int32_t free_memory = kAssumedFreeMemory);

}  // namespace sh::editor

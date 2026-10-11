// The parts of the synthetic install generator (synthetic_install.h), each
// writing one family of files into the context's install. Internal to
// tests/synthetic.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "synthetic/install_files.h"
#include "synthetic/manifest.h"
#include "synthetic/procedural.h"

namespace sh::test::synth {

// The names the generated files share: the classes of the tables and the
// sprites, maps and routes they refer to.
struct Model {
  struct ShipClassNames {
    std::string name;    // class name, e.g. "SYN BB 1"
    std::string sprite;  // 3DSHIPS.CAT member, e.g. "SHIP01.SHP"
    std::string silhouette;  // second 3DSHIPS.CAT member
    std::string idbook;  // IDBOOK.CAT member
  };
  std::vector<ShipClassNames> ships;
  std::vector<std::string> aircraft_sprites;  // 3DAIR.CAT members

  // A map's world file and its open water, for the scenarios.
  struct MapInfo {
    std::string world;  // e.g. "WORLD-1.WLD"
    // World points (x east, y north) of deep water away from the edges.
    std::vector<std::array<std::int32_t, 2>> deep_points;
  };
  std::vector<MapInfo> maps;
  // The classes for the scenarios, by role.
  std::vector<std::string> merchants;
  std::vector<std::string> escorts;
  std::vector<std::string> warships;
  std::vector<std::string> enemy_subs;
  std::vector<std::string> aircraft;
  // The player's classes with their first year in service.
  std::vector<std::pair<std::string, std::uint32_t>> boats;
};

struct Context {
  const Manifest& manifest;
  SynthRandom random;
  InstallFiles& files;
  Model model;

  // Returns the manifest entry of `path`; throws `std::runtime_error` when
  // the manifest has none.
  const FileManifest& entry(const std::string& path) const;
  // Returns the manifest entries whose path starts with `prefix` and whose
  // kind is `kind` (any kind for an empty one).
  std::vector<const FileManifest*> entries(std::string_view prefix, std::string_view kind = {}) const;
};

// Returns the `.SHP` of the frames of `sprite` (procedural pictures in
// the boxes of the manifest), drawn with `seed`.
std::vector<std::uint8_t> sprite_file(const SpriteManifest& sprite, std::uint32_t seed);

void generate_tables(Context& context);
void generate_art(Context& context);
void generate_world(Context& context);
void generate_sounds(Context& context);
void generate_videos(Context& context);
void generate_scenarios(Context& context);
void generate_misc(Context& context);

}  // namespace sh::test::synth

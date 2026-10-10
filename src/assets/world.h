// `.WLD` patrol maps (`DATA/WORLD-<n>.WLD`): the terrain/depth grid the
// simulation samples, the painted navigation chart, the landmarks and the
// map's geographic bounds; plus the map's coordinate conventions. Layout from
// `world_load_awld` (0x9102d) and `world_load_awld_chart` (0x91222); docs/formats.md
// ".WLD".
//
// World positions (here, in `.SCN` and in `.CR`) are 32-bit world units with
// `x` east and the second horizontal axis (`y` in `.WLD`/`.SCN`, `z` in `.CR`
// and in the simulation) counting north from the bottom grid row. A grid cell
// is `0x10000 >> scale_shift` units wide: with the shipped `scale_shift` 0 a
// position is a 16.16 fixed-point grid cell, and 16 units are one foot.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/fixed.h"

namespace sh {

// The game's location array holds this many entries (0x16e58c).
inline constexpr std::size_t kMaxLocations = 400;

// Grid values: `kCoastCell` is the coastline, values above it are land
// (`value - kCoastCell` height steps), values below it water depth steps.
inline constexpr std::uint8_t kCoastCell = 0x20;

// First palette index of the grid and chart colour tables.
inline constexpr int kGridDacFirstIndex = 0;
inline constexpr int kChartDacFirstIndex = 128;

// What a landmark is: indexes the `.OBS` objects (its 3D sprite) and names
// it in radio reports (0-5, table 0x10eb54; 6-8 are named from their
// sprites). Other values are kept as read.
enum class LocationType : std::int32_t {
  kBuoy = 0,
  kCity = 1,
  kPortFacilities = 2,
  kIndustrialFacilities = 3,
  kTown = 4,
  kLightHouse = 5,
  kMooredSubmarine = 6,
  kPalmIslet = 7,
  kShark = 8,
};

// A map landmark, drawn as a 3D object (one render-list entry each).
struct Location {
  LocationType type = LocationType::kBuoy;
  std::int32_t x = 0;  // world units, east
  std::int32_t z = 0;  // height above the sea (render y)
  std::int32_t y = 0;  // world units, north
  Angle10 heading;     // read by the game as a short
};

// An 8-bit indexed picture, row-major (row 0 at the top).
struct IndexedRaster {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<std::uint8_t> pixels;

  std::uint8_t at(std::int32_t column, std::int32_t row) const {
    return pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(column)];
  }
};

// The map's geographic extent, in 16.16 fixed-point degrees (south and west
// negative).
struct MapBounds {
  Fixed16 lat_north;
  Fixed16 lat_south;
  Fixed16 lon_west;
  Fixed16 lon_east;
};

// A grid cell: `column` from the west edge, `row` from the top (north) row.
struct GridCell {
  std::int32_t column = 0;
  std::int32_t row = 0;
  constexpr bool operator==(const GridCell&) const = default;
};

// A latitude or longitude as the chart reports print it.
struct GeoAngle {
  Fixed16 degrees;              // signed value
  std::int32_t whole_degrees = 0;  // of the absolute value
  std::int32_t minutes = 0;        // 0..59, truncated
  char hemisphere = 'N';           // 'N'/'S' or 'E'/'W'
};

// A parsed `.WLD` file.
struct World {
  std::uint8_t version = 0;   // 7 shipped; >= 4 accepted
  std::string region_name;    // version >= 6, e.g. "Southern Japan"
  std::uint32_t scale_shift = 0;
  std::vector<Location> locations;
  // Terrain/depth level per cell, `kCoastCell`-relative (see above).
  IndexedRaster grid;
  // 64 colours of 6-bit VGA RGB for palette indices 0-63 (the grid).
  std::array<std::uint8_t, 0xc0> grid_dac{};
  // The painted navigation chart (the grid scaled by `chart.width / grid.width`).
  IndexedRaster chart;
  // The declared size of the chart's row block (`world_load_awld_chart`'s
  // allocation 0xf3420, part of the editor's memory budget).
  std::uint32_t chart_block_size = 0;
  // 112 colours of 6-bit VGA RGB for palette indices 128-239 (the chart).
  std::array<std::uint8_t, 0x150> chart_dac{};
  // The chart icons: an embedded `.SHP` file kept as raw bytes (version >= 5).
  std::vector<std::uint8_t> icons_shp;
  std::optional<MapBounds> bounds;  // version >= 7

  // Returns the width of a grid cell in world units.
  std::int32_t cell_size() const { return 0x10000 >> scale_shift; }

  // Returns the grid cell holding world point (`x`, `y`), or nothing outside
  // the grid, with the original's truncating divisions
  // (`world_terrain_height_at`, 0x9449b): the row is
  // `(grid.height * cell_size() - y) / cell_size()`.
  std::optional<GridCell> cell_at(std::int32_t x, std::int32_t y) const;

  // Returns the longitude of world `x` and the latitude of world `y`
  // (`world_pos_to_latlon`, 0x95c12). Throw `DataError` if the map has no
  // bounds (version < 7).
  GeoAngle longitude_at(std::int32_t x) const;
  GeoAngle latitude_at(std::int32_t y) const;
};

// Parses a whole `.WLD` file; `context` names it in errors. Throws
// `DataError` if it is malformed.
World parse_world(std::span<const std::uint8_t> data, std::string_view context);

// Returns the height in world units of grid level `level` (grid value minus
// `kCoastCell`), not interpolated: land levels times `0x15e0 >> scale_shift`
// (350 ft at 0), water levels -30..-1 times 25 ft and lower levels times
// 50 ft (`world_terrain_level_to_height`, 0x946ea).
std::int32_t terrain_level_to_height(std::int32_t level, std::uint32_t scale_shift);

}  // namespace sh

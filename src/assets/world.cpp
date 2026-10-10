#include "assets/world.h"

#include <algorithm>
#include <cstdlib>
#include <format>

#include "assets/compressed.h"
#include "assets/format_header.h"
#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {
namespace {

constexpr std::uint32_t kMaxScaleShift = 16;  // keeps a cell at least 1 unit wide

// Returns the chart decoded from its rows: `u32 row_size` (including itself)
// followed by one RLE90-packed row.
IndexedRaster decode_chart(ByteReader& rows, std::int32_t height) {
  IndexedRaster chart;
  chart.height = height;
  for (std::int32_t y = 0; y < height; ++y) {
    const std::uint32_t size = rows.u32();
    if (size < 4) {
      rows.fail("chart row shorter than its size field");
    }
    const std::vector<std::uint8_t> row = rle90_decode(rows.bytes(size - 4));
    if (y == 0) {
      chart.width = static_cast<std::int32_t>(row.size());
    } else if (row.size() != static_cast<std::size_t>(chart.width)) {
      rows.fail("chart rows have differing widths");
    }
    chart.pixels.insert(chart.pixels.end(), row.begin(), row.end());
  }
  return chart;
}

// Returns the signed `value` split into degrees, minutes and hemisphere.
GeoAngle split_angle(std::int32_t value, char positive, char negative) {
  const std::uint32_t magnitude = static_cast<std::uint32_t>(std::abs(static_cast<std::int64_t>(value)));
  return GeoAngle{
      .degrees = Fixed16::from_raw(value),
      .whole_degrees = static_cast<std::int32_t>(magnitude >> 16),
      .minutes = static_cast<std::int32_t>(((magnitude & 0xffff) * 60) >> 16),
      .hemisphere = value < 0 ? negative : positive,
  };
}

}  // namespace

std::optional<GridCell> World::cell_at(std::int32_t x, std::int32_t y) const {
  const std::int32_t cell = cell_size();
  const GridCell c{x / cell, (grid.height * cell - y) / cell};
  if (c.column < 0 || c.column > grid.width - 1 || c.row < 0 || c.row > grid.height - 1) {
    return std::nullopt;
  }
  return c;
}

GeoAngle World::longitude_at(std::int32_t x) const {
  if (!bounds) {
    throw DataError(std::format("{}: map has no geographic bounds", region_name));
  }
  std::int64_t span = std::int64_t{bounds->lon_east.raw()} - bounds->lon_west.raw();
  while (span < 0) {
    span += 360 << 16;
  }
  const std::int64_t extent = static_cast<std::int64_t>(cell_size()) * grid.width;
  return split_angle(static_cast<std::int32_t>(bounds->lon_west.raw() + span * x / extent), 'E', 'W');
}

GeoAngle World::latitude_at(std::int32_t y) const {
  if (!bounds) {
    throw DataError(std::format("{}: map has no geographic bounds", region_name));
  }
  const std::int64_t extent = static_cast<std::int64_t>(cell_size()) * grid.height;
  const std::int64_t span = bounds->lat_south.raw() - bounds->lat_north.raw();
  return split_angle(static_cast<std::int32_t>(bounds->lat_north.raw() + span * (extent - y) / extent), 'N', 'S');
}

World parse_world(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  World w;
  w.version = read_format_header(r, "AWLD", 4);
  if (w.version >= 6) {
    w.region_name = r.fixed_string(31);
  }
  w.scale_shift = r.u32();
  if (w.scale_shift > kMaxScaleShift) {
    r.fail(std::format("implausible scale shift {}", w.scale_shift));
  }
  const std::uint16_t n_locations = r.u16();
  if (n_locations > kMaxLocations) {
    r.fail(std::format("{} locations, the game holds {}", n_locations, kMaxLocations));
  }
  w.locations.reserve(n_locations);
  for (std::uint16_t i = 0; i < n_locations; ++i) {
    Location& loc = w.locations.emplace_back();
    loc.type = static_cast<LocationType>(r.i32());
    loc.x = r.i32();
    loc.z = r.i32();
    loc.y = r.i32();
    loc.heading = Angle10(r.i32());
    r.skip(4);  // +0x14: `world_load_awld` 0x9102d loads it, neither SH.EXE nor SE.EXE reads it
  }

  w.grid.width = r.i32();
  w.grid.height = r.i32();
  if (w.grid.width <= 0 || w.grid.height <= 0) {
    r.fail("empty terrain grid");
  }
  // The block's size is checked before decoding it.
  const std::size_t grid_block = r.pos();
  if (r.u32() != static_cast<std::uint64_t>(w.grid.width) * static_cast<std::uint64_t>(w.grid.height)) {
    r.fail("terrain grid block size disagrees with the grid size");
  }
  r.seek(grid_block);
  w.grid.pixels = read_compressed(r);
  if (w.grid.pixels.size() != static_cast<std::size_t>(w.grid.width) * static_cast<std::size_t>(w.grid.height)) {
    r.fail("terrain grid block size disagrees with the grid size");
  }
  r.skip(4);  // a sea state the game loads then overwrites before any read
  std::ranges::copy(r.bytes(w.grid_dac.size()), w.grid_dac.begin());

  // world_load_awld_chart: the declared width is that of the row data minus
  // one; the rows themselves give the real width.
  r.skip(4);
  const std::int32_t chart_height = r.i32();
  const std::uint32_t chart_size = r.u32();
  const std::vector<std::uint8_t> rows = read_compressed(r);
  if (rows.size() > chart_size) {
    r.fail("chart block larger than its declared size");
  }
  if (chart_height <= 0) {
    r.fail("empty chart");
  }
  ByteReader row_reader(rows, std::format("{} chart", context));
  w.chart = decode_chart(row_reader, chart_height);
  w.chart_block_size = chart_size;
  std::ranges::copy(r.bytes(w.chart_dac.size()), w.chart_dac.begin());

  if (w.version >= 5) {
    const std::uint32_t tiles_size = r.u32();
    const auto tiles = r.bytes(tiles_size);
    w.coast_tiles_shp.assign(tiles.begin(), tiles.end());
  }
  if (w.version >= 7) {
    MapBounds b;
    b.lat_north = Fixed16::from_raw(r.i32());
    b.lat_south = Fixed16::from_raw(r.i32());
    b.lon_west = Fixed16::from_raw(r.i32());
    b.lon_east = Fixed16::from_raw(r.i32());
    w.bounds = b;
  }
  return w;
}

std::int32_t terrain_level_to_height(std::int32_t level, std::uint32_t scale_shift) {
  if (level >= 0) {
    return level * (0x15e0 >> scale_shift);
  }
  return level * (level < -30 ? 800 : 400);
}

}  // namespace sh

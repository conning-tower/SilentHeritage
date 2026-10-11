// The maps of the synthetic install: procedural archipelagos (WLD) with
// their charts, landmarks and bases (MAP<n>BASE.DAT), convoy routes (CR)
// between them, the patrol zones (PZ.DAT), the map objects (OBS) and the
// skies (ENV).
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "assets/bases.h"
#include "assets/environment.h"
#include "assets/objects.h"
#include "assets/patrol_zones.h"
#include "assets/routes.h"
#include "assets/world.h"
#include "synthetic/encoders.h"
#include "synthetic/generators.h"
#include "synthetic/table_encoders.h"

namespace sh::test::synth {

namespace {

constexpr int kMaps = 15;
constexpr std::uint8_t kDeepest = 0x00;
constexpr std::uint8_t kChartWater = 128;  // 16 depth shades from here
constexpr std::uint8_t kChartLand = 144;   // 16 height shades from here
constexpr std::uint8_t kChartCoast = 160;
constexpr std::uint8_t kChartGrid = 161;
constexpr std::uint8_t kChartLabel = 162;

// A generated map: its world file and the water cells routes may use.
struct MapPlan {
  World world;
  std::int32_t cell = 0;  // world units per grid cell

  std::uint8_t at(std::int32_t column, std::int32_t row) const {
    return world.grid.pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(world.grid.width) +
                             static_cast<std::size_t>(column)];
  }
  bool deep(std::int32_t column, std::int32_t row) const { return at(column, row) < 0x18; }
  // The world point (x east, y north) of the centre of a cell.
  std::array<std::int32_t, 2> point(std::int32_t column, std::int32_t row) const {
    return {column * cell + cell / 2, (world.grid.height - row) * cell - cell / 2};
  }
};

// Returns the grid level of the terrain at (x, y) in [0, 1] map units:
// islands in a ring, deep water along the edges and in the middle.
std::uint8_t terrain(double x, double y, std::uint32_t seed) {
  const double edge = std::min({x, y, 1 - x, 1 - y});
  const double ring = std::abs(std::hypot(x - 0.5, y - 0.5) - 0.3);
  double e = fractal_noise(x * 9, y * 6, seed, 5) - ring * 1.2;
  if (edge < 0.08) {
    e -= (0.08 - edge) * 8;
  }
  constexpr double kSea = 0.42;
  if (e > kSea) {
    return static_cast<std::uint8_t>(std::min(0x3f, 0x21 + static_cast<int>((e - kSea) * 120)));
  }
  return static_cast<std::uint8_t>(std::max<int>(kDeepest, 0x1f - static_cast<int>((kSea - e) * 120)));
}

VgaPalette::value_type six(double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 63); }

MapPlan make_map(const FileManifest& m, int number) {
  const auto& n = m.numbers;
  if (n.size() < 8) {
    throw std::runtime_error(m.path + ": map manifest too short");
  }
  MapPlan plan;
  World& w = plan.world;
  w.version = static_cast<std::uint8_t>(n[0]);
  w.region_name = std::format("Synthetic Sea {}", number);
  w.scale_shift = static_cast<std::uint32_t>(n[1]);
  plan.cell = 0x10000 >> w.scale_shift;
  const auto gw = static_cast<std::int32_t>(n[3]);
  const auto gh = static_cast<std::int32_t>(n[4]);
  const std::uint32_t seed = name_hash(m.path);
  w.grid = IndexedRaster{.width = gw, .height = gh, .pixels = {}};
  w.grid.pixels.resize(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh));
  for (std::int32_t row = 0; row < gh; ++row) {
    for (std::int32_t col = 0; col < gw; ++col) {
      w.grid.pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(col)] =
          terrain((col + 0.5) / gw, (row + 0.5) / gh, seed);
    }
  }
  for (std::size_t i = 0; i < 64; ++i) {
    const bool land = i >= 0x20;
    const double t = land ? (i - 0x20) / 31.0 : i / 31.0;
    w.grid_dac[3 * i] = six(land ? 0.3 + 0.4 * t : 0.05);
    w.grid_dac[3 * i + 1] = six(land ? 0.5 - 0.2 * t : 0.2 + 0.4 * t);
    w.grid_dac[3 * i + 2] = six(land ? 0.2 : 0.5 + 0.4 * t);
  }
  // The chart: the grid's shades, coast lines, a graticule and the name.
  const auto cw = static_cast<std::int32_t>(n[5]);
  const auto ch = static_cast<std::int32_t>(n[6]);
  w.chart = IndexedRaster{.width = cw, .height = ch, .pixels = {}};
  w.chart.pixels.resize(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch));
  Image8 chart(cw, ch);
  for (std::int32_t y = 0; y < ch; ++y) {
    for (std::int32_t x = 0; x < cw; ++x) {
      const std::int32_t col = std::min(gw - 1, x * gw / cw);
      const std::int32_t row = std::min(gh - 1, y * gh / ch);
      const std::uint8_t v = plan.at(col, row);
      std::uint8_t c = v >= 0x20 ? static_cast<std::uint8_t>(kChartLand + std::min(15, (v - 0x20) / 2))
                                 : static_cast<std::uint8_t>(kChartWater + v / 2);
      if (v >= 0x20 && ((col > 0 && plan.at(col - 1, row) < 0x20) || (row > 0 && plan.at(col, row - 1) < 0x20))) {
        c = kChartCoast;
      }
      if (x % 100 == 0 || y % 100 == 0) {
        c = kChartGrid;
      }
      chart.set(x, y, c);
    }
  }
  draw_label(chart, cw / 3, ch / 2, w.region_name, 3, kChartLabel);
  std::ranges::copy(chart.pixels(), w.chart.pixels.begin());
  for (std::size_t i = 0; i < 112; ++i) {
    const double t = (i % 16) / 15.0;
    const bool water = i < 16;
    const bool land = i >= 16 && i < 32;
    w.chart_dac[3 * i] = six(water ? 0.3 + 0.3 * t : land ? 0.4 + 0.4 * t : i == 34 ? 1 : 0.1);
    w.chart_dac[3 * i + 1] = six(water ? 0.5 + 0.3 * t : land ? 0.6 + 0.2 * t : i == 34 ? 1 : 0.1);
    w.chart_dac[3 * i + 2] = six(water ? 0.9 : land ? 0.3 : i == 34 ? 1 : 0.2);
  }
  if (!m.sprites.empty()) {
    w.coast_tiles_shp = sprite_file(m.sprites[0], seed);
  }
  // The landmarks: on coast cells, cycling through the location types.
  const auto locations = static_cast<std::size_t>(n[2]);
  SynthRandom random(seed);
  for (std::size_t tries = 0; w.locations.size() < locations && tries < locations * 1000; ++tries) {
    const std::int32_t col = random.range(1, gw - 2);
    const std::int32_t row = random.range(1, gh - 2);
    const bool coast = plan.at(col, row) >= 0x20 && plan.at(col, row) < 0x24;
    const auto type = static_cast<LocationType>(w.locations.size() % 9);
    const bool at_sea = type == LocationType::kBuoy || type == LocationType::kShark;
    if (at_sea ? plan.at(col, row) < 0x1c && plan.at(col, row) > 0x10 : coast) {
      const auto p = plan.point(col, row);
      w.locations.push_back(Location{.type = type, .x = p[0], .z = 0, .y = p[1],
                                     .heading = Angle10(static_cast<std::int32_t>(w.locations.size() * 370 % 3600))});
    }
  }
  if (n[7] != 0) {
    const double north = 30.0 - number;
    const double west = 120.0 + number;
    const double span = static_cast<double>(gw) * plan.cell / (6077.0 * 16 * 60);
    w.bounds = MapBounds{Fixed16::from_double(north), Fixed16::from_double(north - span * gh / gw),
                         Fixed16::from_double(west), Fixed16::from_double(west + span)};
  }
  return plan;
}

// Returns up to `count` cells of deep water at least `margin` cells from
// the map's edge, spread over the map.
std::vector<std::array<std::int32_t, 2>> deep_cells(const MapPlan& plan, int count, SynthRandom& random,
                                                    std::int32_t margin) {
  std::vector<std::array<std::int32_t, 2>> out;
  const std::int32_t gw = plan.world.grid.width;
  const std::int32_t gh = plan.world.grid.height;
  for (int tries = 0; static_cast<int>(out.size()) < count && tries < count * 2000; ++tries) {
    const std::int32_t col = random.range(margin, gw - 1 - margin);
    const std::int32_t row = random.range(margin, gh - 1 - margin);
    if (plan.deep(col, row)) {
      out.push_back({col, row});
    }
  }
  return out;
}

std::vector<BaseRecord> make_bases(const MapPlan& plan, int number) {
  SynthRandom random(name_hash(std::format("bases{}", number)));
  std::vector<BaseRecord> bases;
  const std::int32_t gw = plan.world.grid.width;
  const std::int32_t gh = plan.world.grid.height;
  for (int tries = 0; bases.size() < 8 && tries < 200000; ++tries) {
    const std::int32_t col = random.range(2, gw - 3);
    const std::int32_t row = random.range(2, gh - 3);
    // A harbour: shallow water next to land, deep water a few cells out.
    if (plan.at(col, row) < 0x1c || plan.at(col, row) >= 0x20 || plan.at(col + 1, row) < 0x20) {
      continue;
    }
    std::int32_t out_col = col;
    while (out_col > 1 && !plan.deep(out_col, row)) {
      --out_col;
    }
    if (!plan.deep(out_col, row)) {
      continue;
    }
    const auto station = plan.point(col + 1, row);
    const auto harbor = plan.point(col, row);
    const auto patrol = plan.point(out_col, row);
    BaseRecord b;
    b.name = std::format("Base {}-{}", number, bases.size() + 1);
    b.station = {station[0], 0, station[1]};
    b.harbor = {harbor[0], 0, harbor[1]};
    b.patrol = {patrol[0], 0, patrol[1]};
    b.owners[0] = {BaseSide::kJapanese, 1, 1, 1941};
    b.flags = static_cast<std::uint32_t>(BaseFlag::kActive) | static_cast<std::uint32_t>(BaseFlag::kSpawnsPatrols) |
              (bases.size() % 2 == 0 ? static_cast<std::uint32_t>(BaseFlag::kAircraft) : 0u);
    b.anchored_size = 1 + static_cast<std::int32_t>(bases.size() % 3);
    b.anchored_size_cap = 3;
    b.patrol_param_a = 2;
    b.patrol_spawn_pct = 20;
    b.patrol_param_b = 2;
    b.pick_weight = 10;
    bases.push_back(std::move(b));
  }
  return bases;
}

std::vector<ConvoyRoute> make_routes(const MapPlan& plan, int number) {
  SynthRandom random(name_hash(std::format("routes{}", number)));
  std::vector<ConvoyRoute> routes;
  const std::int32_t half = plan.cell / 2;
  for (int r = 0; r < 6; ++r) {
    const auto cells = deep_cells(plan, 4, random, 4);
    if (cells.size() < 2) {
      continue;
    }
    ConvoyRoute route;
    route.first = {1, 1, 1941};
    route.last = {12, 31, 1945};
    route.frequency.fill(10 + 4 * r);
    route.convoy_pct.fill(r == 5 ? 30 : 75);
    route.flags = r == 0 ? ConvoyRoute::kCoastal : 0;
    for (const auto& c : cells) {
      const auto p = plan.point(c[0], c[1]);
      route.waypoints.push_back({p[0] - half, 0, p[1] - half, p[0] + half, 0, p[1] + half});
    }
    routes.push_back(std::move(route));
  }
  return routes;
}

PatrolZone make_zone(int number, const std::string& routes) {
  PatrolZone z;
  z.name = std::format("Synthetic Sea {}", number);
  z.world = std::format("WORLD-{}.WLD", number);
  z.routes = routes;
  z.bases = base_file_name(number);
  z.first_year = 1941;
  z.last_year = 1945;
  z.first_month = 12;
  z.last_month = 9;
  z.year_weight.fill(5);
  z.command = static_cast<Command>(number % 3);
  const int i = number - 1;
  z.orders_rect = {20 + (i % 5) * 110, 20 + (i / 5) * 110, 100 + (i % 5) * 110, 100 + (i / 5) * 110};
  z.start_edges.fill(1);
  z.air_start_edges.fill(1);
  z.sampan_density = 8;
  z.fog_pct.fill(10);
  for (auto& m : z.weather_pct) {
    m = {40, 30, 20, 10};
  }
  for (auto& m : z.convoy_size_pct) {
    m = {40, 40, 20};
  }
  for (auto& m : z.escort_pct) {
    m = {25, 25, 25, 25};
  }
  for (auto& m : z.crew_pct) {
    m = {33, 34, 33};
  }
  z.air_cover_pct.fill(20);
  z.air_raid_pct.fill(5);
  return z;
}

// A sky: the 3D palette ranges, the sea and terrain colours, identity
// remaps, a panorama of clouds, and the sun and moon discs.
std::vector<std::uint8_t> environment_file(const FileManifest& m, int number) {
  Environment env;
  env.version = static_cast<std::uint8_t>(m.numbers.at(0));
  auto& h = env.header;
  const VgaPalette base = synthetic_palette(name_hash(m.path));
  std::ranges::copy(base, h.begin());
  const double dusk = number % 4 == 3 ? 0.6 : 1.0;
  const auto rgb = [&h](std::size_t index, double r, double g, double b) {
    h[3 * index] = six(r);
    h[3 * index + 1] = six(g);
    h[3 * index + 2] = six(b);
  };
  for (std::size_t i = 16; i < 64; ++i) {  // sky, haze and hulls
    const double t = (i - 16) / 47.0;
    rgb(i, (0.3 + 0.6 * t) * dusk, (0.4 + 0.5 * t) * dusk, (0.6 + 0.4 * t) * dusk);
  }
  for (std::size_t i = 80; i < 130; ++i) {  // the sea
    const double t = (i - 80) / 49.0;
    rgb(i, 0.05 + 0.2 * t, 0.15 + 0.35 * t, 0.35 + 0.45 * t);
  }
  h[0x304] = 4;
  for (std::size_t band = 0; band < 6; ++band) {
    for (std::size_t t = 0; t < 16; ++t) {
      h[0x308 + band * 16 + t] = static_cast<std::uint8_t>(80 + band * 8 + t / 2);
    }
  }
  for (std::size_t row = 0; row < 130; ++row) {
    h[0x368 + row] = static_cast<std::uint8_t>(row * 6 / 130);
  }
  for (std::size_t r = 0; r < 6; ++r) {
    for (std::size_t i = 0; i < 256; ++i) {
      h[0x3ea + r * 256 + i] = static_cast<std::uint8_t>(i);
    }
    const auto threshold = static_cast<std::uint32_t>(0x4000u << r);
    for (std::size_t k = 0; k < 4; ++k) {
      h[0x9ec + 4 * r + k] = static_cast<std::uint8_t>(threshold >> (8 * k));
    }
  }
  for (std::size_t i = 0; i < 3 * 4 * 30; ++i) {
    h[0xa04 + i] = static_cast<std::uint8_t>(130 + i % 30);
  }
  h[0xbe4] = 40;
  for (std::size_t b = 0; b < 6; ++b) {
    h[0xbe5 + b] = static_cast<std::uint8_t>(85 + 8 * b);
  }
  for (std::size_t s = 0; s < 3; ++s) {
    for (std::size_t c = 0; c < 30; ++c) {
      const double t = c / 29.0;
      h[0xbeb + 90 * s + 3 * c] = six(0.25 + 0.4 * t);
      h[0xbeb + 90 * s + 3 * c + 1] = six(0.35 + 0.3 * t - 0.05 * static_cast<double>(s));
      h[0xbeb + 90 * s + 3 * c + 2] = six(0.15 + 0.1 * t);
    }
  }
  for (std::size_t r = 0; r < 4; ++r) {
    for (std::size_t i = 0; i < 256; ++i) {
      h[0xd53 + r * 256 + i] = static_cast<std::uint8_t>(i);
    }
  }
  h[0x1153] = static_cast<std::uint8_t>(20 + number);
  h[0x1157] = number % 5 == 0 ? 0x81 : 0x40;
  // The panorama: sky indices 16..63, clouds lighter.
  const auto sw = static_cast<int>(m.numbers.at(1));
  const auto sh = static_cast<int>(m.numbers.at(2));
  Image8 sky(sw, sh);
  for (int y = 0; y < sh; ++y) {
    for (int x = 0; x < sw; ++x) {
      const double cloud = fractal_noise(x / 80.0, y / 20.0, name_hash(m.path), 4);
      const int c = 16 + y * 30 / std::max(1, sh) + (cloud > 0.55 ? static_cast<int>((cloud - 0.55) * 40) : 0);
      sky.set(x, y, static_cast<std::uint8_t>(std::min(63, c)));
    }
  }
  env.sky_pcx = encode_pcx(sky, base);
  const auto disc = [](auto& bitmap, int size, std::uint8_t colour) {
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const int dx = 2 * x - size + 1;
        const int dy = 2 * y - size + 1;
        bitmap[static_cast<std::size_t>(y * size + x)] = dx * dx + dy * dy <= size * size ? colour : 0;
      }
    }
  };
  disc(env.sun, 31, 63);
  env.moon_phases.resize(env.version >= 9 ? 4 : 0);
  for (auto& moon : env.moon_phases) {
    disc(moon, 33, 62);
  }
  return encode_environment(env);
}

}  // namespace

void generate_world(Context& context) {
  std::vector<PatrolZone> zones;
  std::vector<const FileManifest*> route_files = context.entries("DATA/", "size");
  std::erase_if(route_files, [](const FileManifest* f) { return !f->path.ends_with(".CR"); });
  for (int number = 1; number <= kMaps; ++number) {
    const std::string path = std::format("DATA/WORLD-{}.WLD", number);
    const MapPlan plan = make_map(context.entry(path), number);
    Model::MapInfo info{.world = path.substr(5), .deep_points = {}};
    SynthRandom random(name_hash(path));
    for (const auto& c : deep_cells(plan, 64, random, plan.world.grid.width / 8)) {
      info.deep_points.push_back(plan.point(c[0], c[1]));
    }
    context.model.maps.push_back(std::move(info));
    context.files[path] = encode_world(plan.world);
    context.files["DATA/" + base_file_name(number)] = encode_bases(make_bases(plan, number));
    const std::string routes = route_files.empty() ? std::format("ROUTES{}.CR", number)
                                                   : route_files[static_cast<std::size_t>(number - 1) %
                                                                 route_files.size()]
                                                         ->path.substr(5);
    context.files["DATA/" + routes] = encode_routes(make_routes(plan, number));
    zones.push_back(make_zone(number, routes));
  }
  context.files["DATA/PZ.DAT"] = encode_patrol_zones(zones);

  for (const FileManifest* m : context.entries("DATA/", "obs")) {
    std::vector<MapObject> objects;
    for (std::size_t i = 0; i < m->sprites.size(); ++i) {
      objects.push_back(MapObject{.render_type = static_cast<ObjectRenderType>(m->numbers.at(2 * i)),
                                  .size = static_cast<std::int32_t>(m->numbers.at(2 * i + 1)),
                                  .shp = sprite_file(m->sprites[i], name_hash(m->path) + static_cast<std::uint32_t>(i))});
    }
    context.files[m->path] = encode_objects(objects);
  }
  int number = 1;
  for (const FileManifest* m : context.entries("DATA/", "env")) {
    context.files[m->path] = environment_file(*m, number++);
  }
}

}  // namespace sh::test::synth

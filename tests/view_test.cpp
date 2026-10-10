#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <span>
#include <vector>

#include "assets/cat.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "core/fixed_math.h"
#include "game_data.h"
#include "view/polygon.h"
#include "view/render_list.h"
#include "view/world_palette.h"
#include "view/world_view.h"

namespace sh {

namespace {

std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
  std::uint32_t h = 0x811c9dc5u;
  for (const std::uint8_t b : bytes) {
    h = (h ^ b) * 0x01000193u;
  }
  return h;
}

RenderObject object_at(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t size = 46000u << 16) {
  RenderObject o;
  o.type = RenderType::kShip;
  o.x = x;
  o.y = y;
  o.z = z;
  o.size = size;
  return o;
}

// Projects `objects` for `camera` and returns the list.
RenderList project(const std::vector<RenderObject>& objects, const ViewCamera& camera) {
  RenderList list;
  for (const RenderObject& o : objects) {
    list.add(o, 0, 0, 0);
  }
  list.project_all(camera);
  return list;
}

std::array<std::uint8_t, 3> rgb(const VgaPalette& p, int i) {
  const auto at = static_cast<std::size_t>(i) * 3;
  return {p[at], p[at + 1], p[at + 2]};
}

}  // namespace

TEST_CASE("Objects project with the 920/460 focal lengths onto horizon row 130", "[view]") {
  const ViewCamera north = make_view_camera(Angle10(0), 0, 0, 0, Fixed16::from_int(1), 640);
  // Straight ahead on the sea plane at eye level: the centre of the horizon,
  // drawn 1:1 at the distance given by its size.
  const std::vector<RenderObject> ahead = {object_at(0, 0, 46000), object_at(23000, 0, 46000)};
  RenderList list = project(ahead, north);
  REQUIRE(list.order().size() == 2);
  const RenderEntry& centre = list.entries()[0];
  CHECK(centre.screen_x == 320);
  CHECK(centre.screen_y == kHorizonRow);
  CHECK(centre.distance == 46000);
  CHECK(centre.size == 0x10000);
  // Half the distance to the side: 460 px right of the centre.
  CHECK(list.entries()[1].screen_x == 320 + 460);

  // From 20 ft up the sea surface at 46000 units lies 3 rows below the horizon.
  const ViewCamera raised = make_view_camera(Angle10(0), 0, 20 * 16, 0, Fixed16::from_int(1), 640);
  CHECK(project({object_at(0, 0, 46000)}, raised).entries()[0].screen_y == kHorizonRow + 3);

  // Looking east, an object due east is centred; the scope's 201-px view
  // centres it on column 100.
  const ViewCamera east = make_view_camera(Angle10(900), 0, 0, 0, Fixed16::from_int(1), 201);
  const RenderList east_list = project({object_at(46000, 0, 0)}, east);
  const RenderEntry& e = east_list.entries()[0];
  CHECK(e.view_z == 46000);
  CHECK(e.screen_x == 100);

  // A 2x zoom halves the view distance: twice the scale and offsets.
  const ViewCamera zoomed = make_view_camera(Angle10(0), 0, 0, 0, Fixed16::from_int(2), 640);
  RenderList z = project(ahead, zoomed);
  CHECK(z.entries()[0].distance == 23000);
  CHECK(z.entries()[0].size == 0x20000);
  CHECK(z.entries()[1].screen_x == 320 + 920);
}

TEST_CASE("Render-list entries are culled at the near plane and 70 degrees off axis", "[view]") {
  const ViewCamera cam = make_view_camera(Angle10(0), 0, 0, 0, Fixed16::from_int(1), 640);
  const std::vector<RenderObject> objects = {
      object_at(0, 0, kMinEntryZ - 1),  // inside the near plane
      object_at(0, 0, kMinEntryZ),
      object_at(43493, 0, 14976),  // 71 deg to the right
      object_at(-40000, 0, 16000),  // 68 deg to the left
      object_at(0, 0, kMaxEntryZ + 1),
      object_at(0, 0, -46000),  // behind
  };
  const RenderList list = project(objects, cam);
  std::vector<std::size_t> visible(list.order().begin(), list.order().end());
  std::sort(visible.begin(), visible.end());
  CHECK(visible == std::vector<std::size_t>{1, 3});
}

TEST_CASE("The render list is ordered far to near, ties in insertion order", "[view]") {
  const ViewCamera cam = make_view_camera(Angle10(0), 0, 0, 0, Fixed16::from_int(1), 640);
  const std::vector<RenderObject> objects = {object_at(0, 0, 5000), object_at(0, 0, 20000), object_at(100, 0, 10000),
                                             object_at(0, 0, 20000), object_at(0, 0, 30000)};
  const RenderList list = project(objects, cam);
  CHECK(list.order() == std::vector<std::size_t>{4, 1, 3, 2, 0});
}

TEST_CASE("Aspect frames follow the 0x10ff54 table and mirror past the sprite's views", "[view]") {
  SH_REQUIRE_GAME_EXE();
  const ViewCamera cam = make_view_camera(Angle10(0), 0, 0, 0, Fixed16::from_int(1), 640);
  auto frame_for = [&](std::int32_t heading, std::int32_t x, std::int32_t z) {
    RenderObject o = object_at(x, 0, z);
    o.heading = Angle10(heading);
    const RenderList list = project({o}, cam);
    REQUIRE(list.order().size() == 1);
    return render_view_aspect_frame(list.entries()[0]);
  };
  // Dead ahead: heading away shows the stern, toward us the bow.
  CHECK(frame_for(0, 0, 46000) == 22);
  CHECK(frame_for(1800, 0, 46000) == 0);
  CHECK(frame_for(900, 0, 46000) == 11);   // 90 deg: frames start at 75 and 94 deg
  CHECK(frame_for(2700, 0, 46000) == 33);  // the other beam (mirrored frame 12 of 23)
  CHECK(frame_for(1790, 0, 46000) == 1);
  // 30 deg off axis the bearing is corrected by the 5 deg beyond the 25 deg cone.
  const std::int32_t bearing = fx::heading_from_delta(23000, 39837).tenths();
  REQUIRE(bearing > 250);
  const std::int32_t to_camera = fx::heading_from_delta(-23000, -39837).tenths();
  const std::int32_t corrected = fx::angle_sub(Angle10(to_camera), Angle10(bearing - 250)).tenths() / 10;
  CHECK(corrected == 205);
  CHECK(frame_for(0, 23000, 39837) == 28);  // 203 <= 205 < 211; uncorrected would be frame 29
}

TEST_CASE("Polygons fill inclusive spans clipped to the viewport", "[view]") {
  Image8 img(10, 8, 0);
  const Viewport vp = Viewport::full(img);
  const std::array<PolyVertex, 4> square = {{{2, 2, 7 << 16, 0, 0}, {6, 2, 0, 0, 0}, {6, 5, 0, 0, 0}, {2, 5, 0, 0, 0}}};
  fill_polygon(vp, square);
  int n = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 10; ++x) {
      const bool inside = x >= 2 && x <= 6 && y >= 2 && y <= 5;
      CHECK(img.at(x, y) == (inside ? 7 : 0));
      n += img.at(x, y) == 7;
    }
  }
  CHECK(n == 20);

  // Partly off the left and top edges: only the visible part is drawn.
  Image8 clipped(10, 8, 0);
  const std::array<PolyVertex, 3> tri = {{{-4, -4, 3 << 16, 0, 0}, {12, -4, 0, 0, 0}, {-4, 12, 0, 0, 0}}};
  fill_polygon(Viewport::full(clipped), tri);
  // The hypotenuse x + y = 8 starts each row at x + 0.5, truncated.
  CHECK(clipped.at(0, 0) == 3);
  CHECK(clipped.at(8, 0) == 3);
  CHECK(clipped.at(9, 0) == 0);
  CHECK(clipped.at(5, 3) == 3);
  CHECK(clipped.at(6, 3) == 0);
  CHECK(clipped.at(0, 7) == 3);

  // A textured square maps texels through the shade table.
  Image8 tex(2, 2, 0);
  tex.set(0, 0, 1);
  tex.set(1, 0, 2);
  tex.set(0, 1, 3);
  tex.set(1, 1, 4);
  std::array<std::uint8_t, 256> shade{};
  for (int i = 0; i < 256; ++i) {
    shade[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + 100);
  }
  Image8 out(4, 4, 0);
  // Texel corners at 0 and (size - 1) << 16, as the terrain passes them.
  const std::array<PolyVertex, 4> quad = {{{0, 0, 0, 0, 0}, {3, 0, 0, 0x10000, 0}, {3, 3, 0, 0x10000, 0x10000},
                                           {0, 3, 0, 0, 0x10000}}};
  texture_polygon_shaded(Viewport::full(out), quad, tex, shade);
  CHECK(out.at(0, 0) == 101);
  CHECK(out.at(3, 0) == 102);
  CHECK(out.at(0, 3) == 103);
  CHECK(out.at(3, 3) == 104);
}

TEST_CASE("The night palette darkens the world ranges to the ENV ambient minimum", "[view]") {
  SH_REQUIRE_GAME_DATA(install);
  const Environment env = parse_environment(install.read("DATA/ENV-01.ENV"), "ENV-01");
  REQUIRE(env.ambient_minimum_pct() == 24);
  const VgaPalette base = world_base_palette(env, SeaState::kCalm);
  const VgaPalette station = load_pcx(install, "ART/BRIDGE.PCX").palette;

  SECTION("full daylight leaves the world colours as they are") {
    WorldLighting light;
    light.set_sky({Angle10(1800), Angle10(450), Angle10(0), Angle10(-300), 4});
    VgaPalette pal = station;
    light.shade_screen_palette(env, base, pal);
    CHECK(light.light_percent() == 100);
    CHECK_FALSE(light.night());
    for (int i : {16, 38, 80, 100, 129, 130, 159}) {
      CHECK(rgb(pal, i) == rgb(base, i));
    }
    for (int i : {0, 15, 64, 74, 160, 255}) {
      CHECK(rgb(pal, i) == rgb(station, i));
    }
  }

  SECTION("a moonless night keeps 24 percent, the lights, fires and self-lit colours") {
    WorldLighting light;
    light.set_sky({Angle10(0), Angle10(-300), Angle10(0), Angle10(-300), 4});
    light.set_ship_lights(true);
    VgaPalette pal = station;
    const Darkening d = light.shade_screen_palette(env, base, pal);
    CHECK(d.r == 76);
    CHECK(d.g == 76);
    CHECK(d.b == 76);
    CHECK(light.light_percent() == 24);
    CHECK(light.night());
    for (int i : {16, 20, 38, 48, 63, 75, 79, 80, 100, 127, 129, 130, 159}) {
      const auto from = i >= 75 && i <= 79 ? rgb(station, i) : rgb(base, i);
      for (std::size_t c = 0; c < 3; ++c) {
        CHECK(rgb(pal, i)[c] == from[c] - from[c] * 76 / 100);
      }
    }
    for (int i = 39; i <= 43; ++i) {
      CHECK(rgb(pal, i) == rgb(base, i));  // self-lit
    }
    for (int i = 64; i <= 74; ++i) {
      CHECK(rgb(pal, i) == rgb(station, i));  // fires
    }
    for (int i = 44; i <= 47; ++i) {
      CHECK(rgb(pal, i) == std::array<std::uint8_t, 3>{63, 63, 50});  // ship lights
    }
    CHECK(rgb(pal, 128) == std::array<std::uint8_t, 3>{0x26, 0x34, 0x3e});  // night sky / stars
    for (int i : {0, 15, 160, 200, 255}) {
      CHECK(rgb(pal, i) == rgb(station, i));
    }

    // The timers then cycle colour 128 every 40 ticks and flicker 45 every 20.
    CHECK_FALSE(light.cycle_lights(pal));  // skipped right after a rebuild
    for (int t = 0; t < 40; ++t) {
      light.tick();
    }
    CHECK(light.cycle_lights(pal));
    CHECK(rgb(pal, 128) == std::array<std::uint8_t, 3>{0x36, 0x3a, 0x3d});
    CHECK(rgb(pal, 45) == std::array<std::uint8_t, 3>{60, 60, 47});
    CHECK(rgb(pal, 44) == std::array<std::uint8_t, 3>{63, 63, 50});
  }

  SECTION("a full moon high in the sky adds 24 percent where the ENV has sky bodies") {
    const Environment env2 = parse_environment(install.read("DATA/ENV-02.ENV"), "ENV-02");
    REQUIRE(env2.ambient_minimum_pct() == 12);
    WorldLighting light;
    light.set_sky({Angle10(0), Angle10(-300), Angle10(0), Angle10(300), 0});
    const Darkening d = light.update_light(env2);
    CHECK(d.r == 64);
    CHECK(light.light_percent() == 36);
    // ENV-01 is overcast (header 0x1157 = 0x81): no moon, no stars.
    light.update_light(env);
    CHECK(light.light_percent() == 24);
  }

  SECTION("the ENV-02 dusk tint keeps the reds at sunset") {
    const Environment env2 = parse_environment(install.read("DATA/ENV-02.ENV"), "ENV-02");
    WorldLighting light;
    light.set_sky({Angle10(0), Angle10(100), Angle10(0), Angle10(-300), 4});
    const Darkening d = light.update_light(env2);
    CHECK(d.r == 0);
    CHECK(d.g == 22);
    CHECK(d.b == 37);
    CHECK(light.light_percent() == 81);
  }

  SECTION("fog blends toward grey") {
    WorldLighting light;
    light.set_sky({Angle10(0), Angle10(450), Angle10(0), Angle10(-300), 4});
    light.set_fog(100);
    VgaPalette pal = station;
    light.shade_screen_palette(env, base, pal);
    for (int i : {16, 80, 129, 159}) {
      CHECK(rgb(pal, i) == std::array<std::uint8_t, 3>{63, 63, 63});
    }
  }
}

TEST_CASE("The sea drifts faster in rough seas", "[view]") {
  CHECK(sea_drift_step(0, 100) == 0xdd3);
  CHECK(sea_drift_step(1, 100) == 10 * 0xdd3);
  CHECK(sea_drift_step(2, 7) == 15 * 0xdd3 * 7 / 100);
}

TEST_CASE("A bridge view of Southern Japan renders like the original", "[view]") {
  SH_REQUIRE_GAME_DATA(install);
  const WorldViewAssets assets =
      WorldViewAssets::load(install, "DATA/ENV-01.ENV", SeaState::kCalm, "DATA/WORLD-1.WLD", "DATA/WORLD-1.OBS");
  const CatArchive cat = CatArchive::load(install, "ART/3DSHIPS.CAT");
  const Shp fuso = Shp::parse(cat.member("FUSO.SHP"), "FUSO.SHP");

  // From 20 ft looking east at the coast near the first cities of the map,
  // with the battleship Fuso 417 yd off the starboard bow.
  Scene scene;
  scene.heading = Angle10(900);
  scene.x = 18380000;
  scene.y = 20 * 16;
  scene.z = 18170000;
  RenderObject ship = object_at(scene.x + 20000, 0, scene.z - 3000, 33000u << 16);
  ship.sprite = &fuso;
  ship.heading = Angle10(300);
  ship.ship.range_yards = 417;
  ship.ship.hull_down_start_yards = 10000;
  ship.ship.hull_down_end_yards = 20000;
  ship.ship.speed_tenths = 120;
  ship.ship.bow_wave_size = 34000u << 16;
  ship.ship.mast_height_ft = 270;
  ship.ship.fire_height_ft = 45;
  ship.ship.hull_down_height_ft = 242;
  scene.objects.push_back(ship);

  WorldLighting light;
  light.set_sky({Angle10(1100), Angle10(450), Angle10(800), Angle10(200), 1});
  Image8 frame = make_world_view_frame();
  WatcomRandom random;
  const RenderList list = render_world_view(Viewport::full(frame), scene, assets, light, random);

  // The ship plus the landmarks in view, the ship nearest.
  REQUIRE_FALSE(list.order().empty());
  CHECK(list.order().back() == 0);
  const RenderEntry& e = list.entries()[0];
  CHECK(e.screen_x == 320 + 3000 * 920 / 20000);
  CHECK(e.screen_y == kHorizonRow + 320 * 460 / 20000);
  // Sky above the horizon (ENV sky colours 96-129), sea texture below
  // (the sea colours 80-95), hull colours on the ship.
  CHECK(frame.at(600, 10) >= 96);
  CHECK(frame.at(600, 10) <= 129);
  CHECK(frame.at(10, 250) >= 80);
  CHECK(frame.at(10, 250) <= 95);
  const std::uint8_t hull = frame.at(e.screen_x, e.screen_y - 10);
  CHECK(hull >= 16);
  CHECK(hull <= 63);
  // Checked by eye: written out as a PNG with the shaded BRIDGE.PCX palette,
  // the frame shows the overcast panorama, the textured sea, the town of
  // WORLD-1's first locations with the textured hills behind and Fuso with
  // her bow wave, cut at the waterline.
  CHECK(fnv1a(frame.pixels()) == 0x72386964u);

  // Her wake (ten trail points back along her course) lightens the sea
  // colours under it by up to four steps (twice where neighbouring quads
  // share an edge), here two rows near the horizon.
  RenderObject wake;
  wake.type = RenderType::kShipWake;
  wake.x = ship.x;
  wake.z = ship.z;
  wake.wake_width = 94 * 16;
  for (std::int32_t k = 10; k >= 1; --k) {
    wake.trail.push_back({ship.x - k * 750, ship.z - k * 1299});
  }
  scene.objects.push_back(wake);
  scene.heading = Angle10(1100);
  Image8 with_wake = make_world_view_frame();
  render_world_view(Viewport::full(with_wake), scene, assets, light, random);
  scene.objects.pop_back();
  Image8 without = make_world_view_frame();
  render_world_view(Viewport::full(without), scene, assets, light, random);
  int changed = 0;
  for (int y = 0; y < kWorldViewHeight; ++y) {
    for (int x = 0; x < kWorldViewWidth; ++x) {
      const std::uint8_t a = with_wake.at(x, y);
      const std::uint8_t b = without.at(x, y);
      if (a != b) {
        ++changed;
        CHECK((y >= 135 && y <= 140));
        CHECK(b >= 0x52);
        CHECK(b <= 0x5f);
        CHECK(a < b);
        CHECK(a >= 0x52);
      }
    }
  }
  CHECK(changed > 500);
}

TEST_CASE("A night periscope view shows the moon and the stars", "[view]") {
  SH_REQUIRE_GAME_DATA(install);
  const WorldViewAssets assets =
      WorldViewAssets::load(install, "DATA/ENV-02.ENV", SeaState::kModerate, "DATA/WORLD-1.WLD", "DATA/WORLD-1.OBS");
  // Periscope at 2x from 68 ft, looking north-east over the town.
  Scene scene;
  scene.heading = Angle10(600);
  scene.x = 18380000;
  scene.y = 68 * 16;
  scene.z = 18170000;
  scene.zoom = Fixed16::from_int(2);
  scene.screen_width = 201;
  scene.sea_drift = 12345;
  WorldLighting light;
  light.set_sky({Angle10(2700), Angle10(-200), Angle10(640), Angle10(600), 1});
  light.set_ship_lights(true);
  VgaPalette pal = load_pcx(install, "ART/SCOPE.PCX").palette;
  light.shade_screen_palette(assets.env, world_base_palette(assets.env, SeaState::kModerate), pal);
  REQUIRE(light.night());
  Image8 frame = make_world_view_frame();
  WatcomRandom random;
  render_world_view(Viewport::full(frame), scene, assets, light, random);
  // Nothing is drawn right of the scope's 201 columns.
  for (int y = kHorizonRow; y < kWorldViewHeight; ++y) {
    CHECK(frame.at(201, y) == 0);
  }
  // Checked by eye with the shaded SCOPE.PCX palette: the gibbous moon above
  // the dark hills, a star, the lit windows (44-47) and the crane lights.
  CHECK(fnv1a(frame.pixels()) == 0x4a9aca20u);
}

}  // namespace sh

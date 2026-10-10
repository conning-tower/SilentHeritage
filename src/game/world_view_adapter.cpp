#include "game/world_view_adapter.h"

#include "core/fixed_math.h"
#include "core/install.h"
#include "game/mission.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/depth_charge.h"
#include "sim/effects.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/line_of_sight.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/sighting.h"
#include "sim/torpedo.h"

namespace sh {

namespace {

constexpr std::uint32_t kLimitedVisibility = 0x80;  // option bit of `ship_add_render`

// The render sizes of the weapons and effects (16.16 world units).
constexpr std::uint32_t kDepthChargeSize = 15000u << 16;  // dc_queue_draw
constexpr std::uint32_t kShellSize = 4000u << 16;         // shell_queue_draw
constexpr std::uint32_t kAaRoundSize = 200u << 16;
constexpr std::uint32_t kBombSize = 2000u << 16;  // bomb_queue_draw
constexpr std::uint32_t kWakeSize = 0x10000;
constexpr std::uint32_t kRaftSize = 2000u << 16;  // pilot_queue_draw
constexpr std::uint32_t kAirGroupDotSize = 0x10000;
constexpr std::int32_t kMaxAirGroupDots = 30;

std::vector<WakePoint> trail_of(const MotionState& m) {
  std::vector<WakePoint> trail;
  const auto n = static_cast<std::size_t>(std::min(m.wake_count, MotionState::kMaxWakePoints));
  trail.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    trail.push_back({m.wake[i].x, m.wake[i].z});
  }
  return trail;
}

RenderObject wake_object(RenderType type, const fx::Vec3& position, Angle10 heading, const MotionState& m,
                         std::int32_t width, bool draw_unplaced) {
  RenderObject o;
  o.type = type;
  o.x = position.x;
  o.y = 0;
  o.z = position.z;
  o.heading = heading;
  o.size = kWakeSize;
  o.draw_unplaced = draw_unplaced;
  o.trail = trail_of(m);
  o.wake_width = width;
  return o;
}

// Returns the horizon of the boat's lookouts in yards (the `aux` output of
// `player_sighting_check`, ship +0x490): from the raised periscope or the
// bridge, 0 when the eye is under water.
std::int32_t player_horizon_yd(const PlayerSub& sub) {
  const SubClassParams& c = sub.sub();
  const std::int32_t eye_ft = sub.periscope_raised() ? c.periscope_height_ft : c.bridge_height_ft;
  const std::int32_t eye = sub.position().y + eye_ft * kUnitsPerFoot;
  if (eye <= 0) {
    return 0;
  }
  const std::int32_t ft = eye / kUnitsPerFoot;
  return sighting_compute_range(ft == 0 ? 1 : ft, 0, {}).horizon_yd;
}

}  // namespace

ViewEye bridge_eye(const PlayerSub& sub, Angle10 bearing) {
  // bridge_render_view (0x17ac6)
  const fx::Vec3 p = sub.position();
  return {fx::angle_add(sub.heading(), bearing.normalized()),
          p.x,
          sub.sub().bridge_height_ft * kUnitsPerFoot + p.y,
          p.z,
          Fixed16::from_int(1),
          kBridgeViewWidth};
}

ViewEye scope_eye(const PlayerSub& sub, Angle10 bearing, Fixed16 zoom) {
  // scope_render_view (0x6d3ca)
  const fx::Vec3 p = sub.position();
  return {fx::angle_add(sub.heading(), bearing.normalized()),
          p.x,
          sub.sub().periscope_height_ft * kUnitsPerFoot + p.y,
          p.z,
          zoom,
          kScopeViewWidth};
}

ViewEye tbt_eye(const PlayerSub& sub, Angle10 bearing, Fixed16 zoom) {
  // tbt_update (0x77b9c)
  const fx::Vec3 p = sub.position();
  return {fx::angle_add(sub.heading(), bearing.normalized()),
          p.x,
          sub.sub().bridge_height_ft * kUnitsPerFoot + p.y,
          p.z,
          zoom,
          kTbtViewWidth};
}

ViewEye deck_gun_eye(const PlayerSub& sub, const DeckGun::Mount& mount, Fixed16 zoom) {
  // deckgun_update (0x36fa4)
  return {
      fx::angle_add(sub.heading(), mount.train.normalized()), mount.x, kDeckGunEyeY, mount.z, zoom, kDeckGunViewWidth};
}

WorldViewAdapter::WorldViewAdapter(const Install& install, const Mission& mission)
    : assets_(WorldViewAssets::load(install, "DATA/" + mission.environment_file(),
                                    static_cast<SeaState>(mission.sea_picture()),
                                    "DATA/" + mission.scenario().header.world, "DATA/WORLD-1.OBS")),
      ships_cat_(CatArchive::load(install, "ART/3DSHIPS.CAT")),
      air_cat_(CatArchive::load(install, "ART/3DAIR.CAT")),
      base_palette_(world_base_palette(assets_.env, assets_.sea_state)) {
  lighting_.set_fog(mission.fog_pct());
  lighting_.set_sky(mission.sky_bodies());
  lighting_.set_ship_lights(mission.sky().ship_lights());
}

const Shp* WorldViewAdapter::ship_sprite(const std::string& name) {
  // load_ships_dat: the class sprites come from 3DSHIPS.CAT; a class
  // without a second sprite has none.
  if (name.empty() || !ships_cat_.find(name)) {
    return nullptr;
  }
  auto it = ship_sprites_.find(name);
  if (it == ship_sprites_.end()) {
    it = ship_sprites_.emplace(name, Shp::parse(ships_cat_.member(name), name)).first;
  }
  return &it->second;
}

const Shp* WorldViewAdapter::aircraft_sprite(const std::string& name) {
  // load_aircraft_dat -> load_3dair_cat: AIRCRAFT.DAT +76 names the sprite.
  if (name.empty() || !air_cat_.find(name)) {
    return nullptr;
  }
  auto it = air_sprites_.find(name);
  if (it == air_sprites_.end()) {
    it = air_sprites_.emplace(name, Shp::parse(air_cat_.member(name), name)).first;
  }
  return &it->second;
}

void WorldViewAdapter::add_hull(Scene& scene, const Mission& mission, const Hull& hull, const ViewEye& eye) {
  // ship_add_render (0x8197e) / enemy_sub_add_render (0x7fb89) and their
  // render callbacks ship_render_cb (0x81a84) / enemy_sub_render_cb (0x7fc8f)
  const ShipClass& cls = *hull.cls;
  const MotionState& m = *hull.motion;
  const fx::Vec3 p = m.position;
  std::int32_t range = hull.distance_yd;
  if ((mission.sim().conditions().options & kLimitedVisibility) != 0) {
    if (range < 0) {
      return;
    }
  } else {
    const TerrainGrid* grid = mission.sim().sensors().environment().grid;
    if (grid != nullptr && los_blocked_by_land(*grid, {eye.x, eye.z}, {p.x, p.z}, -1)) {
      return;
    }
    range = static_cast<std::int32_t>(fx::approx_distance(p.x - eye.x, p.z - eye.z) / kUnitsPerYard);
  }
  const std::int32_t horizon = player_horizon_yd(mission.player());

  RenderObject& o = scene.objects.emplace_back();
  o.type = hull.type;
  o.sprite = ship_sprite(cls.sprite1);
  o.x = p.x;
  o.y = p.y;
  o.z = p.z;
  o.heading = m.heading;
  o.size = cls.render_size;
  o.draw_unplaced = true;
  ShipLook& look = o.ship;
  look.sinking_sprite = ship_sprite(cls.sprite2);
  look.fire_order = cls.fire_order;
  look.range_yards = range;
  look.hull_down_start_yards = horizon;
  look.hull_down_end_yards = hull.player_range_yd;
  look.role = static_cast<std::uint8_t>(cls.role);
  look.speed_tenths = m.speed_tenths;
  look.bow_wave_size = cls.bow_wave_size;
  look.mast_height_ft = static_cast<std::int32_t>(cls.visual_size);
  look.fire_height_ft = static_cast<std::int32_t>(cls.deck_height_ft);
  look.hull_down_height_ft = hull.hull_down_height_ft;
  const ShipEffects& fx = *hull.effects;
  look.fx.wave_phase = fx.bow_wave_frame;
  look.fx.on_fire = fx.burning;
  look.fx.fire_frame = fx.fire_frame;
  look.fx.sinking = fx.sinking;
  look.fx.blast_frame = fx.explosion_frame;
  look.fx.blast_sprite = fx.explosion_sprite;

  if (options_.ship_wakes && range <= horizon) {
    scene.objects.push_back(
        wake_object(RenderType::kShipWake, p, m.heading, m, static_cast<std::int32_t>(cls.beam_ft) << 4, true));
  }
}

void WorldViewAdapter::add_ship(Scene& scene, const Mission& mission, const Ship& ship, const ViewEye& eye) {
  const ShipClass* cls = ship.setup().ship_class;
  if (cls == nullptr) {
    return;
  }
  const ShipAi& ai = ship.ai();
  // The hull-down height: SHIPS.DAT +0xbc - +0xb8 (mast above the keel).
  add_hull(scene, mission,
           {cls, &ship.motion(), &ship.combat().effects, ai.player_distance_yd, ai.player_range_yd,
            static_cast<std::int32_t>(cls->visual_size) - static_cast<std::int32_t>(cls->draft_ft), RenderType::kShip},
           eye);
}

void WorldViewAdapter::add_enemy_sub(Scene& scene, const Mission& mission, const EnemySub& sub, const ViewEye& eye) {
  const ShipClass* cls = sub.setup().ship_class;
  if (cls == nullptr) {
    return;
  }
  // enemy_sub_render_cb passes SHIPS.DAT +0xbc alone as the hull-down height.
  add_hull(scene, mission,
           {cls, &sub.motion(), &sub.combat().effects, sub.player_distance_yd(), sub.player_range_yd(),
            static_cast<std::int32_t>(cls->visual_size), RenderType::kEnemySub},
           eye);
}

Scene WorldViewAdapter::build_scene(const Mission& mission, const ViewEye& eye, WatcomRandom& random) {
  Scene scene;
  scene.heading = eye.heading;
  scene.x = eye.x;
  scene.y = eye.y;
  scene.z = eye.z;
  scene.zoom = eye.zoom;
  scene.screen_width = eye.screen_width;
  scene.detail = options_.detail;
  scene.sea_drift = sea_drift_;
  // veh_simulate_frame adds every live vehicle in list order (slot +0x14).
  for (const auto& v : mission.sim().vehicles()) {
    if (v->removed()) {
      continue;
    }
    switch (v->class_id()) {
      case VehicleClass::kPlayer: {
        // player_add_wake (0x5c0fa): surfaced only, kept to its cells.
        const auto& sub = static_cast<const PlayerSub&>(*v);
        if (options_.ship_wakes && sub.position().y + sub.sub().bridge_height_ft * kUnitsPerFoot > 0) {
          scene.objects.push_back(wake_object(RenderType::kShipWake, sub.position(), sub.heading(), sub.motion(),
                                              sub.sub().beam_ft << 4, false));
        }
        break;
      }
      case VehicleClass::kShip:
        add_ship(scene, mission, static_cast<const Ship&>(*v), eye);
        break;
      case VehicleClass::kTorpedo: {
        // torpedo_add_wake_render (0x85be3): no wake behind electric ones.
        const auto& t = static_cast<const TorpedoVehicle&>(*v);
        if (options_.torpedo_wakes && t.has_wake()) {
          scene.objects.push_back(wake_object(RenderType::kTorpedoWake, t.position(), t.heading(), t.motion(),
                                              static_cast<std::int32_t>(t.weapon().beam_ft) << 4, true));
        }
        break;
      }
      case VehicleClass::kDepthCharge: {
        // dc_queue_draw (0x8658c): in the air, or its splash.
        const auto& dc = static_cast<const DepthCharge&>(*v);
        const fx::Vec3 p = dc.position();
        if (p.y > 0 || dc.splash_frame() >= 0) {
          RenderObject& o = scene.objects.emplace_back();
          o.type = RenderType::kDepthCharge;
          o.sprite = &assets_.effect(EffectSprite::kDepthCharge);
          o.x = p.x;
          o.y = dc.splash_frame() < 0 ? p.y : 0;
          o.z = p.z;
          o.heading = dc.flight_heading();
          o.size = kDepthChargeSize;
          o.frame = dc.splash_frame() >= 0 ? dc.splash_frame() : -1;
        }
        break;
      }
      case VehicleClass::kShell: {
        // shell_queue_draw (0x86f28)
        const auto& s = static_cast<const Shell&>(*v);
        RenderObject& o = scene.objects.emplace_back();
        o.type = RenderType::kShell;
        o.sprite = &assets_.effect(EffectSprite::kShell);
        const fx::Vec3 p = s.position();
        o.x = p.x;
        o.y = p.y;
        o.z = p.z;
        o.heading = s.flight_heading();
        o.size = s.caliber() == 0 ? kAaRoundSize : kShellSize;
        break;
      }
      case VehicleClass::kBomb: {
        // bomb_queue_draw (0x87a4d)
        const auto& b = static_cast<const Bomb&>(*v);
        const fx::Vec3 p = b.position();
        if (p.y > 0 || b.splash_frame() >= 0) {
          RenderObject& o = scene.objects.emplace_back();
          o.type = RenderType::kBomb;
          o.sprite = &assets_.effect(EffectSprite::kBomb);
          o.x = p.x;
          o.y = b.splash_frame() < 0 ? p.y : 0;
          o.z = p.z;
          o.heading = b.flight_heading();
          o.size = kBombSize;
          o.frame = b.splash_frame() >= 0 ? b.splash_frame() : -1;
        }
        break;
      }
      case VehicleClass::kSplash: {
        // splash_queue_draw (0x8820d)
        const auto& s = static_cast<const Splash&>(*v);
        RenderObject& o = scene.objects.emplace_back();
        o.type = RenderType::kSplash;
        o.sprite = &assets_.effect(EffectSprite::kSpout);
        const fx::Vec3 p = s.position();
        o.x = p.x;
        o.y = p.y;
        o.z = p.z;
        o.size = static_cast<std::uint32_t>(s.size()) << 16;
        o.frame = s.frame();
        break;
      }
      case VehicleClass::kExplosion: {
        // explosion_fx_queue_draw (0x885b2): GUNFIRE, GUNEXPL or EXPL-1.
        const auto& e = static_cast<const ExplosionFx&>(*v);
        RenderObject& o = scene.objects.emplace_back();
        o.type = RenderType::kExplosion;
        const fx::Vec3 p = e.position();
        o.x = p.x;
        o.y = p.y;
        o.z = p.z;
        switch (e.kind()) {
          case ExplosionKind::kMuzzleFlash:
            o.sprite = &assets_.effect(EffectSprite::kGunFire);
            o.size = 15000u << 16;
            break;
          case ExplosionKind::kShellHit:
            o.sprite = &assets_.effect(EffectSprite::kGunExplosion);
            o.size = 10000u << 16;
            break;
          case ExplosionKind::kBomb:
            o.sprite = &assets_.effect(EffectSprite::kExplosion1);
            o.size = 20000u << 16;
            break;
        }
        o.frame = e.frame();
        break;
      }
      case VehicleClass::kEnemySub:
        add_enemy_sub(scene, mission, static_cast<const EnemySub&>(*v), eye);
        break;
      case VehicleClass::kAircraft: {
        // aircraft_add_render (0x84871): airborne and sighted.
        const auto& a = static_cast<const Aircraft&>(*v);
        const AircraftType* type = a.setup().type;
        if (a.airborne() && a.sighted() && type != nullptr) {
          const AircraftFlight& f = a.flight();
          RenderObject& o = scene.objects.emplace_back();
          o.type = RenderType::kAircraft;
          o.sprite = aircraft_sprite(type->sprite);
          o.x = f.position.x;
          o.y = f.position.y;
          o.z = f.position.z;
          o.heading = f.heading;
          o.size = type->render_size;
          o.pitch = f.pitch;
          o.roll = f.roll;
        }
        break;
      }
      case VehicleClass::kBomberGroup: {
        // airgroup_queue_draw (0x8aa75): up to 30 dots in a formation drawn
        // from the group's seed; the game's generator is then reseeded with
        // the value it drew first.
        const auto& g = static_cast<const AirGroup&>(*v);
        if (!g.sighted()) {
          break;
        }
        const auto saved = static_cast<std::uint32_t>(random.rand());
        random.srand(g.formation_seed());
        std::int32_t left = random.random_int(5) + 5;
        std::int32_t spread = 700;
        const std::int32_t dots = std::min(g.plane_count(), kMaxAirGroupDots);
        const fx::Vec3 p = g.position();
        for (std::int32_t i = 0; i < dots; ++i) {
          const std::int32_t dx = random.random_int(700);
          const std::int32_t dz = random.random_int(700);
          const std::int32_t dy = random.random_int(500);
          RenderObject& o = scene.objects.emplace_back();
          o.type = RenderType::kAirGroup;
          o.x = p.x + (dx + spread) * 16;
          o.y = dy * 16 + p.y;
          o.z = p.z + (dz + spread) * 16;
          o.heading = g.heading();
          o.size = kAirGroupDotSize;
          if (--left == 0) {
            left = random.random_int(5) + 5;
            spread += random.random_int(600) + 400;
          }
        }
        random.srand(saved);
        break;
      }
      case VehicleClass::kDownedPilot: {
        // pilot_queue_draw (0x892e0): RAFT.SHP while visible.
        const auto& pilot = static_cast<const DownedPilot&>(*v);
        if (pilot.sighted()) {
          RenderObject& o = scene.objects.emplace_back();
          o.type = RenderType::kRaft;
          o.sprite = &assets_.effect(EffectSprite::kRaft);
          const fx::Vec3 p = pilot.position();
          o.x = p.x;
          o.y = p.y;
          o.z = p.z;
          o.heading = pilot.heading();
          o.size = kRaftSize;
        }
        break;
      }
      case VehicleClass::kSmoke: {
        // smoke_queue_draw (0x8b852): SMOKE.SHP as a static object growing
        // from 2000 to 6000 units over its half second.
        const auto& smoke = static_cast<const Smoke&>(*v);
        const std::int32_t grown = (0x8000 - smoke.lifetime()) * 4000 / 0x8000;
        RenderObject& o = scene.objects.emplace_back();
        o.type = RenderType::kStaticObject;
        o.sprite = &assets_.effect(EffectSprite::kSmoke);
        const fx::Vec3 p = smoke.position();
        o.x = p.x;
        o.y = p.y;
        o.z = p.z;
        o.size = static_cast<std::uint32_t>(grown + 2000) << 16;
        break;
      }
      default:
        // Blast noises, debris and the air reports draw nothing.
        break;
    }
  }
  return scene;
}

void WorldViewAdapter::advance(std::int32_t timer_ticks, std::int32_t sea_state) {
  // The per-frame update 0x7b394 drifts the sea; the timer interrupt runs
  // world_tick_light_cycle at 100 Hz.
  sea_drift_ += sea_drift_step(sea_state, timer_ticks);
  for (std::int32_t i = 0; i < timer_ticks; ++i) {
    lighting_.tick();
  }
}

bool WorldViewAdapter::update_sky(Mission& mission) {
  Sky& sky = mission.sky();
  lighting_.set_sky(mission.sky_bodies());
  lighting_.set_ship_lights(sky.ship_lights());
  lighting_.set_fog(sky.fog_pct());
  return sky.take_palette_dirty();
}

RenderList WorldViewAdapter::render(const Mission& mission, const ViewEye& eye, WatcomRandom& random) {
  scene_ = build_scene(mission, eye, random);
  const std::uint32_t shift = assets_.world ? assets_.world->scale_shift : 0;
  camera_ = make_view_camera(eye.heading, eye.x, eye.y, eye.z, eye.zoom, eye.screen_width, shift);
  frame_.fill(0);
  return render_world_view(Viewport::full(frame_), scene_, assets_, lighting_, random);
}

Darkening WorldViewAdapter::shade_palette(VgaPalette& palette) {
  return lighting_.shade_screen_palette(assets_.env, base_palette_, palette);
}

std::optional<int> WorldViewAdapter::project_x(const ViewEye& eye, std::int32_t x, std::int32_t z) const {
  // tdc_draw_target_marker (0x7c60e): the point relative to the camera
  // through the zoomed view matrix.
  const std::uint32_t shift = assets_.world ? assets_.world->scale_shift : 0;
  const ViewCamera cam = make_view_camera(eye.heading, eye.x, eye.y, eye.z, eye.zoom, eye.screen_width, shift);
  const fx::Vec3 v = fx::transform(cam.view, {x - cam.x, 0, z - cam.z});
  if (v.z < kNearZ) {
    return std::nullopt;
  }
  return fx::imul(v.x, kFocalX) / v.z + cam.half_width;
}

std::optional<std::array<int, 2>> WorldViewAdapter::project_point(const ViewEye& eye, const fx::Vec3& p) const {
  // player_draw_recon_target_marker (0x60e17)
  const std::uint32_t shift = assets_.world ? assets_.world->scale_shift : 0;
  const ViewCamera cam = make_view_camera(eye.heading, eye.x, eye.y, eye.z, eye.zoom, eye.screen_width, shift);
  const fx::Vec3 v = fx::transform(cam.view, {p.x - cam.x, p.y - cam.y, p.z - cam.z});
  if (v.z < kNearZ) {
    return std::nullopt;
  }
  return std::array<int, 2>{fx::imul(v.x, kFocalX) / v.z + cam.half_width,
                            fx::imul(v.y, kFocalY) / v.z + kHorizonRow};
}

}  // namespace sh

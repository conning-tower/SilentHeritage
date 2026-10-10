// The short-lived effect vehicles the weapons spawn: water splashes
// (class 9), gun flashes and explosions (class 10) and underwater blast
// noises (class 11), the boat's jettisoned debris (class 12), with the
// helpers the weapons share (the player boat, sound volumes by distance,
// `ai_broadcast_contact`).
//
// Splashes and explosions live for one run of their animation (a frame
// every 0.05 s), so their lifetime is the frame count of their sprite.
#pragma once

#include <array>
#include <cstdint>

#include "core/fixed.h"
#include "core/fixed_math.h"
#include "sim/vehicle.h"

namespace sh {

class Install;
class PlayerSub;

// Frame counts of the effect sprites (`veh_load_effect_sprites`,
// 0x129d08..): the defaults are those of the original files.
struct EffectFrames {
  std::int32_t spout = 30;    // SPOUT.SHP, splashes.
  std::int32_t gunfire = 10;  // GUNFIRE.SHP, explosion kind 0 (muzzle flash).
  std::int32_t gunexpl = 9;   // GUNEXPL.SHP, kind 1 (shell hit).
  std::int32_t expl = 30;     // EXPL-1.SHP, kind 2 (bomb).
  std::int32_t fire = 30;     // FIRE-0.SHP, burning ships.
  std::array<std::int32_t, 2> sinking_expl = {17, 30};  // EXPL-0/1.SHP, a ship blowing up.

  // Reads the counts from the sprites of `install`.
  static EffectFrames load(const Install& install);
};

// The kinds of explosion (+0xe4), which pick the sprite and sound.
enum class ExplosionKind : std::int32_t {
  kMuzzleFlash = 0,
  kShellHit = 1,
  kBomb = 2,
};

// A water splash (class 9) at (x, 0, z).
class Splash : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // `play_sound`: the spout sound when the player is at the surface.
  Splash(std::int32_t x, std::int32_t z, bool play_sound, std::int32_t frame_count);

  VehicleClass class_id() const override { return VehicleClass::kSplash; }
  // `splash_init` (0x88097).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `splash_update` (0x8817c).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }

  std::int32_t frame() const { return frame_; }
  // +0xd4: the 0.15 s periods since it rose.
  std::int32_t ticks() const { return ticks_; }
  // +0xec: the drawn size (10000, a twentieth for machine-gun rounds).
  std::int32_t size() const { return size_; }
  void set_size(std::int32_t size) { size_ = size; }

 private:
  fx::Vec3 position_;           // +0xd8
  bool play_sound_;
  std::int32_t frame_count_;
  std::int32_t frame_ = 0;      // +0xe4
  std::int32_t frame_timer_ = 0;  // +0xe8
  std::int32_t tick_timer_ = 0;   // +0xd0
  std::int32_t ticks_ = 0;        // +0xd4, counts 0.15 s periods.
  std::int32_t size_ = 10000;     // +0xec
};

// A gun flash or explosion (class 10).
class ExplosionFx : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  ExplosionFx(const fx::Vec3& position, ExplosionKind kind, std::int32_t frame_count);

  VehicleClass class_id() const override { return VehicleClass::kExplosion; }
  // Plays sfx 0x22 (flash) or 0x23 by distance (`explosion_fx_init`).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `explosion_fx_update` (0x8854a).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }

  // Slot +0xd4: the only class whose noise masks the escorts' sonar; its
  // noise level stays the default -1, so it never does (docs/structures.md "Bugs").
  bool masks_sonar() const override { return true; }

  ExplosionKind kind() const { return kind_; }
  std::int32_t frame() const { return frame_; }

 private:
  fx::Vec3 position_;  // +0xd0
  ExplosionKind kind_; // +0xe4
  std::int32_t frame_count_;
  std::int32_t frame_ = 0;        // +0xdc
  std::int32_t frame_timer_ = 0;  // +0xe0
};

// The noise of an underwater blast (class 11), heard for 180 s.
class UnderwaterBlast : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  explicit UnderwaterBlast(const fx::Vec3& position) : position_(position) {}

  VehicleClass class_id() const override { return VehicleClass::kBlastNoise; }
  // `uw_blast_init` (0x887f3).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `uw_blast_update` (0x88851).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }
  // Slot +0x6c: loud, but it does not mask the sonar (slot +0xd4 stays
  // false, docs/structures.md "Bugs in the original").
  std::int32_t noise_level() const override { return 200; }

 private:
  fx::Vec3 position_;            // +0xd0
  std::int32_t lifetime_ = 0;    // +0xdc, 16.16 s.
};

// Debris jettisoned by the submerged boat (class 12, the 1MC "Jettison
// debris"), floating at the surface for 1500 s. The escorts and aircraft
// that see it may take it for a sinking boat (sim/ai_ship.h); the 3D view
// does not draw it (slot +0x14 is a no-op).
class Debris : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  explicit Debris(const fx::Vec3& position) : position_(position) {}

  VehicleClass class_id() const override { return VehicleClass::kDebris; }
  // `debris_init` (0x88963).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `debris_update` (0x889c0): the lifetime's countdown.
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }
  // Slot +0x6c: heard, but silent.
  std::int32_t noise_level() const override { return 0; }
  bool is_contact() const override { return true; }
  // Slot +0x8c: 4 ft above the sea.
  std::int32_t visual_height() const override { return 0x40; }
  bool is_debris() const override { return true; }

  // +0xdc: the time left, 16.16 s.
  std::int32_t lifetime() const { return lifetime_; }

 private:
  fx::Vec3 position_;          // +0xd0
  std::int32_t lifetime_ = 0;  // +0xdc
};

// Spawns debris at the player boat's (x, z) on the surface
// (`cmd_jettison_debris`, 0x610ad); returns it.
Debris& spawn_debris(SimContext& ctx, const fx::Vec3& boat);

// Returns the player boat of the context's simulation, or null.
PlayerSub* context_player(SimContext& ctx);
// Returns the effect frame counts of the context's mission.
EffectFrames context_effect_frames(SimContext& ctx);

// Returns the horizontal distance in yards from the player boat to (x, z)
// (0 without a player).
std::int32_t player_range_yards(SimContext& ctx, std::int32_t x, std::int32_t z);

// Spawns a splash at (x, z) (the `splash_construct` + `splash_init`
// sequence of the weapons); returns it.
Splash& spawn_splash(SimContext& ctx, std::int32_t x, std::int32_t z, bool play_sound);
// Spawns an explosion; returns it.
ExplosionFx& spawn_explosion(SimContext& ctx, const fx::Vec3& position, ExplosionKind kind);

// Tells every vehicle of a noise or sighting at `position`
// (`ai_broadcast_contact`, 0x14beb).
void broadcast_contact(SimContext& ctx, const fx::Vec3& position, Vehicle* target);

}  // namespace sh

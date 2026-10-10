// The editor's scenario objects (docs/editor-structures.md "Editor objects"):
// one class per original class with the vtable slots that do not draw
// (position, type, convoy queries, special-mission flag, info text,
// visibility from the player, memory cost, 3D model names, `.SCN` record),
// and the reference-counted 3D-model cache of the memory estimate.
//
// Each object keeps its `.SCN` record whole (as the original embeds it at
// +8), so loading and saving copy it unchanged; the toolbox edits the
// public records directly. Drawing and hit testing (slots +0x08 / +0x0c)
// belong to the chart screen.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "assets/scn_file.h"
#include "editor/data.h"
#include "sim/sighting.h"

namespace sh::editor {

// The object kinds (vtable slot +0x10).
enum class ObjectType : std::int32_t {
  kPlayer = 0,
  kShip = 1,
  kSubmarine = 2,
  kAircraft = 3,
  kAirstrike = 4,
  kPhotoRecon = 5,
};

// A world position (`x` east, `y` north, as in `.SCN` records).
struct WorldPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;

  bool operator==(const WorldPoint&) const = default;
};

// The hover-popup text of `describe`: 6 lines of up to 75 characters.
inline constexpr std::size_t kDescribeLines = 6;
using DescribeText = std::array<std::string, kDescribeLines>;

// What the object slots read besides the object itself.
struct ObjectEnv {
  const EditorData& data;
  const WorldData& world;
  // Whether the chart is in click mode 0 (select / move): the player's
  // text is empty in the other modes.
  bool select_mode = true;
};

// What the visibility slot (+0x28) reads: the observer and the scenario's
// conditions.
struct DetectionEnv {
  const EditorData& data;
  const TerrainGrid* grid = nullptr;  // land hides targets when given
  const ScnPlayerSub& player;
  // The scenario weather (0xaf8a4), fog (0xf6228) and light (0xf6224).
  SightingConditions conditions;
  std::uint32_t equipment = 0;  // 0xaf930, for the surface radar range
};

// The surface-radar range of the player's fit in yards
// (`sb_player_radar_range_yd`, 0x443a1): 0 without one, 15000 (SJa) or
// 35000 (SJ-1).
std::int32_t player_radar_range_yd(std::uint32_t equipment);

// Returns `value` as `digits` zero-padded decimal digits, negative values
// as zeros (`sb_format_int_padded`, 0x43ced).
std::string format_padded(std::int32_t value, int digits);

// Returns a latitude or longitude as "DD\x1aMM\x1bH" (`sb_format_latlon`,
// 0x43c45; glyphs 0x1a / 0x1b are the degree and minute marks).
std::string format_latlon(const SeExe& se, const GeoAngle& angle);

// strcpy into a record's char field: the text (cut to fit) and its NUL,
// the bytes after it left as they are.
template <std::size_t N>
void set_scn_text(ScnChars<N>& field, std::string_view text) {
  const std::size_t n = std::min(text.size(), N - 1);
  std::copy_n(text.begin(), n, field.begin());
  field[n] = '\0';
}

// An editor object {+0 next, +4 vtable, +8 record}.
class EditorObject {
 public:
  virtual ~EditorObject() = default;

  // +0x00: the world position.
  virtual WorldPoint position(const ObjectEnv& env) const = 0;
  // +0x10.
  virtual ObjectType type() const = 0;
  // +0x14: "PLAYER" .. "PHOTO-RECON".
  std::string_view type_name(const SeExe& se) const;
  // +0x18: the convoy id (1-8) of a Convoy Leader ship (`leader`) or a
  // Convoy Member ship (not `leader`), else 0.
  virtual std::int32_t convoy_group(bool leader) const;
  // +0x1c: the convoy an escort ship or an escorting aircraft guards, else 0.
  virtual std::int32_t escorted_group() const;
  // +0x20: whether the object is an airstrike or a photo recon (the
  // writer's `special_mission`).
  virtual bool is_special_mission() const;
  // +0x24: the hover-popup text.
  virtual DescribeText describe(const ObjectEnv& env) const = 0;
  // +0x28: recomputes the visibility from the player (vessels only).
  virtual void update_detection(const DetectionEnv& env);
  // +0x2c: the sighting range against the player in yards (vessels only).
  virtual std::int32_t visual_range() const;
  // +0x30: whether the player sees the object (not `radar`) or has it in
  // its surface-radar range (`radar`), from the last `update_detection`.
  virtual bool in_detection_range(bool radar) const;
  // +0x34: the bytes the object adds to the memory estimate.
  virtual std::int32_t memory_cost() const = 0;
  // +0x38 / +0x3c: the 3D model files of the memory estimate (empty: none).
  virtual std::string_view model_name1() const;
  virtual std::string_view model_name2() const;
  // +0x40: the `.SCN` record.
  virtual ScnRecord record() const = 0;
};

// The player's submarine (`sbplayer`, always object 0).
class PlayerSub final : public EditorObject {
 public:
  explicit PlayerSub(const ScnPlayerSub& rec) : rec(rec) {}

  WorldPoint position(const ObjectEnv&) const override { return {rec.x, rec.y}; }
  ObjectType type() const override { return ObjectType::kPlayer; }
  DescribeText describe(const ObjectEnv& env) const override;
  std::int32_t memory_cost() const override { return 0; }
  ScnRecord record() const override { return rec; }

  // The model half of `sbplayer_move_to` (0x3ca10), once the chart has
  // checked the point (inside the map margin, water deeper than the draft
  // + 1 ft): the depth is kept above the bottom (`water_depth_ft` - 1).
  void move_to(WorldPoint p, std::int32_t water_depth_ft);

  ScnPlayerSub rec;
};

// A ship or enemy submarine (`sbvessel`): both records are kept and the
// kind says which one is live, so a class change between a surface class
// and a submarine class switches the kind and keeps what both share.
class Vessel final : public EditorObject {
 public:
  enum class Kind : std::int32_t { kShip = 0, kSubmarine = 1 };

  WorldPoint position(const ObjectEnv&) const override;
  ObjectType type() const override { return kind == Kind::kShip ? ObjectType::kShip : ObjectType::kSubmarine; }
  std::int32_t convoy_group(bool leader) const override;
  std::int32_t escorted_group() const override;
  DescribeText describe(const ObjectEnv& env) const override;
  void update_detection(const DetectionEnv& env) override;
  std::int32_t visual_range() const override { return sighting_range_yd_; }
  bool in_detection_range(bool radar) const override { return radar ? in_radar_range_ : seen_; }
  std::int32_t memory_cost() const override { return kind == Kind::kShip ? 1300 : 3000; }
  std::string_view model_name1() const override { return kind == Kind::kShip ? ship_model1 : sub_model1; }
  std::string_view model_name2() const override { return kind == Kind::kShip ? ship_model2 : sub_model2; }
  ScnRecord record() const override;

  // The live part's class name, orders and course.
  std::string class_name() const;
  ShipOrders orders() const { return kind == Kind::kShip ? ship.orders : sub.orders; }
  Waypoints& waypoints() { return kind == Kind::kShip ? ship.waypoints : sub.waypoints; }
  const Waypoints& waypoints() const { return kind == Kind::kShip ? ship.waypoints : sub.waypoints; }

  // The toolbox fields that write both parts.
  void set_heading(std::int32_t heading);
  void set_speed(std::int32_t speed);
  void set_crew(CrewQuality crew);
  void set_damage(std::int32_t damage_pct);
  // The model half of `sbvessel_move_to` (0x3cb34), once the chart has
  // checked the point: both parts move with their waypoint 0 (when the live
  // course has points); a submarine stays above the bottom.
  void move_to(WorldPoint p, std::int32_t water_depth_ft);

  Kind kind = Kind::kShip;
  ScnShip ship;
  ScnEnemySub sub;
  // The SHIPS.DAT sprite names of each part (+0x14c / +0x16a ship,
  // +0x2a8 / +0x2c6 sub).
  std::string ship_model1;
  std::string ship_model2;
  std::string sub_model1;
  std::string sub_model2;

 private:
  bool seen_ = false;               // +0xc
  bool in_radar_range_ = false;     // +0x10
  std::int32_t sighting_range_yd_ = 0;  // +0x14
};

// An aircraft (`sbair`).
class Aircraft final : public EditorObject {
 public:
  WorldPoint position(const ObjectEnv&) const override { return {rec.x, rec.y}; }
  ObjectType type() const override { return ObjectType::kAircraft; }
  std::int32_t escorted_group() const override;
  DescribeText describe(const ObjectEnv& env) const override;
  std::int32_t memory_cost() const override { return 900; }
  std::string_view model_name1() const override { return model; }
  ScnRecord record() const override { return rec; }

  // `sbair_move_to` (0x3ccfb): the position and the path points move by the
  // same offset.
  void move_to(WorldPoint p);

  ScnAircraft rec;
  std::string model;  // the AIRCRAFT.DAT sprite (+0x114)
};

// An airstrike (tag 5) or a photo-recon mission (tag 6) (`sbmission`).
class SpecialMission final : public EditorObject {
 public:
  using Record = std::variant<BomberGroupRecord, PhotoReconRecord>;

  explicit SpecialMission(const Record& rec) : rec(rec) {}

  // The target base's chart point; (0, 0) without a valid base (the
  // original reads outside the base table, its icon is not drawn then).
  WorldPoint position(const ObjectEnv& env) const override;
  ObjectType type() const override;
  bool is_special_mission() const override { return true; }
  DescribeText describe(const ObjectEnv& env) const override;
  std::int32_t memory_cost() const override;
  ScnRecord record() const override;

  // The target base index (-1 until a city is picked).
  std::int32_t base() const;

  Record rec;
};

// The reference-counted 3D-model cache 0xae754 (130 entries): the memory
// estimate counts each model file once, whatever the number of objects
// using it.
class ModelCache {
 public:
  static constexpr std::size_t kEntries = 130;

  // `model_cache_add` (0x4629f): returns the bytes to add to the estimate,
  // the model's catalogue size the first time, else 0 (also for a model no
  // catalogue holds, or a full cache).
  std::int32_t add(std::string_view name, const ModelCatalog& catalog);
  // `model_cache_release` (0x463cf): returns the bytes to subtract, the
  // size when the last reference goes, else 0.
  std::int32_t release(std::string_view name);
  // `model_cache_clear` (0x463f4).
  void clear();
  // Returns the references to `name` (0 when not cached).
  std::int32_t references(std::string_view name) const;

 private:
  struct Entry {
    std::string name;
    std::int32_t count = 0;  // free when 0
    std::int32_t size = 0;
  };
  Entry* find(std::string_view name);

  std::array<Entry, kEntries> entries_{};
};

}  // namespace sh::editor

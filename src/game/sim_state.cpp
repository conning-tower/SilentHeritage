#include "game/sim_state.h"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <unordered_map>

#include "core/error.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/mission_rules.h"
#include "game/mission_screen.h"
#include "game/patrol.h"
#include "game/radio_room.h"
#include "game/stations/attack_services.h"
#include "game/stations/cabin.h"
#include "game/stations/chart.h"
#include "game/stations/gauges.h"
#include "game/vehicle_state.h"
#include "sim/air_sorties.h"
#include "sim/aircraft_strike.h"
#include "sim/clock.h"
#include "sim/player_combat.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "sim/sky.h"
#include "sim/state_blocks.h"
#include "sim/torpedo.h"
#include "sim/weather.h"

namespace sh {

namespace {

// The ranges of `player_write_state` (0x61b35) in the block from 0x11a9c4:
// motion, dive, route x/z, index, count, 0x11abfc, the collision warning
// and enemies-near flags, the name, 0x11ac27, crew, follow route,
// periscope, view bearing, zoom, helm, telegraph, the diesel-mode byte,
// 0x200 bytes from the engines (overlapping what follows), then fuel ..
// the deck guns, the silent-running flag and the radio page.
constexpr std::array<StateRange, 7> kPlayerRanges = {{
    {0x000, 0x144}, {0x144, 0x24}, {0x168, 0xdc}, {0x244, 0x40}, {0x284, 0x200}, {0x2c4, 0x488}, {0x750, 4},
}};
// `tdc_write_state` (0x7d7ec) in the block from 0x129a20: the target id
// 0x129b7c, then 0x129a24..0x129b7b.
constexpr std::uint32_t kTdcSize = 0x180;
constexpr std::array<StateRange, 2> kTdcRanges = {{{0x15c, 4}, {0x04, 0x158}}};
// `deckgun_write_state` (0x38963) in the block from 0x11c188: 0x11c188..
// 0x11c1bf, 0x11c1c8..0x11c1df, then the target id 0x11c1c0.
constexpr std::uint32_t kDeckGunSize = 0x58;
constexpr std::array<StateRange, 3> kDeckGunRanges = {{{0x00, 0x38}, {0x40, 0x18}, {0x38, 4}}};
// `contact_group_write_base` (0x2e36a); the kinds' own fields follow from
// +0x138 to the end of the object.
constexpr std::array<StateRange, 5> kGroupBaseRanges = {
    {{0x04, 0xc}, {0x10, 0xc}, {0x1c, 0xf0}, {0x10c, 0x20}, {0x130, 4}}};
constexpr std::size_t kGroupWaypoints = 20;

constexpr std::array<std::uint8_t, 4> kExtensionMagic = {'S', 'H', 'R', 'X'};
constexpr std::uint32_t kExtensionVersion = 1;
constexpr std::uint32_t kSimSection = 0x204d4953;  // "SIM "
constexpr std::uint32_t kUiSection = 0x20204955;   // "UI  "
constexpr std::uint32_t kRuleSection = 0x454c5552;  // "RULE"

std::vector<StateRange> group_ranges(ContactGroupKind kind) {
  std::vector<StateRange> r(kGroupBaseRanges.begin(), kGroupBaseRanges.end());
  r.push_back({0x138, contact_group_size(kind) - 0x138});
  return r;
}

// ---------------------------------------------------------------- records

template <class Io, class A>
void bind_log_angle(Io& io, std::uint32_t off, A& a) {
  io.i32(off, a.degrees);
  io.i32(off + 4, a.minutes);
  auto hemi = static_cast<std::uint8_t>(a.hemisphere);
  io.u8(off + 8, hemi);
  if constexpr (Io::kReading) {
    a.hemisphere = static_cast<char>(hemi);
  }
}

// A patrol-log node (0x4b bytes, `player_log_event` 0x5e594).
template <class Io, class E>
void bind_log_entry(Io& io, E& e) {
  io.i32(0x04, e.day);
  io.i32(0x08, e.month);
  io.i32(0x0c, e.year);
  io.i32(0x10, e.hour);
  io.i32(0x14, e.minute);
  bind_log_angle(io, 0x18, e.latitude);
  bind_log_angle(io, 0x21, e.longitude);
  io.i32(0x2a, e.type);
  io.string(0x2e, 0x19, e.name);
  io.i32(0x47, e.tonnage);
}

// A radio message (0xa0 bytes of the lists 0x11b118 / 0x11b11c).
template <class Io, class M>
void bind_radio_message(Io& io, M& m) {
  io.flag(0x04, m.unread);
  io.i32(0x08, m.day);
  io.i32(0x0c, m.month);
  io.i32(0x10, m.year);
  io.i32(0x14, m.hour);
  io.i32(0x18, m.minute);
  io.i32(0x1c, m.template_index);
  for (std::uint32_t i = 0; i < 10; ++i) {
    io.i32(0x20 + i * 4, m.args[i]);
  }
  io.string(0x48, 0x23, m.to);
  io.string(0x6b, 0x23, m.from);
  bind_log_angle(io, 0x8e, m.latitude);
  bind_log_angle(io, 0x97, m.longitude);
}

// A route state (0x1c bytes of 0x1191e8).
template <class Io, class R>
void bind_route_state(Io& io, R& r) {
  io.flag(0x00, r.active);
  io.i32(0x04, r.frequency);
  io.i32(0x08, r.spawn_timer);
  io.i32(0x0c, r.convoy_pct);
  io.u8(0x10, r.flags);
  io.i32(0x14, r.alert);
  io.i32(0x18, r.alert_timer);
}

template <class Io, class P>
void bind_group_point(Io& io, std::uint32_t off, P& p) {
  io.i32(off, p.x);
  io.i32(off + 4, p.y);
  io.i32(off + 8, p.z);
}

// The chart contact of a group from `off` (the route groups' +0x168): the
// flag, position, heading, size, time and date, timer and symbol frame.
template <class Io>
void bind_group_report(Io& io, std::uint32_t off, HqReport& r) {
  io.flag(off, r.valid);
  io.i32(off + 0x04, r.x);
  io.i32(off + 0x08, r.z);
  io.i32(off + 0x0c, r.heading);
  io.i32(off + 0x10, r.size);
  io.i32(off + 0x14, r.hour);
  io.i32(off + 0x18, r.minute);
  io.i32(off + 0x1c, r.month);
  io.i32(off + 0x20, r.day);
  io.i32(off + 0x24, r.timer);
  io.i32(off + 0x28, r.symbol_frame);
}

// A contact group (the `*_write` vtable slot +8 of its kind).
template <class Io>
void bind_group(Io& io, ContactGroup& g) {
  bind_group_point(io, 0x04, g.position);
  bind_group_point(io, 0x10, g.target);
  auto count = static_cast<std::int32_t>(std::min(g.waypoints.size(), kGroupWaypoints));
  io.i32(0x110, count);
  if constexpr (Io::kReading) {
    g.waypoints.resize(static_cast<std::size_t>(std::clamp(count, 0, static_cast<std::int32_t>(kGroupWaypoints))));
  }
  for (std::size_t i = 0; i < std::min(g.waypoints.size(), kGroupWaypoints); ++i) {
    bind_group_point(io, static_cast<std::uint32_t>(0x1c + i * 12), g.waypoints[i]);
  }
  io.i32(0x10c, g.waypoint_index);
  io.i32(0x114, g.heading);
  io.i32(0x118, g.speed);
  io.flag(0x11c, g.finished);
  io.flag(0x120, g.materialized);
  io.flag(0x124, g.seen);
  io.flag(0x128, g.engaged);
  io.flag(0x130, g.near);
  switch (g.kind) {
    case ContactGroupKind::kRoute:
      io.i32(0x138, g.route);
      io.enumeration(0x13c, g.route_kind);
      io.i32(0x140, g.size);
      io.i32(0x144, g.warship_type);
      io.i32(0x148, g.escort);
      io.i32(0x14c, g.crew);
      io.flag(0x150, g.fast_or_intercept);
      io.flag(0x154, g.coastal_or_submerged);
      io.i32(0x158, g.aircraft);
      io.u32(0x15c, g.seed);
      io.i32(0x160, g.spawned);
      io.flag(0x164, g.alert_counted);
      bind_group_report(io, 0x168, g.report);
      io.i32(0x194, g.report.accuracy);
      break;
    case ContactGroupKind::kPatrol:
      if constexpr (!Io::kReading) {
        io.i32(0x138, g.base);
      }
      io.i32(0x13c, g.patrol_size);
      io.i32(0x140, g.crew_quality);
      io.flag(0x144, g.patrol_tight);
      io.u32(0x148, g.seed);
      io.i32(0x14c, g.spawned);
      io.flag(0x150, g.engaged_counted);
      bind_group_report(io, 0x154, g.report);
      io.i32(0x184, g.base);
      break;
    case ContactGroupKind::kAnchored:
      io.i32(0x138, g.merchant_pct);
      io.i32(0x13c, g.anchored_size);
      io.i32(0x140, g.guard_size);
      if constexpr (!Io::kReading) {
        io.i32(0x144, g.heading);
      }
      io.u32(0x148, g.seed);
      io.i32(0x14c, g.spawned);
      bind_group_report(io, 0x150, g.report);
      io.i32(0x180, g.base);
      break;
    case ContactGroupKind::kSampans:
      io.i32(0x138, g.sampan_size);
      io.u32(0x13c, g.seed);
      io.i32(0x140, g.spawned);
      bind_group_report(io, 0x144, g.report);
      break;
  }
  // +0x12c is not saved: the original's reader leaves it as constructed.
  io.x_flag(g.blocked);
}

// The conditions but the mode bits 0-1 of the options word, which come
// from how the mission was started (a saved mission has bit 1).
template <class Io, class C>
void bind_conditions(Io& io, C& c) {
  io.x_i32(c.sea_state);
  std::uint32_t options = c.options & ~3u;
  io.x_u32(options);
  if constexpr (Io::kReading) {
    c.options = (options & ~3u) | (c.options & 3u);
  }
  io.x_flag(c.speech);
  io.x_i32(c.weather);
  io.x_i32(c.fog);
  io.x_i32(c.thermal_layer_ft);
  io.x_i32(c.scenario_roll);
}

template <class Io, class B>
void bind_blips(Io& io, B& blips) {
  bind_x_vector(io, blips, [](Io& x, auto& b) {
    x.x_i32(b.source);
    x.x_angle(b.bearing);
    x.x_i32(b.range_yd);
    x.x_i32(b.strength);
    x.x_i32(b.age);
    x.x_i32(b.random);
  });
}

// ---------------------------------------------------------------- the extension

void write_section(ByteWriter& out, std::uint32_t tag, const std::vector<std::uint8_t>& data) {
  out.u32(tag);
  out.u32(static_cast<std::uint32_t>(data.size()));
  out.bytes(data);
}

}  // namespace

std::uint32_t contact_group_size(ContactGroupKind kind) {
  switch (kind) {
    case ContactGroupKind::kRoute:
      return 0x198;
    case ContactGroupKind::kPatrol:
      return 0x188;
    case ContactGroupKind::kAnchored:
      return 0x184;
    case ContactGroupKind::kSampans:
      return 0x174;
  }
  throw DataError("state file: unknown contact group kind " + std::to_string(static_cast<std::int32_t>(kind)));
}

// ---------------------------------------------------------------- the format

SimStateFile SimStateFile::parse(ByteReader& in, std::int32_t version, bool log_only) {
  // sim_read_state (0x1873b) with player_read_state (0x61188).
  SimStateFile f;
  // The radio page (the last range) and the radio messages follow from
  // version 3.
  read_ranges(in, f.player, std::span(kPlayerRanges).first(kPlayerRanges.size() - 1));
  if (version > 2) {
    read_ranges(in, f.player, std::span(kPlayerRanges).last(1));
    const std::span<const std::uint8_t> buffer = in.bytes(kRadioBufferSize);
    const std::int64_t waiting = in.i32();
    const std::int64_t received = in.i32();
    if (waiting < 0 || received < 0 ||
        static_cast<std::uint64_t>(waiting + received) * kRadioRecordSize > kRadioBufferSize) {
      in.fail("radio message counts out of range");
    }
    for (std::int32_t i = 0; i < waiting + received; ++i) {
      StateImage m(kRadioRecordSize);
      const auto at = static_cast<std::size_t>(i) * kRadioRecordSize;
      std::ranges::copy(buffer.subspan(at, kRadioRecordSize), m.bytes(0, kRadioRecordSize).begin());
      (i < waiting ? f.radio_waiting : f.radio_log).push_back(std::move(m));
    }
  }
  if (version > 6) {
    SavedStationUi u;
    u.compartment = in.i32();
    u.status_page = in.i32();
    u.gauges_page = in.i32();
    u.lower_panel = in.u8();
    u.tdc_panel = in.i32();
    u.station = in.i32();
    u.id_book_page = in.i32();
    u.log_page = in.i32();
    u.sj_mode = in.i32();
    u.sj_beam = in.i32();
    u.sj_scale = in.i32();
    u.sj_hold = in.i32();
    u.sj_nearest = in.i32();
    u.sd_on = in.i32();
    u.sd_scale = in.i32();
    u.sd_nearest = in.i32();
    u.aa_manned = in.i32();
    u.cabin_page = in.i32();
    u.chart_panel = in.i32();
    for (std::int32_t& v : u.chart_panel_xy) {
      v = in.i32();
    }
    u.chart_x = in.i32();
    u.chart_z = in.i32();
    u.chart_zoom = in.i32();
    for (std::int32_t& v : u.swell) {
      v = in.i32();
    }
    u.rig_for_red = in.i32();
    f.ui = u;
  }
  const std::int32_t log_count = in.i32();
  for (std::int32_t i = 0; i < log_count; ++i) {
    StateImage e(kPatrolLogRecordSize);
    read_ranges(in, e, std::array<StateRange, 1>{{{0, kPatrolLogRecordSize}}});
    f.patrol_log.push_back(std::move(e));
  }
  if (log_only) {
    return f;
  }
  if (version > 6) {
    f.tdc = StateImage(kTdcSize);
    read_ranges(in, *f.tdc, kTdcRanges);
    f.deck_gun = StateImage(kDeckGunSize);
    read_ranges(in, *f.deck_gun, kDeckGunRanges);
  }
  const std::int32_t routes = in.i32();
  for (std::int32_t i = 0; i < routes; ++i) {
    StateImage r(kRouteStateSize);
    read_ranges(in, r, std::array<StateRange, 1>{{{0, kRouteStateSize}}});
    f.routes.push_back(std::move(r));
  }
  const std::int32_t groups = in.i32();
  for (std::int32_t i = 0; i < groups; ++i) {
    Group g;
    g.kind = static_cast<ContactGroupKind>(in.i32());
    g.image = StateImage(contact_group_size(g.kind));
    read_ranges(in, g.image, group_ranges(g.kind));
    f.groups.push_back(std::move(g));
  }
  if (version > 3) {
    const std::int32_t vehicles = in.i32();
    for (std::int32_t i = 0; i < vehicles; ++i) {
      Unit u;
      u.cls = static_cast<VehicleClass>(in.i32());
      const VehicleStateLayout& layout = vehicle_state_layout(u.cls);
      u.image = StateImage(layout.image_size());
      read_ranges(in, u.image, layout.ranges);
      f.vehicles.push_back(std::move(u));
    }
  }
  // The reimplementation's extension, when it follows.
  if (in.remaining() >= 8 && std::ranges::equal(in.bytes(4), kExtensionMagic)) {
    if (in.u32() != kExtensionVersion) {
      in.fail("unknown state extension version");
    }
    while (!in.at_end()) {
      const std::uint32_t tag = in.u32();
      const std::uint32_t size = in.u32();
      const std::span<const std::uint8_t> data = in.bytes(size);
      if (tag == kSimSection) {
        f.sim_extension.assign(data.begin(), data.end());
      } else if (tag == kUiSection) {
        f.ui_extension.assign(data.begin(), data.end());
      } else if (tag == kRuleSection) {
        f.rules_extension.assign(data.begin(), data.end());
      }
    }
  }
  return f;
}

std::vector<std::uint8_t> SimStateFile::serialize() const {
  // sim_write_state (0x19061) with player_write_state (0x61b35).
  ByteWriter out;
  write_ranges(out, player, kPlayerRanges);
  // The radio buffer holds 150 records; the original overflows it beyond
  // (here the newest messages are left out).
  std::vector<std::uint8_t> buffer(kRadioBufferSize, 0);
  const std::size_t capacity = kRadioBufferSize / kRadioRecordSize;
  const std::size_t waiting = std::min(radio_waiting.size(), capacity);
  const std::size_t received = std::min(radio_log.size(), capacity - waiting);
  for (std::size_t i = 0; i < waiting + received; ++i) {
    const StateImage& m = i < waiting ? radio_waiting[i] : radio_log[i - waiting];
    std::ranges::copy(m.bytes(0, kRadioRecordSize), buffer.begin() + static_cast<std::ptrdiff_t>(i * kRadioRecordSize));
  }
  out.bytes(buffer);
  out.i32(static_cast<std::int32_t>(waiting));
  out.i32(static_cast<std::int32_t>(received));
  const SavedStationUi u = ui.value_or(SavedStationUi{});
  out.i32(u.compartment);
  out.i32(u.status_page);
  out.i32(u.gauges_page);
  out.u8(u.lower_panel);
  out.i32(u.tdc_panel);
  out.i32(u.station);
  out.i32(u.id_book_page);
  out.i32(u.log_page);
  out.i32(u.sj_mode);
  out.i32(u.sj_beam);
  out.i32(u.sj_scale);
  out.i32(u.sj_hold);
  out.i32(u.sj_nearest);
  out.i32(u.sd_on);
  out.i32(u.sd_scale);
  out.i32(u.sd_nearest);
  out.i32(u.aa_manned);
  out.i32(u.cabin_page);
  out.i32(u.chart_panel);
  for (const std::int32_t v : u.chart_panel_xy) {
    out.i32(v);
  }
  out.i32(u.chart_x);
  out.i32(u.chart_z);
  out.i32(u.chart_zoom);
  for (const std::int32_t v : u.swell) {
    out.i32(v);
  }
  out.i32(u.rig_for_red);
  out.i32(static_cast<std::int32_t>(patrol_log.size()));
  for (const StateImage& e : patrol_log) {
    out.bytes(e.bytes(0, kPatrolLogRecordSize));
  }
  write_ranges(out, tdc.value_or(StateImage(kTdcSize)), kTdcRanges);
  write_ranges(out, deck_gun.value_or(StateImage(kDeckGunSize)), kDeckGunRanges);
  out.i32(static_cast<std::int32_t>(routes.size()));
  for (const StateImage& r : routes) {
    out.bytes(r.bytes(0, kRouteStateSize));
  }
  out.i32(static_cast<std::int32_t>(groups.size()));
  for (const Group& g : groups) {
    out.i32(static_cast<std::int32_t>(g.kind));
    write_ranges(out, g.image, group_ranges(g.kind));
  }
  out.i32(static_cast<std::int32_t>(vehicles.size()));
  for (const Unit& v : vehicles) {
    out.i32(static_cast<std::int32_t>(v.cls));
    write_ranges(out, v.image, vehicle_state_layout(v.cls).ranges);
  }
  if (!sim_extension.empty() || !ui_extension.empty() || !rules_extension.empty()) {
    out.bytes(kExtensionMagic);
    out.u32(kExtensionVersion);
    write_section(out, kSimSection, sim_extension);
    write_section(out, kUiSection, ui_extension);
    // Not written for the original's rules: the faithful variant's files
    // stay as they were.
    if (!rules_extension.empty()) {
      write_section(out, kRuleSection, rules_extension);
    }
  }
  return out.take();
}

std::vector<PatrolLogEntry> saved_patrol_log(const SimStateFile& file) {
  std::vector<PatrolLogEntry> entries;
  VehicleLinks links;
  for (const StateImage& image : file.patrol_log) {
    StateIn in(image, nullptr, links);
    PatrolLogEntry e;
    bind_log_entry(in, e);
    entries.push_back(std::move(e));
  }
  return entries;
}

// ---------------------------------------------------------------- the bindings

template <class Io>
void StateIo::bind(Io& io, PlayerSub& p) {
  // The boat's part of the player block (offsets from 0x11a9c4).
  bind_motion(io, 0x000, p.motion_);
  bind_dive(io, 0x144, p.dive_);
  bind_route(io, 0x168, 0x1cc, 0x234, p.route_);
  bind_route_index(io, 0x230, p.route_index_);
  io.flag(0x26b, p.follow_route_);
  io.flag(0x26f, p.periscope_raised_);
  io.i32(0x27b, p.helm_);
  io.i32(0x27f, p.telegraph_);
  auto diesel = static_cast<std::uint8_t>(p.diesel_mode_ ? 1 : 0);
  io.u8(0x283, diesel);
  // The engines: four 16-byte records, the motor then the diesel; byte +1
  // bit 2 marks a part not fitted.
  for (std::uint32_t room = 0; room < 4; ++room) {
    for (std::uint32_t part = 0; part < 2; ++part) {
      EnginePart& e = p.engines_[room][part];
      const std::uint32_t off = 0x284 + room * 16 + part * 8;
      io.u8(off, e.flags);
      auto byte1 = static_cast<std::uint8_t>(e.fitted ? 0 : 4);
      io.u8(off + 1, byte1);
      io.i32(off + 4, e.power_pct);
      if constexpr (Io::kReading) {
        e.fitted = (byte1 & 4) == 0;
      }
    }
  }
  io.i32(0x2c4, p.fuel_);
  io.i32(0x2c8, p.fuel_fraction_);
  io.i32(0x2cc, p.battery_);
  io.i32(0x2d0, p.battery_fraction_);
  for (std::uint32_t i = 0; i < 24; ++i) {
    SystemState& s = p.systems_[i];
    io.i32(0x43c + i * 12, s.severity);
    io.i32(0x440 + i * 12, s.repair_time);
    io.u8(0x444 + i * 12, s.flags);
  }
  io.i32(0x710, p.air_max_);
  io.i32(0x714, p.air_);
  io.i32(0x718, p.air_fraction_);
  if constexpr (!Io::kReading) {
    io.flag(0x748, p.silent_running());
  }
  if constexpr (Io::kReading) {
    p.diesel_mode_ = diesel != 0;
    // init_restore_saved_mission: the engines are set up again at the next
    // update, the helm is not announced.
    p.applied_telegraph_ = -1;
    p.announced_helm_ = p.helm_;
  }
  io.x_i32(p.applied_telegraph_);
  io.x_i32(p.applied_mode_);
  for (std::uint16_t& f : p.applied_flags_) {
    std::int32_t v = f;
    io.x_i32(v);
    f = static_cast<std::uint16_t>(v);
  }
  io.x_i32(p.announced_helm_);
}

template <class Io>
void StateIo::bind(Io& io, PlayerDamage& d) {
  for (std::uint32_t i = 0; i < PlayerDamage::kCompartments; ++i) {
    CompartmentState& c = d.compartments_[i];
    const std::uint32_t off = 0x55c + i * 20;
    io.i32(off, c.water);
    io.i32(off + 4, c.fire_timer);
    io.i32(off + 8, c.leak);
    io.i32(off + 0xc, c.repair_timer);
    std::int32_t flags = c.flags;
    io.i32(off + 0x10, flags);
    c.flags = static_cast<std::uint16_t>(flags);
  }
  io.i32(0x700, d.hull_damage_);
  io.i32(0x704, d.crush_depth_);
  io.i32(0x708, d.hull_pct_);
  io.i32(0x70c, d.damage_pct_);
  io.i32(0x71c, d.seepage_);
  io.x_i32(d.elapsed_);
  io.x_i32(d.last_depth_);
}

template <class Io>
void StateIo::bind(Io& io, Tdc& t) {
  // tdc_write_state (0x7d7ec), offsets from 0x129a20.
  io.ref(0x15c, t.target_);
  io.i32(0x04, t.sighting_range_);
  io.flag(0x08, t.blink_);
  io.enumeration(0x0c, t.mode_);
  io.flag(0x10, t.locked_);
  io.flag(0x14, t.follow_bearing_);
  io.angle(0x18, t.gyro_[0]);
  io.angle(0x1c, t.gyro_[1]);
  io.i32(0x20, t.spread_);
  io.i32(0x24, t.quality_);
  io.flag(0x28, t.tracking_);
  io.angle(0x2c, t.bearing_);
  io.angle(0x30, t.course_);
  io.i32(0x34, t.range_yd_);
  io.i32(0x38, t.speed_tenths_);
  io.fixed(0x3c, t.speed_);
  io.i32(0x40, t.target_x_);
  io.i32(0x44, t.target_z_);
  io.i64(0x48, t.target_x16_);
  io.i64(0x50, t.target_z16_);
  io.i32(0x58, t.depth_ft_);
  io.i32(0x5c, t.speed_setting_);
  io.i32(0x60, t.type_index_);
  io.i32(0x64, t.solution_ticks_);
  io.i32(0x68, t.blink_ticks_);
  io.i32(0x6c, t.timer_ticks_);
  io.i32(0x70, t.run_time_[0]);
  io.i32(0x74, t.run_time_[1]);
  for (std::uint32_t i = 0; i < Tdc::kTrackedTorpedoes; ++i) {
    io.i32(0x78 + i * 4, t.countdown_[i]);
    io.ref(0xe8 + i * 4, t.tracked_[i]);
  }
}

template <class Io>
void StateIo::bind(Io& io, DeckGun& g) {
  // deckgun_write_state (0x38963), offsets from 0x11c188.
  io.i32(0x04, g.ticks_);
  io.i32(0x08, g.blink_ticks_);
  io.flag(0x0c, g.blink_);
  io.i32(0x14, g.range_yd_);
  io.i32(0x18, g.quality_);
  for (std::uint32_t m = 0; m < 2; ++m) {
    io.flag(0x1c + m * 4, g.mounts_[m].ready);
    io.i32(0x24 + m * 4, g.mounts_[m].x);
    io.i32(0x2c + m * 4, g.mounts_[m].z);
  }
  io.ref(0x38, g.target_);
  io.flag(0x40, g.locked_);
  io.flag(0x44, g.follow_);
  io.i32(0x4c, g.sighting_range_);
  io.i32(0x50, g.selected_);
  io.flag(0x54, g.manual_);
  io.x_i32(g.swell);
}

template <class Io>
void StateIo::bind(Io& io, Sensors& s) {
  io.x_i32(s.frame_counter_);
  io.x_i32(s.quiet_time_);
  io.x_i32(s.env_.light_pct);
  io.x_i32(s.env_.fog_pct);
  bind(io, s.reports_);
  RadarSets& r = s.radar_;
  bind_blips(io, r.sj().blips);
  io.x_flag(r.sj().auto_armed);
  io.x_flag(r.sj().last_auto);
  io.x_i32(r.sj().power_timer);
  bind_blips(io, r.sd().blips);
  io.x_flag(r.sd().auto_armed);
  io.x_flag(r.sd().last_auto);
  io.x_i32(r.sd().power_timer);
  io.x_i32(r.reports().sj_bearing);
  io.x_i32(r.reports().sd_range);
  io.x_i32(r.reports().sj_timer);
  io.x_i32(r.reports().sd_timer);
}

template <class Io>
void StateIo::bind(Io& io, ContactReports& r) {
  io.x_i32(r.tdc_target_range_);
  io.x_i32(r.gun_target_range_);
  io.x_ref(r.nearest_aircraft_);
  io.x_ref(r.rescue_pilot_);
  io.x_flag(r.under_attack_);
  io.x_flag(r.torpedo_warning_);
  io.x_flag(r.group_activity_);
  io.x_flag(r.activity_);
  io.x_i32(r.sd_hold_);
  io.x_i32(r.aircraft_call_);
  io.x_i32(r.spare_timer_);
  io.x_i32(r.torpedo_call_);
  io.x_i32(r.ship_repeat_);
  io.x_i32(r.aircraft_repeat_);
  io.x_i32(r.pilot_repeat_);
  io.x_i32(r.sub_repeat_);
}

template <class Io>
void StateIo::bind(Io& io, GameClock& c) {
  io.x_i32(c.frame_ticks_);
  io.x_i32(c.hundredths_);
  io.x_i32(c.second_);
  io.x_i32(c.minute_);
  io.x_i32(c.hour_);
  io.x_i32(c.day_);
  io.x_i32(c.month_);
  io.x_i32(c.year_);
  io.x_i32(c.weekday_);
  io.x_i32(c.effective_);
  io.x_i32(c.requested_);
  io.x_i32(c.cap_);
  io.x_i32(c.minutes_elapsed_);
  io.x_i32(c.load_ticks_);
  io.x_i32(c.adjust_countdown_);
  io.x_flag(c.force_increase_);
  io.x_flag(c.auto_adjust_);
}

template <class Io>
void StateIo::bind(Io& io, Sky& s) {
  io.x_i32(s.observer_x_);
  io.x_i32(s.observer_z_);
  io.x_angle(s.state_.sun_azimuth);
  io.x_angle(s.state_.sun_elevation);
  io.x_angle(s.state_.moon_azimuth);
  io.x_angle(s.state_.moon_elevation);
  io.x_i32(s.state_.moon_phase);
  for (RiseSetTimes* t : {&s.sun_times_, &s.moon_times_}) {
    io.x_i32(t->rise_hour);
    io.x_i32(t->rise_minute);
    io.x_i32(t->set_hour);
    io.x_i32(t->set_minute);
  }
  io.x_flag(s.ship_lights_);
  io.x_flag(s.palette_dirty_);
  io.x_i32(s.fog_day_);
  io.x_i32(s.daily_fog_);
  io.x_i32(s.fog_pct_);
  io.x_i32(s.fog_density_);
  io.x_enum(s.fog_state_);
  io.x_i32(s.fog_lift_);
  io.x_i32(s.fog_return_);
}

template <class Io>
void StateIo::bind(Io& io, AirSorties& a) {
  io.x_i32(a.range_yd_);
  io.x_i32(a.light_);
  io.x_i32(a.fog_);
  io.x_i32(a.hundredths_);
}

template <class Io>
void StateIo::bind(Io& io, Simulation& s) {
  std::uint32_t seed = s.random_.state();
  io.x_u32(seed);
  if constexpr (Io::kReading) {
    s.random_.srand(seed);
  }
  io.x_i32(s.pending_hundredths_);
  bind_conditions(io, s.env_);
  bind(io, s.clock_);
  bind(io, s.sensors_);
}

template <class Io>
void StateIo::bind(Io& io, Mission& m) {
  // The player block's mission fields: the collision warning and the
  // enemies-near flag, the name, crew and view bearing.
  bool warning = m.sim_->sensors().collision_warning();
  io.flag(0x23c, warning);
  if constexpr (Io::kReading) {
    m.sim_->sensors().set_collision_warning(warning);
  }
  io.flag(0x240, m.sim_->sensors().reports_.enemies_near_);
  io.string(0x244, 0x1f, m.player_name_);
  io.i32(0x267, m.player_crew_);
  if constexpr (Io::kReading) {
    m.combat_->crew = m.player_crew_;
    m.sim_->sensors().environment().crew = m.player_crew_;
  }
  io.angle(0x273, m.combat_->view_bearing);
  bind(io, *m.player_);
  bind(io, m.combat_->damage);
  // The tubes and racks.
  TorpedoTubes& tubes = m.combat_->tubes;
  for (std::uint32_t i = 0; i < TorpedoTubes::kMaxTubes; ++i) {
    std::int32_t content = tubes.content(i);
    std::int32_t timer = tubes.timer(i);
    io.i32(0x2d4 + i * 4, content);
    io.i32(0x2fc + i * 4, timer);
    TubeDamage& d = tubes.damage(i);
    io.i32(0x324 + i * 12, d.severity);
    io.i32(0x328 + i * 12, d.repair_time);
    std::int32_t flags = d.flags;
    io.i32(0x32c + i * 12, flags);
    if constexpr (Io::kReading) {
      tubes.set_tube(i, content);
      tubes.set_timer(i, timer);
      d.flags = static_cast<std::uint16_t>(flags);
    }
  }
  for (auto [rack, off] : {std::pair{&tubes.bow_rack(), 0x39cu}, std::pair{&tubes.stern_rack(), 0x3ecu}}) {
    for (std::uint32_t i = 0; i < std::min<std::size_t>(rack->size(), 20); ++i) {
      io.i32(off + i * 4, (*rack)[i]);
    }
  }
  io.x_flag(tubes.moved_by_hand);
  // 0x124ed0, which player_write_state leaves out (the manned flag is in
  // the UI block).
  io.x_i32(m.combat_->aa_guns.fire_timer);
  // The deck guns' mounts and ammunition.
  DeckGun& gun = m.combat_->deck_gun;
  for (std::uint32_t g = 0; g < 2; ++g) {
    DeckGun::Mount& mount = gun.mounts_[g];
    io.angle(0x724 + g * 4, mount.train);
    io.angle(0x72c + g * 4, mount.elevation);
    io.angle(0x734 + g * 4, mount.elevation_offset);
    io.i32(0x740 + g * 4, mount.reload);
  }
  io.i32(0x73c, gun.ammo_);
}

template <class Io>
void StateIo::bind(Io& io, PatrolWeather& w) {
  for (PatrolWeatherState* s : {&w.state_, &w.saved_}) {
    io.x_i32(s->days_left);
    io.x_i32(s->weather);
    io.x_i32(s->sea_state);
    io.x_i32(s->fog);
    io.x_i32(s->thermal_layer_ft);
  }
  io.x_i32(w.daily_fog_);
  io.x_i32(w.last_day_);
  io.x_i32(w.saved_fog_pct_);
}

template <class Io>
void StateIo::bind(Io& io, ContactGroups& g) {
  io.x_i32(g.last_day_);
  io.x_i32(g.last_month_);
  io.x_i32(g.move_acc_);
  io.x_i32(g.route_acc_);
  io.x_i32(g.bases_day_);
  io.x_flag(g.force_);
  for (std::int32_t& v : g.materialized_) {
    io.x_i32(v);
  }
  for (std::int32_t& v : g.visual_sizes_) {
    io.x_i32(v);
  }
  for (std::uint8_t& v : g.warship_mix_) {
    std::int32_t x = v;
    io.x_i32(x);
    v = static_cast<std::uint8_t>(x);
  }
  GeneratorParams& p = g.params_;
  io.x_string(p.sub_class);
  for (std::int32_t* v : {&p.day, &p.month, &p.year, &p.time, &p.weather, &p.sea, &p.convoy_size, &p.escort,
                          &p.warship_type, &p.enemy, &p.air_cover, &p.fog, &p.patrol_size, &p.crew,
                          &p.special_mission}) {
    io.x_i32(*v);
  }
}

template <class Io>
void StateIo::bind(Io& io, Patrol& p) {
  bind(io, p.weather_);
  for (Patrol::AirMission* a : {&p.raid_, &p.recon_}) {
    io.x_i32(a->hundredths);
    io.x_i32(a->last_day);
    io.x_flag(a->due);
    io.x_i32(a->timer);
    io.x_i32(a->hours);
  }
  io.x_i32(p.last_fuel_);
  if (p.groups_) {
    bind(io, *p.groups_);
  }
}

template <class Io>
void StateIo::bind(Io& io, RadioRoom& r) {
  // The messages delivered in the last frame, not yet in the radio log.
  bind_x_vector(io, r.delivered_, [](Io& x, RadioMessage& m) {
    x.x_flag(m.unread);
    for (std::int32_t* v : {&m.day, &m.month, &m.year, &m.hour, &m.minute, &m.template_index}) {
      x.x_i32(*v);
    }
    for (std::int32_t& a : m.args) {
      x.x_i32(a);
    }
    x.x_string(m.to);
    x.x_string(m.from);
    for (LogAngle* a : {&m.latitude, &m.longitude}) {
      x.x_i32(a->degrees);
      x.x_i32(a->minutes);
      std::int32_t hemisphere = a->hemisphere;
      x.x_i32(hemisphere);
      a->hemisphere = static_cast<char>(hemisphere);
    }
  });
}

template <class Io>
void StateIo::bind(Io& io, ControlBar& b) {
  io.x_i32(b.message_state_);
  io.x_i32(b.message_skip_);
  io.x_i32(b.end_reason_);
}

template <class Io>
void StateIo::bind(Io& io, CabinStation& c) {
  io.x_i32(c.log_page_);
  io.x_i32(c.radio_page_);
}

template <class Io>
void StateIo::bind(Io& io, MissionScreen& s) {
  // The session's fields of the player block: the deepest depth of the
  // dive (0x11b0e4) and the radio log page (0x11b114).
  io.i32(0x720, s.dive_recorder_.deepest_);
  if (s.cabin_ != nullptr) {
    io.i32(0x750, s.cabin_->radio_page_);
  }
}

// ---------------------------------------------------------------- capture

SimStateFile StateIo::capture(Mission& mission, const SimStateParts& parts, const SessionStateParts& session) {
  // sim_write_state (0x19061): every vehicle's +0xc4 becomes its index.
  SimStateFile f;
  Simulation& sim = *mission.sim_;
  const VehicleIds ids(sim.vehicles_);
  ByteWriter ext;
  ByteWriter no_ext;
  {
    StateOut out(f.player, ext, ids);
    bind(out, mission);
    if (session.screen != nullptr) {
      bind(out, *session.screen);
    }
  }
  // The UI globals: the radar controls and the swell are the mission's.
  SavedStationUi ui;
  const RadarSets& radar = sim.sensors_.radar_;
  ui.sj_mode = static_cast<std::int32_t>(radar.sj().mode);
  ui.sj_beam = radar.sj().beam;
  ui.sj_scale = radar.sj().scale;
  ui.sj_hold = radar.sj().hold ? kFlagOn : 0;
  ui.sj_nearest = radar.sj().nearest_yd;
  ui.sd_on = radar.sd().on ? kFlagOn : 0;
  ui.sd_scale = radar.sd().scale;
  ui.sd_nearest = radar.sd().nearest_yd;
  ui.aa_manned = mission.combat_->aa_guns.manned ? kFlagOn : 0;
  const PlayerSub& player = *mission.player_;
  ui.swell = {player.swell_.bob, player.swell_.direction, player.swell_.offset, player.swell_.amplitude,
              player.swell_.rate};
  if (MissionScreen* screen = session.screen; screen != nullptr) {
    const MissionUiState& u = screen->ui_;
    ui.compartment = u.selected_compartment;
    ui.status_page = u.status_page;
    ui.gauges_page = u.gauges_page;
    ui.station = static_cast<std::int32_t>(screen->stations_.current());
    ui.id_book_page = u.id_book_page;
    ui.cabin_page = u.cabin_page;
    ui.rig_for_red = u.rig_for_red ? kFlagOn : 0;
    if (const auto* chart = dynamic_cast<const ChartStation*>(screen->stations_.get(StationId::kChart))) {
      const ChartStation::SavedPanels panels = chart->saved_panels();
      ui.chart_panel = static_cast<std::int32_t>(panels.panel);
      ui.chart_panel_xy = panels.xy;
      ui.chart_x = chart->view().center_x();
      ui.chart_z = chart->view().center_y();
      ui.chart_zoom = chart->view().zoom();
    }
    if (screen->cabin_ != nullptr) {
      ui.log_page = screen->cabin_->log_page_;
      for (const PatrolLogEntry& e : screen->cabin_->logs_.patrol) {
        StateImage image(kPatrolLogRecordSize);
        StateOut out(image, no_ext, ids);
        PatrolLogEntry copy = e;
        bind_log_entry(out, copy);
        f.patrol_log.push_back(std::move(image));
      }
      for (const RadioMessage& m : screen->cabin_->logs_.radio) {
        StateImage image(kRadioRecordSize);
        StateOut out(image, no_ext, ids);
        RadioMessage copy = m;
        bind_radio_message(out, copy);
        f.radio_log.push_back(std::move(image));
      }
    }
    for (const RadioMessage& m : screen->radio_.waiting_) {
      StateImage image(kRadioRecordSize);
      StateOut out(image, no_ext, ids);
      RadioMessage copy = m;
      bind_radio_message(out, copy);
      f.radio_waiting.push_back(std::move(image));
    }
  }
  if (session.attack != nullptr) {
    ui.lower_panel = static_cast<std::uint8_t>(session.attack->lower_panel);
    ui.tdc_panel = static_cast<std::int32_t>(session.attack->tdc_panel);
    StateOut out(f.player, no_ext, ids);
    out.fixed(0x277, session.attack->zoom);
  }
  f.ui = ui;

  f.tdc = StateImage(kTdcSize);
  {
    StateOut out(*f.tdc, ext, ids);
    bind(out, mission.combat_->tdc);
  }
  f.deck_gun = StateImage(kDeckGunSize);
  {
    StateOut out(*f.deck_gun, ext, ids);
    bind(out, mission.combat_->deck_gun);
  }

  // The routes and the contact groups of a patrol.
  ContactGroups* groups = parts.patrol != nullptr ? parts.patrol->groups_.get() : nullptr;
  std::unordered_map<const ContactGroup*, std::int32_t> group_index;
  if (groups != nullptr) {
    for (const RouteState& r : groups->routes_) {
      StateImage image(kRouteStateSize);
      StateOut out(image, no_ext, ids);
      RouteState copy = r;
      bind_route_state(out, copy);
      f.routes.push_back(std::move(image));
    }
    for (const std::unique_ptr<ContactGroup>& g : groups->groups_) {
      group_index[g.get()] = static_cast<std::int32_t>(f.groups.size());
      SimStateFile::Group saved{g->kind, StateImage(contact_group_size(g->kind))};
      StateOut out(saved.image, ext, ids);
      bind_group(out, *g);
      f.groups.push_back(std::move(saved));
    }
  }

  // The vehicles but the player, by class id.
  for (const std::unique_ptr<Vehicle>& v : sim.vehicles_) {
    if (v.get() == mission.player_) {
      continue;
    }
    const VehicleStateLayout& layout = vehicle_state_layout(v->class_id());
    SimStateFile::Unit unit{v->class_id(), StateImage(layout.image_size())};
    StateOut out(unit.image, ext, ids);
    capture_vehicle(*v, out);
    std::int32_t group = -1;
    if (groups != nullptr) {
      const auto owner = groups->owner_.find(v.get());
      if (owner != groups->owner_.end()) {
        group = group_index.at(owner->second);
      }
    }
    unit.image.set_i32(layout.group_offset(), group);
    f.vehicles.push_back(std::move(unit));
  }

  // The rest of the mission state (extension "SIM ").
  {
    StateImage none;
    StateOut out(none, ext, ids);
    bind(out, sim);
    bind(out, *mission.sky_);
    bind(out, mission.air_sorties_);
    bind_conditions(out, mission.conditions_);
    out.x_flag(mission.paused_);
    out.x_u32(mission.restore_seed_);
    out.x_i32(mission.difficulty_rating_);
    out.x_i32(mission.end_code_);
    out.x_i32(mission.game_over_ ? static_cast<std::int32_t>(*mission.game_over_) : 0);
    for (TorpedoChoice& t : mission.combat_->torpedo_types) {
      out.x_string(t.name);
      out.x_flag(t.available);
      out.x_u32(t.weapon_id);
    }
    if (parts.patrol != nullptr) {
      bind(out, *parts.patrol);
    }
    // Appended later: the bomber groups' report flags, in vehicle order.
    for (const std::unique_ptr<Vehicle>& v : sim.vehicles_) {
      if (auto* g = dynamic_cast<AirGroup*>(v.get()); g != nullptr) {
        out.x_flag(g->report_created_);
      }
    }
  }
  f.sim_extension = ext.take();
  if (mission.conditions_.rules != MissionRules{}) {
    f.rules_extension = encode_mission_rules(mission.conditions_.rules);
  }

  // The session (extension "UI  ").
  if (MissionScreen* screen = session.screen; screen != nullptr) {
    ByteWriter ui_ext;
    StateImage none;
    StateOut out(none, ui_ext, ids);
    bind(out, screen->bar_);
    if (screen->cabin_ != nullptr) {
      bind(out, *screen->cabin_);
    }
    bind(out, screen->radio_);
    out.x_i32(screen->ui_.auto_red_state);
    out.x_flag(screen->ui_.red_override);
    f.ui_extension = ui_ext.take();
  }
  return f;
}

// ---------------------------------------------------------------- restore

void StateIo::restore(Mission& mission, const SimStateFile& f, const SimStateParts& parts) {
  // sim_read_state (0x1873b) over the mission of init_simulation.
  Simulation& sim = *mission.sim_;
  ByteReader ext_reader(f.sim_extension, "state extension");
  ByteReader* ext = f.sim_extension.empty() ? nullptr : &ext_reader;
  const MissionRules rules = f.rules_extension.empty() ? MissionRules{} : decode_mission_rules(f.rules_extension);
  mission.conditions_.rules = rules;
  sim.env_.rules = rules;
  VehicleLinks links;
  {
    StateIn in(f.player, ext, links);
    bind(in, mission);
  }
  if (f.ui) {
    // player_read_state's radar controls and swell.
    const SavedStationUi& u = *f.ui;
    RadarSets& radar = sim.sensors_.radar_;
    radar.sj().mode = static_cast<SjMode>(u.sj_mode);
    radar.sj().beam = u.sj_beam;
    radar.sj().scale = u.sj_scale;
    radar.sj().hold = u.sj_hold != 0;
    radar.sj().nearest_yd = u.sj_nearest;
    radar.sd().on = u.sd_on != 0;
    radar.sd().scale = u.sd_scale;
    radar.sd().nearest_yd = u.sd_nearest;
    mission.combat_->aa_guns.manned = u.aa_manned != 0;
    PlayerSub::Swell& s = mission.player_->swell_;
    s = {u.swell[0], u.swell[1], u.swell[2], u.swell[3], u.swell[4]};
  }
  if (f.tdc) {
    StateIn in(*f.tdc, ext, links);
    bind(in, mission.combat_->tdc);
  }
  if (f.deck_gun) {
    StateIn in(*f.deck_gun, ext, links);
    bind(in, mission.combat_->deck_gun);
  }

  // The routes and contact groups replace those of the patrol's start
  // (bases_place_player_start deletes them first).
  ContactGroups* groups = parts.patrol != nullptr ? parts.patrol->groups_.get() : nullptr;
  if (groups != nullptr) {
    groups->groups_.clear();
    groups->owner_.clear();
    groups->routes_.clear();
    for (const StateImage& image : f.routes) {
      StateIn in(image, nullptr, links);
      RouteState r;
      bind_route_state(in, r);
      groups->routes_.push_back(r);
    }
    for (const SimStateFile::Group& saved : f.groups) {
      auto g = std::make_unique<ContactGroup>();
      g->kind = saved.kind;
      StateIn in(saved.image, ext, links);
      bind_group(in, *g);
      groups->groups_.push_back(std::move(g));
    }
  }

  // The vehicles, appended after the boat in their saved order, then their
  // links (slot +0xe4, tdc / deckgun_resolve_target).
  const VehicleRestoreContext ctx{mission.tables_, mission.air_theatre_, mission.combat_->effects};
  for (const SimStateFile::Unit& unit : f.vehicles) {
    StateIn in(unit.image, ext, links);
    std::unique_ptr<Vehicle> v = restore_vehicle(unit.cls, in, ctx);
    const std::int32_t group = unit.image.i32(vehicle_state_layout(unit.cls).group_offset());
    if (groups != nullptr && group >= 0 && static_cast<std::size_t>(group) < groups->groups_.size()) {
      ContactGroup* owner = groups->groups_[static_cast<std::size_t>(group)].get();
      owner->vehicles.push_back(v.get());
      groups->owner_[v.get()] = owner;
    }
    sim.vehicles_.push_back(std::move(v));
  }
  links.resolve(sim.vehicles_);
  // A bomber group linked to its report has created it (the extension below
  // also knows the groups whose report is gone).
  for (const std::unique_ptr<Vehicle>& v : sim.vehicles_) {
    if (auto* g = dynamic_cast<AirGroup*>(v.get()); g != nullptr) {
      g->report_created_ = g->report_ != nullptr;
    }
  }

  // The mission state the original does not save.
  if (ext != nullptr) {
    StateImage none;
    StateIn in(none, ext, links);
    bind(in, sim);
    bind(in, *mission.sky_);
    bind(in, mission.air_sorties_);
    bind_conditions(in, mission.conditions_);
    in.x_flag(mission.paused_);
    in.x_u32(mission.restore_seed_);
    in.x_i32(mission.difficulty_rating_);
    in.x_i32(mission.end_code_);
    std::int32_t game_over = 0;
    in.x_i32(game_over);
    mission.game_over_ = game_over != 0 ? std::optional(static_cast<GameOverReason>(game_over)) : std::nullopt;
    for (TorpedoChoice& t : mission.combat_->torpedo_types) {
      in.x_string(t.name);
      in.x_flag(t.available);
      in.x_u32(t.weapon_id);
    }
    if (parts.patrol != nullptr) {
      bind(in, *parts.patrol);
    }
    // Absent from the saves of earlier builds.
    if (!ext_reader.at_end()) {
      for (const std::unique_ptr<Vehicle>& v : sim.vehicles_) {
        if (auto* g = dynamic_cast<AirGroup*>(v.get()); g != nullptr) {
          in.x_flag(g->report_created_);
        }
      }
    }
    links.resolve(sim.vehicles_);
    if (!ext_reader.at_end()) {
      ext_reader.fail("state extension longer than expected");
    }
  }
  // The light and fog the sensors read, from the restored sky.
  mission.sky_light_.on_sky_changed();
  mission.sim_->sensors().environment().tdc_target = mission.combat_->tdc.target();
  mission.sim_->sensors().environment().gun_target = mission.combat_->deck_gun.target();
}

void StateIo::restore_session(const SimStateFile& f, const SessionStateParts& session) {
  MissionScreen* screen = session.screen;
  if (screen == nullptr) {
    return;
  }
  VehicleLinks links;
  {
    StateIn in(f.player, nullptr, links);
    bind(in, *screen);
  }
  if (screen->cabin_ != nullptr) {
    CabinLogs& logs = screen->cabin_->logs_;
    logs.patrol = saved_patrol_log(f);
    logs.radio.clear();
    for (const StateImage& image : f.radio_log) {
      StateIn in(image, nullptr, links);
      RadioMessage m;
      bind_radio_message(in, m);
      logs.radio.push_back(std::move(m));
    }
  }
  screen->radio_.waiting_.clear();
  for (const StateImage& image : f.radio_waiting) {
    StateIn in(image, nullptr, links);
    RadioMessage m;
    bind_radio_message(in, m);
    screen->radio_.waiting_.push_back(std::move(m));
  }
  if (f.ui) {
    const SavedStationUi& u = *f.ui;
    MissionUiState& ui = screen->ui_;
    ui.selected_compartment = u.compartment;
    ui.status_page = u.status_page;
    ui.gauges_page = u.gauges_page;
    ui.id_book_page = u.id_book_page;
    ui.cabin_page = u.cabin_page;
    ui.rig_for_red = u.rig_for_red != 0;
    if (screen->cabin_ != nullptr) {
      screen->cabin_->log_page_ = u.log_page;
    }
    // chart_view_set_zoom, then chart_view_set_center; a zoom below 1x
    // (the reimplementation's saves before the chart was kept) leaves the
    // view as it is.
    auto* chart = dynamic_cast<ChartStation*>(screen->stations_.get(StationId::kChart));
    if (chart != nullptr && u.chart_zoom >= 0x10000) {
      chart->view().set_zoom(u.chart_zoom);
      chart->view().set_center(u.chart_x, u.chart_z);
      chart->restore_panels({static_cast<ChartPanel>(u.chart_panel), u.chart_panel_xy});
    }
    if (u.station >= 0 && static_cast<std::size_t>(u.station) < kStationCount) {
      screen->stations_.select(static_cast<StationId>(u.station));
    }
    if (session.attack != nullptr) {
      session.attack->lower_panel = static_cast<LowerPanel>(u.lower_panel);
      session.attack->tdc_panel = static_cast<TdcSubPanel>(u.tdc_panel);
    }
  }
  if (session.attack != nullptr) {
    StateIn in(f.player, nullptr, links);
    in.fixed(0x277, session.attack->zoom);
  }
  if (!f.ui_extension.empty()) {
    ByteReader reader(f.ui_extension, "state extension");
    StateImage none;
    StateIn in(none, &reader, links);
    bind(in, screen->bar_);
    if (screen->cabin_ != nullptr) {
      bind(in, *screen->cabin_);
    }
    bind(in, screen->radio_);
    in.x_i32(screen->ui_.auto_red_state);
    in.x_flag(screen->ui_.red_override);
  }
}

SimStateFile capture_sim_state(Mission& mission, const SimStateParts& parts, const SessionStateParts& session) {
  return StateIo::capture(mission, parts, session);
}

void restore_sim_state(Mission& mission, const SimStateFile& file, const SimStateParts& parts) {
  StateIo::restore(mission, file, parts);
}

void restore_session_state(const SimStateFile& file, const SessionStateParts& session) {
  StateIo::restore_session(file, session);
}

}  // namespace sh

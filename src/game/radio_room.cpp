#include "game/radio_room.h"

#include <utility>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/world.h"
#include "sim/player_sub.h"

namespace sh {

namespace {

constexpr std::uint32_t kCommandNames = 0x10eb38;  // char*[4] "ComSubsAsiatic"..

LogAngle log_angle(const GeoAngle& a) { return {a.whole_degrees, a.minutes, a.hemisphere}; }

// player_radio_available (0x5e73f): at radar depth or above, radio working.
bool radio_available(const PlayerSub& player) {
  return player.depth_ft() <= player.sub().radar_depth_ft &&
         !player.systems()[static_cast<std::size_t>(PlayerSystem::kRadio)].impaired();
}

}  // namespace

RadioMessage radio_message(const RadioReport& report, const World& world, const std::string& boat_name,
                           const std::string& from) {
  // The senders' 0xa0-byte records: pilot_init (0x88ad2), airgroup_update
  // (0x89efa), intel_report_update (0x8b277).
  RadioMessage m;
  m.template_index = static_cast<std::int32_t>(report.kind);
  for (std::size_t i = 0; i < report.args.size(); ++i) {
    m.args[i] = report.args[i];
  }
  m.to = boat_name.substr(0, 0x22);
  m.from = from.substr(0, 0x22);
  m.latitude = log_angle(world.latitude_at(report.z));
  m.longitude = log_angle(world.longitude_at(report.x));
  return m;
}

std::string command_name(const ExeImage& exe, Command command) {
  return std::string(exe.string_pointer(kCommandNames + 4 * static_cast<std::uint32_t>(command)));
}

Command command_of_map(std::span<const PatrolZone> zones, std::string_view world_file) {
  for (const PatrolZone& z : zones) {
    if (equals_ignore_case(z.world, world_file)) {
      return z.command;
    }
  }
  return Command::kComSubsAsiatic;
}

RadioRoom::RadioRoom(Mission& mission, std::string from) : mission_(mission), from_(std::move(from)) {}

void RadioRoom::tick() {
  SimContext ctx = mission_.sim().context();
  // player_incoming_radio (0x5e75e)
  if (!waiting_.empty() && radio_available(mission_.player())) {
    const GameClock& clock = ctx.clock;
    for (RadioMessage& m : waiting_) {
      m.unread = true;
      m.day = clock.day();
      m.month = clock.month();
      m.year = clock.year();
      m.hour = clock.hour();
      m.minute = clock.minute();
      delivered_.push_back(std::move(m));
    }
    waiting_.clear();
    ctx.crew_message(kMsgIncomingRadio);
    ctx.clock.reset_compression();
  }
  // radio_queue_push (0x5e715) by this frame's senders.
  for (const RadioReport& r : ctx.events.radio_reports) {
    waiting_.push_back(radio_message(r, mission_.world(), mission_.player_name(), from_));
  }
}

std::vector<RadioMessage> RadioRoom::take_delivered() { return std::exchange(delivered_, {}); }

}  // namespace sh

#include "golden_rig.h"

#include <chrono>
#include <format>

#include "app/command_line.h"
#include "game/sim_state.h"
#include "platform/bios_keys.h"

namespace sh::test {

namespace {

std::span<const std::uint8_t> image_bytes(const StateImage& image) { return image.bytes(0, image.size()); }

}  // namespace

std::string_view station_name(StationId id) {
  constexpr std::string_view kNames[kStationCount] = {"chart", "conn",   "bridge", "deckgun", "gauges", "scope",
                                                      "radar", "status", "tbt",    "tdc",     "cabin"};
  return kNames[static_cast<std::size_t>(id)];
}

std::vector<InputEvent> parse_keys(std::string_view keys) {
  const std::string_view args[] = {"--keys", keys};
  return parse_command_line(args).keys;
}

GoldenTempDir::GoldenTempDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  path_ = std::filesystem::temp_directory_path() / ("sh_golden_" + std::to_string(stamp));
  std::filesystem::create_directories(path_);
}

GoldenTempDir::~GoldenTempDir() { std::filesystem::remove_all(path_); }

void GoldenInput::feed(ui::Gui& gui, const InputEvent& e) {
  switch (e.type) {
    case EventType::kKeyDown:
    case EventType::kText:
      if (const std::optional<std::uint16_t> word = bios_key_word(e)) {
        gui.mouse().post_key(*word, bios_shift_state(e.mods));
      }
      break;
    case EventType::kMouseMove:
    case EventType::kMouseDown:
    case EventType::kMouseUp: {
      const std::uint8_t bit = e.button == MouseButton::kLeft    ? ui::kButtonLeft
                               : e.button == MouseButton::kRight ? ui::kButtonRight
                                                                  : 0;
      if (e.type == EventType::kMouseDown) {
        buttons_ |= bit;
      } else if (e.type == EventType::kMouseUp) {
        buttons_ &= static_cast<std::uint8_t>(~bit);
      }
      gui.mouse().update(e.x, e.y, buttons_, 0);
      break;
    }
    default:
      break;
  }
}

void MissionTracer::attach(MissionSession& session) {
  message_head_ = session.screen().bar().log().head();
  messages_.clear();
}

void MissionTracer::after_frame(MissionSession& session) {
  const CrewMessageLog& log = session.screen().bar().log();
  while (message_head_ != log.head()) {
    message_head_ = (message_head_ + 1) % CrewMessageLog::kSlots;
    const CrewMessageLog::Entry& e = log.entry(message_head_);
    messages_ += std::format("{}{}({},{})", messages_.empty() ? "" : ",", e.id, e.arg1, e.arg2);
  }
}

void MissionTracer::checkpoint(GoldenTrace& trace, std::string_view name, MissionSession& session, bool frame) {
  MissionScreen& screen = session.screen();
  Mission& mission = screen.mission();
  const GameClock& c = mission.sim().clock();
  trace.add(name, "sim.time",
            std::format("day {} {:02}:{:02}:{:02}.{:02} x{}/{}/{}", c.day(), c.hour(), c.minute(), c.second(),
                        c.hundredths(), c.effective_compression(), c.requested_compression(), c.compression_cap()));
  const SimStateFile sim = capture_sim_state(mission, {}, {});
  trace.add_hash(name, "sim.player", image_bytes(sim.player));
  if (sim.tdc) {
    trace.add_hash(name, "sim.tdc", image_bytes(*sim.tdc));
  }
  if (sim.deck_gun) {
    trace.add_hash(name, "sim.deckgun", image_bytes(*sim.deck_gun));
  }
  for (std::size_t i = 0; i < sim.vehicles.size(); ++i) {
    const SimStateFile::Unit& u = sim.vehicles[i];
    trace.add(name, std::format("sim.vehicle.{}", i),
              std::format("{} {}", static_cast<std::int32_t>(u.cls), hex64(fnv1a64(image_bytes(u.image)))));
  }
  trace.add_hash(name, "sim.extension", sim.sim_extension);
  trace.add_random(name, mission.sim().random());
  trace.add(name, "msgs", messages_.empty() ? std::string("-") : messages_);
  messages_.clear();
  const SimStateFile ui = capture_sim_state(mission, {}, {.screen = &screen, .attack = &session.services()});
  trace.add_hash(name, "ui.state", ui.serialize());
  if (frame) {
    trace.add_frame(name, station_name(screen.stations().current()), screen.screen(), screen.dac());
  }
}

GoldenMissionRig::GoldenMissionRig(const GoldenGame& game, std::unique_ptr<Mission> mission, Features features,
                                   int screen_width)
    : game_(game),
      features_(features),
      resources_(ui::GuiResources::load(game.install)),
      saves_(dir_.path(), &game.install) {
  session_ = std::make_unique<MissionSession>(MissionScreenSetup{.install = game.install,
                                                                 .paths = game.paths,
                                                                 .exe = game.exe,
                                                                 .tables = game.tables,
                                                                 .gui = gui_,
                                                                 .options = options_,
                                                                 .detail = detail_,
                                                                 .features = features,
                                                                 .screen_width = [screen_width] { return screen_width; }},
                                              std::move(mission), saves_);
  tracer_.attach(*session_);
}

void GoldenMissionRig::frames(int n) {
  for (int i = 0; i < n && !over_; ++i) {
    frame();
  }
}

void GoldenMissionRig::play(std::string_view keys) {
  for (const InputEvent& e : parse_keys(keys)) {
    if (over_) {
      return;
    }
    input_.feed(gui_, e);
    frame();
  }
}

MissionSaveFile GoldenMissionRig::save_checkpoint(std::string_view name) {
  MissionSaveFile save = capture_mission_save(game_.install, mission(), 1, "GOLDEN",
                                              {.screen = &screen(), .attack = &session_->services()});
  trace_.add_hash(name, "save", save.serialize());
  return save;
}

void GoldenMissionRig::frame() {
  tick_ += 5;
  if (!screen().frame(5)) {
    over_ = true;
  }
  tracer_.after_frame(*session_);
}

}  // namespace sh::test

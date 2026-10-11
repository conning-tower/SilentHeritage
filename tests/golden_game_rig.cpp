#include "golden_game_rig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <vector>

#include "core/byte_reader.h"
#include "game/mission_session.h"
#include "platform/screenshot.h"

namespace sh::test {

namespace {

constexpr int kMaxWait = 20000;
constexpr std::uint64_t kMaxPresents = 2'000'000;

std::string_view place_name(FrontEndPlace place) {
  switch (place) {
    case FrontEndPlace::kMainMenu:
      return "main";
    case FrontEndPlace::kSingleMission:
      return "single";
    case FrontEndPlace::kCareerMenu:
      return "career";
    case FrontEndPlace::kCareerBase:
      return "base";
  }
  return "?";
}

std::deque<std::string> split_steps(std::string_view script) {
  std::deque<std::string> out;
  while (!script.empty()) {
    const std::size_t comma = script.find(',');
    const std::string_view step = script.substr(0, comma);
    if (!step.empty()) {
      out.emplace_back(step);
    }
    script = comma == std::string_view::npos ? std::string_view() : script.substr(comma + 1);
  }
  return out;
}

}  // namespace

GoldenGameRig::GoldenGameRig(const Install& install, Features features, std::uint32_t seed)
    : install_(install),
      paths_(ShPaths::load(install)),
      exe_(ExeImage::load(install)),
      saves_(dir_.path() / "SAVE", &install, paths_.dir(GameDir::kSave)),
      custom_(dir_.path() / "CUSTOM", &install, paths_.dir(GameDir::kCustom)) {
  runner_ = std::make_unique<GameRunner>(GameRunnerServices{.install = install,
                                                            .paths = paths_,
                                                            .exe = exe_,
                                                            .saves = saves_,
                                                            .custom = custom_,
                                                            .wait_tick = [this] { tick_ += 5; },
                                                            .features = features},
                                         *this);
  front_end_ = std::make_unique<FrontEnd>(FrontendServices{.install = install,
                                                           .paths = paths_,
                                                           .exe = exe_,
                                                           .saves = saves_,
                                                           .custom = &custom_,
                                                           .features = features},
                                          *this, *runner_, seed);
  runner_->set_front_end(*front_end_);
  front_end_->set_play_intro(false);
  front_end_->set_editor(this);
}

GoldenGameRig::~GoldenGameRig() = default;

void GoldenGameRig::run(std::string_view script) {
  steps_ = split_steps(script);
  try {
    front_end_->run();
  } catch (const Stop&) {
  }
  CHECK(steps_.empty());
  CHECK(events_.empty());
}

void GoldenGameRig::present(const Image8& screen, const VgaPalette& dac) {
  tick_ += 5;
  if (++presents_ > kMaxPresents) {
    FAIL("the golden script did not end");
  }
  shown_ = screen;
  shown_dac_ = dac;
  MissionSession* session = runner_->session();
  if (session != traced_session_) {
    traced_session_ = session;
    if (session != nullptr) {
      tracer_.attach(*session);
    }
  }
  if (session != nullptr) {
    tracer_.after_frame(*session);
  }
}

void GoldenGameRig::poll_input(ui::Gui& gui) {
  if (!events_.empty()) {
    input_.feed(gui, events_.front());
    events_.pop_front();
    return;
  }
  while (!steps_.empty()) {
    if (take_step(gui)) {
      return;
    }
  }
  throw Stop();
}

bool GoldenGameRig::take_step(ui::Gui& gui) {
  const std::string step = steps_.front();
  if (step.starts_with('@')) {
    steps_.pop_front();
    checkpoint(std::string_view(step).substr(1));
    return false;
  }
  if (step.starts_with("call:")) {
    steps_.pop_front();
    const auto hook = hooks.find(std::string_view(step).substr(5));
    REQUIRE(hook != hooks.end());
    hook->second(*this);
    return false;
  }
  if (step.starts_with("until:")) {
    const std::string_view what = std::string_view(step).substr(6);
    bool done = false;
    if (what == "mission") {
      done = runner_->session() != nullptr;
    } else if (what == "menu") {
      done = runner_->session() == nullptr && editor_ == nullptr;
    } else if (what == "editor") {
      done = editor_ != nullptr && editor_->running();
    } else {
      FAIL("unknown golden step " << step);
    }
    if (done) {
      steps_.pop_front();
      waited_ = 0;
      return false;
    }
    if (++waited_ > kMaxWait) {
      FAIL("golden step " << step << " waited too long");
    }
    return true;
  }
  steps_.pop_front();
  for (const InputEvent& e : parse_keys(step)) {
    events_.push_back(e);
  }
  if (!events_.empty()) {
    input_.feed(gui, events_.front());
    events_.pop_front();
  }
  return true;
}

void GoldenGameRig::checkpoint(std::string_view name) {
  const FrontEndLocation& at = front_end_->location();
  trace_.add(name, "fe.place", std::format("{} {}", place_name(at.place), at.roster_slot));
  trace_.add_random(name, front_end_->random(), "fe.rng", "fe.draws");
  std::string_view shown = "menu";
  if (MissionSession* session = runner_->session()) {
    tracer_.checkpoint(trace_, name, *session, false);
    shown = station_name(session->screen().stations().current());
  } else if (editor_ != nullptr) {
    shown = "editor";
  }
  trace_.add_frame(name, shown, shown_, shown_dac_);
  if (const char* out = std::getenv("SH_SCREENS"); out != nullptr && *out != '\0') {
    save_bmp(std::filesystem::path(out) / std::format("{}.bmp", name), shown_, to_palette(shown_dac_));
  }
  // The files the game wrote, by path.
  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::recursive_directory_iterator(dir_.path())) {
    if (e.is_regular_file()) {
      files.push_back(e.path());
    }
  }
  std::ranges::sort(files);
  for (const std::filesystem::path& f : files) {
    trace_.add_hash(name, "file." + std::filesystem::relative(f, dir_.path()).generic_string(), read_file(f));
  }
}

void GoldenGameRig::report(std::string_view message) { trace_.add("-", "report", std::string(message)); }

EditorExit GoldenGameRig::run_editor() {
  editor::Editor editor({.install = install_, .paths = paths_, .saves = saves_, .custom = custom_}, *this, true,
                        [] { return 1000; });
  editor_ = &editor;
  struct Clear {
    editor::Editor*& editor;
    ~Clear() { editor = nullptr; }
  } clear{editor_};
  return editor.run();
}

}  // namespace sh::test

#include "game/resume_keeper.h"

#include <chrono>
#include <exception>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "game/lifecycle_log.h"
#include "game/saves_options.h"

namespace sh {

namespace {

std::string_view kind_name(ResumeKind kind) {
  switch (kind) {
    case ResumeKind::kFrontEnd:
      return "menus";
    case ResumeKind::kMission:
      return "mission";
    case ResumeKind::kPatrol:
      return "career patrol";
  }
  return "?";
}

}  // namespace

ResumeKeeper::ResumeKeeper(const SaveDir& saves, SnapshotSource snapshot, const LifecycleLog* log)
    : saves_(saves), snapshot_(std::move(snapshot)), log_(log) {}

void ResumeKeeper::launch(std::string_view program) const {
  if (log_ == nullptr) {
    return;
  }
  const std::optional<std::vector<std::uint8_t>> data = saves_.read(kResumeFileName);
  log_->write(data && !data->empty()
                  ? std::format("launch {}: {} of {} bytes to resume", program, kResumeFileName, data->size())
                  : std::format("launch {}: nothing to resume", program));
}

void ResumeKeeper::handle(LifecycleEvent event) {
  // The game runs between the events unless it is in the background (the
  // event poll blocks there) or ending.
  const bool ran = !background_ && !quitting_;
  switch (event) {
    case LifecycleEvent::kWillEnterBackground:
    case LifecycleEvent::kDidEnterBackground:
      background_ = true;
      save(event, ran);
      break;
    case LifecycleEvent::kLowMemory:
    case LifecycleEvent::kFocusLost:
      save(event, ran);
      break;
    case LifecycleEvent::kTerminating:
    case LifecycleEvent::kQuit:
      quitting_ = true;
      save(event, ran);
      break;
    case LifecycleEvent::kWillEnterForeground:
      log(event, "");
      break;
    case LifecycleEvent::kDidEnterForeground:
      background_ = false;
      drop(event);
      break;
    case LifecycleEvent::kFocusGained:
      if (ran) {
        drop(event);
      } else {
        log(event, "ignored (in the background or ending)");
      }
      break;
  }
}

void ResumeKeeper::save(LifecycleEvent event, bool ran) {
  if (!ran && current_) {
    log(event, "snapshot unchanged (the game has not run since)");
    return;
  }
  const auto start = std::chrono::steady_clock::now();
  std::optional<ResumeSnapshot> snapshot;
  try {
    snapshot = snapshot_();
  } catch (const std::exception& e) {
    // Called from the system's notification: the game goes on (or ends)
    // without a snapshot rather than with a fatal error.
    current_ = false;
    log(event, std::format("no snapshot: {}", e.what()));
    return;
  }
  if (!snapshot) {
    current_ = true;
    log(event, std::format("the launch's {} kept (not resumed yet)", kResumeFileName));
    return;
  }
  const std::vector<std::uint8_t> bytes = snapshot->serialize();
  const bool ok = saves_.write(kResumeFileName, bytes);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  current_ = ok;
  written_ = written_ || ok;
  log(event, ok ? std::format("snapshot written ({}, {} bytes, {} ms)", kind_name(snapshot->kind), bytes.size(), ms)
                : std::format("cannot write {}", (saves_.dir() / kResumeFileName).string()));
}

void ResumeKeeper::drop(LifecycleEvent event) {
  current_ = false;
  if (!written_) {
    log(event, "");
    return;
  }
  delete_resume_snapshot(saves_);
  written_ = false;
  log(event, "snapshot deleted (the game goes on)");
}

void ResumeKeeper::exit() const {
  if (quitting_) {
    if (log_ != nullptr) {
      log_->write(current_ ? "exit: snapshot kept" : "exit: no snapshot written since the game last ran");
    }
    return;
  }
  delete_resume_snapshot(saves_);
  if (log_ != nullptr) {
    log_->write("exit through the menus: nothing to resume");
  }
}

void ResumeKeeper::log(LifecycleEvent event, std::string_view what) const {
  if (log_ != nullptr) {
    log_->write(what.empty() ? std::string(lifecycle_event_name(event))
                             : std::format("{}: {}", lifecycle_event_name(event), what));
  }
}

}  // namespace sh

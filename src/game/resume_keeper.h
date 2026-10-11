// The keeping of the resume snapshot RESUME.SAV (game/resume.h) through
// the system's lifecycle events (core/lifecycle.h): written when the
// program may be killed without further notice (sent to the background,
// low on memory, its window's focus lost), terminated or its window closed;
// deleted when the live game goes on (back in the foreground or focus) or
// when the player leaves through the menus. Every event and what became of
// the snapshot go to the lifecycle log.
//
//   ResumeKeeper keeper(saves, [&] { return runner.snapshot(); }, &log);
//   keeper.launch("Silent Heritage");
//   front_end.run();  // the events handled meanwhile, at its event polls
//   keeper.exit();
#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "core/lifecycle.h"
#include "game/resume.h"

namespace sh {

class LifecycleLog;
class SaveDir;

class ResumeKeeper {
 public:
  // Returns the game's snapshot, or nothing while RESUME.SAV still stands
  // for the game (the launch's snapshot, not taken yet).
  using SnapshotSource = std::function<std::optional<ResumeSnapshot>()>;

  // `log` may be null. `saves`, `log` and what `snapshot` reads must
  // outlive the keeper.
  ResumeKeeper(const SaveDir& saves, SnapshotSource snapshot, const LifecycleLog* log);

  // Logs the start of `program` and whether there is a snapshot to resume.
  void launch(std::string_view program) const;
  // Handles `event`. Must be called on the game's thread at a point where
  // the game is whole (its event polls). A snapshot is written only when
  // the game may have changed since the last one (the game does not run
  // in the background nor once ending), so repeated events are harmless.
  void handle(LifecycleEvent event);
  // Ends the program: the snapshot is kept when it ends on a quit or a
  // termination (the window closed, the system ending it), deleted when
  // the player left through the menus.
  void exit() const;

  // Whether the program is in the background.
  bool in_background() const { return background_; }
  // Whether a quit or termination was handled.
  bool quitting() const { return quitting_; }

 private:
  // Writes the snapshot for `event`, unless the game has not run since
  // the last one (`ran` false).
  void save(LifecycleEvent event, bool ran);
  // Deletes the snapshot written, the game going on after `event`.
  void drop(LifecycleEvent event);
  void log(LifecycleEvent event, std::string_view what) const;

  const SaveDir& saves_;
  SnapshotSource snapshot_;
  const LifecycleLog* log_;
  bool background_ = false;
  bool quitting_ = false;
  // RESUME.SAV holds the game as it is now (written since it last ran, or
  // the launch's not taken yet).
  bool current_ = false;
  // RESUME.SAV holds a snapshot written here (deleted when the game goes
  // on).
  bool written_ = false;
};

}  // namespace sh

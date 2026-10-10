// The resume snapshot `SAVE\RESUME.SAV` (not in the original): what the
// program writes when the system suspends it (Android may then kill it
// without notice) or its window is closed, so that the next launch goes
// straight back to where the player was, skipping the intro. It holds the
// mission in play as a MISSn.SAV image (with the session state: station,
// chart view, logs), a career patrol as the roster image of its career
// saved on patrol (the in-game SAVE CAREER's), or else the place of the
// front end (main menu, single-mission menu, career menu, career base).
//
// File (little-endian): "SHRESUME", u8 version (1), u8 kind, u8 place,
// i32 roster slot, u32 the front end's generator state; then for a
// mission i32 save number, u16 + bytes of the save name, u32 + bytes of
// the MISSn.SAV image; for a patrol u32 + bytes of the ROSTERn.SAV image;
// last the u32 FNV-1a hash of everything before it.
//
// The snapshot is consumed when the program starts (`take_resume_snapshot`)
// and deleted when the live game supersedes it (back in the foreground, a
// mission ended, the game left through the menus).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "game/career.h"
#include "game/frontend.h"
#include "game/mission_save.h"

namespace sh {

class Install;
class Mission;
class Patrol;
class SaveDir;

inline constexpr std::string_view kResumeFileName = "RESUME.SAV";

enum class ResumeKind : std::uint8_t {
  kFrontEnd = 1,  // a place of the front end
  kMission = 2,   // a single mission in play
  kPatrol = 3,    // a career patrol in play
};

struct ResumeSnapshot {
  ResumeKind kind = ResumeKind::kFrontEnd;
  // The front end's place; for a mission, the menu it was started from.
  FrontEndLocation location;
  // The front end's generator state (`WatcomRandom::state`).
  std::uint32_t random = 0;
  // `kMission`: the mission, and the save it was continued from or saved
  // to (the in-game SAVE MISSION's overwrite question).
  std::optional<MissionSaveFile> mission;
  SavedMissionSlot slot;
  // `kPatrol`: the career on patrol, with its patrol state lists.
  std::optional<Career> career;

  // Returns the file image.
  std::vector<std::uint8_t> serialize() const;
  // Parses a file image. Throws `DataError` when it is damaged (short,
  // another version, a bad hash, an unreadable mission or career).
  static ResumeSnapshot parse(std::span<const std::uint8_t> data);
};

// Returns the snapshot of single mission `mission` in play: its MISSn.SAV
// image with the session's state (`capture_mission_save`, which sets the
// vehicles' ids), `slot` its save, `location` the menu it returns to.
ResumeSnapshot mission_resume_snapshot(const Install& install, Mission& mission, const SessionStateParts& session,
                                       const SavedMissionSlot& slot, const FrontEndLocation& location);
// Returns the snapshot of career patrol `patrol` in play (its mission
// `mission`): the career with the state lists the in-game SAVE CAREER
// captures (`capture_patrol_state`), the patrol's career left as it was.
ResumeSnapshot patrol_resume_snapshot(Mission& mission, Patrol& patrol, const SessionStateParts& session);

// Writes `snapshot` as RESUME.SAV atomically; false when it cannot be
// written.
bool write_resume_snapshot(const SaveDir& saves, const ResumeSnapshot& snapshot);
// Reads RESUME.SAV and deletes it: nothing when there is none. Throws
// `DataError` (the file deleted all the same) when it is damaged.
std::optional<ResumeSnapshot> take_resume_snapshot(const SaveDir& saves);
// Deletes RESUME.SAV when there is one.
void delete_resume_snapshot(const SaveDir& saves);

}  // namespace sh

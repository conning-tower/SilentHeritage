// The single missions saved in play, `SAVE\MISS<n>.SAV` (module loadsave):
// u8 version 7, the 0x919-byte header `mission_save` (0x534c7) fills and
// `list_saved_missions` lists, then the simulation state lists
// (game/sim_state.h). The original reads only version 7.
//
// Header (offsets in the 0x919 bytes):
//   +0x000 i32 the save number: never written by the original (stack
//          bytes), set from the file name by `mission_list_saves`; written
//          here.
//   +0x004 char[26] the name typed; +0x01e i32 0x125f4c; +0x022 char[30]
//          the boat's name; +0x040 char[10] / +0x04a char[25] the SUBS.DAT
//          class's names; +0x063 char[31] the map's region name.
//   +0x082 the PZ.DAT record in memory (0x11b808, 0x4bc bytes); +0x53e the
//          world-files block (0x104e80, 0x2d1 bytes: SEA.PCC, the .ENV,
//          the .OBS, the .WLD, TTEXT.PCC in char[0x90] each).
//   +0x80f weather, sea state, fog flag, thermal layer; +0x81f month, day,
//          year, hour, minute, second; +0x837 difficulty; +0x83b the end
//          on lost contact flag (0x125ff8); +0x83f fog %; +0x843 the sea
//          picture; +0x847 / +0x854 latitude and longitude (13 bytes:
//          degrees, minutes, hemisphere); +0x861 heading (tenths),
//          +0x865 depth (ft), +0x869 speed (16.16 kn).
//   +0x86d u32: srand()'s seed at `init_restore_saved_mission`, which
//          `mission_save` never writes (stack bytes); written here with the
//          generator's state.
//   +0x871 17 x i32: the options word's realism bits 2-3, 4-5, 6 .. 15,
//          the equipment bits 16-17, 18 .. 21.
//   +0x8b5..+0x918 unused (stack bytes; zeros here).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/scenario.h"
#include "core/random.h"
#include "game/career.h"
#include "game/frontend_results.h"
#include "game/mission.h"
#include "game/sim_state.h"

namespace sh {

class ExeImage;
class MissionSession;
class Patrol;
class Font;
class Install;
class SaveDir;
struct GameTables;

inline constexpr std::size_t kMissionHeaderSize = 0x919;
inline constexpr std::size_t kZoneRecordSize = 0x4bc;
inline constexpr std::size_t kSavedWorldFilesSize = 0x2d1;
// The highest save number (`mission_find_free_slot` tries 0..100).
inline constexpr std::int32_t kMaxMissionSave = 100;

// A latitude or longitude of the header (13 bytes).
struct SavedLatLon {
  std::int32_t degrees = 0;
  std::int32_t minutes = 0;
  char hemisphere = 'N';
  bool operator==(const SavedLatLon&) const = default;
};

struct MissionSaveHeader {
  std::int32_t number = 0;
  std::string name;
  std::int32_t group_activity = 0;
  std::string boat_name;
  std::string class_name;
  std::string class_long_name;
  std::string zone_name;
  std::array<std::uint8_t, kZoneRecordSize> zone{};
  std::array<std::uint8_t, kSavedWorldFilesSize> world_files{};
  std::int32_t weather = 0;
  std::int32_t sea_state = 0;
  std::int32_t fog = 0;
  std::int32_t thermal_layer_ft = 0;
  std::int32_t month = 0;
  std::int32_t day = 1;
  std::int32_t year = 0;
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t second = 0;
  std::int32_t difficulty = 0;
  std::int32_t end_on_lost_contact = 0;
  std::int32_t fog_pct = 0;
  std::int32_t sea_picture = 0;
  SavedLatLon latitude;
  SavedLatLon longitude;
  std::int32_t heading = 0;
  std::int32_t depth_ft = 0;
  std::int32_t speed = 0;
  std::uint32_t seed = 0;
  std::array<std::int32_t, 17> realism{};

  // Parses the 0x919 bytes; throws `DataError` when short.
  static MissionSaveHeader parse(std::span<const std::uint8_t> data);
  std::array<std::uint8_t, kMissionHeaderSize> serialize() const;

  // The options word `continue_mission_loop` (0xa1486) rebuilds: the
  // realism bits, the equipment bits and the saved-mission mode (bit 1).
  std::uint32_t options() const;
  // The `.SCN` equipment bits of the options (0x125fec).
  std::uint32_t equipment() const;
  // The names of the world-files block: 0 sea texture, 1 environment,
  // 2 objects, 3 map, 4 terrain texture.
  std::string world_file(std::size_t index) const;
};

// Returns the world-files block of `names` (char[0x90] each, the last byte
// 0).
std::array<std::uint8_t, kSavedWorldFilesSize> world_files_block(std::span<const std::string, 5> names);

// A whole MISSn.SAV.
struct MissionSaveFile {
  MissionSaveHeader header;
  SimStateFile state;

  // Parses a file; nothing when its version is not 7 (the original then
  // ignores it). Throws `DataError` when it is malformed.
  static std::optional<MissionSaveFile> parse(std::span<const std::uint8_t> data);
  std::vector<std::uint8_t> serialize() const;
};

// Returns "MISS<n>.SAV".
std::string mission_save_file_name(std::int32_t number);

// `mission_list_saves` (0x531b3): the headers of the readable version-7
// MISS*.SAV files, in save-number order, their numbers from the file names.
std::vector<MissionSaveHeader> list_mission_saves(const SaveDir& saves);
// `mission_find_free_slot` (0x53853): the first number 0..100 without a
// file, or -1.
std::int32_t find_free_mission_slot(const SaveDir& saves);
// `mission_load_lists` (0x533c9): MISS<n>.SAV, or nothing when it is
// missing, damaged or not version 7.
std::optional<MissionSaveFile> load_mission_save(const SaveDir& saves, std::int32_t number);
// `mission_delete_save` (0x5390e): removes MISS<n>.SAV and renumbers every
// higher save down by one.
void delete_mission_save(const SaveDir& saves, std::int32_t number);

// `mission_save`'s capture of `mission` named `name` as save `number`:
// the header (the PZ.DAT record of the mission's map from `install`) and
// the state lists of `capture_sim_state` (which sets the vehicles' ids).
MissionSaveFile capture_mission_save(const Install& install, Mission& mission, std::int32_t number,
                                     std::string_view name, const SessionStateParts& session);
// `mission_save` (0x534c7): writes MISS<number>.SAV; false when it cannot
// be written.
bool save_mission(const SaveDir& saves, const MissionSaveFile& save);

// `career_save_patrol_state` (0x18fc0) for the in-game SAVE CAREER: the
// state lists of `patrol`'s mission into its career's `patrol_state`, with
// the patrol-log count (+0x44d); `career_save` then appends them.
void capture_patrol_state(Mission& mission, Patrol& patrol, const SessionStateParts& session);

// The mission a save restarts (`continue_mission_loop`, then
// `init_simulation` with the saved-mission bit: the header's date,
// conditions and boat, its world files and zone, the torpedo load
// `scen_fill_torpedo_load` draws from `setup.random`, no units but the
// boat), with the state lists restored (`init_restore_saved_mission`; an
// original save without the extension then seeds the generator with the
// header's seed and restarts the clock at the saved second).
// `setup.options` is replaced by the header's. Throws `DataError` when a
// file or class is missing.
std::unique_ptr<Mission> restore_mission(const Install& install, const GameTables& tables, const MissionSaveFile& save,
                                         MissionSetup setup);

// The save a single mission was continued from or last saved to (0x122238
// and its name 0x12223c, with the options word's bit 1), for the in-game
// overwrite question; number -1 when none.
struct SavedMissionSlot {
  std::int32_t number = -1;
  std::string name;
};

// `cmd_save_mission` (0x4bc88), the GAME OPTIONS menu's SAVE MISSION in
// `session`: "Overwrite currently saved mission?" when `slot` is set (YES
// keeps its number and name), else the SAVE MISSION name dialog (0x120b20)
// and the first free number; then `mission_save`, or "Cannot save game!"
// when no number is free. `slot` (which must outlive the dialogs) then
// names the save.
void cmd_save_mission(MissionSession& session, const Install& install, const SaveDir& saves, SavedMissionSlot& slot);

// `croster_save_career_as` (0x35669) in a patrol, the GAME OPTIONS menu's
// SAVE CAREER: the BACKUP CAREER name dialog (0x11a078), then the career
// with the patrol's state lists into a free roster slot (drawing the
// career's seed from the mission's generator); `patrol` must outlive the
// dialog.
void cmd_save_career(MissionSession& session, Patrol& patrol, const SaveDir& saves);

// What VIEW CAREER (`view_career` 0x8facf) shows of a roster's career:
// the record and its log, plus, for a career saved on patrol, the patrol
// log of its state lists as sunk entries, their ships and tonnage added to
// the record (`career_load_roster_lists` with `log_only`; none when the
// state is damaged).
struct CareerView {
  CareerRecord record;
  std::vector<SunkListEntry> events;
};
CareerView career_view(const SaveDir& saves, const CareerRecord& record);

// `format_saved_mission_summary` (0xa45ee): the "*** MISSION DATA ***"
// text of the CONTINUE MISSION list, in two columns padded in `font`.
std::string saved_mission_summary(const MissionSaveHeader& header, const ExeImage& exe, const Font& font);

}  // namespace sh

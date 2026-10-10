#include "editor/file_commands.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <system_error>

#include "assets/scn_file.h"
#include "core/error.h"
#include "core/install.h"
#include "editor/dialogs_boxes.h"
#include "editor/dialogs_settings.h"

namespace sh::editor {

namespace {

// SE strings.
namespace str {
constexpr std::uint32_t kScnExtension = 0x98350;  // ".SCN"
constexpr std::uint32_t kDataDir = 0x98374;       // "DATA\"
// Save Scenario.
constexpr std::uint32_t kOverwriteTitle = 0x913a7;  // "WARNING"
constexpr std::uint32_t kOverwriteLine1 = 0x91392;  // "OVERWRITING SCENARIO"
constexpr std::uint32_t kOverwriteLine2 = 0x9136f;  // "Are you sure you want to overwrite"
constexpr std::uint32_t kOverwriteMark = 0x9136d;   // "?"
constexpr std::uint32_t kSaveDiskFull = 0x913da;
constexpr std::uint32_t kSaveDiskFullTitle = 0x913e4;
constexpr std::uint32_t kSaveCannotOpen = 0x913c2;  // "CANNOT OPEN FILE:"
constexpr std::uint32_t kSaveCannotOpenTitle = 0x913d4;
constexpr std::uint32_t kSaveWriteFailed = 0x913ea;  // "UNABLE TO WRITE TO FILE:"
constexpr std::uint32_t kSaveWriteFailedTitle = 0x91403;
// Run.
constexpr std::uint32_t kRunDiskFull = 0x912d0;
constexpr std::uint32_t kRunDiskFullTitle = 0x912da;
constexpr std::uint32_t kRunCannotCreate = 0x912b5;  // "CANNOT CREATE FILE:"
constexpr std::uint32_t kRunCannotCreateTitle = 0x912c9;
constexpr std::uint32_t kRunWriteFailed = 0x912e1;
constexpr std::uint32_t kRunWriteFailedTitle = 0x912fa;
// Delete Scenario.
constexpr std::uint32_t kDeleteTitle = 0x91625;  // "WARNING"
constexpr std::uint32_t kDeleteLine1 = 0x91613;  // "DELETING SCENARIO"
constexpr std::uint32_t kDeleteLine2 = 0x915f3;  // "Are you sure you want to delete"
constexpr std::uint32_t kDeleteMark = 0x915f1;   // "?"
constexpr std::uint32_t kCannotDelete = 0x9162d;  // "CANNOT DELETE FILE:"
constexpr std::uint32_t kCannotDeleteTitle = 0x91641;
// check_ships_in_range / check_scenario_size.
constexpr std::uint32_t kRangeTitle = 0x91717;
constexpr std::uint32_t kRangeLine1 = 0x9171f;  // "NO SHIPS IN RANGE?"
constexpr std::uint32_t kRangeLine2 = 0x91732;
constexpr std::uint32_t kRangeSave = 0x9177a;      // "Are you sure you want to save?"
constexpr std::uint32_t kRangeContinue = 0x91757;  // "... continue?"
constexpr std::uint32_t kSizeTitle = 0x91799;
constexpr std::uint32_t kSizeLine1 = 0x917a1;  // "SCENARIO IS TOO LARGE"
constexpr std::uint32_t kSizeLine2 = 0x917b7;
constexpr std::uint32_t kSizeSave = 0x91811;
constexpr std::uint32_t kSizeContinue = 0x917ee;
// scen_report_load_error (0x46d3d), by code.
constexpr std::uint32_t kLoadErrorTitle = 0x932c2;  // "ERROR"
constexpr std::uint32_t kNotFound = 0x932a9;        // 1 "Scenario file not found:"
constexpr std::uint32_t kBadHeader = 0x932c9;       // 2 "Invalid file header:"
constexpr std::uint32_t kBadVersion = 0x932e4;      // 3 "Bad file version:"
constexpr std::uint32_t kCannotRead = 0x932fc;      // 4 "Cannot read file"
constexpr std::uint32_t kBadObject = 0x93313;       // 5 "Invalid object type:"
// scenario_reload_world (0x2a4fa).
constexpr std::uint32_t kWorldErrorTitle = 0x9146e;
constexpr std::uint32_t kBadWorld = 0x9145b;  // "NOT A VALID WORLD:"
}  // namespace str

// The SBCURSOR.SHP frames of `mouse_set_cursor` around the writes and loads.
constexpr std::size_t kWaitCursor = 3;
constexpr std::size_t kPointerCursor = 1;

// The `.SCN` header `scen_load_ascn` checks first: "ASCN", NUL, version.
constexpr std::size_t kMagicSize = 6;

// The wait cursor for the life of the scope.
class WaitCursor {
 public:
  explicit WaitCursor(EditorUi& ui) : ui_(ui) { ui_.gui().mouse().set_cursor(ui_.cursors(), kWaitCursor); }
  ~WaitCursor() { ui_.gui().mouse().set_cursor(ui_.cursors(), kPointerCursor); }
  WaitCursor(const WaitCursor&) = delete;
  WaitCursor& operator=(const WaitCursor&) = delete;

 private:
  EditorUi& ui_;
};

// Returns whether `data` starts with "ASCN" and a NUL (`strcmp` against
// "ASCN").
bool has_scn_magic(std::span<const std::uint8_t> data) {
  return data.size() >= kMagicSize && data[0] == 'A' && data[1] == 'S' && data[2] == 'C' && data[3] == 'N' &&
         data[4] == 0;
}

// Returns whether `file` (upper case) matches "HIST???.SCN" (a '?' also
// matches nothing at the end, as DOS's `opendir`).
bool is_historical_name(std::string_view file) {
  const std::size_t dot = file.find('.');
  if (dot == std::string_view::npos || file.substr(dot) != ".SCN") {
    return false;
  }
  const std::string_view base = file.substr(0, dot);
  return base.starts_with("HIST") && base.size() <= 7;
}

// Returns the readable content of `name` in `dir`: nothing for a missing
// file or a deleted one (masked by an empty file).
std::optional<std::vector<std::uint8_t>> read_custom(const SaveDir& dir, std::string_view name) {
  std::optional<std::vector<std::uint8_t>> data = dir.read(name);
  if (data && data->empty()) {
    return std::nullopt;
  }
  return data;
}

}  // namespace

WriteResult write_scenario_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  // dfile_open (O_WRONLY | O_CREAT | O_TRUNC) then dfile_write; written
  // through a temporary file renamed over `path` so a failed save keeps the
  // previous scenario.
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  // A device (not a regular file) is written in place.
  const bool in_place = std::filesystem::exists(path, ec) && !std::filesystem::is_regular_file(path, ec);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  const std::filesystem::path& target = in_place ? path : tmp;
#ifdef _WIN32
  // The wide name: the narrow fopen takes the ANSI code page, not UTF-8.
  std::FILE* f = _wfopen(target.c_str(), L"wb");
#else
  std::FILE* f = std::fopen(target.c_str(), "wb");
#endif
  if (f == nullptr) {
    return WriteResult::kCannotOpen;
  }
  errno = 0;
  const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
  const bool flushed = std::fflush(f) == 0;
  const int error = errno;
  const bool closed = std::fclose(f) == 0;
  ec.clear();
  if (written == bytes.size() && flushed && closed) {
    if (!in_place) {
      std::filesystem::rename(tmp, path, ec);
    }
    if (!ec) {
      return WriteResult::kOk;
    }
  }
  if (!in_place) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
  }
  if (ec) {
    return WriteResult::kCannotOpen;
  }
  return error == ENOSPC ? WriteResult::kDiskFull : WriteResult::kWriteFailed;
}

std::vector<std::string> save_list_build(const SaveDir& custom) {
  std::vector<std::string> names;
  for (const std::string& file : custom.list(".SCN")) {
    if (read_custom(custom, file)) {
      names.push_back(file.substr(0, file.find('.')));
    }
  }
  // sort_file_names (0x2a7e4) compares the file names with stricmp: the
  // names are upper case and sorted already.
  return names;
}

std::vector<ScenarioListEntry> scenario_list_build(const FileEnv& env, bool historical) {
  std::vector<std::string> files;
  if (historical) {
    for (const std::string& path : env.install.list(env.paths.dir_name(GameDir::kScen), ".SCN")) {
      std::string file = path.substr(path.rfind('/') + 1);
      if (is_historical_name(file)) {
        files.push_back(std::move(file));
      }
    }
  } else {
    files = env.custom.list(".SCN");
  }
  std::ranges::sort(files);
  std::vector<ScenarioListEntry> entries;
  for (std::string& file : files) {
    // scen_load_ascn(path, 0, 0): the version and the briefing only.
    std::optional<std::vector<std::uint8_t>> data;
    if (historical) {
      const std::string path = env.paths.path(GameDir::kScen, file);
      data = env.install.try_read(path);
    } else {
      data = env.custom.read(file);
    }
    if (!data || !has_scn_magic(*data)) {
      continue;
    }
    // Original bug (scenario lists overwrite the briefing, fixed): SE reads
    // the headers into the current scenario's version and briefing
    // globals; they are read into locals here.
    if ((*data)[5] < 2 || data->size() - kMagicSize < kScnBriefingSize) {
      continue;
    }
    ScnChars<kScnBriefingSize> text{};
    std::copy_n(data->begin() + kMagicSize, kScnBriefingSize, text.begin());
    const std::string briefing = scn_text(text);
    ScenarioListEntry e;
    if (historical) {
      const std::size_t nl = briefing.find('\n');
      const std::size_t rest = nl == std::string::npos ? 0 : nl + 1;
      e.title = briefing.substr(0, std::min(rest, std::size_t{0x31}));
      e.description = briefing.substr(rest);
    } else {
      e.title = file.substr(0, file.find('.'));
      e.description = briefing;
    }
    e.file = std::move(file);
    entries.push_back(std::move(e));
  }
  return entries;
}

bool FileCommands::check_ships_in_range(bool running) {
  const Scenario& s = env_.scenario;
  if (s.header.end_on_lost_contact == 0) {
    return true;
  }
  for (const auto& object : s.objects()) {
    if (object->is_special_mission() || object->in_detection_range(false) || object->in_detection_range(true)) {
      return true;
    }
  }
  return ask_yes_no(env_.ui, str(str::kRangeTitle), str(str::kRangeLine1), str(str::kRangeLine2),
                    str(running ? str::kRangeContinue : str::kRangeSave), str(se_addr::kYes),
                    str(se_addr::kNo)) == ui::kResultAccept;
}

bool FileCommands::check_scenario_size(bool running) {
  if (!env_.scenario.too_large()) {
    return true;
  }
  return ask_yes_no(env_.ui, str(str::kSizeTitle), str(str::kSizeLine1), str(str::kSizeLine2),
                    str(running ? str::kSizeContinue : str::kSizeSave), str(se_addr::kYes),
                    str(se_addr::kNo)) == ui::kResultAccept;
}

void FileCommands::write_error(WriteResult result, std::uint32_t opening, const std::string& path) {
  const bool run = opening == str::kRunCannotCreate;
  switch (result) {
    case WriteResult::kOk:
      break;
    case WriteResult::kDiskFull:
      show_message_box(env_.ui, str(run ? str::kRunDiskFullTitle : str::kSaveDiskFullTitle),
                       str(run ? str::kRunDiskFull : str::kSaveDiskFull), "", "");
      break;
    case WriteResult::kCannotOpen:
      show_message_box(env_.ui, str(run ? str::kRunCannotCreateTitle : str::kSaveCannotOpenTitle), str(opening),
                       path, "");
      break;
    case WriteResult::kWriteFailed:
      show_message_box(env_.ui, str(run ? str::kRunWriteFailedTitle : str::kSaveWriteFailedTitle),
                       str(run ? str::kRunWriteFailed : str::kSaveWriteFailed), path, "");
      break;
  }
}

int FileCommands::save() {
  // cmd_save_scenario (0x2a291)
  if ((range_check_ && !check_ships_in_range(false)) || !check_scenario_size(false)) {
    return ui::kResultCancel;
  }
  const std::string old = name;
  if (!run_save_scenario_dialog(env_.ui, save_list_build(env_.custom), name)) {
    return ui::kResultCancel;
  }
  const std::string file = name + str(str::kScnExtension);
  const std::string path = env_.paths.path(GameDir::kCustom, file);
  if (read_custom(env_.custom, file) &&
      ask_yes_no(env_.ui, str(str::kOverwriteTitle), str(str::kOverwriteLine1), str(str::kOverwriteLine2),
                 name + str(str::kOverwriteMark), str(se_addr::kYes), str(se_addr::kNo)) == ui::kResultCancel) {
    name = old;
    return ui::kResultCancel;
  }
  WriteResult result = WriteResult::kOk;
  {
    const WaitCursor wait(env_.ui);
    result = write_scenario_file(env_.custom.dir() / file, write_scn_file(env_.scenario.save()));
  }
  write_error(result, str::kSaveCannotOpen, path);
  // Original bug (Run after a write error, fixed): after "UNABLE TO WRITE
  // TO FILE" SE also cleared the modified flag, the file being incomplete.
  if (result != WriteResult::kOk) {
    return ui::kResultCancel;
  }
  env_.scenario.modified = false;
  return ui::kResultAccept;
}

int FileCommands::ask_save_changes() {
  return editor::ask_save_changes(env_.ui, env_.scenario.modified, [this] { return save(); });
}

bool FileCommands::new_scenario() {
  if (ask_save_changes() != ui::kResultAccept) {
    return false;
  }
  env_.scenario.new_scenario();
  name.clear();
  return true;
}

bool FileCommands::run() {
  // cmd_run_scenario (0x2a174)
  if (!check_ships_in_range(true) || !check_scenario_size(true)) {
    return false;
  }
  range_check_ = false;
  const int answer = ask_save_changes();
  range_check_ = true;
  if (answer == ui::kResultCancel) {
    return false;
  }
  const std::string path = env_.paths.path(GameDir::kCustom, kRunFileName);
  WriteResult result = WriteResult::kOk;
  {
    const WaitCursor wait(env_.ui);
    result = write_scenario_file(env_.custom.dir() / kRunFileName, write_scn_file(env_.scenario.save()));
  }
  write_error(result, str::kRunCannotCreate, path);
  // Original bug (Run after a write error, fixed): after "UNABLE TO WRITE
  // TO FILE" SE still ended with Run, and SH stopped with a fatal error on
  // the truncated AEONSCEN.TMP; every write error aborts the Run here.
  return result == WriteResult::kOk;
}

int FileCommands::read_into_scenario(const std::string& path, bool custom, const std::string& file) {
  std::optional<std::vector<std::uint8_t>> data;
  if (custom) {
    data = read_custom(env_.custom, file);
  } else {
    data = env_.install.try_read(path);
  }
  // scen_load_ascn's error codes (scen_report_load_error).
  std::uint32_t error = 0;
  std::optional<ScnFile> scn;
  if (!data) {
    error = str::kNotFound;
  } else if (!has_scn_magic(*data)) {
    error = str::kBadHeader;
  } else if ((*data)[5] < 2) {
    error = str::kBadVersion;
  } else {
    try {
      scn = read_scn_file(*data, path);
      Scenario::check_file(*scn);
    } catch (const DataError& e) {
      error = std::string_view(e.what()).find("unknown record tag") != std::string_view::npos ? str::kBadObject
                                                                                              : str::kCannotRead;
    }
  }
  if (error != 0) {
    show_message_box(env_.ui, str(str::kLoadErrorTitle), str(error), path, "");
    env_.scenario.new_scenario();
    return 1;
  }
  try {
    if (env_.scenario.load(*scn, [this](const ScnHeader& h) { return ask_password(env_.ui, h); })) {
      return -1;
    }
  } catch (const DataError&) {
    // scenario_reload_world's box; the scenario is a new one on the
    // previous world.
    show_message_box(env_.ui, str(str::kWorldErrorTitle), str(str::kBadWorld),
                     str(str::kDataDir) + scn_text(scn->world), "");
  }
  return 0;
}

bool FileCommands::load(ScenarioListMode mode) {
  // cmd_load_scenario (0x2a910)
  // Delete does not ask to save first (its list overwrote the briefing in
  // SE: the original bug "scenario lists overwrite the briefing", fixed).
  if (mode != ScenarioListMode::kDeleteScenario && ask_save_changes() == ui::kResultCancel) {
    return false;
  }
  const bool historical = mode == ScenarioListMode::kLoadHistorical;
  const std::vector<ScenarioListEntry> entries = scenario_list_build(env_, historical);
  const std::optional<std::size_t> choice = run_scenario_list_dialog(env_.ui, mode, entries);
  if (!choice) {
    return false;
  }
  const ScenarioListEntry& entry = entries[*choice];
  const std::string path = env_.paths.path(historical ? GameDir::kScen : GameDir::kCustom, entry.file);
  const WaitCursor wait(env_.ui);
  if (mode == ScenarioListMode::kDeleteScenario) {
    if (ask_yes_no(env_.ui, str(str::kDeleteTitle), str(str::kDeleteLine1), str(str::kDeleteLine2),
                   entry.title + str(str::kDeleteMark), str(se_addr::kYes), str(se_addr::kNo)) != ui::kResultCancel) {
      env_.custom.remove(entry.file);
      if (read_custom(env_.custom, entry.file)) {
        show_message_box(env_.ui, str(str::kCannotDeleteTitle), str(str::kCannotDelete), path, "");
      }
    }
    return false;
  }
  name = historical ? std::string() : entry.title.substr(0, 8);
  if (read_into_scenario(path, !historical, entry.file) == 0) {
    name.clear();
  }
  return true;
}

}  // namespace sh::editor

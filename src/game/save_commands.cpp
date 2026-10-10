// The in-game saves of the GAME OPTIONS menu (module guiuser and croster):
// SAVE MISSION (`cmd_save_mission`, 0x4bc88) and SAVE CAREER
// (`croster_save_career_as`, 0x35669), run as modal dialogs of the mission
// screen (game/mission_save.h).

#include <memory>
#include <string>

#include "assets/exe_image.h"
#include "game/career_save.h"
#include "game/command_bar.h"
#include "game/mission_save.h"
#include "game/mission_session.h"
#include "game/patrol.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE strings.
namespace addr {
constexpr std::uint32_t kSaveMission = 0xf43fb;   // "SAVE MISSION"
constexpr std::uint32_t kCancel = 0xf4408;        // "Cancel"
constexpr std::uint32_t kAccept = 0xf440f;        // "Accept"
constexpr std::uint32_t kAskTitle = 0xf4602;      // "SAVE MISSION"
constexpr std::uint32_t kOverwrite = 0xf45df;     // "Overwrite currently saved mission?"
constexpr std::uint32_t kError = 0xf4653;         // "ERROR"
constexpr std::uint32_t kCannotSave = 0xf4641;    // "Cannot save game!"
constexpr std::uint32_t kDeleteSome = 0xf4625;    // "Delete some old saved games"
constexpr std::uint32_t kBackupTitle = 0xf32b9;   // "BACKUP CAREER"
constexpr std::uint32_t kBackupCancel = 0xf32c7;
constexpr std::uint32_t kBackupAccept = 0xf32ce;
constexpr std::uint32_t kBackupLabel = 0xf32d5;   // "Enter Backup Commander Name:"
}  // namespace addr

// The name buffers: 0x11dffc (26 bytes) and the career name (21 bytes).
constexpr int kMissionNameSize = 0x1a;
constexpr int kCareerNameSize = 0x15;

std::string exe_text(const ExeImage& exe, std::uint32_t address) { return std::string(exe.c_string(address)); }

// A name dialog: the title, an optional label, the text entry and Cancel /
// Accept (Accept needs a name: `save_mission_name_accept` 0x4bdb3,
// 0x3576d). Calls `done(name)` on Accept.
void ask_name(MissionScreen& screen, int x, int y, int w, int h, const std::string& title, const std::string* label,
              int entry_w, int max_length, const std::string& cancel, const std::string& accept,
              std::function<void(std::string)> done) {
  StationEnv& env = screen.env();
  auto d = std::make_unique<ui::Dialog>(env.gui, x, y, w, h, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d->emplace<ui::TitleBar>(title);
  auto& entry = d->emplace<ui::TextEntry>(0xc, 0x46, entry_w, max_length, std::string(),
                                          [](std::string& text, int) { return text.empty() ? 0 : ui::kResultAccept; });
  const int accept_x = w - 0x1a - 100;
  d->emplace<ui::Button>(0x1a, 0x6a, 100, cancel, ui::return_cancel);
  d->emplace<ui::Button>(accept_x, 0x6a, 100, accept,
                         [&entry](int) { return entry.text.empty() ? 0 : ui::kResultAccept; });
  if (label != nullptr) {
    d->emplace<ui::Label>(0x3e, 0x30, *label, ui::FontSlot::kFont1);
  }
  ui::TextEntry* text = &entry;
  env.modals.run_modal(std::move(d), ModalFrame::kGrey, [text, done = std::move(done)](int result) {
    if (result == ui::kResultAccept) {
      done(text->text);
    }
  });
}

}  // namespace

void cmd_save_mission(MissionSession& session, const Install& install, const SaveDir& saves, SavedMissionSlot& slot) {
  // cmd_save_mission (0x4bc88). The dialogs run after this returns: the
  // callbacks hold pointers to what outlives them.
  MissionSession* s = &session;
  const Install* inst = &install;
  const SaveDir* dir = &saves;
  SavedMissionSlot* current = &slot;
  const ExeImage* exe = &session.screen().env().exe;
  const auto save = [s, inst, dir, current, exe](std::int32_t number, const std::string& name) {
    if (number < 0) {
      s->screen().bar().message_box(exe_text(*exe, addr::kError), exe_text(*exe, addr::kCannotSave),
                                    exe_text(*exe, addr::kDeleteSome));
      return;
    }
    Mission& mission = s->screen().mission();
    const MissionSaveFile file =
        capture_mission_save(*inst, mission, number, name, {.screen = &s->screen(), .attack = &s->services()});
    save_mission(*dir, file);
    *current = {number, name};
    // The mission now counts as a saved one (options bit 1).
    mission.sim().conditions().options |= option::kModeSavedMission;
  };
  // mission_find_free_slot runs before the questions.
  const std::int32_t free_slot = find_free_mission_slot(saves);
  const auto ask = [s, exe, save, free_slot]() {
    ask_name(s->screen(), 0x69, 0xb4, 0x1a4, 0x91, exe_text(*exe, addr::kSaveMission), nullptr, 0x18c,
             kMissionNameSize, exe_text(*exe, addr::kCancel), exe_text(*exe, addr::kAccept),
             [save, free_slot](const std::string& name) { save(free_slot, name); });
  };
  if (slot.number < 0) {
    ask();
    return;
  }
  session.screen().bar().ask_yes_no(exe_text(*exe, addr::kAskTitle), exe_text(*exe, addr::kOverwrite), slot.name,
                                    [current, save, ask](bool yes) {
                                      if (yes) {
                                        save(current->number, current->name);
                                      } else {
                                        ask();
                                      }
                                    });
}

void cmd_save_career(MissionSession& session, Patrol& patrol, const SaveDir& saves) {
  // croster_save_career_as (0x35669): BACKUP CAREER over a greyed palette,
  // then the copy into a free slot.
  MissionScreen& screen = session.screen();
  const ExeImage& exe = screen.env().exe;
  const std::string label = exe_text(exe, addr::kBackupLabel);
  ask_name(screen, 0x91, 0xb4, 0x154, 0x91, exe_text(exe, addr::kBackupTitle), &label, 0x13c, kCareerNameSize,
           exe_text(exe, addr::kBackupCancel), exe_text(exe, addr::kBackupAccept),
           [s = &session, p = &patrol, dir = &saves](const std::string& name) {
             Mission& mission = s->screen().mission();
             capture_patrol_state(mission, *p, {.screen = &s->screen(), .attack = &s->services()});
             save_career_as(*dir, p->career(), name, mission.sim().random());
           });
}

}  // namespace sh

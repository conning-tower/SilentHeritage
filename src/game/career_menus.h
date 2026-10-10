// The career's menu screens (modules campaign/career, newcarer, croster,
// tcmdlist, vwcareer, vwawards and vwlogbk): CAREER MENU, START A CAREER,
// CAREER ROSTER, BASE MENU, BEST CAREERS, TOP COMMANDERS and the VIEW
// CAREER screens. Each runs modally over the front end (`CareerUi`) with
// the layouts of the original's static initializers (`career_init_dialogs`
// 0x19df8, `new_career_enter_name` 0x5a2d4, `croster_init_dialogs`
// 0x344c4, `cmdlist_init_dialogs` 0x78838, `view_career_init_dialogs`
// 0x8e9c8, 0x8e570, `logbook_init_dialogs` 0x8ff40) and their texts read
// from SH.EXE. The screens show and return choices; the career flow
// (game/career_flow.h) acts on them.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "game/career.h"

namespace sh {

class CareerUi;
class ExeImage;
class Font;
struct BestCareersTable;
struct NewCareerChoice;
struct TopCommanderRow;

// The result codes of the career menus.
namespace career_code {
// CAREER MENU (dialog 0x116cf8).
inline constexpr int kStartCareer = 0x6e;
inline constexpr int kContinueCareer = 0x6f;
inline constexpr int kBestCareers = 0x70;
inline constexpr int kMainMenu = 100;
// BASE MENU (dialog 0x116f0c).
inline constexpr int kStartPatrol = 0x78;
inline constexpr int kTopCommanders = 0x79;
inline constexpr int kViewCareer = 0x7b;
inline constexpr int kLeaveCareer = 0x7d;  // MAIN MENU
inline constexpr int kTransfer = 0x7e;
inline constexpr int kRetire = 0x7f;
// CAREER ROSTER (dialog 0x119d20): VIEW and DELETE (-1 SELECT, -2 CANCEL).
inline constexpr int kRosterView = 0x7b;
inline constexpr int kRosterDelete = 0x82;
}  // namespace career_code

// `career_menu_screen` (0x1b89d): CAREER MENU over M6.PCX; returns one of
// kStartCareer, kContinueCareer, kBestCareers or kMainMenu.
int career_menu_screen(CareerUi& ui);

// `new_career_screen` (0x5aa7f) up to the dialog's end: START A CAREER over
// M10.PCX with the name (trimmed), the date spinners limited to the war
// and, with every zone map installed (`all_maps`, 0x11a7d4), the command
// spinner. Returns the choice on OK, nothing on CANCEL.
std::optional<NewCareerChoice> new_career_screen(CareerUi& ui, bool all_maps);

// `croster_build_list` (0x35393): the CAREER ROSTER rows "n:name date
// status" padded with `font` (the list font, MENU_L).
std::vector<std::string> roster_rows(std::span<const CareerRecord> careers, const ExeImage& exe, const Font& font);

// `croster_select_screen` (0x351d3): CAREER ROSTER over M8.PCX (loaded and
// faded in when `redraw`) with the careers, starting on row `selection`
// and leaving it on the selected row. SELECT is enabled on careers in port
// or on patrol, VIEW / DELETE / SELECT when there is a career. Returns the
// selected row for SELECT (or a double click), -2 for CANCEL,
// kRosterView, or kRosterDelete after the DELETE CAREER confirmation.
int roster_select_screen(CareerUi& ui, std::span<const CareerRecord> careers, int& selection, bool redraw);

// The BACKUP CAREER dialog of `croster_save_career_as` (0x35669) over the
// greyed screen, with the default skin: the backup's commander name, or
// nothing on Cancel (Accept needs a name).
std::optional<std::string> backup_career_dialog(CareerUi& ui);

// `career_base_menu_screen` (0x1b94a): BASE MENU over RTNPORT.PCX (loaded
// and faded in when `redraw`). REQUEST TRANSFER shows when transfers are
// offered (`CareerSummary::transfer_offered`), RETIRE after 4 patrols; the OFFICERS
// CLUB button stays out as in the original. Both ask for confirmation.
// SAVE CAREER calls `save_career` (with the menu still up) and stays.
// A career on patrol returns kStartPatrol at once. Returns a base code.
int base_menu_screen(CareerUi& ui, const Career& career, bool redraw, const std::function<void()>& save_career);

// `best_careers_screen` (0x1cae7): BEST CAREERS over M8.PCX: the ranked
// list ("n name score tonnage") and the selected career's summary
// (`career_end_summary`).
void best_careers_screen(CareerUi& ui, const BestCareersTable& table);

// `top_career_enter_name` (0x1dc29): TOP CAREER over M7.PCX, congratulating
// "Commander <name>" on making the best careers list.
void top_career_screen(CareerUi& ui, const CareerRecord& record);

// `top_commanders_screen` (0x78a3e): the blackboard (BBOARD.PCX, CHALK.FNT)
// with the career's date and the ten `rows` (`top_commanders`); the player's
// row shows the career's name and tonnage.
void top_commanders_screen(CareerUi& ui, const CareerRecord& record, std::span<const TopCommanderRow> rows,
                           std::span<const Commander> commanders);

// `view_career_format_text` (0x8fc23): the values of VIEW CAREER, in the
// order of its fields: date, status, boat, class, home base, location,
// patrols, tonnage, ships sunk, score.
std::vector<std::string> view_career_fields(const CareerRecord& record, const ExeImage& exe);

// `view_career` (0x8facf): VIEW CAREER ("CMDR <name>") over M8.PCX until
// CONTINUE, with VIEW AWARDS (when the career has one) and VIEW LOG BOOK
// (when the log has entries) leading to their screens. `log` is the career
// log (only its sunk entries are listed).
void view_career(CareerUi& ui, const CareerRecord& record, std::span<const SunkListEntry> log);

}  // namespace sh

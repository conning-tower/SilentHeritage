// The editor's small boxes (module guiuser): the message box, the yes / no
// box, "Save changes to scenario?", QUIT TO DOS? and About. They run over
// whatever is on the screen; the caller greys the palette as the menu
// dispatcher (`editor_menu_handle_event`) does.
#pragma once

#include <functional>
#include <string_view>

#include "editor/editor_ui.h"

namespace sh::editor {

// SE strings of the boxes' callers.
namespace se_addr {
inline constexpr std::uint32_t kYes = 0x913b2;  // "YES"
inline constexpr std::uint32_t kNo = 0x913af;   // "NO"
}  // namespace se_addr

// `show_message_box` (SE 0x24d10): `title` and three centred lines, OK.
void show_message_box(EditorUi& ui, std::string_view title, std::string_view line1, std::string_view line2,
                      std::string_view line3);

// `ask_yes_no` (SE 0x2b0c9): `title`, three lines and the buttons `yes` /
// `no` (keys Y / N). Returns -1 (`ui::kResultAccept`) for `yes`, -2 for
// `no` or Esc.
int ask_yes_no(EditorUi& ui, std::string_view title, std::string_view line1, std::string_view line2,
               std::string_view line3, std::string_view yes, std::string_view no);

// `ask_save_changes` (SE 0x2b1c6): when `modified`, WARNING "Save changes
// to scenario?" SAVE / DISCARD / CANCEL. Returns -1 when not modified or
// on DISCARD, -2 on CANCEL or Esc, and `save()`'s result on SAVE (File /
// Save, `cmd_save_scenario`).
int ask_save_changes(EditorUi& ui, bool modified, const std::function<int()>& save);

// The QUIT TO DOS? box of Alt-Q (dialog 0xaa05c, `quit_menu_select`):
// returns true for YES (exit code 3: the game ends too).
bool quit_to_dos_prompt(EditorUi& ui);

// The About box (dialog 0xaa190, `about_box_draw`): the credits with the
// version, the AEON logo with its colours 224..254 in the DAC, CONTINUE.
void show_about_box(EditorUi& ui);

}  // namespace sh::editor

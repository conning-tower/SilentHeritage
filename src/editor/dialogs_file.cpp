#include "editor/dialogs_file.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "ui/controls.h"
#include "ui/draw.h"

namespace sh::editor {

namespace {

constexpr unsigned kDialogFlags = ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel;
constexpr int kButtonWidth = 100;

// SE strings.
constexpr std::uint32_t kSaveTitle = 0x908fd;
constexpr std::uint32_t kSaveCancel = 0x9090b;
constexpr std::uint32_t kSaveButton = 0x90912;
constexpr std::uint32_t kListCancel = 0x90917;
constexpr std::uint32_t kArrowUp = 0x9091e;
constexpr std::uint32_t kArrowDown = 0x90920;
// {title, button} of each `ScenarioListMode`.
constexpr std::array<std::array<std::uint32_t, 2>, 3> kListTexts{
    {{0x91520, 0x91530}, {0x91535, 0x91543}, {0x91548, 0x91558}}};

// The skin of the Load / Delete dialog: `cmd_load_scenario` points the text
// box font (0x9bc5c) at FONT1 and its background colour index (0xa66b8)
// at 1 for the run.
class BriefingViewSkin : public EditorSkin {
 public:
  BriefingViewSkin(ui::GuiResources& resources, const EditorSkin& base, const Font& font)
      : EditorSkin(resources), font_(font) {
    mutable_metrics() = base.metrics();
    list_font = base.list_font;
  }

  ui::Rect textbox_background(const Viewport& gfx, int x, int y, int w, int h) override {
    ui::gui_fill_rect(gfx, x + 2, y + 2, w + 11, h, res_.colours.b[1]);
    return {x, y, x + w + 14, y + h + 3};
  }
  void textbox_text(const Viewport& gfx, int x, int y, std::string_view s, bool) override {
    text(gfx, x + metrics().textbox_origin_x, y + metrics().textbox_origin_y, font_, s, res_.table);
  }
  int textbox_char_width(std::uint8_t ch) override { return font_.glyph_width(ch); }
  void textbox_caret(ui::Dialog& dialog, int x, int y, std::uint8_t ch, bool on) override {
    screen_caret(dialog, dialog.x() + x + 5, dialog.y() + y + 5 + font_.height() - 2, font_.glyph_width(ch) - 1,
                 on ? res_.colours.a[3] : res_.colours.a[0]);
  }

 private:
  const Font& font_;
};

// Installs `skin` for the life of the scope.
class SkinScope {
 public:
  SkinScope(ui::Gui& gui, ui::Skin& skin) : gui_(gui), saved_(gui.skin()) { gui_.set_skin(skin); }
  ~SkinScope() { gui_.set_skin(saved_); }
  SkinScope(const SkinScope&) = delete;
  SkinScope& operator=(const SkinScope&) = delete;

 private:
  ui::Gui& gui_;
  ui::Skin& saved_;
};

}  // namespace

void save_name_on_done(std::string& name) {
  std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (const std::size_t space = name.find(' '); space != std::string::npos) {
    name.resize(space);
  }
}

bool run_save_scenario_dialog(EditorUi& ui, const std::vector<std::string>& names, std::string& name) {
  const std::string old = name;
  ui::Dialog d(ui.gui(), 164, 110, 312, 282, kDialogFlags);
  d.emplace<ui::TitleBar>(ui.str(kSaveTitle), ui::return_cancel);
  auto& entry = d.emplace<ui::TextEntry>(35, 210, 217, 9, name, [](std::string& text, int) {
    save_name_on_done(text);
    return ui::kResultAccept;
  });
  // The list's on_activate (`dialog_return_accept`) is dropped by the list.
  auto& list = d.emplace<ui::List>(
      34, 54, 217, [&names] { return static_cast<int>(names.size()); },
      [&names](int row) { return names[static_cast<std::size_t>(row)]; });
  // save_list_on_select (0x2a780)
  list.on_select = [&entry, &names](int row) {
    entry.text = names[static_cast<std::size_t>(row)];
    entry.caret = std::min(entry.caret, static_cast<int>(entry.text.size()));
    entry.dirty = true;
  };
  d.emplace<ui::Button>(35, 245, kButtonWidth, ui.str(kSaveCancel), ui::return_cancel);
  d.emplace<ui::Button>(178, 245, kButtonWidth, ui.str(kSaveButton), ui::return_accept);
  if (d.run() == ui::kResultCancel || entry.text.empty()) {
    name = old;
    return false;
  }
  name = entry.text;
  return true;
}

std::optional<std::size_t> run_scenario_list_dialog(EditorUi& ui, ScenarioListMode mode,
                                                    const std::vector<ScenarioListEntry>& entries) {
  BriefingViewSkin skin(ui.resources(), ui.skin(), ui.font1());
  const SkinScope scope(ui.gui(), skin);
  const auto& texts = kListTexts[static_cast<std::size_t>(mode)];
  ui::Dialog d(ui.gui(), 134, 50, 372, 367, kDialogFlags);
  // The owner draw 0x2a7e3 draws nothing.
  d.emplace<ui::TitleBar>(ui.str(texts[0]), ui::return_cancel);
  auto& list = d.emplace<ui::List>(
      14, 54, 317, [&entries] { return static_cast<int>(entries.size()); },
      [&entries](int row) { return entries[static_cast<std::size_t>(row)].title; });
  d.emplace<ui::Button>(35, 325, kButtonWidth, ui.str(kListCancel), ui::return_cancel);
  d.emplace<ui::Button>(234, 325, kButtonWidth, ui.str(texts[1]), ui::return_accept).disabled = entries.empty();
  auto& text = d.emplace<ui::TextBox>(14, 210, 302, 100, 12, 1000,
                                      entries.empty() ? std::string() : entries.front().description);
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(337, 212, 20, ui.str(kArrowUp), nullptr, 0, 20);
  auto& down = d.emplace<ui::Button>(337, 288, 20, ui.str(kArrowDown), nullptr, 0, 20);
  d.emplace<ui::GroupBox>(335, 240, 358, 283);

  // scenario_briefing_scroll_up / _down (0x2b087 / 0x2b0a8)
  bool refresh_arrows = false;  // 0xa6c04
  up.on_click = [&](int) {
    text.scroll_up(d);
    text.dirty = true;
    refresh_arrows = true;
    return 0;
  };
  down.on_click = [&](int) {
    text.scroll_down(d);
    text.dirty = true;
    refresh_arrows = true;
    return 0;
  };
  // scenario_list_on_select (0x2b056)
  list.on_select = [&](int row) {
    text.top = 0;
    text.dirty = true;
    refresh_arrows = true;
    text.text = entries[static_cast<std::size_t>(row)].description;
  };
  // scenario_list_idle (0x2afa6): entry 0 selected at first; the arrows are
  // greyed when the text cannot scroll that way.
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      if (!entries.empty()) {
        list.top = 0;
        list.selection = 0;
        list.dirty = true;
      }
      refresh_arrows = true;
    }
    if (!text.dirty && refresh_arrows) {
      const bool can_up = text.scroll_up(d);
      up.disabled = !can_up;
      if (can_up) {
        text.scroll_down(d);
      }
      const bool can_down = text.scroll_down(d);
      down.disabled = !can_down;
      if (can_down) {
        text.scroll_up(d);
      }
      up.dirty = true;
      down.dirty = true;
      refresh_arrows = false;
    }
  });
  if (d.run() == ui::kResultCancel || list.selection < 0 ||
      static_cast<std::size_t>(list.selection) >= entries.size()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(list.selection);
}

}  // namespace sh::editor

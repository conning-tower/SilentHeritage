// The screens around a mission: MISSION BRIEFING (dialog 0x18a404), then
// after a single mission the fate newspaper, the tow out of fuel, the
// return to port, MISSION RESULTS (dialog 0x18a1b4), and the BEST MISSIONS
// screens (dialogs 0x189c68 and HIGH SCORE 0x189fa0).

#include <algorithm>
#include <cctype>

#include "assets/exe_image.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_best.h"
#include "game/frontend_briefing.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"

namespace sh {

namespace {

namespace addr {
constexpr std::uint32_t kMonths = 0x10e910;       // char*[12] "January"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;     // i32 1930
constexpr std::uint32_t kNewsFont = 0x110998;     // "MFONT.FNT"
constexpr std::uint32_t kHeadlineFont = 0x1109a4;  // "MFONT2.FNT"
constexpr std::uint32_t kTowedPcx = 0x11098c;     // "TOWED.PCX"
constexpr std::uint32_t kOutOfFuel = 0xfd6e9;     // "   OUT OF FUEL!"
constexpr std::uint32_t kPortPcx = 0x11097c;      // "PORTBACK.PCX"
constexpr std::uint32_t kResultsPcx = 0x110950;   // "M9.PCX"
constexpr std::uint32_t kBestPcx = 0x110940;      // "M8.PCX"
constexpr std::uint32_t kHighScorePcx = 0x1109b0;  // "M7.PCX"
constexpr std::uint32_t kAnonymous = 0xfd607;     // "Anonymous"
}  // namespace addr

// The newspapers of `show_fate_newspaper` (0xa7246): picture, headline
// suffix and the width the headline is centred in.
struct Newspaper {
  std::uint32_t picture;
  std::uint32_t headline;
  int width;
};
constexpr Newspaper kNewspapers[3] = {
    {0x110958, 0xfd62b, 0x18d},  // PAPER-2.PCX, " LOST AT SEA"
    {0x110964, 0xfd645, 0x1fb},  // PAPER-3.PCX, " CREW CAPTURED"
    {0x110970, 0xfd661, 0x1fb},  // PAPER-4.PCX, " CREW RESCUED"
};

constexpr ui::DialogFlag kMenuFlags = static_cast<ui::DialogFlag>(ui::kDialogTitled | ui::kDialogFramed);

// The port scene: the boat's sprite moves 3 pixels left every 10 ticks
// on the line 0x18b, from x 600, for at most 3000 ticks.
constexpr int kPortLine = 0x18b;
constexpr int kPortStartX = 0x258;
constexpr int kPortStep = 3;
constexpr std::uint32_t kPortStepTicks = 10;
constexpr std::uint32_t kPortMaxTicks = 3000;

}  // namespace

void FrontEnd::wait_for_press() {
  // The loops on `event_get` that end on the first event other than a
  // mouse move.
  while (true) {
    frame();
    while (const std::optional<ui::Event> e = gui_.events().get()) {
      if ((e->flags & ui::kEventMoved) == 0) {
        return;
      }
    }
  }
}

void FrontEnd::show_fate_newspaper(int kind, const MissionOutcome& o) {
  // show_fate_newspaper (0xa7246), kind 1..3.
  const Newspaper& paper = kNewspapers[kind - 1];
  palette_fade_out(0, 0x100);
  gui_.mouse().hide();
  gui_.events().flush();
  const Font news = Font::load(install_, paths_.path(GameDir::kArt, exe_string(addr::kNewsFont)));
  const Font headline_font = Font::load(install_, paths_.path(GameDir::kArt, exe_string(addr::kHeadlineFont)));
  load_screen_pcx(exe_string(paper.picture));
  // The boat's name upper-cased, from 2 characters after its ')' (the
  // original reads from address 2 when there is none).
  std::string name = o.boat_name;
  std::ranges::transform(name, name.begin(),
                         [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  const std::size_t paren = name.find(')');
  const std::string headline =
      (paren == std::string::npos ? name : name.substr(std::min(paren + 2, name.size()))) + exe_string(paper.headline);
  const Viewport vp = Viewport::full(screen_);
  const GlyphColourTable& identity = gui_res_.identity;
  gui_draw_text(vp, (paper.width - headline_font.text_width(headline)) / 2 + 2, 0xa9, headline_font, headline,
                &identity);
  const std::string date = std::string(exe_.string_pointer(addr::kMonths + static_cast<std::uint32_t>(o.month) * 4)) +
                           " " + std::to_string(o.day) + ", " + std::to_string(o.year + exe_.i32(addr::kBaseYear));
  gui_draw_text(vp, 0x274 - news.text_width(date), 0x83, news, date, &identity);
  palette_fade_in(palette_, 0, 10);
  wait_for_press();
  palette_fade_out(1, 5);
  screen_.fill(0);
  gui_.mouse().show();
}

void FrontEnd::show_towed_out_of_fuel(const MissionOutcome& o) {
  // show_towed_out_of_fuel (0xa754d)
  palette_fade_out(0, 0x100);
  gui_.mouse().hide();
  gui_.events().flush();
  load_screen_pcx(exe_string(addr::kTowedPcx));
  const std::string text = o.boat_name + exe_string(addr::kOutOfFuel);
  const Font& font = gui_res_.gui1;
  gui_draw_text(Viewport::full(screen_), (kScreenWidth - font.text_width(text)) / 2, 0x15a, font, text,
                &gui_res_.identity);
  palette_fade_in(palette_, 0, 10);
  wait_for_press();
  palette_fade_out(1, 5);
  screen_.fill(0);
  gui_.mouse().show();
}

void FrontEnd::show_port_return(const MissionOutcome& o) {
  // show_port_return (0xa78b6): the boat's profile, mirrored, sails left.
  port_scene(o.profile_sprite, true);
}

void FrontEnd::show_port_departure(const std::string& profile_sprite) {
  // show_port_departure (0xa767d): the boat's profile sails right.
  port_scene(profile_sprite, false);
}

void FrontEnd::port_scene(const std::string& profile_sprite, bool returning) {
  // Over PORTBACK.PCX until a key or click, or 30 seconds. The PORT.RAW
  // speech it plays with the sound effects on is left out (the front end
  // has no speech output yet).
  palette_fade_out(0, 0x100);
  gui_.mouse().hide();
  gui_.events().flush();
  load_screen_pcx(exe_string(addr::kPortPcx));
  const Shp sprite = Shp::load(install_, paths_.path(GameDir::kArt, profile_sprite));
  const SpriteFrame& boat = sprite.frame(0);
  // The background strip under the boat's line, restored before each
  // move.
  const int top = std::max(kPortLine - boat.height(), 0);
  Image8 strip(kScreenWidth, kPortLine - top + 1);
  for (int y = top; y <= kPortLine; ++y) {
    std::ranges::copy(screen_.row(y), strip.row(y - top).begin());
  }
  Image8 scratch;
  const Viewport vp = Viewport::full(screen_);
  int x = kPortStartX;
  bool first = true;
  const std::uint32_t start = gui_.clock();
  std::uint32_t last_step = start - kPortStepTicks;
  while (true) {
    if (gui_.clock() - last_step >= kPortStepTicks) {
      screen_.blit(strip, 0, top);
      if (returning) {
        sprite_draw_transformed(vp, boat, x, kPortLine, scratch, Angle10(0), Fixed16::from_int(-1),
                                Fixed16::from_int(1), first ? 0u : static_cast<unsigned>(kSpriteScratchReady));
        x -= kPortStep;
      } else {
        draw_sprite_clipped(vp, boat, x, kPortLine);
        x += kPortStep;
      }
      last_step = gui_.clock();
    }
    if (first) {
      palette_fade_in(palette_, 1, 0x14);
      first = false;
    } else {
      frame();
    }
    bool pressed = false;
    while (const std::optional<ui::Event> e = gui_.events().get()) {
      pressed = pressed || (e->flags & ui::kEventMoved) == 0;
    }
    if (pressed || gui_.clock() - start > kPortMaxTicks) {
      break;
    }
  }
  palette_fade_out(2, 5);
  screen_.fill(0);
  gui_.mouse().show();
}

namespace {

// The arrows of a read-only text box, greyed when it cannot scroll that
// way (the `*_dialog_draw` callbacks probe a scroll each way and undo it).
struct TextScroll {
  ui::TextBox& text;
  ui::Button& up;
  ui::Button& down;
  bool refresh = true;  // 0x187244

  void wire(ui::Dialog& d) {
    up.on_click = [this, &d](int) {
      text.scroll_up(d);
      text.dirty = true;
      refresh = true;
      return 0;
    };
    down.on_click = [this, &d](int) {
      text.scroll_down(d);
      text.dirty = true;
      refresh = true;
      return 0;
    };
  }
  void update(ui::Dialog& d) {
    if (text.dirty || !refresh) {
      return;
    }
    const bool can_up = text.scroll_up(d);
    if (can_up) {
      text.scroll_down(d);
    }
    const bool can_down = text.scroll_down(d);
    if (can_down) {
      text.scroll_up(d);
    }
    up.disabled = !can_up;
    down.disabled = !can_down;
    up.dirty = true;
    down.dirty = true;
    refresh = false;
  }
};

}  // namespace

void FrontEnd::mission_briefing_screen(const Mission& mission) {
  // mission_briefing_screen (0xa5a07), dialog 0x18a404: no picture or
  // fade, over the screen as `init_simulation` leaves it.
  const MissionBriefing facts = mission_briefing_facts(mission);
  ui::Dialog d(gui_, 0x14, 0x1e, 0x125, 0x1a4, kMenuFlags);
  d.emplace<ui::TitleBar>(mission_briefing_title(facts, exe_));
  d.emplace<ui::Button>(0x55, 0x172, 0x79, exe_string(0xfc098), ui::return_accept);
  auto& text = d.emplace<ui::TextBox>(0x12, 0x84, 200, 0xd6, 10, 1000,
                                      mission_briefing_text(facts, exe_, menu_res_.small));
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(0xf3, 0x80, 0x21, exe_string(0xfc09b), nullptr, 0, 0x14);
  auto& down = d.emplace<ui::Button>(0xf3, 0x14a, 0x21, exe_string(0xfc09d), nullptr, 0, 0x14);
  d.emplace<ui::GroupBox>(0xf3, 0xa5, 0x113, 0x147);
  add_exit_area(d);
  TextScroll scroll{text, up, down};
  scroll.wire(d);
  // mission_briefing_dialog_draw (0xa68e5)
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      scroll.refresh = true;
    }
    scroll.update(d);
  });
  run_menu_dialog(d);
}

void FrontEnd::mission_results_screen() {
  // mission_results_screen (0xa4c04), dialog 0x18a1b4.
  load_screen_pcx(exe_string(addr::kResultsPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);
  ui::Dialog d(gui_, 10, 12, 0x267, 0x14a, kMenuFlags);
  d.emplace<ui::TitleBar>(exe_string(0xfc081));
  d.emplace<ui::Button>(0xf5, 0x118, 0x79, exe_string(0xfc091), ui::return_accept);
  auto& text = d.emplace<ui::TextBox>(0x12, 0x7a, 0x209, 0x86, 10, 5000,
                                      mission_results_summary(results_, exe_, menu_res_.small));
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(0x235, 0x76, 0x21, exe_string(0xfc094), nullptr, 0, 0x14);
  auto& down = d.emplace<ui::Button>(0x235, 0xef, 0x21, exe_string(0xfc096), nullptr, 0, 0x14);
  d.emplace<ui::GroupBox>(0x235, 0x9b, 0x255, 0xec);
  add_exit_area(d);
  TextScroll scroll{text, up, down};
  scroll.wire(d);
  // mission_results_dialog_draw (0xa5931)
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
      scroll.refresh = true;
    }
    scroll.update(d);
  });
  run_menu_dialog(d);
}

void FrontEnd::best_missions_screen() {
  // best_missions_screen (0xa69a8), dialog 0x189c68. A table that cannot
  // be read shows empty (the original reads through a null pointer).
  load_screen_pcx(exe_string(addr::kBestPcx));
  gui_.set_skin(menu_skin_);
  ui::SkinMetrics& metrics = menu_skin_.mutable_metrics();
  const ui::SkinMetrics saved = metrics;
  metrics.list_rows = 7;
  metrics.list_height = 0x72;
  metrics.list_track = 0x32;
  palette_fade_out(0, 0x100);
  const BestMissionsTable table = BestMissionsTable::load(saves_).value_or(BestMissionsTable{});
  const std::vector<std::string> rows = best_missions_rows(table, menu_res_.large);
  std::vector<const BestMissionEntry*> used;
  for (const std::optional<BestMissionEntry>& e : table.entries) {
    if (e) {
      used.push_back(&*e);
    }
  }
  // best_missions_show_entry (0xa6a7c). `best_missions_build_list` links
  // the stored sunk lists in place; for an empty one it writes the end
  // link 0x53 bytes before the first record, over the low half of the
  // longitude's degrees.
  const auto entry_text = [&](int row) {
    MissionResults r = used[static_cast<std::size_t>(row)]->results;
    if (r.sunk.empty()) {
      r.longitude.degrees &= ~0xffff;
    }
    return mission_results_summary(r, exe_, menu_res_.small);
  };

  ui::Dialog d(gui_, 10, 12, 0x267, 0x1c2, kMenuFlags);
  d.emplace<ui::TitleBar>(exe_string(0xfbff2));
  d.emplace<ui::Button>(0xf5, 400, 0x79, exe_string(0xfc000), ui::return_accept);
  auto& text = d.emplace<ui::TextBox>(0x12, 0x110, 0x209, 0x68, 10, 5000, used.empty() ? std::string() : entry_text(0));
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(0x235, 0x10c, 0x21, exe_string(0xfc003), nullptr, 0, 0x14);
  auto& down = d.emplace<ui::Button>(0x235, 0x167, 0x21, exe_string(0xfc005), nullptr, 0, 0x14);
  d.emplace<ui::GroupBox>(0x235, 0x131, 0x255, 0x164);
  auto& list = d.emplace<ui::List>(
      0x14, 0x91, 0x21b, [&rows] { return static_cast<int>(rows.size()); },
      [&rows](int row) { return rows[static_cast<std::size_t>(row)]; });
  d.emplace<ui::Label>(0x14, 0x7a, exe_string(0xfc007), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x43, 0x7a, exe_string(0xfc00c), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x18b, 0x7a, exe_string(0xfc01d), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x1d6, 0x7a, exe_string(0xfc023), ui::FontSlot::kFont1);
  add_exit_area(d);
  TextScroll scroll{text, up, down};
  scroll.wire(d);
  // best_missions_on_select (0xa6b8a)
  list.on_select = [&](int row) {
    text.text = entry_text(row);
    text.top = 0;
    text.dirty = true;
    scroll.refresh = true;
  };
  // best_missions_dialog_draw (0xa6a9a)
  bool fade_pending = true;  // 0x187240
  d.set_idle([&](const Viewport&, int first) {
    if (first == 0) {
      if (fade_pending) {
        palette_fade_in(palette_, 0, 10);
        fade_pending = false;
      }
    } else {
      list.selection = 0;
      list.dirty = true;
      scroll.refresh = true;
    }
    scroll.update(d);
  });
  run_menu_dialog(d);
  metrics = saved;
}

std::string FrontEnd::high_score_enter_name() {
  // high_score_enter_name (0xa71a9), dialog 0x189fa0: an empty name
  // becomes "Anonymous".
  load_screen_pcx(exe_string(addr::kHighScorePcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);
  ui::Dialog d(gui_, 0x118, 5, 0x157, 300, kMenuFlags);
  d.emplace<ui::TitleBar>(exe_string(0xfc02b));
  auto& entry = d.emplace<ui::TextEntry>(0x14, 0xc2, 0x12e, 0x15, std::string(),
                                         [](std::string&, int) { return ui::kResultAccept; });
  d.emplace<ui::Label>(0x17, 0x7f, exe_string(0xfc036), ui::FontSlot::kFont1);
  d.emplace<ui::Button>(0x72, 0xf5, 0x79, exe_string(0xfc06d), ui::return_accept);
  d.emplace<ui::Label>(0x17, 0xa7, exe_string(0xfc070), ui::FontSlot::kFont1);
  d.emplace<ui::GroupBox>(9, 0x9b, 0x14c, 0xeb);
  add_exit_area(d);
  // high_score_dialog_draw (0xa722e)
  d.set_idle([this](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
  });
  run_menu_dialog(d);
  return entry.text.empty() ? exe_string(addr::kAnonymous) : entry.text;
}

void FrontEnd::best_missions_record_score() {
  // best_missions_record_score (0xa6da9): a scoring mission that ranks in
  // the ten best asks the commander's name and is saved.
  if (results_.score == 0) {
    return;
  }
  std::optional<BestMissionsTable> table = BestMissionsTable::load(saves_);
  if (!table) {
    return;
  }
  const std::optional<std::size_t> rank = table->rank_of(results_.score);
  if (!rank) {
    return;
  }
  std::string name = high_score_enter_name();
  table->insert(*rank, BestMissionEntry{.name = std::move(name), .results = results_});
  table->save(saves_);
}

}  // namespace sh

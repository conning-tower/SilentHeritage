// The screens of the career's REQUESTS (not in the original): the
// REQUESTS menu, TORPEDO LOAD on the status page's torpedo rooms, and DECK
// GUN.

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <string>

#include "assets/exe_image.h"
#include "game/career_requests.h"
#include "game/career_ui.h"
#include "game/stations/status.h"
#include "game/torpedo_art.h"
#include "game/torpedo_room.h"
#include "gfx/sprite.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kMonths = 0x10e910;  // char*[12] "January"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;  // i32 1930
constexpr std::uint32_t kBasePcx = 0x1032fc;  // "RTNPORT.PCX"
constexpr std::uint32_t kYes = 0xf0cdf;
constexpr std::uint32_t kNo = 0xf0cdc;
}  // namespace addr

constexpr unsigned kMenuFlags = ui::kDialogTitled | ui::kDialogFramed;
constexpr std::string_view kBaseMenuLabel = "BASE MENU";

// The menu of BASE MENU's layout: the dialog at x 5, buttons 0xc6 wide at
// x 0x1e, 0x28 apart from y 0x82, the dialog ending 0x46 below the last.
constexpr int kMenuX = 5;
constexpr int kMenuWidth = 0x104;
constexpr int kButtonX = 0x1e;
constexpr int kButtonWidth = 0xc6;
constexpr int kFirstButtonY = 0x82;
constexpr int kButtonStep = 0x28;
constexpr int kMenuBottom = 0x46;

// TORPEDO LOAD: the supply's rows above the rooms, the message line, the
// bottom buttons.
constexpr int kSupplyNameY = 0x0a;
constexpr int kSupplyTorpedoY = 0x22;
constexpr int kSupplyCountY = 0x32;
constexpr int kSupplyLimitY = 0x3e;
constexpr int kSupplyButtonsY = 0x4e;
constexpr int kSupplyHeight = 0x96;  // the supply's click area ends above the rooms
constexpr int kMessageY = 0x84;
constexpr int kBottomButtonsY = 0x1bd;
constexpr int kSupplyColumn = 0x80;
constexpr int kSupplyButtonWidth = 0x3a;
// The pictures' half width (STATUST.SHP frames 0xc..) and height.
constexpr int kTorpedoHalfWidth = 46;
constexpr int kTorpedoHeight = 12;
// The dialog results.
constexpr int kAccept = 1;
constexpr int kReset = 2;

std::string month_year(const ExeImage& exe, std::int32_t month, std::int32_t year) {
  return std::format("{} {}", exe.string_pointer(addr::kMonths + static_cast<std::uint32_t>(month) * 4),
                     year + exe.i32(addr::kBaseYear));
}

// The TORPEDO LOAD screen's drawing and clicks.
class TorpedoLoadScreen {
 public:
  TorpedoLoadScreen(CareerUi& ui, const GameTables& tables, const SubClass& sub, TorpedoLoadEditor& editor,
                    std::string patrol)
      : ui_(ui),
        tables_(tables),
        editor_(editor),
        patrol_(std::move(patrol)),
        texts_(StatusTexts::load(ui.exe())),
        font_(Font::load(ui.install(), ui.paths().path(GameDir::kArt, "MENU_S.FNT"))),
        sprites_(Shp::load(ui.install(), ui.paths().path(GameDir::kArt, "STATUST.SHP"))) {
    light_[0] = 0xd8;
    light_[0xff] = 0xff;
    bow_tubes_ = std::min<std::size_t>(sub.bow_tubes, TorpedoLoad::kTubes);
  }

  // Loads the page and runs it; returns the dialog's result.
  int run() {
    ui_.load_screen_pcx("STATUSBG.PCX");
    ui_.palette_fade_out(0, 0x100);
    art_ = std::make_unique<TorpedoArt>(sprites_, ui_.palette());
    room_.set_sprites(&sprites_, art_.get());
    const Viewport full = Viewport::full(ui_.screen());
    draw_sprite_clipped(full, sprites_.frame(1), 0x144, 0x9a);
    draw_sprite_clipped(full, sprites_.frame(2), 6, 0x9a);
    page_ = ui_.screen();
    racks_background_ = Image8(kScreenWidth, TorpedoRoom::kCardsY - 2 - TorpedoRoom::kRacksY);
    for (int y = 0; y < racks_background_.height(); ++y) {
      for (int x = 0; x < kScreenWidth; ++x) {
        racks_background_.set(x, y, page_.at(x, y + TorpedoRoom::kRacksY));
      }
    }
    held_background_ = Image8(2 * kTorpedoHalfWidth + 1, kTorpedoHeight);

    ui::Dialog d(ui_.gui(), 0, 0, kScreenWidth - 1, kScreenHeight - 1, 0, &ui_.screen());
    d.emplace<ui::Area>(
        0, 0, kScreenWidth - 1, kBottomButtonsY - 3,
        [this](ui::Dialog&, ui::Area&, ui::Msg msg, int, const ui::Event& e) {
          if (msg == ui::Msg::kPress) {
            click(e.x, e.y);
          }
          return 0;
        },
        ui::Area::ActivateFn{});
    // The supply's - and + buttons.
    const std::vector<int> columns = supply_columns();
    for (std::size_t i = 0; i < editor_.offers().size(); ++i) {
      const std::uint32_t id = editor_.offers()[i].id;
      d.emplace<ui::Button>(columns[i] - kSupplyButtonWidth - 1, kSupplyButtonsY, kSupplyButtonWidth, "-",
                            [this, id](int) {
        if (!editor_.remove(id)) {
          message_ = editor_.held() != 0 ? std::string(kPutHint) : std::string();
        }
        changed_ = true;
        return 0;
      });
      d.emplace<ui::Button>(columns[i] + 1, kSupplyButtonsY, kSupplyButtonWidth, "+", [this, id](int) {
        refuse(id, editor_.can_take(id));
        editor_.add(id);
        changed_ = true;
        return 0;
      });
    }
    d.emplace<ui::Button>(0x14, kBottomButtonsY, 0xa0, "DEFAULT LOAD", [](int) { return kReset; });
    d.set_back(d.emplace<ui::Button>(0x140, kBottomButtonsY, 0x8c, "CANCEL", ui::return_cancel));
    d.emplace<ui::Button>(0x1e0, kBottomButtonsY, 0x8c, "ACCEPT", [](int) { return kAccept; });
    ui_.add_exit_area(d);
    d.set_idle([&](const Viewport&, int first) {
      if (first != 0) {
        draw_all();
        d.invalidate();
        ui_.palette_fade_in(ui_.palette(), 0, 10);
        return;
      }
      if (changed_) {
        changed_ = false;
        erase_held();
        draw_all();
        d.invalidate();
      }
      draw_held();
    });
    // The in-game dialogs' look, which the page's palette shows right.
    ui_.gui().set_skin(ui_.default_skin());
    while (true) {
      const int result = d.run();
      if (result != kReset) {
        return result;
      }
      editor_.reset();
      message_.clear();
      changed_ = true;
    }
  }

 private:
  static constexpr std::string_view kTakeHint = "Pick a torpedo up, then put it in a tube or a rack.";
  static constexpr std::string_view kPutHint = "Put it in a tube or a rack, or back on its pile.";

  std::vector<int> supply_columns() const {
    const std::size_t n = editor_.offers().size();
    const int left = (kScreenWidth - static_cast<int>(n) * kSupplyColumn) / 2 + kSupplyColumn / 2;
    std::vector<int> out;
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(left + static_cast<int>(i) * kSupplyColumn);
    }
    return out;
  }

  void centred(int cx, int y, std::string_view text) {
    TorpedoRoom::shadowed_text(Viewport::full(ui_.screen()), cx - font_.text_width(text) / 2, y, text, font_,
                               light_);
  }

  // Shows why a torpedo of `id` cannot be taken (nothing when it can).
  void refuse(std::uint32_t id, TorpedoLoadEditor::Refusal why) {
    const Torpedo* t = tables_.find_torpedo_by_id(id);
    const std::string name = t != nullptr ? t->long_name : std::string();
    switch (why) {
      case TorpedoLoadEditor::Refusal::kNone:
        message_.clear();
        break;
      case TorpedoLoadEditor::Refusal::kNotOffered:
        message_ = editor_.held() != 0 ? std::string(kPutHint) : std::string();
        break;
      case TorpedoLoadEditor::Refusal::kSupply:
        message_ = std::format("Only {} {} can be had this month.", editor_.limit(id).value_or(0), name);
        break;
      case TorpedoLoadEditor::Refusal::kTypes:
        message_ = "Three types of torpedoes aboard at most.";
        break;
      case TorpedoLoadEditor::Refusal::kFull:
        message_ = "Every tube and rack is loaded.";
        break;
    }
  }

  void draw_all() {
    Image8& screen = ui_.screen();
    screen = page_;
    const Viewport full = Viewport::full(screen);
    hotspots_.clear();
    // The supply.
    const std::vector<int> columns = supply_columns();
    for (std::size_t i = 0; i < editor_.offers().size(); ++i) {
      const TorpedoOffer& o = editor_.offers()[i];
      const Torpedo* t = tables_.find_torpedo_by_id(o.id);
      centred(columns[i], kSupplyNameY, t != nullptr ? t->long_name : std::string());
      room_.draw_torpedo(full, 0xc + 2 * static_cast<std::size_t>(o.id) - 2, o.id, false, columns[i],
                         kSupplyTorpedoY, false);
      centred(columns[i], kSupplyCountY, std::format("Aboard: {}", editor_.count(o.id)));
      centred(columns[i], kSupplyLimitY, o.limit ? std::format("Supply: {}", *o.limit) : "Supply: any");
    }
    centred(kScreenWidth / 2, kMessageY,
            !message_.empty() ? message_
            : editor_.held() != 0
                ? std::string(kPutHint)
                : std::format("The load of the patrol of {}. {}", patrol_, kTakeHint));
    // The rooms.
    std::array<TorpedoRoom::Tube, TorpedoLoad::kTubes> tubes{};
    const std::size_t n = editor_.places(TorpedoPlace::Group::kTube);
    for (std::size_t i = 0; i < n; ++i) {
      tubes[i].content = editor_.at({TorpedoPlace::Group::kTube, i});
    }
    room_.draw_tubes(full, std::span(tubes).first(n), static_cast<std::int32_t>(bow_tubes_), &hotspots_);
    room_.draw_racks(full,
                     std::span(editor_.load().bow_racks).first(editor_.places(TorpedoPlace::Group::kBowRack)),
                     std::span(editor_.load().stern_racks).first(editor_.places(TorpedoPlace::Group::kSternRack)),
                     &racks_background_, &hotspots_);
    // The cards of the types aboard.
    const std::vector<std::uint32_t> types = editor_.types();
    const std::vector<int> xs = room_.card_positions(types.size());
    for (std::size_t i = 0; i < types.size(); ++i) {
      room_.draw_card(full, xs[i], TorpedoRoom::kCardsY, types[i], tables_, font_, light_, texts_);
    }
    drawn_x_ = -1;
  }

  void click(int x, int y) {
    message_.clear();
    changed_ = true;
    if (y < kSupplyHeight) {
      if (editor_.held() != 0) {
        editor_.drop();
        return;
      }
      const std::vector<int> columns = supply_columns();
      for (std::size_t i = 0; i < columns.size(); ++i) {
        if (std::abs(x - columns[i]) <= kTorpedoHalfWidth && y >= kSupplyTorpedoY - 6 &&
            y <= kSupplyTorpedoY + kTorpedoHeight + 4) {
          const std::uint32_t id = editor_.offers()[i].id;
          refuse(id, editor_.can_take(id));
          editor_.take(id);
        }
      }
      return;
    }
    const TorpedoRoom::Hotspot* hit = nullptr;
    for (const TorpedoRoom::Hotspot& h : hotspots_) {
      if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) {
        hit = &h;
      }
    }
    if (hit == nullptr) {
      return;
    }
    const TorpedoPlace place{hit->kind == TorpedoRoom::kTubeHotspot      ? TorpedoPlace::Group::kTube
                             : hit->kind == TorpedoRoom::kBowRackHotspot ? TorpedoPlace::Group::kBowRack
                                                                         : TorpedoPlace::Group::kSternRack,
                             static_cast<std::size_t>(hit->index)};
    if (editor_.held() != 0) {
      editor_.put(place);
    } else {
      editor_.pick(place);
    }
  }

  void erase_held() {
    if (drawn_x_ == -1) {
      return;
    }
    const Viewport full = Viewport::full(ui_.screen());
    for (int j = 0; j < held_background_.height(); ++j) {
      for (int i = 0; i < held_background_.width(); ++i) {
        const int sx = drawn_x_ - kTorpedoHalfWidth + i;
        const int sy = drawn_y_ + j - 2;
        if (full.bitmap->contains(sx, sy)) {
          full.bitmap->set(sx, sy, held_background_.at(i, j));
        }
      }
    }
    drawn_x_ = -1;
  }

  // The held torpedo under the pointer, mirrored over the stern room.
  void draw_held() {
    const ui::Mouse& mouse = ui_.gui().mouse();
    const int x = mouse.x();
    const int y = std::min(mouse.y(), kBottomButtonsY - kTorpedoHeight);
    if (editor_.held() == 0 || (x == drawn_x_ && y == drawn_y_)) {
      if (editor_.held() == 0) {
        erase_held();
      }
      return;
    }
    erase_held();
    const Image8& screen = ui_.screen();
    for (int j = 0; j < held_background_.height(); ++j) {
      for (int i = 0; i < held_background_.width(); ++i) {
        const int sx = x - kTorpedoHalfWidth + i;
        const int sy = y + j - 2;
        held_background_.set(i, j, screen.contains(sx, sy) ? screen.at(sx, sy) : 0);
      }
    }
    drawn_x_ = x;
    drawn_y_ = y;
    const std::uint32_t id = editor_.held();
    room_.draw_torpedo(Viewport::full(ui_.screen()), 0xd + 2 * static_cast<std::size_t>(id) - 2, id, true, x, y,
                       x < kScreenWidth / 2);
  }

  CareerUi& ui_;
  const GameTables& tables_;
  TorpedoLoadEditor& editor_;
  std::string patrol_;
  StatusTexts texts_;
  Font font_;
  Shp sprites_;
  std::unique_ptr<TorpedoArt> art_;
  TorpedoRoom room_;
  GlyphColourTable light_{};
  Image8 page_;
  Image8 racks_background_;
  Image8 held_background_;
  std::vector<TorpedoRoom::Hotspot> hotspots_;
  std::string message_;
  bool changed_ = false;
  int drawn_x_ = -1;
  int drawn_y_ = -1;
  std::size_t bow_tubes_ = 0;
};

}  // namespace

std::optional<std::size_t> requests_menu_screen(CareerUi& ui, std::span<const RequestItem> items, bool redraw) {
  if (redraw) {
    ui.load_screen_pcx(ui.exe_string(addr::kBasePcx));
  }
  ui.gui().set_skin(ui.menu_skin());
  if (redraw) {
    ui.palette_fade_out(0, 0x100);
    ui.palette_fade_in(ui.palette(), 0, 10);
  }
  const int count = static_cast<int>(items.size());
  const int back_y = kFirstButtonY + kButtonStep * count;
  const int h = back_y + kMenuBottom;
  ui::Dialog d(ui.gui(), kMenuX, (kScreenHeight - h) / 2, kMenuWidth, h, kMenuFlags);
  ui.add_exit_area(d);
  d.emplace<ui::TitleBar>(std::string(kRequestsLabel));
  for (int i = 0; i < count; ++i) {
    const RequestItem& item = items[static_cast<std::size_t>(i)];
    auto& b = d.emplace<ui::Button>(kButtonX, kFirstButtonY + kButtonStep * i, kButtonWidth, item.label,
                                    [i](int) { return i + 1; });
    b.disabled = !item.enabled;
  }
  d.set_back(d.emplace<ui::Button>(kButtonX, back_y, kButtonWidth, std::string(kBaseMenuLabel), ui::return_cancel));
  const int result = ui.run_menu_dialog(d);
  if (result < 1 || result > count) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(result - 1);
}

std::optional<std::size_t> list_choice_screen(CareerUi& ui, std::string_view title,
                                              std::span<const std::string> rows, std::size_t selection) {
  constexpr int kWidth = 0x1c0;
  constexpr int kHeight = 0x170;
  constexpr int kListY = 0x84;
  constexpr int kButtonsY = kHeight - 0x46;
  constexpr int kSmallButton = 0x79;
  ui.gui().set_skin(ui.menu_skin());
  ui::Dialog d(ui.gui(), (kScreenWidth - kWidth) / 2, (kScreenHeight - kHeight) / 2, kWidth, kHeight, kMenuFlags);
  d.emplace<ui::TitleBar>(std::string(title));
  auto& list = d.emplace<ui::List>(
      0x1e, kListY, kWidth - 0x3c, [&rows] { return static_cast<int>(rows.size()); },
      [&rows](int row) { return rows[static_cast<std::size_t>(row)]; });
  list.on_activate = [&d](int) { d.set_result(ui::kResultAccept); };
  d.set_back(d.emplace<ui::Button>(0x2a, kButtonsY, kSmallButton, "CANCEL", ui::return_cancel));
  d.emplace<ui::Button>(kWidth - 0x2a - kSmallButton, kButtonsY, kSmallButton, "SELECT", ui::return_accept);
  ui.add_exit_area(d);
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      list.selection = static_cast<int>(selection);
      list.top = 0;
      list.dirty = true;
    }
  });
  const int result = ui.run_menu_dialog(d);
  if (result != ui::kResultAccept || list.selection < 0 || list.selection >= static_cast<int>(rows.size())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(list.selection);
}

bool torpedo_load_screen(CareerUi& ui, const GameTables& tables, const SubClass& sub, TorpedoLoadEditor& editor,
                         std::int32_t patrol_month, std::int32_t patrol_year) {
  TorpedoLoadScreen screen(ui, tables, sub, editor, month_year(ui.exe(), patrol_month, patrol_year));
  const int result = screen.run();
  editor.drop();
  return result == kAccept;
}

void deck_gun_screen(CareerUi& ui, const SubClass& sub, const CareerRecord& record, BoatFit& fit) {
  constexpr int kMove = 1;
  constexpr int kSecond = 2;
  constexpr int kWidth = 0x1a4;
  while (true) {
    const SubClass fitted = fitted_sub_class(sub, fit);
    const std::string guns =
        fitted.deck_gun_count > 1
            ? std::format("Two {}-inch guns, forward and aft, {} rounds.", fitted.deck_gun_caliber_in,
                          fitted.deck_gun_rounds)
            : std::format("One {}-inch gun {}, {} rounds.", fitted.deck_gun_caliber_in,
                          fitted.single_gun_mount == 0 ? "forward" : "aft", fitted.deck_gun_rounds);
    const std::string note = fit.second_gun                                ? "The second gun stays fitted."
                             : can_fit_second_gun(sub, fit, record)        ? "A second gun can be fitted."
                             : fleet_boat_with_one_gun(sub)                ? "A second gun needs a commander in good standing."
                                                                           : "The guns of this class cannot be changed.";
    // The lines below the title, then the buttons.
    constexpr int kLinesY = 0x82;
    constexpr int kLineStep = 0x12;
    constexpr int kButtonsY = kLinesY + 3 * kLineStep + 0x0a;
    const int h = kButtonsY + 2 * kButtonStep + kMenuBottom;
    ui::Dialog d(ui.gui(), (kScreenWidth - kWidth) / 2, (kScreenHeight - h) / 2, kWidth, h, kMenuFlags);
    ui.add_exit_area(d);
    d.emplace<ui::TitleBar>(std::string(kDeckGunLabel));
    const std::array<std::string, 3> lines{record.boat + ", " + sub.display_name, guns, note};
    for (std::size_t i = 0; i < lines.size(); ++i) {
      d.emplace<ui::Label>(0x14, kLinesY + kLineStep * static_cast<int>(i), lines[i], ui::FontSlot::kFont1);
    }
    const int bx = (kWidth - kButtonWidth) / 2;
    auto& move = d.emplace<ui::Button>(bx, kButtonsY, kButtonWidth,
                                       fitted.single_gun_mount == 0 ? "MOVE GUN AFT" : "MOVE GUN FORWARD",
                                       [](int) { return kMove; });
    move.disabled = !can_move_deck_gun(sub, fit);
    auto& second = d.emplace<ui::Button>(bx, kButtonsY + kButtonStep, kButtonWidth, "FIT SECOND GUN",
                                         [](int) { return kSecond; });
    second.disabled = !can_fit_second_gun(sub, fit, record);
    d.set_back(d.emplace<ui::Button>(bx, kButtonsY + 2 * kButtonStep, kButtonWidth, "DONE", ui::return_cancel));
    const int result = ui.run_menu_dialog(d);
    if (result == kMove) {
      fit.gun_mount = fitted.single_gun_mount == 0 ? 1u : 0u;
    } else if (result == kSecond) {
      ui.gui().set_skin(ui.default_skin());
      const int answer = ui.ask_yes_no(kDeckGunLabel, std::format("Fit a second {}-inch gun?", sub.deck_gun_caliber_in),
                                       "It stays fitted.", ui.exe_string(addr::kYes), ui.exe_string(addr::kNo));
      if (answer == ui::kResultAccept) {
        fit.second_gun = true;
      }
    } else {
      return;
    }
  }
}

}  // namespace sh

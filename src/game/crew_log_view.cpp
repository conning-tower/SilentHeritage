#include "game/crew_log_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "assets/exe_tables.h"
#include "sim/clock.h"
#include "ui/controls.h"
#include "ui/dialog.h"
#include "ui/hotspot.h"

namespace sh {
namespace {

// The dialog, over the stations above the bar.
constexpr int kDialogX = 0x28;
constexpr int kDialogY = 0x10;
constexpr int kDialogW = 0x230;
constexpr int kDialogH = 0x1b4;
// The list's box and lines, then the scroll bar on its right.
constexpr int kListX = 0x12;
constexpr int kListY = 0x34;
constexpr int kListW = 0x1f0;
constexpr int kRowHeight = 13;
constexpr int kRows = 25;
constexpr int kListH = kRows * kRowHeight + 6;
constexpr int kTextX = kListX + 6;
constexpr int kTextY = kListY + 4;
constexpr int kBarX = kListX + kListW + 8;
constexpr int kBarW = 20;
constexpr int kThumbH = 16;
constexpr int kButtonW = 0x79;

// The hotspots of the list besides the lines (sub 0) and the track (1).
enum Sub : int { kSubLines = 0, kSubTrack, kSubUp, kSubDown, kSubPageUp, kSubPageDown, kSubHome, kSubEnd };

// Returns the date heading of `entry`: "June 3, 1943".
std::string date_text(const ExeImage& exe, const CrewLogEntry& entry) {
  return exe_month_name(exe, entry.month) + " " + std::to_string(entry.day) + ", " +
         std::to_string(entry.year + kCalendarBaseYear);
}

// The list: its box, the visible lines and the scroll bar's track. The
// arrows are buttons of the dialog, disabled at the ends.
class LogList : public ui::Control {
 public:
  // `button_height` is the skin's, which places the arrows.
  LogList(const ExeImage& exe, const CrewLog& log, std::vector<CrewLogLine> lines, int time_width, int button_height)
      : exe_(exe), log_(log), lines_(std::move(lines)), time_width_(time_width), button_height_(button_height),
        scroll_(static_cast<int>(lines_.size()), kRows) {}

  // Returns the y of the up and down arrows.
  static int up_y() { return kListY + 2; }
  int down_y() const { return kListY + kListH - 2 - button_height_; }

  // The arrow buttons, updated with the position.
  ui::Button* up = nullptr;
  ui::Button* down = nullptr;

  // Scrolls by `lines`; returns whether it moved.
  bool scroll_by(ui::Dialog& dialog, int lines) { return moved(dialog, scroll_.scroll_by(lines)); }

  bool handle(ui::Dialog& dialog, ui::Msg msg, int sub, const ui::Event& event) override {
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(dialog, *this, {kListX, kListY, kListX + kListW - 1, kListY + kListH - 1}, kSubLines,
                                0, ui::kFocusNone);
        ui::add_control_hotspot(dialog, *this, track_rect(), kSubTrack, 0, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubUp, ui::kKeyUp, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubDown, ui::kKeyDown, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubPageUp, ui::kKeyPageUp, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubPageDown, ui::kKeyPageDown, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubHome, ui::kKeyHome, ui::kFocusNone);
        ui::add_key_hotspot(dialog, *this, kSubEnd, ui::kKeyEnd, ui::kFocusNone);
        update_arrows();
        return false;
      case ui::Msg::kPaint:
        draw(dialog);
        return true;
      case ui::Msg::kPress:
        if (event.is_key()) {
          return false;
        }
        press_y_ = event.y - dialog.y();
        press_top_ = scroll_.top();
        if (sub == kSubTrack) {
          const ui::Rect t = thumb_rect();
          dragging_thumb_ = press_y_ >= t.y0 && press_y_ <= t.y1;
          if (!dragging_thumb_) {
            return scroll_by(dialog, press_y_ < t.y0 ? -kRows : kRows);
          }
        }
        return false;
      case ui::Msg::kRepeat: {
        if (event.is_key()) {
          return false;
        }
        const int dy = event.y - dialog.y() - press_y_;
        if (sub == kSubLines) {
          // The lines follow the pointer (a touch drag).
          return moved(dialog, scroll_.scroll_to(press_top_ - dy / kRowHeight));
        }
        if (sub == kSubTrack && dragging_thumb_ && scroll_.max_top() > 0) {
          const int span = track_rect().y1 - track_rect().y0 + 1 - kThumbH;
          return moved(dialog, scroll_.scroll_to(press_top_ + (dy * scroll_.max_top() + span / 2) / span));
        }
        return false;
      }
      default:
        return false;
    }
  }

  int activate(ui::Dialog& dialog, int sub, const ui::Event&) override {
    switch (sub) {
      case kSubUp:
        scroll_by(dialog, -1);
        break;
      case kSubDown:
        scroll_by(dialog, 1);
        break;
      case kSubPageUp:
        scroll_by(dialog, -kRows);
        break;
      case kSubPageDown:
        scroll_by(dialog, kRows);
        break;
      case kSubHome:
        moved(dialog, scroll_.scroll_to(0));
        break;
      case kSubEnd:
        moved(dialog, scroll_.scroll_to(scroll_.max_top()));
        break;
      default:
        break;
    }
    return 0;
  }

 private:
  // Between the arrows' frames (2 pixels around a button).
  ui::Rect track_rect() const { return {kBarX, up_y() + button_height_ + 4, kBarX + kBarW - 1, down_y() - 5}; }
  ui::Rect thumb_rect() const {
    const ui::Rect t = track_rect();
    const int span = t.y1 - t.y0 + 1 - kThumbH;
    const int y = scroll_.max_top() == 0 ? 0 : (scroll_.top() * span + scroll_.max_top() / 2) / scroll_.max_top();
    return {t.x0, t.y0 + y, t.x1, t.y0 + y + kThumbH - 1};
  }

  // Redraws after a move; returns `did`.
  bool moved(ui::Dialog& dialog, bool did) {
    if (did) {
      draw(dialog);
      update_arrows();
    }
    return did;
  }

  void update_arrows() {
    for (const auto& [button, end] : {std::pair{up, scroll_.at_start()}, std::pair{down, scroll_.at_end()}}) {
      if (button != nullptr && button->disabled != end) {
        button->disabled = end;
        button->dirty = true;
      }
    }
  }

  void draw(ui::Dialog& dialog) {
    ui::Skin& skin = dialog.skin();
    const Viewport& gfx = dialog.gfx();
    // The box and the track: the skin's text box frame, sunken.
    dialog.add_dirty(skin.textbox_frame(gfx, kListX, kListY, kListW - 15, kListH - 4));
    skin.textbox_background(gfx, kListX, kListY, kListW - 15, kListH - 4);
    const ui::Rect t = track_rect();
    dialog.add_dirty(skin.textbox_frame(gfx, t.x0, t.y0, kBarW - 15, t.y1 - t.y0 - 3));
    skin.textbox_background(gfx, t.x0, t.y0, kBarW - 15, t.y1 - t.y0 - 3);
    const ui::Rect th = thumb_rect();
    skin.button(gfx, th.x0 + 3, th.y0 + 2, kBarW - 6, "", false, false, false, scroll_.max_top() == 0);

    if (lines_.empty()) {
      skin.label(gfx, kTextX, kTextY, ui::FontSlot::kSmall, "No messages.", true, nullptr);
      return;
    }
    const int x1 = kListX + kListW - 8;
    int y = kTextY;
    const int end = std::min(scroll_.top() + kRows, static_cast<int>(lines_.size()));
    for (int i = scroll_.top(); i < end; ++i, y += kRowHeight) {
      const CrewLogLine& line = lines_[static_cast<std::size_t>(i)];
      const CrewLogEntry& e = log_.entries()[line.entry];
      switch (line.kind) {
        case CrewLogLine::Kind::kDate: {
          const std::string date = date_text(exe_, e);
          skin.label(gfx, kTextX, y, ui::FontSlot::kSmall, date, true, nullptr);
          const int w = skin.font(ui::FontSlot::kSmall).text_width(date);
          skin.line(gfx, kTextX + w + 6, y + 5, x1, y + 5, 3);
          break;
        }
        case CrewLogLine::Kind::kMessage:
          skin.label(gfx, kTextX, y, ui::FontSlot::kSmall, crew_log_time(e), true, nullptr);
          [[fallthrough]];
        case CrewLogLine::Kind::kContinuation:
          skin.label(gfx, kTextX + time_width_, y, ui::FontSlot::kSmall, line.text, true, nullptr);
          break;
      }
    }
  }

  const ExeImage& exe_;
  const CrewLog& log_;
  std::vector<CrewLogLine> lines_;
  int time_width_;
  int button_height_;
  CrewLogScroll scroll_;
  int press_y_ = 0;
  int press_top_ = 0;
  bool dragging_thumb_ = false;
};

}  // namespace

void open_crew_log(StationEnv& env, const CrewLog& log, std::function<void()> done) {
  const Font& font = env.gui.skin().font(ui::FontSlot::kSmall);
  const int time_width = font.text_width("00:00:00") + 8;
  auto d = std::make_unique<ui::Dialog>(env.gui, kDialogX, kDialogY, kDialogW, kDialogH,
                                        ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d->emplace<ui::TitleBar>("CREW MESSAGES", ui::return_accept);
  auto& list = d->emplace<LogList>(env.exe, log, crew_log_lines(log.entries(), font, kListW - 12 - time_width),
                                   time_width, env.gui.skin().metrics().button_height);
  LogList* l = &list;
  list.up = &d->emplace<ui::Button>(
      kBarX, LogList::up_y(), kBarW, "\x1e",
      [l, dialog = d.get()](int) {
        l->scroll_by(*dialog, -1);
        return 0;
      },
      0, 5);
  list.down = &d->emplace<ui::Button>(
      kBarX, list.down_y(), kBarW, "\x1f",
      [l, dialog = d.get()](int) {
        l->scroll_by(*dialog, 1);
        return 0;
      },
      0, 5);
  d->emplace<ui::Button>(kDialogW - 0x14 - kButtonW, kDialogH - 0x26, kButtonW, "Done", ui::return_accept);
  d->emplace<ui::Area>(-1, -1, -1, -1, nullptr,
                       [](ui::Dialog&, ui::Area&, int, const ui::Event&) { return ui::kResultAccept; }, kCrewLogKey);
  env.modals.run_modal(std::move(d), ModalFrame::kMenu, [done = std::move(done)](int) {
    if (done) {
      done();
    }
  });
}

}  // namespace sh

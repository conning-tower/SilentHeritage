#include "game/command_bar.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "game/crew_log_view.h"
#include "game/extended_config.h"
#include "game/mission.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "ui/draw.h"

namespace sh {

namespace {

using HA = HotkeyAction;

// The GLOBAL.SHP frames of the bar.
constexpr std::size_t kClockBoxFrame = 0x24;
constexpr std::uint8_t kCompressionFrame = 0x12;
constexpr std::uint8_t kCompressionPressedFrame = 0x13;
// The bar dialog 0x11e540: 640 x 20 at y 459, cancellable, no navigation.
constexpr int kBarY = 0x1cb;
constexpr int kBarWidth = 0x280;
constexpr int kBarHeight = 0x14;
constexpr int kButtonWidth = 0x48;
constexpr int kButtonStep = 0x49;
// The menus grey the palette to 70 % (`palette_grey_out(0x46, 0xff)`).
constexpr std::int32_t kSfxClick = 2;
constexpr std::int32_t kSfxDive = 8;
constexpr std::int32_t kSfxBlow = 0xc;
constexpr std::int32_t kSfxSurface = 0xd;
constexpr std::int32_t kSfxFire = 0x14;

// Crew messages.
constexpr std::int32_t kMsgUpPeriscope = 3;
constexpr std::int32_t kMsgRadarDepth = 6;
constexpr std::int32_t kMsgStraightAndLevel = 0xb;
constexpr std::int32_t kMsgPlanesRise = 0xf;
constexpr std::int32_t kMsgPlanesDive = 0x10;
constexpr std::int32_t kMsgAaManned = 0x3e;
constexpr std::int32_t kMsgSilentRunning = 0x40;
constexpr std::int32_t kMsgSecureSilent = 0x41;
constexpr std::int32_t kMsgJettison = 0x46;
constexpr std::int32_t kMsgRigForRed = 0x4a;

// The menu data in SH.EXE.
constexpr std::uint32_t k1mcCells = 0x108fd4;
constexpr std::uint32_t kQuitCells = 0x109114;
constexpr std::uint32_t kEndMissionCells = 0x109164;
constexpr std::uint32_t kEndPatrolCells = 0x1091dc;
constexpr std::uint32_t kExecutiveOfficer = 0x108e68;  // char* "Executive Officer"
constexpr std::uint32_t kCannotReturn = 0x108e84;      // char*[5]

// The menus' texts in SH.EXE (gui.cpp / guiuser.cpp strings).
namespace text {
constexpr std::uint32_t k1mc = 0xf42a4;
constexpr std::uint32_t kQuitToDos = 0xf42a8;
constexpr std::uint32_t kEndMission = 0xf42b5;
constexpr std::uint32_t kEndPatrol = 0xf42c2;
constexpr std::uint32_t kTimeCompression = 0xf42ce;
constexpr std::uint32_t kMinus = 0xf42df;
constexpr std::uint32_t kPlus = 0xf42e1;
constexpr std::uint32_t kDetailLevel = 0xf42e3;
constexpr std::uint32_t kMiles = 0xf42f0;
constexpr std::uint32_t kArrowUp = 0xf42f6;  // glyph 0x1e
constexpr std::uint32_t kArrowDown = 0xf42f8;  // glyph 0x1f
constexpr std::uint32_t kVisibility = 0xf42fa;
constexpr std::uint32_t kClouds = 0xf4306;
constexpr std::uint32_t kWaves = 0xf430d;
constexpr std::uint32_t kLandTexture = 0xf4313;
constexpr std::uint32_t kShipWakes = 0xf4320;
constexpr std::uint32_t kTorpedoWakes = 0xf432b;
constexpr std::uint32_t kGameOptions = 0xf4339;
// The extended variant's GAME OPTIONS button and page title (not in the
// original).
constexpr std::string_view kExtendedButton = "EXTENDED";
constexpr std::string_view kExtendedTitle = "EXTENDED OPTIONS";
constexpr std::uint32_t kAutoTorpLoad = 0xf4346;
constexpr std::uint32_t kAutoSjRadar = 0xf4355;
constexpr std::uint32_t kAutoSdRadar = 0xf4363;
constexpr std::uint32_t kAutoRedLights = 0xf4371;
constexpr std::uint32_t kSoundEffects = 0xf4381;
constexpr std::uint32_t kEngineSounds = 0xf438f;
constexpr std::uint32_t kCollisionAlarm = 0xf439d;
constexpr std::uint32_t kSpeech = 0xf43ad;
constexpr std::uint32_t kEffectsSoft = 0xf43b4;
constexpr std::uint32_t kEffectsLoud = 0xf43b9;
constexpr std::uint32_t kEffectsVolume = 0xf43be;
constexpr std::uint32_t kSpeechVolume = 0xf43d9;
constexpr std::uint32_t kSpeechSoft = 0xf43e7;
constexpr std::uint32_t kSpeechLoud = 0xf43ec;
constexpr std::uint32_t kOk = 0xf43f1;
constexpr std::uint32_t kYes = 0xf443f;
constexpr std::uint32_t kNo = 0xf4443;
constexpr std::uint32_t kAreYouSure = 0xf4446;  // ABORT MISSION's
constexpr std::uint32_t kAbortTheMission = 0xf4454;
constexpr std::uint32_t kAbortMission = 0xf4467;
constexpr std::uint32_t kSaveCareer = 0xf4581;  // cmd_game_options' retitles
constexpr std::uint32_t kSaveMission = 0xf458d;
constexpr std::uint32_t kManDeckGun = 0xf468b;
constexpr std::uint32_t kSecureDeckGun = 0xf4698;
constexpr std::uint32_t kManAaGuns = 0xf46a8;
constexpr std::uint32_t kSecureAaGuns = 0xf46b4;
constexpr std::uint32_t kSecureFromRed = 0xf46c3;
constexpr std::uint32_t kRigForRed = 0xf46d3;
constexpr std::uint32_t kSecureFromSilent = 0xf46df;
constexpr std::uint32_t kRigForSilent = 0xf46fa;
constexpr std::uint32_t kAreYouSureCaptain = 0xf4711;
constexpr std::uint32_t kScuttle = 0xf4728;
constexpr std::uint32_t kAreYouSurePatrol = 0xf4746;  // ABORT PATROL's
constexpr std::uint32_t kExitWithoutSaving = 0xf4754;
constexpr std::uint32_t kAbortPatrol = 0xf4775;
}  // namespace text

std::string exe_string_at(const ExeImage& exe, std::uint32_t pointer) { return std::string(exe.c_string(exe.u32(pointer))); }

// Reads `count` 0x28-byte button grid cells at `address`.
std::vector<ui::GridCell> read_cells(const ExeImage& exe, std::uint32_t address, int count) {
  std::vector<ui::GridCell> cells;
  for (int i = 0; i < count; ++i) {
    const std::uint32_t a = address + static_cast<std::uint32_t>(0x28 * i);
    ui::GridCell c;
    c.x = static_cast<std::int32_t>(exe.u32(a));
    c.y = static_cast<std::int32_t>(exe.u32(a + 4));
    c.w = static_cast<std::int32_t>(exe.u32(a + 8));
    c.label = std::string(exe.c_string(exe.u32(a + 0xc)));
    c.up = static_cast<std::int32_t>(exe.u32(a + 0x10));
    c.down = static_cast<std::int32_t>(exe.u32(a + 0x14));
    c.left = static_cast<std::int32_t>(exe.u32(a + 0x18));
    c.right = static_cast<std::int32_t>(exe.u32(a + 0x1c));
    c.key = exe.u32(a + 0x20);
    c.disabled = exe.u32(a + 0x24) != 0;
    cells.push_back(std::move(c));
  }
  return cells;
}

// Returns `value` as `digits` zero-padded digits (`int_to_padded_str`).
std::string padded(std::int32_t value, int digits) {
  std::string s = std::to_string(value);
  if (static_cast<int>(s.size()) < digits) {
    s.insert(0, static_cast<std::size_t>(digits) - s.size(), '0');
  }
  return s;
}

}  // namespace

const std::array<Hotkey, 67>& hotkey_table() {
  static const std::array<Hotkey, 67> table{{
      {0x3b00, HA::kStationConn},       {0x3d00, HA::kStationTbt},        {0x3c00, HA::kStationPeriscope},
      {0x3f00, HA::kStationChart},      {0x4100, HA::kStationGauges},     {0x3e00, HA::kStationBridge},
      {0x6b00, HA::kStationDeckGun},    {0x4000, HA::kStationTdc},        {0x4200, HA::kStationStatus},
      {0x4300, HA::kStationRadar},      {0x4400, HA::kStationLogbook},    {0xfe2b, HA::kCompressionUp},
      {0xfe2d, HA::kCompressionDown},   {0x1900, HA::kTogglePeriscope},   {0x0b30, HA::kAllStop},
      {0x0231, HA::kAheadOneThird},     {0x0332, HA::kAheadTwoThirds},    {0x0433, HA::kAheadStandard},
      {0x0534, HA::kAheadFull},         {0x0635, HA::kAheadFlank},        {0x0736, HA::kBackOneThird},
      {0x0837, HA::kBackTwoThirds},     {0x0938, HA::kBackFull},          {0x0a39, HA::kBackEmergency},
      {0x4d00, HA::kRudderRight},       {0x4b00, HA::kRudderLeft},        {0x4800, HA::kPlanesRise},
      {0x5000, HA::kPlanesDive},        {0x4cf0, HA::kStraightAndLevel},  {0xff42, HA::kBlowBallast},
      {0xff53, HA::kSurface},           {0xff50, HA::kPeriscopeDepth},    {0xff43, HA::kCrashDive},
      {0xff48, HA::kSteerViewBearing},  {0x0e08, HA::kRepeatMessage},     {0x7800, HA::kFireTube1},
      {0x7900, HA::kFireTube2},         {0x7a00, HA::kFireTube3},         {0x7b00, HA::kFireTube4},
      {0x7c00, HA::kFireTube5},         {0x7d00, HA::kFireTube6},         {0x7e00, HA::kFireTube7},
      {0x7f00, HA::kFireTube8},         {0x8000, HA::kFireTube9},         {0x8100, HA::kFireTube10},
      {0x1200, HA::kDebugBboxes},       {0x2000, HA::kDetailLevel},       {0x1400, HA::kPadlockTarget},
      {0xff44, HA::kDebugDamage},       {0xff52, HA::kRadarDepth},        {0x1f00, HA::kToggleSjRadar},
      {0x1e00, HA::kToggleSdRadar},     {0xff56, HA::kViewAhead},         {0xff0d, HA::kCompressionReset},
      {0x1000, HA::kQuitToDos},         {0x2200, HA::kManDeckGun},        {0x1800, HA::kGameOptions},
      {0x2500, HA::kDebugKillTarget},   {0x3200, HA::kDebugMemstat},      {0x1300, HA::kDebugRoutes},
      {0x2e00, HA::kDebugGroupLabels},  {0x7100, HA::kStationCalendar},   {0x7000, HA::kStationRadioLog},
      {0x3000, HA::kDebugBases},        {0xff4d, HA::kMenu1mc},           {0xff45, HA::kRescuePilot},
      {0x0f09, HA::kReconPhoto},
  }};
  return table;
}

std::int32_t telegraph_for_digit(char digit) {
  // key_all_stop .. key_back_emergency (0x4b9f7..0x4ba5a)
  if (digit >= '0' && digit <= '5') {
    return digit - '0';
  }
  return 15 - (digit - '0');
}

// --- Crew message log -------------------------------------------------------

void CrewMessageLog::reset() {
  head_ = tail_ = shown_ = 0;
  push(0, -1, -1, {});
}

void CrewMessageLog::push(std::int32_t id, std::int32_t arg1, std::int32_t arg2, std::string_view text) {
  head_ = (head_ + 1) % kSlots;
  if (tail_ == head_) {
    // The ring is full: the oldest slot goes; the original then copies the
    // new oldest slot over the next one.
    tail_ = (tail_ + 1) % kSlots;
    ring_[(tail_ + 1) % kSlots] = ring_[tail_];
  }
  Entry& e = ring_[head_];
  e.id = id;
  e.arg1 = arg1;
  e.arg2 = arg2;
  e.text = std::string(text.substr(0, kTextMax));
  shown_ = cursor_ = head_;
}

bool CrewMessageLog::repeat() {
  if (tail_ == head_) {
    return false;
  }
  shown_ = cursor_;
  cursor_ = cursor_ == 0 ? kSlots - 1 : cursor_ - 1;
  if (cursor_ == tail_) {
    cursor_ = head_;
  }
  return true;
}

std::string format_crew_message(const CrewMessages& messages, const CrewMessageLog::Entry& entry) {
  // crew_message_format_current (0x4ca19)
  const CrewMessage* m = messages.find(static_cast<std::uint8_t>(entry.id));
  if (m == nullptr) {
    return {};
  }
  std::string out = m->speaker_name + ": ";
  const std::int32_t args[2] = {entry.arg1, entry.arg2};
  for (const char ch : m->text) {
    const auto c = static_cast<std::uint8_t>(ch);
    if (c == 1 || c == 2) {
      if (args[c - 1] != -1) {
        out += std::to_string(args[c - 1]);
      }
    } else if (c == 3) {
      out += entry.text;
    } else if (c == 9 || c == 10) {
      if (args[c - 9] != -1) {
        out += padded(args[c - 9], 3);
      }
    } else if (c == 0x11 || c == 0x12) {
      if (args[c - 0x11] >= 2) {
        out += 's';
      }
    } else {
      out += ch;
    }
  }
  return out;
}

// --- The bar --------------------------------------------------------------

// The sprite buttons of the bar (`sprite_button_proc` 0x4922c,
// `sprite_button_draw` 0x49291, `sprite_button_activate` 0x4939e).
class ControlBar::SpriteButton : public ui::Control {
 public:
  SpriteButton(ControlBar& bar, int x0, int y0, int x1, int y1, BarButton button)
      : bar_(bar), x0_(x0), y0_(y0), x1_(x1), y1_(y1), button_(std::move(button)) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    bool down = false;
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(dialog, *this, {x0_, y0_, x1_, y1_}, 0, 0, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        bar_.env_.sound.play_sfx(kSfxClick, 0x1e);
        down = true;
        break;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        break;
      default:
        return false;
    }
    dialog.add_dirty(draw(dialog, down));
    return true;
  }

  int activate(ui::Dialog&, int, const ui::Event&) override {
    if (button_.action) {
      button_.action();
    }
    return 0;
  }

 private:
  ui::Rect draw(ui::Dialog& dialog, bool down) {
    const std::uint8_t f = down ? button_.pressed_frame : button_.frame;
    const SpriteFrame& frame = bar_.env_.assets.global.frame(f);
    draw_sprite_clipped(dialog.gfx(), frame, x0_, y0_);
    const ui::Rect r{x0_, y0_, x0_ + frame.width() - 1, y0_ + frame.height() - 1};
    if (f == kCompressionFrame || f == kCompressionPressedFrame) {
      // The effective compression, centred, glyph colour 7 drawn black.
      const std::int32_t compression = bar_.mission_clock_effective();
      const std::string text = std::to_string(compression);
      const Font& font = bar_.env_.assets.font1;
      const std::int32_t free = r.x1 - r.x0 + 1 - font.text_width(text) - 1;
      int x = x0_;
      int y = y0_;
      if (down) {
        ++y;
        --x;
      }
      GlyphColourTable table = ui::GuiResources::identity_table();
      table[7] = 0;
      gui_draw_text(dialog.gfx(), x + 1 + free / 2, y + 5, font, text, &table);
      bar_.shown_compression_ = compression;
    }
    return r;
  }

  ControlBar& bar_;
  int x0_, y0_, x1_, y1_;
  BarButton button_;
};

ControlBar::ControlBar(StationEnv& env) : env_(env), messages_(env.exe) {}

ControlBar::~ControlBar() = default;

std::int32_t ControlBar::mission_clock_effective() const { return env_.mission.sim().clock().effective_compression(); }

PlayerSub& ControlBar::player() { return env_.mission.player(); }

SimContext ControlBar::sim_context() { return env_.mission.sim().context(); }

void ControlBar::flush_sim_events() {
  const MissionEvents events = env_.mission.sim().take_events();
  for (const std::string& file : events.speech) {
    env_.sound.play_speech(file);
  }
  post(events.crew_messages);
  for (const MissionEvents::Sfx& s : events.sfx) {
    env_.sound.play_sfx(s.id, s.volume);
  }
}

void ControlBar::open(const BarConfig& config) {
  // control_bar_open (0x48d02)
  config_ = config;
  button_slots_ = static_cast<std::int32_t>(config.buttons.size()) + (config.time_compression ? 1 : 0);
  build(config);
  dialog_->open();
  open_ = true;
  redraw();
  shown_compression_ = mission_clock_effective();
  env_.default_cursor();
  message_drawn_ = false;
  escape_passthrough_ = false;
}

void ControlBar::build(const BarConfig& config) {
  // control_bar_build (0x48e84)
  dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, kBarY, kBarWidth, kBarHeight,
                                         ui::kDialogCancel | ui::kDialogNoNavigation, &env_.gui.screen());
  if (config.hotkeys) {
    // control_bar_add_hotkeys: key-only areas running `hotkey_proc`.
    for (const Hotkey& h : hotkey_table()) {
      const HotkeyAction action = h.action;
      dialog_->emplace<ui::Area>(
          -1, -1, -1, -1,
          [this, action](ui::Dialog&, ui::Area& area, ui::Msg msg, int, const ui::Event&) {
            // hotkey_proc (0x493ad): the pending events are dropped; an
            // activation among them is posted again.
            if (msg != ui::Msg::kPress || area.pressed) {
              return 0;
            }
            std::optional<ui::Event> activation;
            while (const std::optional<ui::Event> e = env_.gui.events().get()) {
              if (e->is_key() && e->key == ui::kKeyActivate) {
                activation = e;
              }
            }
            run(action);
            if (activation) {
              env_.gui.events().post(*activation);
            }
            return 0;
          },
          nullptr, h.key);
    }
  }
  if (config.clock) {
    // control_bar_add_clock: text field 0x11f4bc.
    auto& clock = dialog_->emplace<ui::TextField>(
        4, 4, 0x45, 0x12,
        [this] {
          // format_clock_hms (0x4b762)
          const GameClock& c = env_.mission.sim().clock();
          return padded(c.hour(), 2) + ":" + padded(c.minute(), 2) + ":" + padded(c.second(), 2);
        },
        ui::FontSlot::kCustom, TextAlign::kLeft);
    clock.use_colours = false;
    clock.fill = 0;
    clock.custom = &env_.assets.digits2;
    clock_field_ = &clock;
  } else {
    clock_field_ = nullptr;
  }
  if (env_.ui.features.crew_log) {
    // The message strip and `L` open the crew log (not in the original).
    const auto open_log = [this](ui::Dialog&, ui::Area&, int, const ui::Event&) {
      open_crew_log();
      return 0;
    };
    dialog_->emplace<ui::Area>(0, 0, kBarWidth - button_slots_ * kButtonStep - 2, kBarHeight - 1, nullptr, open_log);
    if (config.hotkeys) {
      dialog_->emplace<ui::Area>(-1, -1, -1, -1, nullptr, open_log, kCrewLogKey);
    }
  }
  int x = kBarWidth - kButtonWidth;
  for (const BarButton& b : config.buttons) {
    dialog_->emplace<SpriteButton>(*this, x, 0, x + kButtonWidth, 0x13, b);
    x -= kButtonStep;
  }
  if (config.time_compression) {
    // control_bar_add_time_compression (0x49128)
    compression_button_ = &dialog_->emplace<SpriteButton>(
        *this, x, 0, x + kButtonWidth, 0x13,
        BarButton{kCompressionFrame, kCompressionPressedFrame, [this] { menu_time_compression(); }});
  } else {
    compression_button_ = nullptr;
  }
}

void ControlBar::redraw() {
  // control_bar_redraw (0x48f37)
  const Viewport& gfx = dialog_->gfx();
  ui::gui_fill_rect(gfx, 0, 0, gfx.x1 - gfx.x0 + 1, gfx.y1 - gfx.y0 + 1, 0);
  dialog_->add_dirty({0, 0, gfx.x1 - gfx.x0, gfx.y1 - gfx.y0});
  if (config_.clock) {
    draw_sprite_clipped(gfx, env_.assets.global.frame(kClockBoxFrame), 2, 2);
  }
  dialog_->invalidate();
}

void ControlBar::close() {
  // control_bar_close (0x48d67)
  if (!open_) {
    return;
  }
  redraw();
  dialog_->close();
  open_ = false;
}

void ControlBar::event(const ui::Event& event) {
  // control_bar_handle_event (0x48d9f)
  if (message_state_ != 0 && message_skip_ != 0) {
    --message_skip_;
  } else if (message_state_ != 0 && (event.flags & 0x802a) != 0 &&
             (!event.is_key() || event.key != ui::kKeyActivate)) {
    ++message_state_;
  }
  const bool escape = (event.is_key() && event.key == ui::kKeyEsc) || (event.flags & ui::kEventRightUp) != 0;
  if (!escape_passthrough_ || !escape) {
    dialog_->handle_event(event);
  }
}

void ControlBar::process() {
  // control_bar_process (0x48d76)
  if (dialog_->process() == ui::kResultCancel) {
    if (config_.on_escape) {
      config_.on_escape();
    } else {
      menu_end_mission();
    }
  }
  dialog_->set_result(0);
}

void ControlBar::update() {
  // control_bar_update (0x48e20). The original runs it after the menu a
  // hotkey opened has returned: it waits for the pending menu here.
  if (env_.modals.modal_active()) {
    return;
  }
  if (compression_button_ != nullptr && mission_clock_effective() != shown_compression_) {
    compression_button_->dirty = true;
  }
  if (message_state_ == 0) {
    if (clock_field_ != nullptr) {
      clock_field_->dirty = true;
    }
    return;
  }
  draw_message();
  if (env_.timer(GameTimer::kCrewMessage) > 500) {
    ++message_state_;
  }
  if (message_state_ > 1) {
    message_state_ = 0;
    message_drawn_ = false;
    redraw();
  }
}

void ControlBar::draw_message() {
  // control_bar_draw_message (0x49156): the line left of the buttons.
  if (message_drawn_) {
    return;
  }
  message_drawn_ = true;
  Viewport vp = dialog_->gfx();
  const int w = vp.x1 - vp.x0 + 1 - button_slots_ * kButtonStep - 1;
  vp.x1 = vp.x0 + w - 1;
  ui::fill_rect(vp, 0, 0, vp.x1 - vp.x0, vp.y1 - vp.y0, 0);
  ui::gui_focus_rect(vp, 0, 1, w, vp.y1 - vp.y0, 0xe, false);
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  gui_draw_text(vp, 6, 7, env_.assets.msg, current_message(), &identity);
}

std::string ControlBar::current_message() const { return format_crew_message(messages_, log_.entry(log_.shown())); }

void ControlBar::crew_message(std::int32_t id, std::int32_t arg1, std::int32_t arg2, std::string_view text) {
  // crew_message (0x4c843)
  const CrewMessage* m = messages_.find(static_cast<std::uint8_t>(id));
  if (m == nullptr) {
    return;
  }
  env_.sound.play_crew_speech(m->sound);
  log_.push(id, arg1, arg2, text);
  if (env_.ui.features.crew_log) {
    // The full text: the ring keeps 0x31 characters.
    const std::string line = format_crew_message(messages_, {id, arg1, arg2, std::string(text)});
    history_.add(env_.mission.sim().clock(), m->speaker_name, line.substr(m->speaker_name.size() + 2));
  }
  message_state_ = 1;
  message_drawn_ = false;
  env_.timer(GameTimer::kCrewMessage) = 0;
}

void ControlBar::open_crew_log() {
  if (!env_.modals.modal_active()) {
    sh::open_crew_log(env_, history_, [this] { after_menu(); });
  }
}

void ControlBar::post(const std::vector<CrewReport>& reports) {
  for (const CrewReport& r : reports) {
    crew_message(r.id, r.arg, r.arg2, r.text);
  }
}

void ControlBar::reset_log() {
  // crew_message_log_reset (0x4c76e)
  message_state_ = 0;
  message_drawn_ = false;
  message_skip_ = 0;
  log_.reset();
}

// --- Hotkey actions -------------------------------------------------------

bool ControlBar::can_raise_periscope() const {
  const PlayerSub& p = env_.mission.player();
  return p.sub().periscope_height_ft * 16 + p.position().y > 0 &&
         !p.systems()[static_cast<std::size_t>(PlayerSystem::kPeriscope)].impaired();
}

bool ControlBar::surfaced() const {
  const PlayerSub& p = env_.mission.player();
  return p.sub().bridge_height_ft * 16 + p.position().y > 0;
}

void ControlBar::toggle_periscope() {
  // player_toggle_periscope (0x6073e)
  PlayerSub& p = player();
  if (!p.periscope_raised() && !can_raise_periscope()) {
    return;
  }
  p.set_periscope_raised(!p.periscope_raised());
  if (!p.systems()[static_cast<std::size_t>(PlayerSystem::kPeriscope)].impaired()) {
    crew_message(p.periscope_raised() ? kMsgUpPeriscope : kMsgUpPeriscope + 1);
  }
}

void ControlBar::select_station(StationId id) {
  if (env_.stations.get(id) != nullptr) {
    env_.stations.select(id);
  }
}

void ControlBar::station_key(HotkeyAction action) {
  StationRegistry& reg = env_.stations;
  switch (action) {
    case HA::kStationConn:
      select_station(StationId::kConn);
      break;
    case HA::kStationPeriscope:
      // key_station_periscope (0x4b87e)
      if (can_raise_periscope()) {
        if (!player().periscope_raised()) {
          toggle_periscope();
        }
        select_station(StationId::kScope);
      }
      break;
    case HA::kStationTbt:
      if (surfaced()) {
        select_station(StationId::kTbt);
      }
      break;
    case HA::kStationBridge:
      if (surfaced()) {
        select_station(StationId::kBridge);
      }
      break;
    case HA::kStationChart:
      select_station(StationId::kChart);
      break;
    case HA::kStationTdc:
      select_station(StationId::kTdc);
      break;
    case HA::kStationGauges:
      select_station(StationId::kGauges);
      break;
    case HA::kStationRadar:
      select_station(StationId::kRadar);
      break;
    case HA::kStationStatus:
      // key_station_status (0x4b90a): a second press swaps the page (the
      // original redraws the station in place).
      if (reg.current() != StationId::kStatus) {
        select_station(StationId::kStatus);
      } else {
        env_.ui.status_page ^= 1;
        reg.request_reopen();
      }
      break;
    case HA::kStationLogbook:
    case HA::kStationRadioLog:
    case HA::kStationCalendar: {
      // key_station_logbook / _radio_log / _calendar (0x4b938..0x4b9a0)
      const std::int32_t page = action == HA::kStationLogbook ? 3 : action == HA::kStationRadioLog ? 4 : 2;
      if (reg.current() == StationId::kCabin) {
        if (env_.ui.cabin_page != page) {
          env_.ui.cabin_page = page;
          reg.request_reopen();
        }
      } else if (reg.get(StationId::kCabin) != nullptr) {
        reg.select(StationId::kCabin);
        env_.ui.cabin_page = page;
      }
      break;
    }
    case HA::kStationDeckGun: {
      // key_station_deck_gun (0x4b8ca): a second press swaps the gun.
      if (!surfaced()) {
        break;
      }
      if (reg.current() != StationId::kDeckGun) {
        select_station(StationId::kDeckGun);
      } else if (env_.mission.player_class().deck_gun_count > 1) {
        DeckGun& gun = env_.mission.player_combat().deck_gun;
        gun.select(gun.selected() ^ 1);
        reg.request_reopen();
      }
      break;
    }
    default:
      break;
  }
}

void ControlBar::rudder(std::int32_t step) {
  // cmd_rudder_right / cmd_rudder_left (0x607bf / 0x607e3)
  PlayerSub& p = player();
  if (p.helm() != 7 * step) {
    p.stop_following_route();
    p.set_helm(p.helm() + step);
  }
}

void ControlBar::planes_rise() {
  // cmd_planes_rise (0x60824)
  if (env_.mission.sim().clock().reset_compression()) {
    env_.sound.play_sfx(kSfxClick, 0x1e);
  }
  player().dive().planes = 60;
  player().set_ordered_depth(-1);
  crew_message(kMsgPlanesRise);
}

void ControlBar::planes_dive() {
  // cmd_planes_dive (0x60858)
  if (env_.mission.sim().clock().reset_compression()) {
    env_.sound.play_sfx(kSfxClick, 0x1e);
  }
  PlayerSub& p = player();
  if (p.dive().depth_ft <= p.sub().draft_ft && p.dive().planes >= 0) {
    env_.sound.play_sfx(kSfxDive);
  }
  p.dive().planes = -60;
  p.set_ordered_depth(-1);
  crew_message(kMsgPlanesDive);
}

void ControlBar::straight_and_level() {
  // cmd_straight_and_level (0x608b3)
  PlayerSub& p = player();
  p.stop_following_route();
  p.set_helm(0);
  p.set_ordered_depth(-1);
  p.dive().planes = 0;
  crew_message(kMsgStraightAndLevel);
}

void ControlBar::radar_depth() {
  // cmd_radar_depth (0x60adb)
  PlayerSub& p = player();
  if (p.dive().depth_ft <= p.sub().draft_ft && p.dive().planes >= 0) {
    env_.sound.play_sfx(kSfxDive);
  }
  if (p.dive().ordered_depth_ft != p.sub().radar_depth_ft) {
    crew_message(kMsgRadarDepth);
  }
  p.dive().planes = -60;
  p.set_ordered_depth(p.sub().radar_depth_ft);
}

void ControlBar::steer_view_bearing() {
  // cmd_steer_view_bearing (0x60bc0): from the periscope, TBT, bridge or
  // deck gun, the ordered course becomes the view bearing.
  const StationId s = env_.stations.current();
  PlayerCombat& combat = env_.mission.player_combat();
  Angle10 bearing = combat.view_bearing;
  if (s == StationId::kDeckGun) {
    bearing = combat.deck_gun.mount(combat.deck_gun.selected()).train;
  } else if (s != StationId::kScope && s != StationId::kTbt && s != StationId::kBridge) {
    return;
  }
  PlayerSub& p = player();
  p.stop_following_route();
  // angle_add (0x5447c)
  p.set_course(Angle10(p.heading().tenths() + bearing.tenths()).normalized());
}

void ControlBar::view_ahead() {
  // cmd_view_ahead (0x60c2a)
  const StationId s = env_.stations.current();
  PlayerCombat& combat = env_.mission.player_combat();
  if (s == StationId::kScope || s == StationId::kTbt || s == StationId::kBridge) {
    combat.view_bearing = Angle10(0);
    combat.tdc.toggle_bearing_follow(true);
  } else if (s == StationId::kDeckGun) {
    const std::int32_t m = combat.deck_gun.selected();
    combat.deck_gun.mount(m).train = Angle10(m == 0 ? 0 : 1800);
    combat.deck_gun.set_follow(false);
  }
}

void ControlBar::toggle_silent_running() {
  // cmd_toggle_silent_running (0x60a23): only submerged.
  PlayerSub& p = player();
  if (p.diesel_mode()) {
    return;
  }
  if (!p.silent_running()) {
    p.set_telegraph(PlayerSub::kTelegraphSilent);
    crew_message(kMsgSilentRunning);
  } else {
    p.set_telegraph(1);
    crew_message(kMsgSecureSilent);
  }
}

void ControlBar::jettison_debris() {
  // cmd_jettison_debris (0x610ad): submerged only.
  if (!surfaced()) {
    crew_message(kMsgJettison);
    env_.sound.play_sfx(kSfxFire);
    SimContext ctx = sim_context();
    spawn_debris(ctx, player().position());
  }
}

bool ControlBar::aa_manned() const { return env_.mission.player_combat().aa_guns.manned; }

void ControlBar::man_aa_guns() {
  // cmd_man_aa_guns(0, -1) (0x61141): toggles; manning needs the surface.
  bool& manned = env_.mission.player_combat().aa_guns.manned;
  const bool on = !manned;
  if (surfaced() || !on) {
    manned = on;
    crew_message(on ? kMsgAaManned : kMsgAaManned + 1);
  }
}

void ControlBar::fire_tube(std::size_t tube) {
  // key_fire_tube_N (0x4bad6..0x4bb30), around `fire_tube`.
  PlayerCombat& combat = env_.mission.player_combat();
  if (combat.tubes.count() <= 0 || tube >= static_cast<std::size_t>(combat.tubes.count())) {
    return;
  }
  // The launch sound and the speech come with the simulation's events.
  SimContext ctx = sim_context();
  combat.fire_tube(ctx, player(), tube);
  flush_sim_events();
}

void ControlBar::run(HotkeyAction action) {
  PlayerSub& p = player();
  GameClock& clock = env_.mission.sim().clock();
  PlayerCombat& combat = env_.mission.player_combat();
  const std::int32_t compression = clock.requested_compression();
  switch (action) {
    case HA::kStationConn:
    case HA::kStationTbt:
    case HA::kStationPeriscope:
    case HA::kStationChart:
    case HA::kStationGauges:
    case HA::kStationBridge:
    case HA::kStationDeckGun:
    case HA::kStationTdc:
    case HA::kStationStatus:
    case HA::kStationRadar:
    case HA::kStationLogbook:
    case HA::kStationCalendar:
    case HA::kStationRadioLog:
      station_key(action);
      return;
    case HA::kCompressionUp:
      if (clock.double_compression()) {
        env_.sound.play_sfx(kSfxClick, 0x1e);
      }
      return;
    case HA::kCompressionDown:
      if (clock.halve_compression()) {
        env_.sound.play_sfx(kSfxClick, 0x1e);
      }
      return;
    case HA::kCompressionReset:
      if (clock.reset_compression()) {
        env_.sound.play_sfx(kSfxClick, 0x1e);
      }
      return;
    case HA::kTogglePeriscope:
      toggle_periscope();
      return;
    case HA::kAllStop:
    case HA::kAheadOneThird:
    case HA::kAheadTwoThirds:
    case HA::kAheadStandard:
    case HA::kAheadFull:
    case HA::kAheadFlank:
    case HA::kBackOneThird:
    case HA::kBackTwoThirds:
    case HA::kBackFull:
    case HA::kBackEmergency:
      p.set_telegraph(telegraph_for_digit(static_cast<char>(
          '0' + static_cast<int>(action) - static_cast<int>(HA::kAllStop))));
      return;
    case HA::kRudderRight:
      rudder(1);
      return;
    case HA::kRudderLeft:
      rudder(-1);
      return;
    case HA::kPlanesRise:
      planes_rise();
      return;
    case HA::kPlanesDive:
      planes_dive();
      return;
    case HA::kStraightAndLevel:
      straight_and_level();
      return;
    case HA::kBlowBallast:
    case HA::kSurface:
    case HA::kPeriscopeDepth:
    case HA::kCrashDive: {
      SimContext ctx = sim_context();
      if (action == HA::kBlowBallast) {
        const bool blowing = p.dive().mode == DiveState::Mode::kBlow;
        p.order_blow_ballast(ctx);
        if (!blowing && p.dive().mode == DiveState::Mode::kBlow) {
          env_.sound.play_sfx(kSfxBlow);
        }
        if (p.dive().mode == DiveState::Mode::kBlow) {
          env_.ui.quiet_order = true;
        }
      } else if (action == HA::kSurface) {
        // cmd_surface (0x609a8): the surfacing sound when under water with
        // the planes not already on rise.
        if (p.sub().draft_ft < p.dive().depth_ft && p.dive().planes < 1) {
          env_.sound.play_sfx(kSfxSurface);
        }
        p.order_surface(ctx);
      } else if (action == HA::kPeriscopeDepth) {
        if (p.dive().depth_ft <= p.sub().draft_ft && p.dive().planes >= 0) {
          env_.sound.play_sfx(kSfxDive);
        }
        p.order_periscope_depth(ctx);
      } else {
        if (p.dive().depth_ft <= p.sub().draft_ft && p.dive().planes >= 0) {
          env_.sound.play_sfx(kSfxDive);
        }
        p.order_dive(ctx);
        env_.ui.quiet_order = true;
      }
      if (clock.requested_compression() != compression) {
        env_.sound.play_sfx(kSfxClick, 0x1e);
      }
      flush_sim_events();
      return;
    }
    case HA::kSteerViewBearing:
      steer_view_bearing();
      return;
    case HA::kRepeatMessage:
      // key_repeat_message (0x4c7c1)
      if (log_.repeat()) {
        message_state_ = 1;
        message_drawn_ = false;
        env_.timer(GameTimer::kCrewMessage) = 0;
      }
      return;
    case HA::kFireTube1:
    case HA::kFireTube2:
    case HA::kFireTube3:
    case HA::kFireTube4:
    case HA::kFireTube5:
    case HA::kFireTube6:
    case HA::kFireTube7:
    case HA::kFireTube8:
    case HA::kFireTube9:
    case HA::kFireTube10:
      fire_tube(static_cast<std::size_t>(static_cast<int>(action) - static_cast<int>(HA::kFireTube1)));
      return;
    case HA::kDetailLevel:
      menu_detail_level();
      return;
    case HA::kPadlockTarget:
      // key_padlock_target (0x4bdc2)
      combat.tdc.toggle_bearing_follow(false);
      if (combat.deck_gun.locked()) {
        combat.deck_gun.set_follow(!combat.deck_gun.following());
      }
      return;
    case HA::kRadarDepth:
      radar_depth();
      return;
    case HA::kToggleSjRadar:
    case HA::kToggleSdRadar: {
      SimContext ctx = sim_context();
      RadarSets& radar = env_.mission.sim().sensors().radar();
      if (action == HA::kToggleSjRadar) {
        radar.set_sj_power(ctx, p, false, true, true, true);
      } else {
        radar.set_sd_power(ctx, p, false, true);
      }
      flush_sim_events();
      return;
    }
    case HA::kViewAhead:
      view_ahead();
      return;
    case HA::kQuitToDos:
      menu_quit();
      return;
    case HA::kManDeckGun: {
      SimContext ctx = sim_context();
      combat.deck_gun.set_manned(ctx, p, false, true);
      flush_sim_events();
      return;
    }
    case HA::kGameOptions:
      menu_game_options();
      return;
    case HA::kMenu1mc:
      menu_1mc();
      return;
    case HA::kRescuePilot: {
      // key_rescue_pilot (0x4be78): `player_pilot_rescued(0)`; its events
      // reach the mission screen with the next frame's.
      SimContext ctx = sim_context();
      p.rescue_pilot(ctx);
      return;
    }
    case HA::kReconPhoto: {
      // key_take_recon_photo (0x4be7f): `player_take_recon_photo`; its
      // events reach the mission screen with the next frame's.
      SimContext ctx = sim_context();
      env_.mission.sim().sensors().reports().take_recon_photo(ctx);
      return;
    }
    case HA::kDebugBboxes:
      if (env_.ui.debug) {
        env_.ui.debug_bboxes = !env_.ui.debug_bboxes;
      }
      return;
    case HA::kDebugRoutes:
      if (env_.ui.debug) {
        env_.ui.debug_routes = !env_.ui.debug_routes;
      }
      return;
    case HA::kDebugGroupLabels:
      if (env_.ui.debug) {
        env_.ui.debug_labels = !env_.ui.debug_labels;
      }
      return;
    case HA::kDebugBases:
      if (env_.ui.debug) {
        env_.ui.debug_bases = !env_.ui.debug_bases;
      }
      return;
    case HA::kDebugDamage:
    case HA::kDebugKillTarget:
    case HA::kDebugMemstat:
      // The /SS305 cheats (system damage, kill the TDC target, memory
      // report) are not reimplemented.
      return;
  }
}

// --- Rig for red ----------------------------------------------------------

void ControlBar::toggle_rig_for_red() {
  // The 1MC "Night Lights" item.
  env_.ui.rig_for_red = !env_.ui.rig_for_red;
  if (Station* s = env_.stations.get(env_.stations.current()); s != nullptr && s->rig_red_redraw()) {
    env_.stations.request_reopen();
  }
  crew_message(env_.ui.rig_for_red ? kMsgRigForRed : kMsgRigForRed + 1);
}

void ControlBar::auto_rig_for_red(bool initial) {
  // cmd_toggle_rig_for_red (0x60f99)
  MissionUiState& ui = env_.ui;
  if (initial) {
    ui.auto_red_state = 0;
  }
  if (env_.options.auto_red_lights == 0) {
    ui.auto_red_state = 0;
    return;
  }
  const std::int32_t light = env_.mission.sim().sensors().environment().light_pct;
  const auto reopen = [&] {
    if (Station* s = env_.stations.get(env_.stations.current()); s != nullptr && s->rig_red_redraw()) {
      env_.stations.request_reopen();
    }
  };
  if (ui.auto_red_state != 1 && light < 30) {
    if (!ui.rig_for_red && !ui.red_override) {
      ui.rig_for_red = true;
      reopen();
      if (!initial) {
        crew_message(kMsgRigForRed);
      }
    }
    ui.red_override = false;
    ui.auto_red_state = 1;
    return;
  }
  if (ui.auto_red_state != 2 && light > 29) {
    if (ui.rig_for_red && !ui.red_override) {
      ui.rig_for_red = false;
      reopen();
      if (!initial) {
        crew_message(kMsgRigForRed + 1);
      }
    }
    ui.red_override = false;
    ui.auto_red_state = 2;
  }
}

// --- Menus ----------------------------------------------------------------

std::string ControlBar::str(std::uint32_t address) const { return std::string(env_.exe.c_string(address)); }

void ControlBar::after_menu() {
  // The common end of the menus: the pause and palette are the host's.
  env_.ui.redraw = true;
}

void ControlBar::menu_time_compression() {
  // cmd_time_compression_box (0x49094), dialog 0x11fbf8.
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0x97, 0xc3, 0x151, 0x59, 7);
  d->emplace<ui::TitleBar>(str(text::kTimeCompression), ui::return_accept);
  d->emplace<ui::GroupBox>(0x62, 0x2d, 0xf0, 0x48);
  GameClock& clock = env_.mission.sim().clock();
  auto field = std::make_unique<ui::TextField>(
      100, 0x34, 0xee, 0x42, [&clock] { return std::to_string(clock.requested_compression()) + "x"; },
      ui::FontSlot::kLarge, TextAlign::kCentre);
  ui::TextField* text = field.get();
  d->emplace<ui::Button>(0x40, 0x2f, 0x19, str(text::kMinus),
                         [this, &clock, text](int) {
                           // time_compression_box_decrease (0x4bee1)
                           if (clock.halve_compression()) {
                             env_.sound.play_sfx(kSfxClick, 0x1e);
                           }
                           text->dirty = true;
                           return 0;
                         },
                         0xfe2d, 0x14);
  d->emplace<ui::Button>(0xfa, 0x2f, 0x19, str(text::kPlus),
                         [this, &clock, text](int) {
                           // time_compression_box_increase (0x4beca)
                           if (clock.double_compression()) {
                             env_.sound.play_sfx(kSfxClick, 0x1e);
                           }
                           text->dirty = true;
                           return 0;
                         },
                         0xfe2b, 0x14);
  d->add(std::move(field));
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this](int) { after_menu(); });
}

void ControlBar::menu_1mc() {
  // menu_1mc (0x4c1a6), dialog 0x11f728.
  std::vector<ui::GridCell> cells = read_cells(env_.exe, k1mcCells, 8);
  const PlayerSub& p = player();
  const bool up = surfaced();
  cells[0].disabled = p.depth_ft() > 0xb4;
  cells[1].label = str(env_.mission.player_combat().deck_gun.manual() ? text::kManDeckGun : text::kSecureDeckGun);
  cells[1].disabled = !up;
  cells[2].label = str(aa_manned() ? text::kSecureAaGuns : text::kManAaGuns);
  cells[2].disabled = !up;
  cells[3].label = str(env_.ui.rig_for_red ? text::kSecureFromRed : text::kRigForRed);
  cells[4].label = str(p.silent_running() ? text::kSecureFromSilent : text::kRigForSilent);
  cells[4].disabled = p.diesel_mode();
  cells[5].disabled = up;
  cells[6].disabled = !p.can_rescue_pilot();  // `player_pilot_rescued(-1)`
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0xbd, 0x77, 0x108, 0xff, 7);
  d->emplace<ui::TitleBar>(str(text::k1mc));
  d->emplace<ui::GroupBox>(0xb, 0x2c, 0xfd, 0xf4);
  auto& grid = d->emplace<ui::ButtonGrid>(std::move(cells), ui::return_accept, 7);
  ui::ButtonGrid* g = &grid;
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this, g](int) {
    const int choice = g->selection;
    switch (choice) {
      case 0:
        ask_yes_no(exe_string_at(env_.exe, kExecutiveOfficer), str(text::kScuttle), str(text::kAreYouSureCaptain),
                   [this](bool yes) {
                     if (yes) {
                       env_.mission.set_end_code(0x12);
                     }
                   });
        break;
      case 1: {
        SimContext ctx = sim_context();
        env_.mission.player_combat().deck_gun.set_manned(ctx, player(), false, true);
        flush_sim_events();
        break;
      }
      case 2:
        man_aa_guns();
        break;
      case 3:
        toggle_rig_for_red();
        break;
      case 4:
        toggle_silent_running();
        break;
      case 5:
        jettison_debris();
        break;
      case 6: {
        SimContext ctx = sim_context();
        player().rescue_pilot(ctx);
        break;
      }
      default:
        break;
    }
    after_menu();
  });
}

void ControlBar::menu_quit() {
  // cmd_quit_to_dos (0x4be84), dialog 0x11f85c.
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0xd3, 0xbf, 0xdb, 0x74, 7);
  d->emplace<ui::TitleBar>(str(text::kQuitToDos));
  d->emplace<ui::GroupBox>(0xb, 0x2c, 0xd0, 0x69);
  d->emplace<ui::ButtonGrid>(read_cells(env_.exe, kQuitCells, 2),
                             [this](int choice) {
                               // quit_menu_select (0x4c17b)
                               if (choice == 1) {
                                 env_.mission.set_end_code(3);
                               }
                               return ui::kResultAccept;
                             },
                             0);
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this](int) { after_menu(); });
}

void ControlBar::apply_end_reason() {
  // end_mission_menu_select (0x4c5cb), Return To Base: refused while a
  // reason holds (0x11dff8).
  PlayerSub& p = player();
  std::int32_t engines = 0;
  for (const EngineRoom& room : p.engines()) {
    // A fitted diesel not damaged (bits 0x404 of the room's diesel word).
    if (room[1].fitted && (room[1].flags & EnginePart::kDamaged) == 0) {
      ++engines;
    }
  }
  if (env_.mission.sim().sensors().reports().enemies_near()) {
    end_reason_ = 1;
  } else if (engines == 0) {
    end_reason_ = 3;
  } else if (p.depth_ft() > p.sub().draft_ft) {
    end_reason_ = 2;
  } else if (p.motion().keel_clearance < 1) {
    env_.mission.set_end_code(5);
  } else {
    end_reason_ = 4;
  }
}

void ControlBar::menu_end_mission() {
  // cmd_abort_mission (0x48fa1) and, in a career, cmd_abort_patrol
  // (0x4c537): dialogs 0x11f990 / 0x11fac4.
  const bool patrol = (env_.mission.sim().conditions().options & option::kModeCareer) != 0;
  end_reason_ = 0;
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0xc9, 0xb3, 0xef, 0x8c, 7);
  d->emplace<ui::TitleBar>(str(patrol ? text::kEndPatrol : text::kEndMission));
  d->emplace<ui::GroupBox>(0xb, 0x2c, 0xe4, 0x81);
  d->emplace<ui::ButtonGrid>(read_cells(env_.exe, patrol ? kEndPatrolCells : kEndMissionCells, 3),
                             [this](int choice) {
                               if (choice == 1) {
                                 env_.mission.set_end_code(1);
                               } else if (choice == 2) {
                                 apply_end_reason();
                               }
                               return ui::kResultAccept;
                             },
                             0);
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this, patrol](int) {
    if (env_.mission.end_code() == 1) {
      env_.mission.set_end_code(0);
      ask_yes_no(str(patrol ? text::kAbortPatrol : text::kAbortMission),
                 str(patrol ? text::kExitWithoutSaving : text::kAbortTheMission),
                 str(patrol ? text::kAreYouSurePatrol : text::kAreYouSure), [this](bool yes) {
                   if (yes) {
                     env_.mission.set_end_code(1);
                   }
                 });
    }
    if (end_reason_ > 0) {
      message_box(exe_string_at(env_.exe, kExecutiveOfficer), exe_string_at(env_.exe, kCannotReturn),
                  exe_string_at(env_.exe, kCannotReturn + 4 * static_cast<std::uint32_t>(end_reason_)));
    }
    after_menu();
  });
}

void ControlBar::ask_yes_no(std::string title, std::string line1, std::string line2, std::function<void(bool)> done) {
  // ask_yes_no (0x4c670), dialog 0x12094c: as wide as its text.
  const Font& text_font = env_.gui.skin().font(ui::FontSlot::kFont1);
  const Font& title_font = env_.gui.skin().font(ui::FontSlot::kLarge);
  const int button_w = 100;
  const int w = std::max({text_font.text_width(line1), text_font.text_width(line2), title_font.text_width(title),
                          2 * button_w});
  auto d = std::make_unique<ui::Dialog>(env_.gui, (0x280 - (w + 0x28)) / 2, 200, w + 0x28, 0x76, 7);
  d->emplace<ui::TitleBar>(title);
  d->emplace<ui::Button>(0xf, 0x53, button_w, str(text::kNo), ui::return_cancel, 0xff4e);
  d->emplace<ui::Button>(w + 0x19 - button_w, 0x53, button_w, str(text::kYes), ui::return_accept, 0xff59);
  auto& l1 = d->emplace<ui::TextField>(0x14, 0x2f, w + 0x14, 0x3a, [line1] { return line1; }, ui::FontSlot::kFont1,
                                       TextAlign::kCentre);
  l1.fill = 6;
  auto& l2 = d->emplace<ui::TextField>(0x14, 0x3c, w + 0x14, 0x44, [line2] { return line2; }, ui::FontSlot::kFont1,
                                       TextAlign::kCentre);
  l2.fill = 6;
  env_.modals.run_modal(std::move(d), ModalFrame::kGrey,
                        [done = std::move(done)](int result) { done(result == ui::kResultAccept); });
}

void ControlBar::message_box(std::string title, std::string line1, std::string line2, std::function<void()> done) {
  // show_message_box (0x4c38d), dialog 0x1207b4.
  const Font& text_font = env_.gui.skin().font(ui::FontSlot::kFont1);
  const Font& title_font = env_.gui.skin().font(ui::FontSlot::kLarge);
  const int button_w = 100;
  const int w = std::max({text_font.text_width(line1), text_font.text_width(line2), title_font.text_width(title),
                          button_w});
  const int dw = w + 0x28;
  auto d = std::make_unique<ui::Dialog>(env_.gui, (0x280 - dw) / 2, 200, dw, 0x76, 7);
  d->emplace<ui::TitleBar>(title);
  d->emplace<ui::Button>((dw - button_w) / 2, 0x53, button_w, str(text::kOk), ui::return_accept);
  auto& l1 = d->emplace<ui::TextField>(0x14, 0x2f, w + 0x14, 0x3a, [line1] { return line1; }, ui::FontSlot::kFont1,
                                       TextAlign::kCentre);
  l1.fill = 6;
  auto& l2 = d->emplace<ui::TextField>(0x14, 0x3c, w + 0x14, 0x44, [line2] { return line2; }, ui::FontSlot::kFont1,
                                       TextAlign::kCentre);
  l2.fill = 6;
  env_.modals.run_modal(std::move(d), ModalFrame::kGrey, [done = std::move(done)](int) {
    if (done) {
      done();
    }
  });
}

void ControlBar::menu_game_options() {
  // cmd_game_options (0x4bba0), dialog 0x1200c8.
  GameOptions& o = env_.options;
  auto changed = std::make_shared<bool>(false);
  auto save = std::make_shared<bool>(false);
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0x9d, 0x1c, 0x141, 0x19c, 7);
  d->emplace<ui::TitleBar>(str(text::kGameOptions), ui::return_accept);
  d->emplace<ui::GroupBox>(0x11, 0x2b, 0x12f, 0x8f);
  // The check boxes toggle -1 / 0 values (game_options_toggle_*).
  const auto toggle = [changed](std::int32_t& value) {
    return [changed, &value](bool flip) {
      if (flip) {
        *changed = true;
        value = ~value;
      }
      return value != 0;
    };
  };
  d->emplace<ui::CheckBox>(0x36, 0x32, str(text::kAutoTorpLoad), toggle(o.auto_torp_load));
  d->emplace<ui::CheckBox>(0x36, 0x49, str(text::kAutoSjRadar), toggle(o.auto_sj_radar));
  d->emplace<ui::CheckBox>(0x36, 0x60, str(text::kAutoSdRadar), toggle(o.auto_sd_radar));
  d->emplace<ui::CheckBox>(0x36, 0x77, str(text::kAutoRedLights), toggle(o.auto_red_lights));
  d->emplace<ui::GroupBox>(0x11, 0x91, 0x12f, 0xf7);
  d->emplace<ui::CheckBox>(0x36, 0x99, str(text::kSoundEffects), toggle(o.sound_effects));
  d->emplace<ui::CheckBox>(0x36, 0xb0, str(text::kEngineSounds), toggle(o.engine_sounds));
  d->emplace<ui::CheckBox>(0x36, 0xc7, str(text::kCollisionAlarm), toggle(o.collision_alarm));
  d->emplace<ui::CheckBox>(0x36, 0xde, str(text::kSpeech), toggle(o.speech));
  const Font* font1 = &env_.assets.font1;
  d->emplace<ui::Label>(0x26, 0x117, str(text::kEffectsSoft), ui::FontSlot::kCustom, false, font1);
  d->emplace<ui::Slider>(0x55, 0x116, 0x96, 0x7f, o.effects_volume, [changed](int&) { *changed = true; });
  d->emplace<ui::Label>(0xfe, 0x117, str(text::kEffectsLoud), ui::FontSlot::kCustom, false, font1);
  d->emplace<ui::GroupBox>(0x11, 0xf9, 0x12f, 0x166);
  d->emplace<ui::Label>(0x6e, 0x101, str(text::kEffectsVolume), ui::FontSlot::kCustom, false, font1);
  const bool career = (env_.mission.sim().conditions().options & option::kModeCareer) != 0;
  // Not in the original: EXTENDED beside SAVE.
  const bool extended = static_cast<bool>(on_features_changed);
  if (extended) {
    d->emplace<ui::Button>(0xab, 0x171, 0x82, std::string(text::kExtendedButton), [this](int) {
      extended_requested_ = true;
      return ui::kResultAccept;
    });
  }
  auto& save_button = d->emplace<ui::Button>(extended ? 0x14 : 0x5f, 0x171, 0x82,
                                             str(career ? text::kSaveCareer : text::kSaveMission),
                                             [save](int) {
                                               // game_options_save_button (0x4bf51)
                                               *save = true;
                                               return ui::kResultAccept;
                                             });
  save_button.disabled = !on_save;
  d->emplace<ui::Label>(0x71, 0x12f, str(text::kSpeechVolume), ui::FontSlot::kCustom, false, font1);
  d->emplace<ui::Slider>(0x55, 0x144, 0x96, 0x7f, o.speech_volume, [changed](int&) { *changed = true; });
  d->emplace<ui::Label>(0x26, 0x145, str(text::kSpeechSoft), ui::FontSlot::kCustom, false, font1);
  d->emplace<ui::Label>(0xfe, 0x145, str(text::kSpeechLoud), ui::FontSlot::kCustom, false, font1);
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this, changed, save](int) {
    if (*save && on_save) {
      on_save();
    }
    if (*changed && on_options_changed) {
      on_options_changed();
    }
    if (extended_requested_) {
      extended_requested_ = false;
      menu_extended_options();
      return;
    }
    after_menu();
  });
}

void ControlBar::menu_extended_options() {
  // Not in the original: the GAME OPTIONS dialog's frame and check boxes.
  Features& features = env_.ui.features;
  auto changed = std::make_shared<bool>(false);
  constexpr int kRow = 0x17;
  const int h = 0x50 + static_cast<int>(kFeatureOptions.size()) * kRow;
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0x9d, (kScreenHeight - h) / 2, 0x141, h, 7);
  d->emplace<ui::TitleBar>(std::string(text::kExtendedTitle), ui::return_accept);
  d->emplace<ui::GroupBox>(0x11, 0x2b, 0x12f, 0x2b + static_cast<int>(kFeatureOptions.size()) * kRow + 0x10);
  int y = 0x32;
  for (const FeatureOption& o : kFeatureOptions) {
    auto& box = d->emplace<ui::CheckBox>(0x36, y, std::string(o.label), [&features, changed, o](bool flip) {
      if (flip) {
        features.*o.member = !(features.*o.member);
        *changed = true;
      }
      return features.*o.member;
    });
    box.disabled = !o.in_mission;
    y += kRow;
  }
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this, changed, &features](int) {
    if (*changed && on_features_changed) {
      on_features_changed(features);
    }
    after_menu();
  });
}

void ControlBar::menu_detail_level() {
  // cmd_detail_level (0x4bb4b), dialog 0x11fdb8.
  DetailOptions& o = env_.detail;
  auto changed = std::make_shared<bool>(false);
  auto d = std::make_unique<ui::Dialog>(env_.gui, 0x9d, 0x82, 0x141, 0x10d, 7);
  d->emplace<ui::TitleBar>(str(text::kDetailLevel), ui::return_accept);
  d->emplace<ui::GroupBox>(0x28, 0x2e, 0x116, 0xba);
  d->emplace<ui::GroupBox>(0x28, 0xbc, 0x116, 0xfd);
  d->emplace<ui::Label>(0xb8, 0xd8, str(text::kMiles), ui::FontSlot::kSmall, false);
  auto field = std::make_unique<ui::TextField>(0x9b, 0xd8, 0xb3, 0xe1,
                                               [&o] { return std::to_string((o.detail_bits & 0xff) >> 3); },
                                               ui::FontSlot::kSmall, TextAlign::kRight);
  ui::TextField* visibility = field.get();
  // detail_visibility_increase / _decrease (0x4beed / 0x4bf34): 10..30
  // miles in bits 3..7; the arrow labels are glyphs 0x1e / 0x1f.
  d->emplace<ui::Button>(0xf5, 0xc2, 0x19, str(text::kArrowUp),
                         [&o, changed, visibility](int) {
                           if ((o.detail_bits & 0xf8) < 0xf0) {
                             const std::uint32_t miles = ((o.detail_bits & 0xff) >> 3) + 1;
                             o.detail_bits = (o.detail_bits & ~0xf8u) | ((miles & 0x1f) << 3);
                             *changed = true;
                           }
                           visibility->dirty = true;
                           return 0;
                         },
                         0xfe2d, 0x14);
  d->emplace<ui::Button>(0xf5, 0xe0, 0x19, str(text::kArrowDown),
                         [&o, changed, visibility](int) {
                           if ((o.detail_bits & 0xf8) > 0x50) {
                             const std::uint32_t miles = ((o.detail_bits & 0xff) >> 3) - 1;
                             o.detail_bits = (o.detail_bits & ~0xf8u) | ((miles & 0x1f) << 3);
                             *changed = true;
                           }
                           visibility->dirty = true;
                           return 0;
                         },
                         0xfe2b, 0x14);
  d->add(std::move(field));
  d->emplace<ui::Label>(0x3b, 0xd8, str(text::kVisibility), ui::FontSlot::kSmall, false);
  const auto bit = [&o, changed](std::uint32_t mask) {
    return [&o, changed, mask](bool flip) {
      if (flip) {
        *changed = true;
        o.detail_bits ^= mask;
      }
      return (o.detail_bits & mask) != 0;
    };
  };
  const auto flag = [changed](std::int32_t& value) {
    return [changed, &value](bool flip) {
      if (flip) {
        *changed = true;
        value = ~value;
      }
      return value != 0;
    };
  };
  d->emplace<ui::CheckBox>(0x36, 0x3c, str(text::kClouds), bit(DetailOptions::kClouds));
  d->emplace<ui::CheckBox>(0x36, 0x53, str(text::kWaves), bit(DetailOptions::kWaves));
  d->emplace<ui::CheckBox>(0x36, 0x6a, str(text::kLandTexture), bit(DetailOptions::kLandTexture));
  d->emplace<ui::CheckBox>(0x36, 0x81, str(text::kShipWakes), flag(o.ship_wakes));
  d->emplace<ui::CheckBox>(0x36, 0x99, str(text::kTorpedoWakes), flag(o.torpedo_wakes));
  env_.modals.run_modal(std::move(d), ModalFrame::kMenu, [this, changed](int) {
    if (*changed && on_detail_changed) {
      on_detail_changed();
    }
    after_menu();
  });
}

}  // namespace sh

// The dialog engine of the original GUI toolkit (module gui, 0x3bd41..
// 0x40848): the shared state (`Gui`), the control interface with the
// original six vtable slots, and dialogs with their hotspots, focus,
// press / release / auto-repeat state machine, dirty rectangles and run
// loop.
//
// Typical use (a modal dialog):
//   ui::Dialog d(gui, x, y, w, h, ui::kDialogFramed | ui::kDialogCancel);
//   d.emplace<ui::Button>(10, 10, 80, "OK", [](int) { return ui::kResultAccept; });
//   int result = d.run();  // or run_begin() then run_step() once per frame
// Stations that keep a dialog on screen call `open`, then `handle_event`
// for each event and `process` once per frame, and `close`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "core/image.h"
#include "gfx/viewport.h"
#include "ui/event.h"
#include "ui/hotspot.h"
#include "ui/mouse.h"
#include "ui/skin.h"

namespace sh::ui {

class Dialog;

// The messages of the `handle` slot.
enum class Msg : int {
  kNone = 0,      // no pending message
  kRegister = 1,  // register the hotspots (layout)
  kPress = 2,
  kRelease = 3,   // release, or loss of focus
  kRepeat = 4,    // the pointer moved while pressed over the same hotspot
  kPaint = 5,
};

// The sub-index `gui_dialog_redraw_controls` passes when it repaints a
// control: the original passes its gfx pointer there, which matches no part.
inline constexpr int kSubRepaint = 0x7fffffff;

// The dialog result codes; any other non-zero value is the return value of
// the activated control (e.g. a button's `on_click`).
inline constexpr int kResultContinue = 0;
inline constexpr int kResultAccept = -1;    // `gui_return_accept`
inline constexpr int kResultCancel = -2;    // `gui_return_cancel`, Esc
inline constexpr int kResultNoHotspots = -3;

// The usual button callbacks (`gui_return_accept` / `gui_return_cancel`).
inline int return_accept(int) { return kResultAccept; }
inline int return_cancel(int) { return kResultCancel; }

// A hover callback: `on` is true when the pointer enters the control, false
// when it leaves (`gui_nop` when unset).
using HoverFn = std::function<void(bool on, int sub, const Event& event)>;

// What a text field takes, which picks the soft keyboard.
enum class TextFieldKind : std::uint8_t { kText, kDigits };

// The system's text input that the text fields' edit loops turn on, for
// touch screens' soft keyboards (injected by the program; none by
// default): `begin` turns it on for a field of a kind at an area (screen
// coordinates), or shows the keyboard again when it is on; `end` turns it
// off.
struct TextInputHooks {
  std::function<void(const Rect& area, TextFieldKind kind)> begin;
  std::function<void()> end;
};

// The state shared by every dialog: the original's globals (hotspot table,
// event queue, timer, mouse, skin, screen).
class Gui {
 public:
  // `screen` is the 640x480 frame the dialogs appear on; `ticks` the 100 Hz
  // GUI timer. `skin` must outlive the Gui (see `set_skin`).
  Gui(Image8& screen, TickSource ticks, Skin& skin);

  Image8& screen() { return *screen_; }
  EventQueue& events() { return events_; }
  HotspotTable& hotspots() { return hotspots_; }
  Mouse& mouse() { return mouse_; }
  Skin& skin() { return *skin_; }
  // Installs another hook table (e.g. `menu_skin_install`).
  void set_skin(Skin& skin) { skin_ = &skin; }
  // Returns the GUI tick count (`gui_get_ticks`).
  std::uint32_t ticks() const { return ticks_(); }
  // Returns the C library clock (`clock()`, 100 per second), which the list
  // double click is timed with; the GUI timer by default.
  std::uint32_t clock() const { return clock_(); }
  void set_clock(TickSource clock) { clock_ = std::move(clock); }

  // The default dialog cursor (`gui_init` config 0x11dca4/a8: CURSORS.SHP
  // frame 3), set by `Dialog::run_begin` when cursors are loaded.
  void set_dialog_cursor(const Shp* cursors, std::size_t frame) {
    cursors_ = cursors;
    cursor_frame_ = frame;
  }
  const Shp* dialog_cursor_sprites() const { return cursors_; }
  std::size_t dialog_cursor_frame() const { return cursor_frame_; }

  // The function the modal loops (`Dialog::run`, the text edit loops) call
  // once per iteration in place of the original's busy wait: it should
  // present the screen and feed new input into `events()` and `mouse()`.
  void set_pump(std::function<void()> pump) { pump_ = std::move(pump); }
  // Returns the pump set (to put it back after replacing it).
  const std::function<void()>& pump_function() const { return pump_; }
  void pump() const {
    if (pump_) {
      pump_();
    }
  }

  // Installs the text input hooks.
  void set_text_input(TextInputHooks hooks) { text_input_ = std::move(hooks); }
  bool has_text_input() const { return static_cast<bool>(text_input_.begin); }
  // The edit loops' side (through `TextInputScope`): a field starts or
  // ends editing; when an edit nested in another ends, the outer field's
  // input is turned on again.
  void begin_text_input(const Rect& area, TextFieldKind kind);
  void end_text_input();
  // Shows the keyboard again for the field being edited (tapped again
  // after the player hid the keyboard).
  void show_text_input();

  // The list double click memory (0x108938 selection, 0x11c9d0 clock) and
  // the button grid "click armed" flag (0x11dee0).
  int list_last_selection = -1;
  std::uint32_t list_last_click = 0;
  bool grid_disarmed = true;

 private:
  Image8* screen_;
  TickSource ticks_;
  TickSource clock_;
  EventQueue events_;
  HotspotTable hotspots_;
  Mouse mouse_;
  Skin* skin_;
  const Shp* cursors_ = nullptr;
  std::size_t cursor_frame_ = 3;
  std::function<void()> pump_;
  TextInputHooks text_input_;
  // The fields being edited, innermost last.
  std::vector<std::pair<Rect, TextFieldKind>> text_fields_;
};

// Turns the GUI's text input on for a field for its lifetime.
class TextInputScope {
 public:
  TextInputScope(Gui& gui, const Rect& area, TextFieldKind kind) : gui_(gui) { gui_.begin_text_input(area, kind); }
  ~TextInputScope() { gui_.end_text_input(); }
  TextInputScope(const TextInputScope&) = delete;
  TextInputScope& operator=(const TextInputScope&) = delete;

 private:
  Gui& gui_;
};

// A control of a dialog: the original 6-slot vtable as virtual methods.
// Controls register hotspots in screen coordinates and draw through the
// dialog's skin into its gfx viewport (dialog coordinates).
class Control {
 public:
  virtual ~Control() = default;

  // Slot 0 (+0x00): handles `msg` for hotspot sub-index `sub`. Returns true
  // when something was redrawn.
  virtual bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) = 0;
  // Slot 1 (+0x04): the click action; its return value becomes the dialog
  // result.
  virtual int activate(Dialog& dialog, int sub, const Event& event);
  // Slot 2 (+0x08): returns true when the part `sub` is unusable.
  virtual bool is_disabled(int sub) const;
  // Slot 3 (+0x0c): returns true for the text entries, which start editing
  // when they get the focus.
  virtual bool wants_keyboard() const;
  // Slot 4 (+0x10): returns true while pressed (redraws use msg 2 then).
  virtual bool is_pressed() const;
  // Slot 5 (+0x14): the hover notification.
  virtual void hover(bool on, int sub, const Event& event);

  bool dirty = true;     // +4
  bool pressed = false;  // +8
};

// Bits of the dialog flags (+0x10).
enum DialogFlag : unsigned {
  kDialogTitled = 1,        // a border strip (`SkinMetrics::border`) is added
  kDialogFramed = 2,        // the frame hook draws the background
  kDialogCancel = 4,        // Esc / a right click returns -2
  kDialogNoNavigation = 8,  // no Tab / Enter / Space handling
};

// A dialog. Its geometry is in screen coordinates; its gfx viewport draws
// into `target` (offset by x, y) or, without a target, into a buffer of its
// own whose dirty parts are copied to the screen, with the background saved
// at open and restored at close.
class Dialog {
 public:
  // The blit callback (+0x20): copies the region (`src.x0`..`src.x1`,
  // `src.y0`..`src.y1`) of `src.bitmap` to the screen at x0, y0 .. x1, y1.
  using BlitFn = std::function<void(const Viewport& src, int x0, int y0, int x1, int y1)>;
  // The idle callback (+0x1c): called on every run iteration with the run
  // argument (-1 on the first iteration, then 0).
  using IdleFn = std::function<void(const Viewport& gfx, int arg)>;
  // The extra dirty rectangle callback (+0x18): returns true to add `rect`
  // when the dirty list is flushed.
  using ExtraDirtyFn = std::function<bool(const Viewport& gfx, Rect& rect)>;

  // `dirty_capacity` is the size of the dirty rectangle list (100 for most
  // static dialogs).
  Dialog(Gui& gui, int x, int y, int w, int h, unsigned flags, Image8* target = nullptr,
         std::size_t dirty_capacity = 100);
  Dialog(const Dialog&) = delete;
  Dialog& operator=(const Dialog&) = delete;
  ~Dialog();

  // Appends a control (`gui_dialog_add_control`); the dialog owns it.
  Control& add(std::unique_ptr<Control> control);
  template <class T, class... Args>
  T& emplace(Args&&... args) {
    return static_cast<T&>(add(std::make_unique<T>(std::forward<Args>(args)...)));
  }

  // Opens the dialog (`gui_dialog_open`): flushes the events, prepares the
  // gfx viewport (saving the screen under it without a target), registers
  // the hotspots, invalidates everything and focuses the first focusable
  // hotspot.
  void open();
  // Closes it (`gui_dialog_close`): removes its hotspots, restores the
  // background without a target, flushes the events.
  void close();
  // Feeds one event (`gui_dialog_handle_event`).
  void handle_event(const Event& event);
  // Runs one frame of the state machine (`gui_dialog_process`): focus
  // check, redraws, auto-repeat, the pending activation and message, the
  // dirty-rect flush. Returns the result (0 = continue).
  int process();
  // Marks everything for a full redraw (`gui_dialog_invalidate`).
  void invalidate();
  // Repaints the dirty controls (`gui_dialog_redraw_controls`); returns
  // true when something was drawn.
  bool redraw_controls();
  // Copies the dirty rectangles to the screen (`gui_flush_dirty_rects`).
  void flush_dirty_rects();
  // Adds `rect` (dialog coordinates) to the dirty list (`dirty_rect_add`).
  void add_dirty(Rect rect);

  // `gui_dialog_run` split in steps: `run_begin` opens the dialog (returns
  // -3 at once, closed again, when it has no hotspot), `run_step` runs one
  // iteration (events, process, idle callback) and returns the result,
  // closing the dialog when it is non-zero.
  int run_begin();
  int run_step();
  // Runs the dialog modally, calling `Gui::pump` between iterations.
  int run();

  // Accessors.
  Gui& gui() { return *gui_; }
  Skin& skin() { return gui_->skin(); }
  HotspotTable& hotspots() { return gui_->hotspots(); }
  const Viewport& gfx() const { return gfx_; }
  int x() const { return x_; }
  int y() const { return y_; }
  int width() const { return w_; }
  int height() const { return h_; }
  unsigned flags() const { return flags_; }
  // The first hotspot index (+0x30) and the hotspot count (+0xac).
  int first_hotspot() const { return first_hotspot_; }
  int hotspot_count() const { return hotspot_count_; }
  // The focused hotspot (+0x34, relative; -1 none).
  int focus() const { return focus_; }
  void set_focus(int focus) { focus_ = focus; }
  // Returns the control owning the focused hotspot, or nullptr.
  const Control* focused_control() const;
  // Returns the sub-index of the focused hotspot (-1 without focus).
  int focused_sub() const;
  int result() const { return result_; }
  void set_result(int result) { result_ = result; }
  // The auto-repeat interval while pressed (+0x38, ticks; 0 = none), set by
  // controls on press.
  void set_repeat_interval(std::uint32_t ticks) { repeat_interval_ = ticks; }
  std::uint32_t repeat_interval() const { return repeat_interval_; }
  // The hovered hotspot (+0x8c, relative; -1 none).
  int hover() const { return hover_; }
  void set_hover(int hover) { hover_ = hover; }
  // The last event the dialog handled (+0xb4).
  const Event& last_event() const { return last_event_; }
  void set_last_event(const Event& e) { last_event_ = e; }
  // Re-reads the first hotspot index (done at the start of each frame).
  void refresh_first_hotspot() { first_hotspot_ = hotspots().first_of(this); }
  // Calls the hover slot when the hovered hotspot changes
  // (`gui_dialog_update_highlight`).
  void update_highlight();
  // Calls the idle callback with the run argument, then clears it.
  void call_idle();

  void set_idle(IdleFn idle) { idle_ = std::move(idle); }
  void set_blit(BlitFn blit) { blit_ = std::move(blit); }
  void set_extra_dirty(ExtraDirtyFn fn) { extra_dirty_ = std::move(fn); }
  bool has_idle() const { return static_cast<bool>(idle_); }

 private:
  void register_hotspots();
  void focus_first_focusable();
  // Returns the hotspot (absolute index `first_hotspot_ + rel`).
  Hotspot& rel_hotspot(int rel) { return hotspots()[first_hotspot_ + rel]; }
  bool rel_disabled(int rel) {
    const Hotspot& h = rel_hotspot(rel);
    return h.control->is_disabled(h.sub);
  }

  Gui* gui_;
  int x_;
  int y_;
  int w_;
  int h_;
  unsigned flags_;
  Image8* target_;
  IdleFn idle_;
  BlitFn blit_;
  ExtraDirtyFn extra_dirty_;
  std::vector<std::unique_ptr<Control>> controls_;
  std::size_t dirty_capacity_;
  std::vector<Rect> dirty_;
  bool frame_dirty_ = false;    // +0x24
  int result_ = 0;              // +0x28
  bool full_redraw_ = false;    // +0x2c
  int first_hotspot_ = -1;      // +0x30
  int focus_ = -1;              // +0x34
  std::uint32_t repeat_interval_ = 0;  // +0x38
  Image8 buffer_;               // +0x50: the drawing buffer without a target
  Image8 background_;           // +0x64: the saved screen
  Viewport gfx_;                // +0x78
  int hover_ = -1;              // +0x8c
  int prev_hover_ = -1;         // +0x90
  int target_index_ = -1;       // +0x94: the hotspot the pending message goes to
  int pressed_index_ = -1;      // +0x98
  int prev_focus_ = -1;         // +0x9c
  Msg pending_msg_ = Msg::kNone;  // +0xa0
  bool pending_activate_ = false;  // +0xa4
  int hotspot_count_ = 0;       // +0xac
  std::uint32_t press_time_ = 0;   // +0xb0
  Event last_event_;            // +0xb4
  Control* highlighted_ = nullptr;  // +0xcc
  int run_arg_ = 0;             // +0xd0
  bool open_ = false;
};

}  // namespace sh::ui

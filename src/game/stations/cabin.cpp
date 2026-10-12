#include "game/stations/cabin.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/stations/idbook.h"
#include "gfx/text.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The pages (0x11bd00).
constexpr std::int32_t kPageCabin = 0;
constexpr std::int32_t kPageIdBook = 1;
constexpr std::int32_t kPageCalendar = 2;
constexpr std::int32_t kPageLog = 3;
constexpr std::int32_t kPageRadio = 4;
// The pictures are drawn above the command bar (`viewport_new(screen, 0,
// 0, w, 0x1ca)`).
constexpr int kPictureBottom = 0x1ca;

// The cursors of the hotspots (CURSORS.SHP).
constexpr std::size_t kCursorBack = 0x21;
constexpr std::size_t kCursorIdBook = 0x24;
constexpr std::size_t kCursorCalendar = 0x25;
constexpr std::size_t kCursorLogBook = 0x26;
constexpr std::size_t kCursorRadioLog = 0x27;
constexpr std::size_t kCursorOpenBook = 0x18;
constexpr std::size_t kCursorPrevPage = 0xe;
constexpr std::size_t kCursorNextPage = 0xf;

// SH.EXE tables of names.
constexpr std::uint32_t kShipTypes = 0x10ea04;  // {char*, u32}[24]
constexpr std::uint32_t kLocationTypes = 0x10eb54;
constexpr std::size_t kLocationTypeCount = 6;
// The ship types a log entry names the class of (`player_log_event`:
// carriers .. submarines).
constexpr std::int32_t kLastNamedType = 0xb;
constexpr std::int32_t kSubmarineType = 0xb;

// The bar of the cabin (0x1194f8): one button, GLOBAL frames 2/3, back to
// the conn (0x342fe).
constexpr std::uint8_t kConnFrame = 2;

// The patrol log entries per page.
constexpr std::int32_t kLogEntriesPerPage = 9;

// The name tables of SH.EXE (`char*` arrays) and their sizes.
struct NameTable {
  std::uint32_t address;
  std::int32_t count;
};
constexpr NameTable kWeekdayNames{0x10e970, 7};
constexpr NameTable kWeatherNames{0x10e98c, 4};
constexpr NameTable kSeaNames{0x10e99c, 3};
constexpr NameTable kMoonNames{0x10e9ec, 5};

// Returns entry `i` of `table`, empty when out of range.
std::string table_name(const ExeImage& exe, NameTable table, std::int32_t i) {
  return i >= 0 && i < table.count ? std::string(exe.string_pointer(table.address + 4 * static_cast<std::uint32_t>(i)))
                                   : std::string();
}

std::string exe_text(const ExeImage& exe, std::uint32_t address) { return std::string(exe.c_string(address)); }

// The texts of SH.EXE (the original's `cquarter.cpp` / `player.cpp`
// strings).
namespace text {
constexpr std::uint32_t kRadioHarbor = 0xf31f9;      // cquarter_format_message
constexpr std::uint32_t kLogHarbor = 0xf7945;        // player_log_event
constexpr std::uint32_t kMonthYearGap = 0xf2d8b;     // "  "
constexpr std::uint32_t kSunrise = 0xf2da8;
constexpr std::uint32_t kSunset = 0xf2de9;
constexpr std::uint32_t kMoonrise = 0xf2e29;
constexpr std::uint32_t kMoonset = 0xf2e6b;
constexpr std::uint32_t kMoon = 0xf2eac;
constexpr std::uint32_t kSea = 0xf2ece;
constexpr std::uint32_t kWeather = 0xf2eef;
constexpr std::uint32_t kFoggy = 0xf2f07;
constexpr std::uint32_t kDateTime = 0xf2f0d;
constexpr std::uint32_t kLocation = 0xf2f17;
constexpr std::uint32_t kDescription = 0xf2f20;
constexpr std::uint32_t kTonnage = 0xf2f2c;
constexpr std::uint32_t kRescuedPilot = 0xf301e;
constexpr std::uint32_t kPhotoRecon = 0xf302c;
constexpr std::uint32_t kClass = 0xf3052;            // " Class"
constexpr std::uint32_t kPatrolTonnage = 0xf3059;
constexpr std::uint32_t kCareerTonnage = 0xf306e;
constexpr std::uint32_t kPage = 0xf308b;             // "Page "
constexpr std::uint32_t kOf = 0xf30ab;               // " of "
constexpr std::uint32_t kMessageNumber = 0xf30d9;
constexpr std::uint32_t kDate = 0xf30e3;
constexpr std::uint32_t kTime = 0xf312f;
constexpr std::uint32_t kFrom = 0xf315e;
constexpr std::uint32_t kTo = 0xf3164;
constexpr std::uint32_t kLastMessage = 0xf3175;
constexpr std::uint32_t kNoMessages = 0xf318a;
}  // namespace text

std::string padded(std::int32_t value, int digits) {
  // int_to_padded_str
  std::string s = std::to_string(value);
  if (static_cast<int>(s.size()) < digits) {
    s.insert(0, static_cast<std::size_t>(digits) - s.size(), '0');
  }
  return s;
}

std::string upper(std::string s) {
  // strupr
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

LogAngle log_angle(const GeoAngle& a) { return {a.whole_degrees, a.minutes, a.hemisphere}; }

// Returns "<deg>-<mm><H>" as the logs print a position.
std::string angle_text(const LogAngle& a) {
  return std::to_string(a.degrees) + "-" + padded(a.minutes, 2) + std::string(1, a.hemisphere);
}

// Returns the x that centres `text` of `font` in a `width`-pixel column at
// `x` (the C division of the original).
int centred(const Font& font, std::string_view text, int width, int x) {
  return (width - font.text_width(text)) / 2 + x;
}

}  // namespace

// --- Radio log text box hooks ------------------------------------------

// The text box hooks the radio log swaps in while its dialog runs
// (`cquarter_radio_install_gui_hooks` 0x34284): nothing drawn for the frame
// and background, the text in MENU_S with GUI glyph 0x3f drawn as 0x3d, no
// caret and no text origin offset. Every other hook is the current skin's.
class CabinStation::RadioSkin : public ui::Skin {
 public:
  RadioSkin(ui::Skin& base, const Font& font) : base_(base), font_(font), table_(gui_colour_table()) {
    table_[0x3f] = 0x3d;
  }

  const ui::SkinMetrics& metrics() const override { return base_.metrics(); }
  const Font& font(ui::FontSlot slot, const Font* custom) const override { return base_.font(slot, custom); }
  ui::Rect dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) override {
    return base_.dialog_frame(gfx, x, y, w, h, titled);
  }
  ui::Rect label(const Viewport& gfx, int x, int y, ui::FontSlot f, std::string_view text, bool use_colours,
                 const Font* custom) override {
    return base_.label(gfx, x, y, f, text, use_colours, custom);
  }
  ui::Rect group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) override {
    return base_.group_box(gfx, x0, y0, x1, y1, caption);
  }
  ui::Rect line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) override {
    return base_.line(gfx, x0, y0, x1, y1, colour);
  }
  ui::Rect title_bar(const Viewport& gfx, int w, int h, std::string_view text, bool has_close_box) override {
    return base_.title_bar(gfx, w, h, text, has_close_box);
  }
  ui::Rect close_box(const Viewport& gfx, int x, int y, bool pressed) override {
    return base_.close_box(gfx, x, y, pressed);
  }
  ui::Rect text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, ui::FontSlot f, std::string_view text,
                      int fill, TextAlign align, bool use_colours, const Font* custom) override {
    return base_.text_field(gfx, x0, y0, x1, y1, f, text, fill, align, use_colours, custom);
  }
  ui::Rect button(const Viewport& gfx, int x, int y, int w, std::string_view text, bool pressed, bool highlighted,
                  bool focused, bool disabled) override {
    return base_.button(gfx, x, y, w, text, pressed, highlighted, focused, disabled);
  }
  ui::Rect check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused, bool disabled,
                     std::string_view text) override {
    return base_.check_box(gfx, x, y, pressed, checked, focused, disabled, text);
  }
  int check_box_width(std::string_view text) override { return base_.check_box_width(text); }
  ui::Rect radio_button(const Viewport& gfx, int x, int y, bool pressed, bool selected, bool focused, bool disabled,
                        std::string_view text) override {
    return base_.radio_button(gfx, x, y, pressed, selected, focused, disabled, text);
  }
  int radio_button_width(std::string_view text) override { return base_.radio_button_width(text); }
  ui::Rect cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                        std::string_view text) override {
    return base_.cycle_button(gfx, x, y, w, pressed, focused, disabled, text);
  }
  ui::Rect entry_box(const Viewport& gfx, int x, int y, int w) override { return base_.entry_box(gfx, x, y, w); }
  ui::Rect entry_text(const Viewport& gfx, int x, int y, int w, std::string_view text, bool read_only) override {
    return base_.entry_text(gfx, x, y, w, text, read_only);
  }
  void entry_caret(ui::Dialog& dialog, int x, int y, std::string_view text, int caret, bool on) override {
    base_.entry_caret(dialog, x, y, text, caret, on);
  }
  int entry_char_index_at(int click_x, int x, std::string_view text) override {
    return base_.entry_char_index_at(click_x, x, text);
  }
  ui::Rect spin_entry_box(const Viewport& gfx, int x, int y, int w) override {
    return base_.spin_entry_box(gfx, x, y, w);
  }
  ui::Rect spin_up_rect(int x, int y, int w) override { return base_.spin_up_rect(x, y, w); }
  ui::Rect spin_down_rect(int x, int y, int w) override { return base_.spin_down_rect(x, y, w); }
  ui::Rect spin_arrows(const Viewport& gfx, int x, int y, int w, int pressed, bool read_only) override {
    return base_.spin_arrows(gfx, x, y, w, pressed, read_only);
  }
  // 0x341ca: the rectangle of the frame only.
  ui::Rect textbox_frame(const Viewport&, int x, int y, int w, int h) override {
    return {x - 4, y - 4, x + w + 0x1b, y + h + 0x16};
  }
  // 0x34205
  ui::Rect textbox_background(const Viewport&, int x, int y, int w, int h) override {
    return {x, y, x + w + 0x13, y + h + 0xe};
  }
  // cquarter_radio_draw_text (0x3422f)
  void textbox_text(const Viewport& gfx, int x, int y, std::string_view text, bool) override {
    gui_draw_text(gfx, x, y, font_, text, &table_);
  }
  // 0x34264
  int textbox_char_width(std::uint8_t ch) override { return font_.glyph_width(ch); }
  void textbox_caret(ui::Dialog&, int, int, std::uint8_t, bool) override {}
  void textbox_text_origin(int&, int&) override {}
  ui::Rect list_frame(const Viewport& gfx, int x, int y, int w) override { return base_.list_frame(gfx, x, y, w); }
  ui::Rect list_up_rect(int x, int y, int w) override { return base_.list_up_rect(x, y, w); }
  ui::Rect list_down_rect(int x, int y, int w) override { return base_.list_down_rect(x, y, w); }
  ui::Rect list_track_rect(int x, int y, int w) override { return base_.list_track_rect(x, y, w); }
  ui::Rect list_item(const Viewport& gfx, int x, int y, int w, std::string_view text, bool selected) override {
    return base_.list_item(gfx, x, y, w, text, selected);
  }
  ui::Rect list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) override {
    return base_.list_scrollbar(gfx, x, y, w, top, count);
  }
  ui::Rect list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) override {
    return base_.list_arrow(gfx, x, y, w, down, pressed);
  }
  ui::Rect list_focus(const Viewport& gfx, int x, int y, int w, bool focused) override {
    return base_.list_focus(gfx, x, y, w, focused);
  }
  ui::Rect slider_frame(const Viewport& gfx, int x, int y, int w, bool disabled) override {
    return base_.slider_frame(gfx, x, y, w, disabled);
  }
  ui::Rect slider_left_rect(int x, int y, int w) override { return base_.slider_left_rect(x, y, w); }
  ui::Rect slider_right_rect(int x, int y, int w) override { return base_.slider_right_rect(x, y, w); }
  ui::Rect slider_track_rect(int x, int y, int w) override { return base_.slider_track_rect(x, y, w); }
  ui::Rect slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool disabled) override {
    return base_.slider_track(gfx, x, y, w, permille, disabled);
  }
  ui::Rect slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool disabled) override {
    return base_.slider_arrow(gfx, x, y, w, right, pressed, disabled);
  }
  ui::Rect slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) override {
    return base_.slider_focus(gfx, x, y, w, focused);
  }
  ui::Rect dial(const Viewport& gfx, const ui::Dial& d, std::string_view entry_text) override {
    return base_.dial(gfx, d, entry_text);
  }
  ui::Rect grid_cell(const Viewport& gfx, const ui::GridCell& cell, bool highlighted) override {
    return base_.grid_cell(gfx, cell, highlighted);
  }

  ui::Skin& base() { return base_; }

 private:
  ui::Skin& base_;
  const Font& font_;
  GlyphColourTable table_;
};

// --- Free functions -------------------------------------------------------

std::string format_radio_message(const RadioMessage& message, std::string_view text, const RadioTextContext& names) {
  // cquarter_format_message (0x33dc7)
  std::string out;
  std::size_t arg = 0;
  const auto next = [&]() -> std::int32_t { return arg < message.args.size() ? message.args[arg++] : 0; };
  const auto name_at = [](std::span<const std::string> list, std::int32_t i) -> std::string {
    return i >= 0 && static_cast<std::size_t>(i) < list.size() ? list[static_cast<std::size_t>(i)] : std::string();
  };
  for (const char ch : text) {
    switch (static_cast<RadioPlaceholder>(ch)) {
      case RadioPlaceholder::kNumber:
        out += std::to_string(next());
        break;
      case RadioPlaceholder::kTwoDigits:
        out += padded(next(), 2);
        break;
      case RadioPlaceholder::kThreeDigits:
        out += padded(next(), 3);
        break;
      case RadioPlaceholder::kMonthName: {
        const std::int32_t m = next();
        out += exe_month_name(game_exe(), m);
        break;
      }
      case RadioPlaceholder::kBaseName:
        out += name_at(names.base_names, next());
        break;
      case RadioPlaceholder::kBoatName:
        out += names.boat_name;
        break;
      case RadioPlaceholder::kLocationType: {
        const std::int32_t t = next();
        out += t == -1 ? exe_text(game_exe(), text::kRadioHarbor) : name_at(names.location_types, t);
        break;
      }
      case RadioPlaceholder::kLatitude:
        out += angle_text(message.latitude);
        break;
      case RadioPlaceholder::kLongitude:
        out += angle_text(message.longitude);
        break;
      default:
        out += ch;
        break;
    }
  }
  return upper(std::move(out));
}

PatrolLogEntry sinking_log_entry(const ShipClass& ship_class, const GameClock& clock, const World& world,
                                 std::int32_t x, std::int32_t z) {
  // player_log_event (0x5e594) with -1: a ship sunk.
  PatrolLogEntry e;
  e.day = clock.day();
  e.month = clock.month();
  e.year = clock.year();
  e.hour = clock.hour();
  e.minute = clock.minute();
  e.latitude = log_angle(world.latitude_at(z));
  e.longitude = log_angle(world.longitude_at(x));
  e.type = static_cast<std::int8_t>(ship_class.ship_type);
  e.tonnage = static_cast<std::int32_t>(ship_class.displacement_tons);
  if (e.type <= kLastNamedType) {
    e.name = ship_class.name.substr(0, 24);
  }
  return e;
}

PatrolLogEntry rescue_log_entry(const GameClock& clock, const World& world, std::int32_t x, std::int32_t z) {
  // player_log_event (0x5e594) with 0: type 0x16, no name, no tonnage.
  PatrolLogEntry e;
  e.day = clock.day();
  e.month = clock.month();
  e.year = clock.year();
  e.hour = clock.hour();
  e.minute = clock.minute();
  e.latitude = log_angle(world.latitude_at(z));
  e.longitude = log_angle(world.longitude_at(x));
  e.type = PatrolLogEntry::kRescuedPilot;
  return e;
}

PatrolLogEntry photo_recon_log_entry(const GameClock& clock, const World& world, std::int32_t x, std::int32_t z,
                                     std::int32_t location_type, std::span<const std::string> location_types) {
  // player_log_event (0x5e594) with 1: type 0x17, the place, no tonnage.
  PatrolLogEntry e = rescue_log_entry(clock, world, x, z);
  e.type = PatrolLogEntry::kPhotoRecon;
  if (location_type < 0) {
    e.name = exe_text(game_exe(), text::kLogHarbor);
  } else if (static_cast<std::size_t>(location_type) < location_types.size()) {
    e.name = location_types[static_cast<std::size_t>(location_type)].substr(0, 24);
  }
  return e;
}

std::vector<std::string> calendar_lines(const GameClock& clock, const Sky& sky, std::int32_t sea_state,
                                        std::int32_t weather) {
  // cquarter_calendar_page (0x32a2c)
  const ExeImage& exe = game_exe();
  const auto str = [&exe](std::uint32_t address) { return exe_text(exe, address); };
  std::vector<std::string> lines;
  lines.push_back(table_name(exe, kWeekdayNames, clock.weekday()));
  lines.push_back(std::to_string(clock.day()));
  lines.push_back(exe_month_name(exe, clock.month()) + str(text::kMonthYearGap) +
                  std::to_string(clock.year() + kCalendarBaseYear));
  const auto time = [](std::string label, std::int32_t h, std::int32_t m) {
    return upper(std::move(label) + padded(h, 2) + ":" + padded(m, 2));
  };
  const RiseSetTimes& sun = sky.sun_times();
  const RiseSetTimes& moon = sky.moon_times();
  lines.push_back(time(str(text::kSunrise), sun.rise_hour, sun.rise_minute));
  lines.push_back(time(str(text::kSunset), sun.set_hour, sun.set_minute));
  lines.push_back(time(str(text::kMoonrise), moon.rise_hour, moon.rise_minute));
  lines.push_back(time(str(text::kMoonset), moon.set_hour, moon.set_minute));
  lines.push_back(upper(str(text::kMoon) + table_name(exe, kMoonNames, sky.state().moon_phase)));
  lines.push_back(upper(str(text::kSea) + table_name(exe, kSeaNames, sea_state)));
  // The fog density 0x170f88.
  lines.push_back(upper(str(text::kWeather) +
                        (sky.fog_pct() < 0x1f ? table_name(exe, kWeatherNames, weather) : str(text::kFoggy))));
  return lines;
}

// --- The station ----------------------------------------------------------

CabinStation::CabinStation(StationEnv& env) : env_(env) {
  for (std::size_t i = 0; i < kLocationTypeCount; ++i) {
    location_types_.emplace_back(env_.exe.c_string(env_.exe.u32(kLocationTypes + 4 * static_cast<std::uint32_t>(i))));
  }
}

CabinStation::~CabinStation() = default;

const std::string& CabinStation::radio_text(int side) const {
  static const std::string empty;
  const ui::TextBox* box = radio_boxes_[side == 0 ? 0 : 1];
  return box != nullptr ? box->text : empty;
}

const PcxImage& CabinStation::picture(const char* name) {
  for (const auto& [n, pcx] : pictures_) {
    if (n == name) {
      return pcx;
    }
  }
  pictures_.emplace_back(name, load_pcx(env_.install, env_.paths.path(GameDir::kArt, name)));
  return pictures_.back().second;
}

void CabinStation::draw_picture(const PcxImage& pcx) {
  // load_pcx into the viewport above the bar, palette included.
  Image8& screen = env_.screen();
  const int h = std::min(pcx.image.height(), kPictureBottom + 1);
  const int w = std::min(pcx.image.width(), screen.width());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      screen.set(x, y, pcx.image.at(x, y));
    }
  }
  env_.palette = pcx.palette;
}

ui::Area& CabinStation::add_area(ui::Dialog& dialog, int x0, int y0, int x1, int y1, std::function<void()> click,
                                 std::function<std::size_t()> cursor) {
  auto& area = dialog.emplace<ui::Area>(
      x0, y0, x1, y1,
      [click = std::move(click)](ui::Dialog&, ui::Area& a, ui::Msg msg, int, const ui::Event&) {
        // hotkey_proc_simple (0x49432)
        if (msg == ui::Msg::kPress && !a.pressed && click) {
          click();
        }
        return 0;
      },
      nullptr);
  area.on_hover = [this, cursor = std::move(cursor)](bool on, int, const ui::Event&) {
    const std::size_t frame = on ? cursor() : 0;
    if (frame != 0) {
      env_.set_cursor(frame);
    } else {
      env_.default_cursor();
    }
  };
  return area;
}

void CabinStation::open() {
  // cquarter_screen (0x3260b)
  ++env_.ui.pause;
  open_page(env_.ui.cabin_page);
  env_.bar.open({{BarButton{kConnFrame, kConnFrame + 1, [this] { env_.bar.select_station(StationId::kConn); }}},
                 {},
                 true,
                 true,
                 true});
  --env_.ui.pause;
}

void CabinStation::open_page(std::int32_t page) {
  // cquarter_open_page (0x32640)
  if (page < kPageCabin || page > kPageRadio) {
    page = kPageCabin;
    env_.ui.cabin_page = page;
  }
  open_page_ = page;
  env_.dac.fade_out(0x100);
  ui::gui_fill_rect(Viewport::full(env_.screen()), 0, 0, env_.screen().width(), env_.screen().height(), 0);
  dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, 0, 0x27f, 0x1df, 0, &env_.screen());
  const auto font = [this](const char* name) { return Font::load(env_.install, env_.paths.path(GameDir::kArt, name)); };
  switch (page) {
    case kPageCabin:
      // Dialog 0x119510.
      draw_picture(picture("CAPCAB.PCX"));
      if (env_.ui.rig_for_red) {
        load_screen_palette(env_, "CAPTCAB.PAL");
      }
      add_area(*dialog_, 0xa5, 0x75, 0xd6, 0x106, [this] { goto_page(kPageIdBook); }, [] { return kCursorIdBook; });
      add_area(*dialog_, 0x11a, 0xb, 0x16d, 0x78, [this] { goto_page(kPageCalendar); }, [] { return kCursorCalendar; });
      add_area(*dialog_, 0x69, 0x75, 0xa4, 0x106, [this] { goto_page(kPageLog); }, [] { return kCursorLogBook; });
      add_area(*dialog_, 0x12, 0x106, 0xb2, 0x132, [this] { goto_page(kPageRadio); }, [] { return kCursorRadioLog; });
      break;
    case kPageIdBook:
      // Dialog 0x1197d0: the book's hotspots, then the back areas above
      // and below it.
      id_book_ = std::make_unique<IdBook>(env_, IdBookSite::kCabin);
      id_book_->open();
      id_book_->add_hotspots(*dialog_);
      add_area(*dialog_, 0, 0, 0x27f, 0x72, [this] { goto_page(kPageCabin); }, [] { return kCursorBack; });
      add_area(*dialog_, 0, 0x175, 0x27f, 0x1c8, [this] { goto_page(kPageCabin); }, [] { return kCursorBack; });
      break;
    case kPageCalendar:
      // Dialog 0x1196c4.
      calendar_fonts_[0] = font("CALEND1.FNT");
      calendar_fonts_[1] = font("CALEND2.FNT");
      calendar_fonts_[2] = font("CALEND3.FNT");
      draw_calendar();
      add_area(*dialog_, 0, 0, 0x27f, 0x1ca, [this] { goto_page(kPageCabin); }, [] { return kCursorBack; });
      break;
    case kPageLog:
      // Dialog 0x1199bc.
      page_font_ = font("SCRIPT.FNT");
      log_page_ = 1;
      add_area(*dialog_, 0, 0x175, 0x27f, 0x1c8, [this] { goto_page(kPageCabin); }, [] { return kCursorBack; });
      add_area(
          *dialog_, 0x29, 0x15, 0x13f, 0x1a8,
          [this] {
            // cquarter_log_prev_page (0x338f8)
            if (log_page_ < 1) {
              goto_page(kPageCabin);
            } else {
              log_dirty_ = true;
              --log_page_;
            }
          },
          [this] { return log_page_ == 0 ? kCursorBack : kCursorPrevPage; });
      add_area(
          *dialog_, 0x140, 0x15, 600, 0x1a8,
          [this] {
            // cquarter_log_next_page (0x338cc)
            if (log_page_ < log_pages_) {
              log_dirty_ = true;
              ++log_page_;
            }
          },
          [this] { return log_page_ < log_pages_ ? (log_page_ == 0 ? kCursorOpenBook : kCursorNextPage) : 0; });
      dialog_->open();
      draw_patrol_log();
      return;
    case kPageRadio: {
      // Dialog 0x119b00.
      page_font_ = font("MENU_S.FNT");
      add_area(
          *dialog_, 0x29, 0x15, 0x13f, 0x1a8,
          [this] {
            // cquarter_radio_prev_page (0x341a2)
            if (radio_page_ < 1) {
              goto_page(kPageCabin);
            } else {
              radio_dirty_ = true;
              --radio_page_;
            }
          },
          [this] { return radio_page_ == 0 ? kCursorBack : kCursorPrevPage; });
      add_area(
          *dialog_, 0x140, 0x15, 600, 0x1a8,
          [this] {
            // cquarter_radio_next_page (0x34176)
            if (radio_page_ < radio_pages_) {
              radio_dirty_ = true;
              ++radio_page_;
            }
          },
          [this] { return radio_page_ < radio_pages_ ? (radio_page_ == 0 ? kCursorOpenBook : kCursorNextPage) : 0; });
      for (int side = 0; side < 2; ++side) {
        auto& box = dialog_->emplace<ui::TextBox>(side == 0 ? 0x32 : 0x163, 0x9b, 0xe9, 0xea, 0xd, 0, std::string());
        box.read_only = true;
        radio_boxes_[side] = &box;
      }
      radio_skin_ = std::make_unique<RadioSkin>(env_.gui.skin(), page_font_);
      dialog_->open();
      draw_radio_log();
      process();
      return;
    }
    default:
      break;
  }
  dialog_->open();
}

void CabinStation::close_page() {
  // cquarter_close_page (0x327e0)
  if (dialog_ != nullptr) {
    dialog_->close();
    dialog_.reset();
  }
  radio_boxes_[0] = radio_boxes_[1] = nullptr;
  radio_skin_.reset();
  id_book_.reset();
  open_page_ = -1;
  if (!env_.stations.reopen_requested()) {
    env_.ui.cabin_page = kPageCabin;
  }
}

void CabinStation::goto_page(std::int32_t page) {
  // cquarter_goto_page (0x34327): the page closes and the new one opens
  // with its bar, the palette fading in again. The loop reopens the
  // station (it calls this from the page's own dialog).
  env_.ui.cabin_page = page;
  env_.default_cursor();
  env_.stations.request_reopen();
}

void CabinStation::close() { close_page(); }

void CabinStation::event(const ui::Event& event) {
  if (dialog_ != nullptr) {
    dialog_->handle_event(event);
  }
}

void CabinStation::process() {
  if (dialog_ == nullptr) {
    return;
  }
  if (open_page_ == kPageRadio && radio_skin_ != nullptr) {
    // The radio hooks are installed only while its dialog runs.
    env_.gui.set_skin(*radio_skin_);
    dialog_->process();
    env_.gui.set_skin(radio_skin_->base());
    return;
  }
  dialog_->process();
}

void CabinStation::update() {
  // cquarter_station_update (0x3298c)
  if (open_page_ == kPageCalendar && calendar_day_ != env_.mission.sim().clock().day()) {
    draw_calendar();
  }
  if (open_page_ == kPageLog) {
    // cquarter_log_refresh (0x3387f): new entries or a page turn.
    if (log_drawn_ != logs_.patrol.size()) {
      log_dirty_ = true;
    }
    if (log_dirty_) {
      draw_patrol_log();
      env_.ui.redraw = true;
    }
  }
  if (open_page_ == kPageRadio) {
    // cquarter_radio_refresh (0x34139): new messages or a page turn; an
    // empty log redraws every time.
    if (radio_drawn_ == 0 || radio_drawn_ != logs_.radio.size()) {
      radio_dirty_ = true;
    }
    if (radio_dirty_) {
      draw_radio_log();
      env_.ui.redraw = true;
    }
  }
  if ((env_.ui.redraw || first_frame()) && dialog_ != nullptr) {
    dialog_->invalidate();
  }
  if (first_frame()) {
    env_.dac.fade_in(env_.palette, 10);
  }
}

void CabinStation::draw_calendar() {
  // cquarter_calendar_page (0x32a2c)
  draw_picture(picture("CALENDAR.PCX"));
  if (env_.ui.rig_for_red) {
    load_screen_palette(env_, "CALENDR.PAL");
  }
  const GameClock& clock = env_.mission.sim().clock();
  const MissionConditions& c = env_.mission.sim().conditions();
  const std::vector<std::string> lines = calendar_lines(clock, env_.mission.sky(), c.sea_state, c.weather);
  const Viewport vp = Viewport::full(env_.screen());
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  const Font& day_font = calendar_fonts_[0];
  const Font& name_font = calendar_fonts_[1];
  const Font& small = calendar_fonts_[2];
  gui_draw_text(vp, centred(name_font, lines[0], 0xd0, 0xeb), 0x88, name_font, lines[0], &identity);
  gui_draw_text(vp, centred(day_font, lines[1], 0xd0, 0xeb), 0xb6, day_font, lines[1], &identity);
  gui_draw_text(vp, centred(name_font, lines[2], 0xd0, 0xeb), 0x100, name_font, lines[2], &identity);
  gui_draw_text(vp, 0xf0, 0x131, small, lines[3], &identity);
  gui_draw_text(vp, 0x163, 0x131, small, lines[4], &identity);
  gui_draw_text(vp, 0xf0, 0x145, small, lines[5], &identity);
  gui_draw_text(vp, 0x163, 0x145, small, lines[6], &identity);
  gui_draw_text(vp, 0xf0, 0x159, small, lines[7], &identity);
  gui_draw_text(vp, 0x163, 0x159, small, lines[8], &identity);
  gui_draw_text(vp, centred(small, lines[9], 200, 0xf0), 0x16d, small, lines[9], &identity);
  calendar_day_ = clock.day();
}

void CabinStation::draw_patrol_log() {
  // cquarter_patrol_log (0x330de): nine entries a page, LOG-1.PCX (the
  // cover) on page 0.
  draw_picture(picture(log_page_ == 0 ? "LOG-1.PCX" : "LOG-2.PCX"));
  const Viewport vp{&env_.screen(), 0, 0, env_.screen().width() - 1, kPictureBottom};
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  const Font& f = page_font_;
  const int x = 0x32;
  int y = 0x23;
  if (log_page_ != 0) {
    gui_draw_text(vp, 0x32, 0x23, f, env_.exe.c_string(text::kDateTime), &identity);
    gui_draw_text(vp, 0xaa, 0x23, f, env_.exe.c_string(text::kLocation), &identity);
    gui_draw_text(vp, 0x14a, 0x23, f, env_.exe.c_string(text::kDescription), &identity);
    gui_draw_text(vp, 0x218, 0x23, f, env_.exe.c_string(text::kTonnage), &identity);
    ui::draw_line(vp, 0x32, 0x2d, 0x24c, 0x2d, 0);
    y = 0x3d;
  }
  const auto right = [&](const std::string& s, int line_y) {
    gui_draw_text(vp, 0x21a - f.text_width(s) + x, line_y, f, s, &identity);
  };
  std::int32_t total = 0;
  std::int32_t on_page = 0;
  log_pages_ = 1;
  for (const PatrolLogEntry& e : logs_.patrol) {
    if (on_page >= kLogEntriesPerPage) {
      on_page = 0;
      ++log_pages_;
    }
    total += e.tonnage;
    if (log_pages_ == log_page_) {
      const std::string month = exe_month_name(env_.exe, e.month);
      gui_draw_text(vp, x, y, f,
                    month + " " + std::to_string(e.day) + " " + std::to_string(e.year + kCalendarBaseYear),
                    &identity);
      gui_draw_text(vp, x, y + 0xd, f, padded(e.hour, 2) + ":" + padded(e.minute, 2), &identity);
      gui_draw_text(vp, x + 0x78, y, f, angle_text(e.latitude) + ", " + angle_text(e.longitude), &identity);
      const int dx = x + 0x118;
      if (e.type == PatrolLogEntry::kRescuedPilot) {
        gui_draw_text(vp, dx, y, f, env_.exe.c_string(text::kRescuedPilot), &identity);
      } else if (e.type == PatrolLogEntry::kPhotoRecon) {
        gui_draw_text(vp, dx, y, f, env_.exe.c_string(text::kPhotoRecon), &identity);
        gui_draw_text(vp, dx, y + 0xd, f, e.name, &identity);
      } else {
        gui_draw_text(vp, dx, y, f,
                      env_.exe.c_string(env_.exe.u32(kShipTypes + 8 * static_cast<std::uint32_t>(e.type))),
                      &identity);
        if (!e.name.empty()) {
          gui_draw_text(vp, dx, y + 0xd, f, e.type != kSubmarineType ? e.name + exe_text(env_.exe, text::kClass) : e.name, &identity);
        }
      }
      right(std::to_string(e.tonnage), y);
      y += 0x27;
    }
    ++on_page;
  }
  if (on_page >= kLogEntriesPerPage) {
    ++log_pages_;
  }
  if (log_page_ != 0) {
    if (log_page_ == log_pages_) {
      gui_draw_text(vp, x + 0x118, y, f, env_.exe.c_string(text::kPatrolTonnage), &identity);
      right(std::to_string(total), y);
      if ((env_.mission.sim().conditions().options & option::kModeCareer) != 0) {
        y += 0xd;
        gui_draw_text(vp, x + 0x118, y, f, env_.exe.c_string(text::kCareerTonnage), &identity);
        right(std::to_string(logs_.career_tonnage + total), y);
      }
    }
    right(exe_text(env_.exe, text::kPage) + std::to_string(log_page_) + exe_text(env_.exe, text::kOf) +
              std::to_string(log_pages_),
          0x19c);
    gui_draw_text(vp, 0x32, 0x19c, f, env_.mission.player_name(), &identity);
  }
  log_drawn_ = logs_.patrol.size();
  log_dirty_ = false;
}

void CabinStation::draw_radio_log() {
  // cquarter_radio_messages (0x33928): two messages a page; the first
  // unread message's page shows, and every message is then read.
  radio_pages_ = 1;
  bool unread_found = false;
  for (std::size_t i = 0; i < logs_.radio.size(); ++i) {
    radio_pages_ = static_cast<std::int32_t>(i) / 2 + 1;
    if (logs_.radio[i].unread && !unread_found) {
      unread_found = true;
      radio_page_ = radio_pages_;
    }
    logs_.radio[i].unread = false;
  }
  draw_picture(picture(radio_page_ == 0 ? "RADIO-1.PCX" : "RADIO-2.PCX"));
  for (ui::TextBox* box : radio_boxes_) {
    if (box != nullptr) {
      box->text.clear();
    }
  }
  const Viewport vp{&env_.screen(), 0, 0, env_.screen().width() - 1, kPictureBottom};
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  const Font& f = page_font_;
  // The base names come from the map's base table (0x116240).
  std::vector<std::string> base_names;
  for (const BaseRecord& b : env_.mission.bases()) {
    base_names.push_back(b.name);
  }
  const RadioTextContext names{env_.mission.player_name(), base_names, location_types_};
  for (std::size_t i = 0; i < logs_.radio.size(); ++i) {
    const RadioMessage& m = logs_.radio[i];
    radio_pages_ = static_cast<std::int32_t>(i) / 2 + 1;
    if (radio_pages_ != radio_page_) {
      continue;
    }
    const int side = static_cast<int>(i % 2);
    const int x = side == 0 ? 0x32 : 0x163;
    gui_draw_text(vp, x, 0x26, f, env_.exe.c_string(text::kMessageNumber), &identity);
    gui_draw_text(vp, x + 0x3f, 0x26, f, std::to_string(i + 1), &identity);
    gui_draw_text(vp, x, 0x40, f, env_.exe.c_string(text::kDate), &identity);
    const std::string month = exe_month_name(env_.exe, m.month);
    gui_draw_text(vp, x + 0x28, 0x40, f,
                  month + " " + std::to_string(m.day) + ", " + std::to_string(m.year + kCalendarBaseYear), &identity);
    gui_draw_text(vp, x, 0x4d, f, env_.exe.c_string(text::kTime), &identity);
    gui_draw_text(vp, x + 0x28, 0x4d, f, padded(m.hour, 2) + ":" + padded(m.minute, 2), &identity);
    gui_draw_text(vp, x, 0x5a, f, env_.exe.c_string(text::kFrom), &identity);
    gui_draw_text(vp, x + 0x28, 0x5a, f, m.from, &identity);
    gui_draw_text(vp, x, 0x67, f, env_.exe.c_string(text::kTo), &identity);
    gui_draw_text(vp, x + 0x28, 0x67, f, m.to, &identity);
    const std::size_t t = static_cast<std::size_t>(m.template_index);
    const std::string text =
        t < env_.tables.radio_messages.size() ? format_radio_message(m, env_.tables.radio_messages[t], names) : "";
    if (radio_boxes_[side] != nullptr) {
      // The boxes hold 300 bytes.
      radio_boxes_[side]->text = text.substr(0, 299);
    }
    if (i + 1 == logs_.radio.size()) {
      gui_draw_text(vp, x + 0x36, 0x192, f, env_.exe.c_string(text::kLastMessage), &identity);
    }
  }
  if (radio_page_ != 0 && logs_.radio.empty()) {
    gui_draw_text(vp, 0x68, 0x26, f, env_.exe.c_string(text::kNoMessages), &identity);
  }
  radio_drawn_ = logs_.radio.size();
  radio_dirty_ = false;
}

}  // namespace sh

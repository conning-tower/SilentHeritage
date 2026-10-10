#include "game/stations/idbook.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include "assets/pcx.h"
#include "core/error.h"
#include "core/install.h"
#include "gfx/sprite.h"
#include "gfx/viewport.h"
#include "ui/controls.h"
#include "ui/skin.h"

namespace sh {

namespace {

// The type names: {char* abbreviation, char* name}[24] at 0x10ea00, by
// `ShipType`.
constexpr std::uint32_t kShipTypeNames = 0x10ea00;
// The first ship type of the merchants: no class line, the merchant stamp.
constexpr std::uint8_t kFirstMerchantType = 12;

// The ship types of the tab rows: int32[8] at 0x1094e0.
constexpr std::uint32_t kTabTypes = 0x1094e0;
constexpr std::size_t kTabRows = 8;

// The texts of SH.EXE (`idbook.cpp` strings): the class suffix of the cabin
// and of the panels, the labels, "NONE".
namespace text {
constexpr std::uint32_t kCabinClass = 0xf5a3a;  // " CLASS"
constexpr std::uint32_t kPanelClass = 0xf5a87;
constexpr std::uint32_t kTonnage = 0xf5a8e;
constexpr std::uint32_t kLength = 0xf5a97;
constexpr std::uint32_t kBeam = 0xf5a9f;
constexpr std::uint32_t kDraft = 0xf5aa5;
constexpr std::uint32_t kSpeed = 0xf5aac;
constexpr std::uint32_t kGuns = 0xf5ab3;
constexpr std::uint32_t kDcs = 0xf5ab9;
constexpr std::uint32_t kDcRacks = 0xf5abf;
constexpr std::uint32_t kDcThrowers = 0xf5ac9;
constexpr std::uint32_t kTorpedoes = 0xf5ad6;
constexpr std::uint32_t kGunsNone = 0xf5b6b;
constexpr std::uint32_t kChargesNone = 0xf5b7b;
constexpr std::uint32_t kRacksNone = 0xf5b8b;
constexpr std::uint32_t kThrowersNone = 0xf5b9b;
}  // namespace text

// The book on the scope and TBT (`idbook_show(0, 299, 0x260, 0xa0)`, tab
// rows of 0x13 from +4) and in the cabin (`idbook_enter`: (0, 0x75),
// 0x23f x 0xfe, tab rows of 0x1a from +0x17).
constexpr int kPanelY = 299;
constexpr int kPanelW = 0x260;
constexpr int kPanelH = 0xa0;
constexpr int kCabinY = 0x75;
constexpr int kCabinW = 0x23f;
constexpr int kCabinH = 0xfe;
// The cabin's stamp (IDBOOK.SHP frame 0 warship, 1 merchant).
constexpr int kStampX = 0x1e0;
constexpr int kStampY = 0x1a;
// The cabin's pictures are loaded above the bar.
constexpr int kPictureBottom = 0x1ca;

// The glyph tables of the panels, BSS tables whose shades 0xf0..0xf7 the
// screens set (scope_screen: 0x126748, tbt_screen: 0x1283f4); the pictures
// go through them too, every other colour drawn as 0.
GlyphColourTable panel_colours(IdBookSite site) {
  GlyphColourTable t{};
  const std::array<std::uint8_t, 8> scope = {0xd1, 0xce, 0xcb, 0xc9, 0xc6, 0xc4, 0xc2, 0xc0};
  const std::array<std::uint8_t, 8> tbt = {0xbd, 0xb9, 0xb5, 0xb1, 0xf4, 0xf6, 0xf8, 0xfa};
  const auto& shades = site == IdBookSite::kScope ? scope : tbt;
  std::copy(shades.begin(), shades.end(), t.begin() + 0xf0);
  t[0xff] = 0xff;
  return t;
}

std::string upper(std::string s) {
  // strupr
  for (char& c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

// Returns the NUL-terminated string of `n` bytes at `offset` of `raw`.
std::string raw_string(const std::array<std::uint8_t, ShipClass::kSize>& raw, std::size_t offset, std::size_t n) {
  std::string s;
  for (std::size_t i = offset; i < offset + n && raw[i] != 0; ++i) {
    s.push_back(static_cast<char>(raw[i]));
  }
  return s;
}

std::int32_t raw_i32(const std::array<std::uint8_t, ShipClass::kSize>& raw, std::size_t offset) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(raw[offset]) |
                                   static_cast<std::uint32_t>(raw[offset + 1]) << 8 |
                                   static_cast<std::uint32_t>(raw[offset + 2]) << 16 |
                                   static_cast<std::uint32_t>(raw[offset + 3]) << 24);
}

// ltoa(value, 10), or `none` for 0.
std::string count_or(std::int32_t value, std::string_view none) {
  return value == 0 ? std::string(none) : std::to_string(value);
}

// Draws `text` glyph after glyph (`font_draw_string`, 0xae766): a do-while,
// so an empty text draws glyph 0.
void draw_string(const Viewport& vp, int x, int y, const Font& font, std::string_view text,
                 const GlyphColourTable& colours) {
  std::size_t i = 0;
  do {
    const std::uint8_t code = i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0;
    x += font_draw_char(vp, x, y, font, code, &colours);
  } while (++i < text.size());
}

// The dialog of the hotspots on the scope and TBT (0x126bf8 / 0x128bdc).
class IdBookPanel final : public StationPanel {
 public:
  IdBookPanel(StationEnv& env, IdBookSite site) : env_(env), book_(env, site) {}

  void open() override {
    // scope_load_overlays / tbt_load_overlays, mode 2
    book_.open();
    first_update_ = true;
    dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &env_.screen());
    book_.add_hotspots(*dialog_);
    dialog_->open();
  }
  void event(const ui::Event& event) override { dialog_->handle_event(event); }
  void process() override { dialog_->process(); }
  void update(bool full_redraw) override {
    // scope_update_mode / tbt_panel_update, mode 2: the station picture
    // comes back on the first frame and the dialog is invalidated on a full
    // redraw. The original's page stays on the VGA then; here it is drawn
    // again over the restored picture.
    if (first_update_ || full_redraw) {
      book_.redraw();
      first_update_ = false;
    }
    if (full_redraw) {
      dialog_->invalidate();
    }
  }
  void close() override {
    dialog_->close();
    dialog_.reset();
  }

 private:
  StationEnv& env_;
  IdBook book_;
  std::unique_ptr<ui::Dialog> dialog_;
  bool first_update_ = false;
};

}  // namespace

IdBookPageOrigin id_book_page_origin(IdBookSite site) {
  if (site == IdBookSite::kCabin) {
    return {.picture_x = 0x61, .picture_y = 0x49, .title_x = 0x6a, .title_y = 0x1a};
  }
  return {.picture_x = 0x8d, .picture_y = 1, .title_x = 0x1e0, .title_y = 1};
}

std::vector<IdBookText> id_book_page_texts(const ShipClass& ship, const ExeImage& exe, IdBookSite site) {
  const IdBookPageOrigin o = id_book_page_origin(site);
  const auto type = static_cast<std::uint8_t>(ship.ship_type);
  const std::uint32_t names = kShipTypeNames + 8u * type;
  const bool submarine = ship.ship_type == ShipType::kSubmarine;
  std::vector<IdBookText> out;
  // The type and abbreviation lines, then the class line: (0x6a, 0x1a /
  // 0x2a / 0x34) in the cabin, (0x1e0, 1 / 0x12 / 0x1c) on the panels.
  const int abbrev_dy = site == IdBookSite::kCabin ? 0x10 : 0x11;
  const int class_dy = site == IdBookSite::kCabin ? 0x1a : 0x1b;
  out.push_back({o.title_x, o.title_y, IdBookFont::kTitle, upper(std::string(exe.string_pointer(names + 4)))});
  out.push_back({o.title_x, o.title_y + abbrev_dy, IdBookFont::kSubtitle,
                 upper("[" + std::string(exe.string_pointer(names)) + "]")});
  if (type < kFirstMerchantType) {
    out.push_back({o.title_x, o.title_y + class_dy, IdBookFont::kSubtitle,
                   upper(ship.name + (submarine ? std::string()
                                                : std::string(exe.c_string(site == IdBookSite::kCabin ? text::kCabinClass
                                                                                                    : text::kPanelClass))))});
  }
  // The labels at +0x153 and the values at +0x19a from the picture, rows of
  // 10 pixels from +0x42.
  const int lx = o.picture_x + 0x153;
  const int vx = o.picture_x + 0x19a;
  const auto row = [&o](int i) { return o.picture_y + 0x42 + 10 * i; };
  const auto label = [&](int i, std::uint32_t address) {
    out.push_back({lx, row(i), IdBookFont::kText, std::string(exe.c_string(address))});
  };
  const auto value = [&](int i, std::string text) { out.push_back({vx, row(i), IdBookFont::kText, std::move(text)}); };
  label(0, text::kTonnage);
  label(1, text::kLength);
  label(2, text::kBeam);
  label(3, text::kDraft);
  label(4, text::kSpeed);
  label(5, text::kGuns);
  if (submarine) {
    label(6, text::kTorpedoes);
  } else {
    label(6, text::kDcs);
    label(7, text::kDcRacks);
    label(8, text::kDcThrowers);
  }
  const auto& raw = ship.raw;
  value(0, std::to_string(raw_i32(raw, 196)));
  value(1, std::to_string(raw_i32(raw, 176)) + "'");
  value(2, std::to_string(raw_i32(raw, 180)) + "'");
  value(3, std::to_string(raw_i32(raw, 184)) + "'");
  // +160 the surface speed, +168 the submerged speed (char[8] knots).
  std::string speed = raw_string(raw, 160, 8);
  if (submarine) {
    speed += " (" + raw_string(raw, 168, 8) + ")";
  }
  value(4, speed);
  const std::int32_t guns = raw_i32(raw, 136);
  value(5, guns == 0 ? std::string(exe.c_string(text::kGunsNone)) : std::to_string(guns) + " x " + raw_string(raw, 140, 8) + "\"");
  // +120 the depth charges (torpedoes on submarines), +124 the racks, +128
  // the throwers.
  value(6, count_or(raw_i32(raw, 120), exe.c_string(text::kChargesNone)));
  if (!submarine) {
    value(7, count_or(raw_i32(raw, 124), exe.c_string(text::kRacksNone)));
    value(8, count_or(raw_i32(raw, 128), exe.c_string(text::kThrowersNone)));
  }
  return out;
}

IdBook::IdBook(StationEnv& env, IdBookSite site)
    : env_(env),
      site_(site),
      ships_(env.tables.ships),
      cat_(CatArchive::load(env.install, env.paths.path(GameDir::kArt, "IDBOOK.CAT"))) {
  // idbook_open: the index count byte minus one.
  if (ships_.empty()) {
    throw DataError("SHIPS.DAT: no class for the recognition manual");
  }
  count_ = static_cast<std::int32_t>(ships_.size()) - 1;
  const char* const font_names[3] = {"ID_1.FNT", "ID_2.FNT", "ID_3.FNT"};
  for (int i = 0; i < 3; ++i) {
    fonts_[i] = Font::load(env.install, env.paths.path(GameDir::kArt, font_names[i]));
  }
  if (site == IdBookSite::kCabin) {
    stamps_ = Shp::load(env.install, env.paths.path(GameDir::kArt, "IDBOOK.SHP"));
    colours_ = ui::GuiResources::identity_table();
    y_ = kCabinY;
    w_ = kCabinW;
    h_ = kCabinH;
    tab_offset_ = 0x17;
    tab_step_ = 0x1a;
    has_cover_ = true;
  } else {
    colours_ = panel_colours(site);
    y_ = kPanelY;
    w_ = kPanelW;
    h_ = kPanelH;
    tab_offset_ = 4;
    tab_step_ = 0x13;
  }
  background_ = Image8(w_ + 1, h_ + 1, 0);
}

void IdBook::open() {
  std::int32_t& page = env_.ui.id_book_page;
  if (site_ != IdBookSite::kCabin) {
    // idbook_show
    if (page < 0) {
      ++page;
    }
    open_ = page >= 0;
    const Image8& screen = env_.screen();
    for (int y = 0; y <= h_; ++y) {
      for (int x = 0; x <= w_; ++x) {
        const bool inside = screen.contains(x_ + x, y_ + y);
        background_.set(x, y, inside ? screen.at(x_ + x, y_ + y) : 0);
      }
    }
    draw_page();
    return;
  }
  // idbook_enter
  open_ = page >= 0;
  if (!open_) {
    load_picture("ID-1.PCX");
    return;
  }
  load_picture("ID-2.PCX");
  draw_page();
}

void IdBook::load_picture(const char* name) {
  const PcxImage pcx = load_pcx(env_.install, env_.paths.path(GameDir::kArt, name));
  Image8& screen = env_.screen();
  const int h = std::min({pcx.image.height(), screen.height(), kPictureBottom + 1});
  const int w = std::min(pcx.image.width(), screen.width());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      screen.set(x, y, pcx.image.at(x, y));
    }
  }
  for (int y = 0; y <= h_; ++y) {
    for (int x = 0; x <= w_; ++x) {
      const bool inside = pcx.image.contains(x_ + x, y_ + y);
      background_.set(x, y, inside ? pcx.image.at(x_ + x, y_ + y) : 0);
    }
  }
  env_.palette = pcx.palette;
}

void IdBook::reload_pages_picture() {
  if (reload_pages_ && site_ == IdBookSite::kCabin) {
    load_picture("ID-2.PCX");
    reload_pages_ = false;
  }
}

bool IdBook::draw_page() {
  // idbook_draw_ship_page (0x4e565)
  const std::int32_t page = env_.ui.id_book_page;
  if (page < 0 || page >= count_) {
    return false;
  }
  const ShipClass& ship = ships_[static_cast<std::size_t>(page)];
  if (ship.idbook_picture.empty()) {
    return false;
  }
  const Shp picture = Shp::parse(cat_.member(ship.idbook_picture), ship.idbook_picture);
  Image8 book = background_;
  const Viewport vp = Viewport::full(book);
  const std::vector<IdBookText> texts = id_book_page_texts(ship, env_.exe, site_);
  const auto draw_text = [&](const IdBookText& t) {
    draw_string(vp, t.x, t.y, fonts_[static_cast<std::size_t>(t.font)], t.text, colours_);
  };
  // The type lines, the cabin's stamp, the picture (through the same
  // table), then the characteristics.
  std::size_t i = 0;
  for (; i < texts.size() && texts[i].font != IdBookFont::kText; ++i) {
    draw_text(texts[i]);
  }
  if (stamps_) {
    const bool merchant = static_cast<std::uint8_t>(ship.ship_type) >= kFirstMerchantType;
    draw_sprite_clipped(vp, stamps_->frame(merchant ? 1 : 0), kStampX, kStampY);
  }
  const IdBookPageOrigin o = id_book_page_origin(site_);
  draw_sprite_remapped(vp, picture.frame(0), o.picture_x, o.picture_y, colours_);
  for (; i < texts.size(); ++i) {
    draw_text(texts[i]);
  }
  env_.screen().blit(book, x_, y_);
  return true;
}

void IdBook::redraw() {
  if (open_) {
    draw_page();
  }
}

void IdBook::next_page() {
  // idbook_next_page (0x4e13a)
  reload_pages_picture();
  std::int32_t& page = env_.ui.id_book_page;
  bool drawn = false;
  while (page < count_ - 1 && !drawn) {
    ++page;
    drawn = draw_page();
  }
  open_ = page >= 0;
}

void IdBook::prev_page() {
  // idbook_prev_page (0x4e1d5)
  reload_pages_picture();
  std::int32_t& page = env_.ui.id_book_page;
  if (page == 0 && has_cover_) {
    load_picture("ID-1.PCX");
    env_.default_cursor();
    open_ = false;
    reload_pages_ = true;
    page = -1;
    return;
  }
  bool drawn = false;
  while (page > 0 && !drawn) {
    --page;
    drawn = draw_page();
  }
}

void IdBook::index_click(int mouse_y) {
  // idbook_index_click (0x4e338)
  const int dy = mouse_y - y_ - tab_offset_;
  if (dy < 0) {
    return;
  }
  const std::size_t row = std::min<std::size_t>(static_cast<std::size_t>(dy / tab_step_), kTabRows - 1);
  const std::uint32_t tab_type = env_.exe.u32(kTabTypes + 4 * static_cast<std::uint32_t>(row));
  for (std::int32_t i = 0; i < count_; ++i) {
    const ShipClass& ship = ships_[static_cast<std::size_t>(i)];
    if (!ship.idbook_picture.empty() && static_cast<std::uint32_t>(ship.ship_type) == tab_type) {
      env_.ui.id_book_page = i;
      break;
    }
  }
  reload_pages_picture();
  // The original draws without updating the open flag (0x122200).
  draw_page();
}

std::optional<std::size_t> IdBook::next_cursor() const {
  if (!open_) {
    return kCursorOpenBook;
  }
  if (page() < count_ - 1) {
    return kCursorNextPage;
  }
  return std::nullopt;
}

std::optional<std::size_t> IdBook::prev_cursor() const {
  if (has_cover_ ? open_ : page() > 0) {
    return kCursorPrevPage;
  }
  return std::nullopt;
}

std::optional<std::size_t> IdBook::tabs_cursor() const {
  return open_ ? std::optional<std::size_t>(kCursorTabs) : std::nullopt;
}

void IdBook::turn(void (IdBook::*action)()) {
  // The thunks (scope_id_next_page...): the simulation held, then a full
  // redraw.
  ++env_.ui.pause;
  (this->*action)();
  env_.ui.redraw = true;
  --env_.ui.pause;
}

void IdBook::add_hotspots(ui::Dialog& dialog) {
  // The rectangles of the dialogs' controls: next, previous, tabs.
  struct Rects {
    ui::Rect next, prev, tabs;
  };
  const Rects r = site_ == IdBookSite::kCabin
                      ? Rects{{0x52, 0x75, 0x221, 0x173}, {0, 0x75, 0x51, 0x173}, {0x22d, 0x8a, 0x23f, 0x15a}}
                      : Rects{{0x77, 300, 0x260, 0x1ca}, {0, 300, 0x5c, 0x1ca}, {0x261, 0x12e, 0x272, 0x1c6}};
  const auto hover = [this](std::optional<std::size_t> (IdBook::*cursor)() const) {
    return [this, cursor](bool on) {
      const std::optional<std::size_t> frame = on ? (this->*cursor)() : std::nullopt;
      if (frame) {
        env_.set_cursor(*frame);
      } else {
        env_.default_cursor();
      }
    };
  };
  add_click_area(dialog, r.next.x0, r.next.y0, r.next.x1, r.next.y1, 0, [this] { turn(&IdBook::next_page); },
                 hover(&IdBook::next_cursor));
  add_click_area(dialog, r.prev.x0, r.prev.y0, r.prev.x1, r.prev.y1, 0, [this] { turn(&IdBook::prev_page); },
                 hover(&IdBook::prev_cursor));
  add_click_area(
      dialog, r.tabs.x0, r.tabs.y0, r.tabs.x1, r.tabs.y1, 0,
      [this] {
        ++env_.ui.pause;
        index_click(env_.gui.mouse().y());
        env_.ui.redraw = true;
        --env_.ui.pause;
      },
      hover(&IdBook::tabs_cursor));
}

std::unique_ptr<StationPanel> make_id_book_panel(StationEnv& env, PanelSite site) {
  switch (site) {
    case PanelSite::kScope:
      return std::make_unique<IdBookPanel>(env, IdBookSite::kScope);
    case PanelSite::kTbt:
      return std::make_unique<IdBookPanel>(env, IdBookSite::kTbt);
    case PanelSite::kBridge:
      break;
  }
  return nullptr;
}

}  // namespace sh

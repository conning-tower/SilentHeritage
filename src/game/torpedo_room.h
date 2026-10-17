// The torpedo rooms of the status station's torpedo page (module status):
// the tubes, the reload racks and the torpedo type cards, drawn from
// STATUST.SHP over STATUSBG.PCX with the hotspots of their places
// (`status_draw_tubes` 0x7622e, `status_draw_torpedo_racks` 0x765f2,
// `status_draw_torpedo_info` 0x75922). The station draws the boat's tubes
// and racks with them; the extended variant's TORPEDO LOAD request draws
// the load it edits in port (game/career_requests.h).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/fnt.h"
#include "assets/shp.h"
#include "core/image.h"
#include "gfx/text.h"
#include "gfx/viewport.h"

namespace sh {

class TorpedoArt;
struct StatusTexts;

class TorpedoRoom {
 public:
  // The hotspot kinds (the `focus` field of the original's raw hotspots).
  static constexpr std::int32_t kTubeHotspot = 10;
  static constexpr std::int32_t kBowRackHotspot = 11;
  static constexpr std::int32_t kSternRackHotspot = 12;
  // The racks' first row, and the half width of a slot's box, the mirrored
  // stern ones included.
  static constexpr int kRacksY = 0xa6;
  static constexpr int kSlotHalfWidth = 46;
  // The type cards' row.
  static constexpr int kCardsY = 0x158;

  // A hotspot (screen rectangle, inclusive) of a place: its kind and its
  // index in the tubes (bow first) or its rack.
  struct Hotspot {
    int x0, y0, x1, y1;
    std::int32_t kind;
    std::int32_t index;
  };
  // A tube as drawn: its content (`TorpedoTubes::content`: a weapon id,
  // negative while loading, 0 empty), whether it is damaged and whether
  // its torpedo is moving (a timer runs).
  struct Tube {
    std::int32_t content = 0;
    bool damaged = false;
    bool moving = false;
  };

  // Draws with `sprites` (STATUST.SHP) and, when not null, the extended
  // variant's pictures `art` in place of the torpedo frames; both must
  // outlive the room's use.
  void set_sprites(const Shp* sprites, const TorpedoArt* art) {
    sprites_ = sprites;
    art_ = art;
  }
  const TorpedoArt* art() const { return art_; }

  // `status_draw_tubes`: the `bow` bow tubes of `tubes` on the right, the
  // others mirrored on the left, 29 pixels apart; their hotspots appended
  // to `hotspots` when not null.
  void draw_tubes(const Viewport& full, std::span<const Tube> tubes, std::int32_t bow,
                  std::vector<Hotspot>* hotspots);
  // `status_draw_torpedo_racks`: the bow and stern rack slots (a weapon id,
  // 0 empty, or -(id << 8 | tube) being unloaded into), two columns per
  // room. With the pictures each slot is first cleared from `background`
  // (the page from `kRacksY`, null: not cleared).
  void draw_racks(const Viewport& full, std::span<const std::int32_t> bow, std::span<const std::int32_t> stern,
                  const Image8* background, std::vector<Hotspot>* hotspots);
  // Draws torpedo `id` (STATUST.SHP `frame`, or its picture) with its
  // hotspot at (x, y), still or `moving`, `mirror`ed for the stern room.
  void draw_torpedo(const Viewport& full, std::size_t frame, std::uint32_t id, bool moving, int x, int y,
                    bool mirror);
  // `status_draw_torpedo_info`: the card of torpedo `id` at (x, y) with its
  // WEAPONS.DAT figures, in `font` lit by `light` (MENU_S.FNT on the
  // page).
  void draw_card(const Viewport& full, int x, int y, std::uint32_t id, const GameTables& tables, const Font& font,
                 const GlyphColourTable& light, const StatusTexts& texts);
  // Returns the x of `count` cards centred around x 320, 7 pixels apart.
  std::vector<int> card_positions(std::size_t count) const;
  // Returns the card's width and height (STATUST.SHP frame 0).
  int card_width() const;
  int card_height() const;

  // `status_draw_shadowed_text` (0x754d1): `text` in `font` with its
  // shadow below.
  static void shadowed_text(const Viewport& full, int x, int y, std::string_view text, const Font& font,
                            const GlyphColourTable& light);

 private:
  const Shp* sprites_ = nullptr;
  const TorpedoArt* art_ = nullptr;
  Image8 scratch_;  // sprite_draw_transformed's buffer
};

}  // namespace sh

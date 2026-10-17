#include "game/torpedo_room.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/fixed.h"
#include "game/stations/status.h"
#include "game/torpedo_art.h"
#include "gfx/sprite.h"
#include "ui/draw.h"
#include "ui/skin.h"

namespace sh {

namespace {

// The box of `frame` drawn with its hotspot at (x, y).
struct Box {
  int x0, y0, x1, y1;
};
Box frame_box(const SpriteFrame& f, int x, int y) {
  return {x + f.x0, y + f.y0, x + f.x0 + f.width() - 1, y + f.y0 + f.height() - 1};
}

}  // namespace

void TorpedoRoom::shadowed_text(const Viewport& full, int x, int y, std::string_view text, const Font& font,
                                const GlyphColourTable& light) {
  // status_draw_shadowed_text (0x754d1)
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  gui_draw_text(full, x, y + 1, font, text, &identity);
  gui_draw_text(full, x, y, font, text, &light);
}

void TorpedoRoom::draw_tubes(const Viewport& full, std::span<const Tube> tubes, std::int32_t bow,
                             std::vector<Hotspot>* hotspots) {
  // status_draw_tubes (0x7622e): the bow tubes on the right, the stern
  // tubes mirrored on the left, 29 pixels apart; a damaged tube is frame 4,
  // a torpedo in it frame 0xc + 2 x (type - 1), + 1 while moving.
  const Shp& sprites = *sprites_;
  std::size_t t = 0;
  for (std::int32_t i = 0; i < bow && t < tubes.size(); ++i, ++t) {
    const Tube& tb = tubes[t];
    const SpriteFrame& tube = sprites.frame(tb.damaged ? 4 : 3);
    const int y = 0x9e + 0x1d * i;
    draw_sprite_clipped(full, tube, 0x246, y);
    if (tb.content != 0) {
      const std::size_t f = (tb.moving ? 0xd : 0xc) + 2 * static_cast<std::size_t>(std::abs(tb.content)) - 2;
      draw_torpedo(full, f, static_cast<std::uint32_t>(std::abs(tb.content)), tb.moving, 0x246, 0xa5 + 0x1d * i,
                   false);
    }
    if (hotspots != nullptr) {
      const Box b = frame_box(tube, 0x246, y);
      hotspots->push_back({b.x0, b.y0, b.x1, b.y1, kTubeHotspot, static_cast<std::int32_t>(t)});
    }
  }
  const Fixed16 one = Fixed16::from_int(1);
  for (std::int32_t i = 0; t < tubes.size(); ++i, ++t) {
    const Tube& tb = tubes[t];
    const SpriteFrame& tube = sprites.frame(tb.damaged ? 4 : 3);
    const int y = 0x9e + 0x1d * i;
    sprite_draw_transformed(full, tube, 0x38, y, scratch_, Angle10(0), -one, one, 0);
    if (tb.content != 0) {
      const std::size_t f = (tb.moving ? 0xd : 0xc) + 2 * static_cast<std::size_t>(std::abs(tb.content)) - 2;
      draw_torpedo(full, f, static_cast<std::uint32_t>(std::abs(tb.content)), tb.moving, 0x38, 0xa5 + 0x1d * i,
                   true);
    }
    if (hotspots != nullptr) {
      // The original's rectangle: 4 pixels wider than the sprite.
      const int x0 = 0x38 + tube.x0;
      const int y0 = y + tube.y0;
      hotspots->push_back({x0, y0, x0 + tube.width() + 4 - 1, y0 + tube.height() - 1, kTubeHotspot,
                           static_cast<std::int32_t>(t)});
    }
  }
}

void TorpedoRoom::draw_racks(const Viewport& full, std::span<const std::int32_t> bow_rack,
                             std::span<const std::int32_t> stern_rack, const Image8* background,
                             std::vector<Hotspot>* hotspots) {
  // status_draw_torpedo_racks (0x765f2): two columns per room, 17 pixels
  // apart: empty (frame 9), a torpedo (0xc + 2 x (type - 1)), or one being
  // unloaded into it (0xd + ...).
  const Shp& sprites = *sprites_;
  const auto frame_of = [](std::int32_t slot) -> std::size_t {
    if (slot == 0) {
      return 9;
    }
    if (slot > 0) {
      return 0xc + 2 * static_cast<std::size_t>(slot) - 2;
    }
    return 0xd + 2 * static_cast<std::size_t>(std::abs(slot) >> 8) - 2;
  };
  // A torpedo's weapon id (in a slot being unloaded into: -(id << 8 | tube)).
  const auto id_of = [](std::int32_t slot) {
    return static_cast<std::uint32_t>(slot < 0 ? std::abs(slot) >> 8 : slot);
  };
  // With the pictures (not in the original, where every torpedo covers the
  // empty slot's outline), the page under the slot at (x, y) put back first.
  const auto clear_slot = [&](int x, int y) {
    if (art_ == nullptr || background == nullptr) {
      return;
    }
    const int h = sprites.frame(9).y0 + sprites.frame(9).height();
    for (int j = 0; j < h; ++j) {
      for (int i = -kSlotHalfWidth; i <= kSlotHalfWidth; ++i) {
        if (background->contains(x + i, y + j - kRacksY)) {
          full.bitmap->set(x + i, y + j, background->at(x + i, y + j - kRacksY));
        }
      }
    }
  };
  {
    std::int32_t half = (static_cast<std::int32_t>(bow_rack.size()) + 1) / 2;
    int x = 0x1d9;
    int y = kRacksY;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(bow_rack.size()); ++i, y += 0x11) {
      if (i >= half) {
        half = 1000;
        y = kRacksY;
        x -= 0x62;
      }
      const std::int32_t slot = bow_rack[static_cast<std::size_t>(i)];
      const SpriteFrame& f = sprites.frame(std::min(frame_of(slot), sprites.frame_count() - 1));
      clear_slot(x, y);
      if (slot == 0) {
        draw_sprite_clipped(full, f, x, y);
      } else {
        draw_torpedo(full, frame_of(slot), id_of(slot), slot < 0, x, y, false);
      }
      if (hotspots != nullptr) {
        const Box b = frame_box(f, x, y);
        hotspots->push_back({b.x0, b.y0 - 5, b.x1, b.y1 + 5, kBowRackHotspot, i});
      }
    }
  }
  std::int32_t half = (static_cast<std::int32_t>(stern_rack.size()) + 1) / 2;
  int x = 0xa7;
  int y = kRacksY;
  const Fixed16 one = Fixed16::from_int(1);
  for (std::int32_t i = 0; i < static_cast<std::int32_t>(stern_rack.size()); ++i, y += 0x11) {
    if (i >= half) {
      half = 1000;
      y = kRacksY;
      x += 0x62;
    }
    const std::int32_t slot = stern_rack[static_cast<std::size_t>(i)];
    const SpriteFrame& f = sprites.frame(std::min(frame_of(slot), sprites.frame_count() - 1));
    clear_slot(x, y);
    if (slot == 0) {
      sprite_draw_transformed(full, f, x, y, scratch_, Angle10(0), -one, one, 0);
    } else {
      draw_torpedo(full, frame_of(slot), id_of(slot), slot < 0, x, y, true);
    }
    if (hotspots != nullptr) {
      const int x0 = x + f.x0;
      const int y0 = y + f.y0;
      hotspots->push_back({x0, y0 - 5, x0 + f.width() - 1, y0 + f.height() - 1 + 5, kSternRackHotspot, i});
    }
  }
}

void TorpedoRoom::draw_torpedo(const Viewport& full, std::size_t frame, std::uint32_t id, bool moving, int x, int y,
                               bool mirror) {
  const Shp& sprites = *sprites_;
  if (art_ != nullptr && id >= 1 && id <= TorpedoArt::kTypes) {
    TorpedoArt::draw(full, art_->picture(id, moving), x, y, mirror, scratch_);
  } else if (frame >= sprites.frame_count()) {
    return;
  } else if (mirror) {
    const Fixed16 one = Fixed16::from_int(1);
    sprite_draw_transformed(full, sprites.frame(frame), x, y, scratch_, Angle10(0), -one, one, 0);
  } else {
    draw_sprite_clipped(full, sprites.frame(frame), x, y);
  }
}

void TorpedoRoom::draw_card(const Viewport& full, int x, int y, std::uint32_t weapon_id, const GameTables& tables,
                            const Font& font, const GlyphColourTable& light, const StatusTexts& texts) {
  // status_draw_torpedo_info (0x75922): the card of a torpedo type.
  const Shp& sprites = *sprites_;
  draw_sprite_clipped(full, sprites.frame(0), x, y + 2);
  draw_torpedo(full, 2 * weapon_id + 0xa, weapon_id, false, x + 0x49, y + 8, false);
  const Torpedo* t = tables.find_torpedo_by_id(weapon_id);
  if (t == nullptr) {
    return;
  }
  const auto text = [&](int tx, int ty, std::string_view s) { shadowed_text(full, tx, ty, s, font, light); };
  const std::string& name = t->long_name;
  const int tw = font.text_width(name);
  const int nx = x + (sprites.frame(0).width() - tw) / 2;
  text(nx, y + 0x1e, name);
  ui::draw_line(full, nx, y + 0x27, nx + tw - 1, y + 0x27, 0);
  ui::draw_line(full, nx, y + 0x26, nx + tw - 1, y + 0x26, 0xd8);
  const bool two_speeds = t->speed_settings > 1;
  std::string range = std::to_string(t->range_long_yd);
  std::string speed = std::to_string(t->speed_low_kts);
  if (two_speeds) {
    range += "/" + std::to_string(t->range_short_yd);
    speed += "/" + std::to_string(t->speed_high_kts);
  }
  text(x + 3, y + 0x32, texts.range);
  text(x + 0x41, y + 0x32, range + texts.yards);
  text(x + 3, y + 0x3c, texts.speed);
  text(x + 0x41, y + 0x3c, speed + texts.knots);
  text(x + 3, y + 0x46, texts.warhead);
  text(x + 0x41, y + 0x46, std::to_string(t->warhead_lb) + texts.pounds);
  text(x + 3, y + 0x50, texts.motor);
  const auto motor = static_cast<std::size_t>(t->propulsion);
  text(x + 0x41, y + 0x50, motor < texts.motors.size() ? texts.motors[motor] : std::string());
}

std::vector<int> TorpedoRoom::card_positions(std::size_t count) const {
  // The cards centred around x 320 (the original's torpedo page open).
  const auto n = static_cast<std::int32_t>(count);
  const int w = card_width();
  const std::int32_t half = n / 2;
  int tx = (n & 1) != 0 ? 0x140 - w / 2 : 0x13d;
  tx -= half * w;
  if (half != 0) {
    tx -= (half - 1) * 7;
  }
  std::vector<int> out;
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(tx);
    tx += w + 7;
  }
  return out;
}

int TorpedoRoom::card_width() const { return sprites_->frame(0).width(); }
int TorpedoRoom::card_height() const { return sprites_->frame(0).height(); }

}  // namespace sh

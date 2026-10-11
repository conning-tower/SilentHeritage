#include "game/equipment_art.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh {

namespace {

// EQUIP.SHP frames: the Mark 18-1 (its label has the "T") and the SD.
constexpr std::size_t kTorpedoFrame = 0;
constexpr std::size_t kSdFrame = 3;
constexpr int kSdWidth = 150;
constexpr int kSdHeight = 184;
// The SD's mast: 14 shades across columns 51..64, sampled on row 100.
constexpr int kMastX = 51;
constexpr int kMastRow = 100;
constexpr int kMastWidth = 14;
// The rows of the SD's antenna and cone top, cleared inside the border.
constexpr int kClearTop = 10;
constexpr int kClearBottom = 56;
constexpr int kBorder = 8;
// The periscope: the mast up from row 40, a neck of 7 shades (rows 30..39),
// a head of 11 (rows 14..29) under a rounded cap (rows 11..13), its window
// (rows 17..21) with a lit sill (row 22) and its lower bevel (row 29).
constexpr std::array<int, 7> kNeckShades = {0, 2, 4, 6, 9, 11, 13};
constexpr std::array<int, 11> kHeadShades = {0, 1, 3, 4, 5, 6, 8, 9, 10, 12, 13};
// The label: the SD's "D" (columns 56..60, rows 157..171) becomes the
// torpedo label's "T" (columns 150..155, rows 78..92).
constexpr int kLabelTop = 157;
constexpr int kLabelHeight = 15;
constexpr int kDX = 56;
constexpr int kDWidth = 5;
constexpr int kTX = 150;
constexpr int kTY = 78;
constexpr int kTWidth = 6;

// Returns the frame's pixels, transparent ones black.
Image8 flatten(const SpriteFrame& frame) {
  const DecodedFrame d = frame.decode();
  Image8 out(d.image.width(), d.image.height(), 0);
  for (int y = 0; y < out.height(); ++y) {
    for (int x = 0; x < out.width(); ++x) {
      if (d.opaque.at(x, y) != 0) {
        out.set(x, y, d.image.at(x, y));
      }
    }
  }
  return out;
}

}  // namespace

Image8 st_radar_picture(const Shp& equip) {
  if (equip.frame_count() <= kSdFrame) {
    return {};
  }
  Image8 pic = flatten(equip.frame(kSdFrame));
  const Image8 torpedo = flatten(equip.frame(kTorpedoFrame));
  if (pic.width() != kSdWidth || pic.height() != kSdHeight || torpedo.width() < kTX + kTWidth ||
      torpedo.height() < kTY + kLabelHeight) {
    return pic;
  }
  const std::uint8_t sea = pic.at(20, 20);
  std::array<std::uint8_t, kMastWidth> mast{};
  for (int i = 0; i < kMastWidth; ++i) {
    mast[static_cast<std::size_t>(i)] = pic.at(kMastX + i, kMastRow);
  }
  const auto shade = [&mast](int i) { return mast[static_cast<std::size_t>(i)]; };

  for (int y = kClearTop; y <= kClearBottom; ++y) {
    for (int x = kBorder; x < kSdWidth - kBorder; ++x) {
      pic.set(x, y, sea);
    }
  }
  for (int y = 40; y <= kClearBottom; ++y) {
    for (int i = 0; i < kMastWidth; ++i) {
      pic.set(kMastX + i, y, shade(i));
    }
  }
  for (int y = 30; y < 40; ++y) {
    for (std::size_t i = 0; i < kNeckShades.size(); ++i) {
      pic.set(54 + static_cast<int>(i), y, shade(kNeckShades[i]));
    }
  }
  for (int y = 11; y < 30; ++y) {
    const int cut = y < 14 ? 14 - y : 0;
    for (int i = cut; i < static_cast<int>(kHeadShades.size()) - cut; ++i) {
      pic.set(52 + i, y, shade(kHeadShades[static_cast<std::size_t>(i)]));
    }
  }
  for (int x = 52; x < 63; ++x) {
    pic.set(x, 29, shade(1));
  }
  for (int x = 55; x < 60; ++x) {
    for (int y = 17; y < 22; ++y) {
      pic.set(x, y, shade(0));
    }
    pic.set(x, 22, shade(10));
  }

  const std::uint8_t torpedo_ground = torpedo.at(20, 20);
  for (int y = 0; y < kLabelHeight; ++y) {
    for (int x = 0; x < kDWidth; ++x) {
      pic.set(kDX + x, kLabelTop + y, sea);
    }
    for (int x = 0; x < kTWidth; ++x) {
      const std::uint8_t c = torpedo.at(kTX + x, kTY + y);
      if (c != torpedo_ground) {
        pic.set(kDX + x, kLabelTop + y, c);
      }
    }
  }
  return pic;
}

}  // namespace sh

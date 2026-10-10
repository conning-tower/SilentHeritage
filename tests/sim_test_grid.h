// A small synthetic chart terrain grid for the sensor and AI tests: open
// water with land cells set by hand.
#pragma once

#include <cstdint>
#include <vector>

#include "sim/line_of_sight.h"

namespace sh::test {

class TestGrid : public TerrainGrid {
 public:
  // A `width` x `height` grid of water cells (0x10) at scale 0.
  TestGrid(std::int32_t width, std::int32_t height)
      : width_(width), height_(height), cells_(static_cast<std::size_t>(width * height), 0x10) {}

  std::int32_t width() const override { return width_; }
  std::int32_t height() const override { return height_; }
  std::uint32_t scale_shift() const override { return 0; }
  std::uint8_t value_at(std::int64_t index) const override {
    return index >= 0 && index < static_cast<std::int64_t>(cells_.size()) ? cells_[static_cast<std::size_t>(index)]
                                                                          : 0;
  }

  void set(std::int32_t column, std::int32_t row, std::uint8_t value) {
    cells_[static_cast<std::size_t>(row * width_ + column)] = value;
  }
  // Returns the world x of the middle of `column`, and z of `row`.
  std::int32_t x_of(std::int32_t column) const { return column * cell_size() + cell_size() / 2; }
  std::int32_t z_of(std::int32_t row) const { return (height_ - row) * cell_size() - cell_size() / 2; }

 private:
  std::int32_t width_;
  std::int32_t height_;
  std::vector<std::uint8_t> cells_;
};

}  // namespace sh::test

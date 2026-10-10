#include "ui/hotspot.h"

namespace sh::ui {

bool hotkey_matches(std::uint32_t hotkey, std::uint16_t key) {
  if ((hotkey >> 8) == 0xff) {
    return (hotkey & 0xdf) == (key & 0xdfu);
  }
  if ((hotkey >> 8) == 0xfe) {
    return (hotkey & 0xff) == (key & 0xffu);
  }
  return hotkey == key;
}

void HotspotTable::add(const Hotspot& hotspot) {
  if (hotspots_.size() != kMaxHotspots) {
    hotspots_.push_back(hotspot);
  }
}

void HotspotTable::pop() {
  if (!hotspots_.empty()) {
    hotspots_.pop_back();
  }
  if (hover_cache_ == static_cast<int>(hotspots_.size())) {
    hover_cache_ = -1;
  }
}

void HotspotTable::disable(int i) {
  if (i == static_cast<int>(hotspots_.size())) {
    return;
  }
  Hotspot& h = (*this)[i];
  h.flags = static_cast<std::uint8_t>((h.flags + 2) & 0xfe);
  if (i == hover_cache_) {
    hover_cache_ = -1;
  }
}

void HotspotTable::enable(int i) {
  if (i == static_cast<int>(hotspots_.size())) {
    return;
  }
  Hotspot& h = (*this)[i];
  if ((h.flags & 1) == 0) {
    h.flags = static_cast<std::uint8_t>(h.flags - 2);
    if ((h.flags >> 1) == 0) {
      h.flags |= 1;
    }
  }
}

void HotspotTable::disable_all() {
  for (int i = 0; i < static_cast<int>(hotspots_.size()); ++i) {
    disable(i);
  }
}

void HotspotTable::enable_all() {
  for (int i = 0; i < static_cast<int>(hotspots_.size()); ++i) {
    enable(i);
  }
}

int HotspotTable::hit_test(int x, int y) {
  // The cached hotspot is returned while the point stays inside it, without
  // checking its enabled bit again (disabling it clears the cache). Removing
  // a dialog's hotspots leaves the cache as is in the original, which then
  // reads a stale entry: an out-of-range cache is ignored here.
  if (hover_cache_ != -1 && hover_cache_ < static_cast<int>(hotspots_.size())) {
    const Hotspot& c = (*this)[hover_cache_];
    if (x >= c.x0 && y >= c.y0 && x <= c.x1 && y <= c.y1) {
      return hover_cache_;
    }
  }
  for (hover_cache_ = static_cast<int>(hotspots_.size()) - 1; hover_cache_ >= 0; --hover_cache_) {
    const Hotspot& h = (*this)[hover_cache_];
    if (h.x0 <= x && h.y0 <= y && x <= h.x1 && y <= h.y1 && h.enabled()) {
      return hover_cache_;
    }
  }
  return -1;
}

int HotspotTable::find(const Control* control, int sub, bool enabled) const {
  for (std::size_t i = 0; i < hotspots_.size(); ++i) {
    const Hotspot& h = hotspots_[i];
    if (h.enabled() == enabled && h.control == control && h.sub == sub) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int HotspotTable::find_hotkey(const Dialog* owner, std::uint16_t key, std::uint8_t mask) const {
  for (std::size_t i = 0; i < hotspots_.size(); ++i) {
    const Hotspot& h = hotspots_[i];
    if ((h.flags & mask) != 0 && h.owner == owner && hotkey_matches(h.key, key)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int HotspotTable::first_of(const Dialog* owner) const {
  for (std::size_t i = 0; i < hotspots_.size(); ++i) {
    if (hotspots_[i].owner == owner) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void HotspotTable::remove_owned_by(const Dialog* owner) {
  std::erase_if(hotspots_, [owner](const Hotspot& h) { return h.owner == owner; });
}

}  // namespace sh::ui

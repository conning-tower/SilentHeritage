// File name ordering for the viewer lists.
#pragma once

#include <string_view>

namespace sh {

// Orders names of equal stems with numbers by value ("WORLD-2.WLD" before
// "WORLD-10.WLD", "BUD/2.SMK" before "BUD/10.SMK"): shorter names first, then
// alphabetically.
inline bool numbered_name_less(std::string_view a, std::string_view b) {
  return a.size() != b.size() ? a.size() < b.size() : a < b;
}

}  // namespace sh

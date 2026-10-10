#include "game/shpath.h"

#include <cctype>

#include "core/install.h"

namespace sh {

namespace {

constexpr std::array<std::string_view, kGameDirCount> kKeys = {
    "DATA", "SFX", "SPEECH", "CINES", "SAVE", "SCEN", "BUD", "ART", "ALT", "CUSTOM",
};

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

// A cursor over the file text with the scanf primitives `load_shpath_ini`
// relies on.
class Scanner {
 public:
  explicit Scanner(std::string_view text) : text_(text) {}

  bool at_end() const { return pos_ >= text_.size(); }
  void skip_space() {
    while (!at_end() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
  }
  // Reads a `%s` word (after skipping whitespace); empty at the end.
  std::string_view word() {
    skip_space();
    const std::size_t start = pos_;
    while (!at_end() && !std::isspace(static_cast<unsigned char>(text_[pos_]))) {
      ++pos_;
    }
    return text_.substr(start, pos_ - start);
  }
  // Consumes `c` if it is next.
  bool literal(char c) {
    if (at_end() || text_[pos_] != c) {
      return false;
    }
    ++pos_;
    return true;
  }

 private:
  std::string_view text_;
  std::size_t pos_ = 0;
};

}  // namespace

std::string_view game_dir_key(GameDir dir) { return kKeys[static_cast<std::size_t>(dir)]; }

ShPaths::ShPaths() {
  for (std::size_t i = 0; i < kGameDirCount; ++i) {
    dirs_[i] = std::string(kKeys[i]) + '\\';
  }
}

ShPaths ShPaths::parse(std::string_view text) {
  ShPaths paths;
  Scanner in(text);
  // Like the original's `fscanf(f, "%s = %s", key, value)` loop: it stops only
  // at the end of the file; on a partial match the key is still looked up
  // and `value` keeps the previous line's path (with no previous path, the
  // original's uninitialized buffer: the default is kept here).
  std::string value;
  for (;;) {
    const std::string_view key = in.word();
    if (key.empty()) {
      break;
    }
    in.skip_space();
    if (in.literal('=')) {
      const std::string_view v = in.word();
      if (!v.empty()) {
        value = v;
      }
    }
    if (!value.empty() && value.back() != '\\') {
      value += '\\';
    }
    for (std::size_t k = 0; k < kGameDirCount; ++k) {
      if (iequals(key, kKeys[k])) {
        if (!value.empty()) {
          paths.dirs_[k] = value;
        }
        break;
      }
    }
  }
  return paths;
}

ShPaths ShPaths::load(const Install& install) {
  const std::optional<std::vector<std::uint8_t>> data = install.try_read("SHPATH.INI");
  if (!data) {
    return ShPaths();
  }
  return parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
}

std::string_view ShPaths::dir_name(GameDir dir) const {
  std::string_view d = this->dir(dir);
  d.remove_suffix(1);
  return d;
}

}  // namespace sh

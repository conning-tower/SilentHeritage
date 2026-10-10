#include "editor/options.h"

#include <array>
#include <optional>

#include "core/byte_reader.h"

namespace sh::editor {
namespace {

// Returns pointers to the options of `o` (const or not) in file order.
template <class Options>
auto fields(Options& o) {
  return std::array{&o.limit_ships,   &o.limit_subs,   &o.limit_aircraft,      &o.limit_patrol_areas, &o.limit_equipment,
                    &o.range_circles, &o.convoy_icons, &o.end_on_lost_contact, &o.randomize};
}

}  // namespace

std::vector<std::uint8_t> EditorOptions::to_bytes() const {
  std::vector<std::uint8_t> out{kVersion};
  for (const std::int32_t* field : fields(*this)) {
    const auto u = static_cast<std::uint32_t>(*field);
    for (int shift = 0; shift < 32; shift += 8) {
      out.push_back(static_cast<std::uint8_t>(u >> shift));
    }
  }
  return out;
}

EditorOptions EditorOptions::load(const SaveDir& dir) {
  EditorOptions o;
  int open_attempts = 2;
  bool rewrote_other_version = false;
  for (;;) {
    const std::optional<std::vector<std::uint8_t>> data = dir.read(kFileName);
    if (!data) {
      if (--open_attempts == 0) {
        return o;
      }
    } else if (data->empty()) {
      return o;  // the version byte cannot be read: no rewrite
    } else if ((*data)[0] == kVersion) {
      ByteReader r(std::span(*data).subspan(1), kFileName);
      for (std::int32_t* field : fields(o)) {
        if (r.remaining() < 4) {
          break;
        }
        *field = r.i32();
      }
      return o;
    } else if (rewrote_other_version) {
      // Original bug: SE retries forever when the rewrite did not replace a
      // file of another version (only failed opens are counted), so it hangs
      // at start-up. Here the current values are kept.
      return o;
    } else {
      rewrote_other_version = true;
    }
    o.save(dir);
  }
}

}  // namespace sh::editor

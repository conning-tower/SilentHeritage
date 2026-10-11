// Golden traces: hashes of the game's state recorded at checkpoints of a
// scripted run, compared with a trace file committed in tests/golden/ so
// that any change of the faithful behaviour fails a test, with the
// checkpoints that changed.
//
// A trace is a list of lines "<checkpoint> <kind> <value>", e.g.
// "scope sim.vehicle.3 9c0d...". The kinds name what is hashed:
//
//   sim.*    the simulation's state lists (game/sim_state.h), one block each
//   rng      the game generator's state, and `draws` the draws since the
//            previous checkpoint
//   msgs     the crew messages raised since the previous checkpoint
//   ui.*     the mission's UI state (station, logs, pages)
//   frame    the station shown and the screen's hash; `dac` its palette
//   save     the bytes of the MISSn.SAV the game would write
//
// Recording: with SH_GOLDEN_RECORD set (to 1 for every trace, or to a
// substring of the trace names to record), `check_golden` writes the trace
// file instead of comparing. Record only from a faithful build of main, or
// when a change of the faithful behaviour is deliberate (the commit then
// says so).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"
#include "core/random.h"

namespace sh::test {

// Returns the 64-bit FNV-1a hash of `bytes`.
std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes, std::uint64_t hash = 0xcbf29ce484222325ull);

// Returns `hash` as 16 hexadecimal digits.
std::string hex64(std::uint64_t hash);

// Returns the number of draws that take `from` to `to` in the Watcom
// generator's sequence, nothing past `limit` draws.
std::optional<std::uint64_t> draws_between(std::uint32_t from, std::uint32_t to, std::uint64_t limit = 50'000'000);

struct GoldenLine {
  std::string checkpoint;
  std::string kind;
  std::string value;

  bool operator==(const GoldenLine&) const = default;
};

// The lines of a scripted run, in order. Checkpoint names need not be
// unique; a line is identified by its position.
class GoldenTrace {
 public:
  void add(std::string_view checkpoint, std::string_view kind, std::string value);
  // Adds a `hex64` hash line.
  void add_hash(std::string_view checkpoint, std::string_view kind, std::span<const std::uint8_t> bytes);
  // Adds the `<kind>` line of the generator's state and, from the second
  // one, the `<draws>` line of the draws since the previous `<kind>` line.
  void add_random(std::string_view checkpoint, const WatcomRandom& random, std::string_view kind = "rng",
                  std::string_view draws = "draws");
  // Adds the `frame` line of `screen` shown at `station` and its `dac`.
  void add_frame(std::string_view checkpoint, std::string_view station, const Image8& screen,
                 std::span<const std::uint8_t> dac);

  const std::vector<GoldenLine>& lines() const { return lines_; }
  // Returns the lines whose kind starts with one of `kinds` (all for none).
  std::vector<GoldenLine> filtered(std::span<const std::string_view> kinds) const;

 private:
  std::vector<GoldenLine> lines_;
  std::map<std::string, std::uint32_t, std::less<>> last_random_;
};

// The kind prefixes of the simulation's trace: what an improvement to the
// interface must leave unchanged.
inline constexpr std::string_view kSimKinds[] = {"sim.", "rng", "draws", "msgs"};
// Those of the whole program's traces: the simulation's and the front
// end's generator (`fe.rng`, `fe.draws`).
inline constexpr std::string_view kProgramSimKinds[] = {"sim.", "rng", "draws", "msgs", "fe.rng", "fe.draws"};

// Returns the directory of the committed traces (tests/golden).
std::filesystem::path golden_dir();

// Returns whether SH_GOLDEN_RECORD asks to record the trace `name`.
bool golden_recording(std::string_view name);

// Records `trace` as tests/golden/<name>.golden (in record mode), or
// compares it with that file: a test failure lists the first differing
// lines, expected and actual. With `kinds`, only the lines of those kinds
// are compared (and record mode does not apply). `header` lines (e.g. the
// install's fingerprint) are written at the top; a file whose header
// differs makes the test skip, since it was recorded on other data.
void check_golden(std::string_view name, const GoldenTrace& trace, std::span<const std::string_view> kinds = {},
                  std::span<const std::string> header = {});

}  // namespace sh::test

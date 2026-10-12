#include "golden_trace.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace sh::test {

namespace {

constexpr std::size_t kDiffLinesShown = 12;

std::vector<std::string> read_text_lines(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<std::string> out;
  for (std::string line; std::getline(in, line);) {
    out.push_back(line);
  }
  return out;
}

std::string format_line(const GoldenLine& l) { return std::format("{} {} {}", l.checkpoint, l.kind, l.value); }

// Splits a trace file into its header ("@..." lines) and its trace lines;
// '#' lines are comments.
void parse_golden(const std::vector<std::string>& text, std::vector<std::string>& header,
                  std::vector<GoldenLine>& lines) {
  for (const std::string& t : text) {
    if (t.empty() || t.starts_with('#')) {
      continue;
    }
    if (t.starts_with('@')) {
      header.push_back(t.substr(1));
      continue;
    }
    std::istringstream s(t);
    GoldenLine l;
    s >> l.checkpoint >> l.kind;
    std::getline(s >> std::ws, l.value);
    lines.push_back(std::move(l));
  }
}

bool kind_selected(std::string_view kind, std::span<const std::string_view> kinds) {
  return kinds.empty() ||
         std::ranges::any_of(kinds, [&](std::string_view k) { return kind.starts_with(k); });
}

}  // namespace

std::uint64_t fnv1a64(std::span<const std::uint8_t> bytes, std::uint64_t hash) {
  for (const std::uint8_t b : bytes) {
    hash = (hash ^ b) * 0x100000001b3ull;
  }
  return hash;
}

std::string hex64(std::uint64_t hash) { return std::format("{:016x}", hash); }

std::optional<std::uint64_t> draws_between(std::uint32_t from, std::uint32_t to, std::uint64_t limit) {
  WatcomRandom r(from);
  for (std::uint64_t n = 0; n <= limit; ++n) {
    if (r.state() == to) {
      return n;
    }
    r.rand();
  }
  return std::nullopt;
}

void GoldenTrace::add(std::string_view checkpoint, std::string_view kind, std::string value) {
  lines_.push_back({std::string(checkpoint), std::string(kind), std::move(value)});
}

void GoldenTrace::add_hash(std::string_view checkpoint, std::string_view kind, std::span<const std::uint8_t> bytes) {
  add(checkpoint, kind, hex64(fnv1a64(bytes)));
}

void GoldenTrace::add_random(std::string_view checkpoint, const WatcomRandom& random, std::string_view kind,
                             std::string_view draws) {
  const std::uint32_t state = random.state();
  add(checkpoint, kind, std::format("{:08x}", state));
  if (const auto last = last_random_.find(kind); last != last_random_.end()) {
    const std::optional<std::uint64_t> n = draws_between(last->second, state);
    add(checkpoint, draws, n ? std::to_string(*n) : std::string("?"));
    last->second = state;
  } else {
    last_random_.emplace(std::string(kind), state);
  }
}

void GoldenTrace::add_frame(std::string_view checkpoint, std::string_view station, const Image8& screen,
                            std::span<const std::uint8_t> dac) {
  add(checkpoint, "frame", std::format("{} {}", station, hex64(fnv1a64(screen.pixels()))));
  add_hash(checkpoint, "dac", dac);
}

std::vector<GoldenLine> GoldenTrace::filtered(std::span<const std::string_view> kinds) const {
  std::vector<GoldenLine> out;
  std::ranges::copy_if(lines_, std::back_inserter(out), [&](const GoldenLine& l) { return kind_selected(l.kind, kinds); });
  return out;
}

std::filesystem::path golden_dir() { return SH_GOLDEN_DIR; }

bool golden_recording(std::string_view name) {
  const char* v = std::getenv("SH_GOLDEN_RECORD");
  if (v == nullptr || *v == '\0' || std::string_view(v) == "0") {
    return false;
  }
  return std::string_view(v) == "1" || name.find(v) != std::string_view::npos;
}

void check_golden(std::string_view name, const GoldenTrace& trace, std::span<const std::string_view> kinds,
                  std::span<const std::string> header) {
  const std::filesystem::path path = golden_dir() / (std::string(name) + ".golden");
  if (kinds.empty() && golden_recording(name)) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << "# Golden trace \"" << name << "\" of tests/golden_test.cpp, recorded from the classic game.\n"
        << "# Re-record deliberately with SH_GOLDEN_RECORD=" << name << " (see tests/golden_trace.h).\n";
    for (const std::string& h : header) {
      out << '@' << h << '\n';
    }
    for (const GoldenLine& l : trace.lines()) {
      out << format_line(l) << '\n';
    }
    REQUIRE(out.good());
    WARN("recorded " << path.string() << " (" << trace.lines().size() << " lines)");
    return;
  }

  if (!std::filesystem::exists(path)) {
    FAIL("no golden trace " << path.string() << ": record it with SH_GOLDEN_RECORD=" << name);
  }
  std::vector<std::string> expected_header;
  std::vector<GoldenLine> expected_all;
  parse_golden(read_text_lines(path), expected_header, expected_all);
  if (!std::ranges::equal(expected_header, header)) {
    SKIP("golden trace " << path.string() << " was recorded on other game data");
  }

  std::vector<GoldenLine> expected;
  std::ranges::copy_if(expected_all, std::back_inserter(expected),
                       [&](const GoldenLine& l) { return kind_selected(l.kind, kinds); });
  const std::vector<GoldenLine> actual = trace.filtered(kinds);
  if (expected == actual) {
    SUCCEED();
    return;
  }
  // The readable difference: the first lines that changed, by position.
  std::ostringstream diff;
  diff << "golden trace " << name << " differs (" << expected.size() << " expected lines, " << actual.size()
       << " actual):\n";
  std::size_t shown = 0;
  const std::size_t n = std::max(expected.size(), actual.size());
  for (std::size_t i = 0; i < n && shown < kDiffLinesShown; ++i) {
    const GoldenLine* e = i < expected.size() ? &expected[i] : nullptr;
    const GoldenLine* a = i < actual.size() ? &actual[i] : nullptr;
    if (e != nullptr && a != nullptr && *e == *a) {
      continue;
    }
    diff << "  line " << i + 1 << ":\n    expected " << (e ? format_line(*e) : "(none)") << "\n    actual   "
         << (a ? format_line(*a) : "(none)") << '\n';
    ++shown;
  }
  FAIL(diff.str());
}

}  // namespace sh::test

#include "game/crew_log.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

#include "core/byte_reader.h"
#include "sim/state_io.h"

namespace sh {

void CrewLog::add(const GameClock& clock, std::string speaker, std::string text) {
  if (entries_.size() >= kMaxEntries) {
    entries_.pop_front();
  }
  entries_.push_back({.day = clock.day(),
                      .month = clock.month(),
                      .year = clock.year(),
                      .hour = clock.hour(),
                      .minute = clock.minute(),
                      .second = clock.second(),
                      .speaker = std::move(speaker),
                      .text = std::move(text)});
}

namespace {

void write_string(ByteWriter& out, std::string_view s) {
  out.u32(static_cast<std::uint32_t>(s.size()));
  out.bytes(std::span(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

std::string read_string(ByteReader& in) {
  const std::span<const std::uint8_t> b = in.bytes(in.u32());
  return {b.begin(), b.end()};
}

}  // namespace

std::vector<std::uint8_t> CrewLog::serialize() const {
  ByteWriter out;
  out.u32(static_cast<std::uint32_t>(entries_.size()));
  for (const CrewLogEntry& e : entries_) {
    for (const std::int32_t v : {e.day, e.month, e.year, e.hour, e.minute, e.second}) {
      out.i32(v);
    }
    write_string(out, e.speaker);
    write_string(out, e.text);
  }
  return out.take();
}

CrewLog CrewLog::parse(std::span<const std::uint8_t> data) {
  ByteReader in(data, "crew log");
  CrewLog log;
  const std::uint32_t count = in.u32();
  for (std::uint32_t i = 0; i < count; ++i) {
    CrewLogEntry e;
    for (std::int32_t* v : {&e.day, &e.month, &e.year, &e.hour, &e.minute, &e.second}) {
      *v = in.i32();
    }
    e.speaker = read_string(in);
    e.text = read_string(in);
    if (log.entries_.size() >= kMaxEntries) {
      log.entries_.pop_front();
    }
    log.entries_.push_back(std::move(e));
  }
  if (!in.at_end()) {
    in.fail("crew log longer than expected");
  }
  return log;
}

namespace {

// Appends the lines of `text` wrapped to `width` pixels of `font`: the
// first `kMessage`, the others `kContinuation`.
void wrap(std::vector<CrewLogLine>& lines, std::size_t entry, std::string_view text, const Font& font, int width) {
  CrewLogLine::Kind kind = CrewLogLine::Kind::kMessage;
  std::string line;
  const auto flush = [&] {
    lines.push_back({kind, entry, std::move(line)});
    line.clear();
    kind = CrewLogLine::Kind::kContinuation;
  };
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t space = text.find(' ', at);
    std::string_view word = text.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at);
    at = space == std::string_view::npos ? text.size() : space + 1;
    if (word.empty()) {
      continue;
    }
    const std::string joined = line.empty() ? std::string(word) : line + " " + std::string(word);
    if (font.text_width(joined) <= width) {
      line = joined;
      continue;
    }
    if (!line.empty()) {
      flush();
    }
    // A word wider than the line is cut where it overflows.
    while (word.size() > 1 && font.text_width(word) > width) {
      std::size_t fit = 1;
      while (fit < word.size() && font.text_width(word.substr(0, fit + 1)) <= width) {
        ++fit;
      }
      line = std::string(word.substr(0, fit));
      flush();
      word.remove_prefix(fit);
    }
    line = std::string(word);
  }
  if (!line.empty() || kind == CrewLogLine::Kind::kMessage) {
    flush();
  }
}

}  // namespace

std::vector<CrewLogLine> crew_log_lines(const std::deque<CrewLogEntry>& entries, const Font& font, int width) {
  std::vector<CrewLogLine> lines;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (i == 0 || !entries[i].same_day(entries[i - 1])) {
      lines.push_back({CrewLogLine::Kind::kDate, i, {}});
    }
    wrap(lines, i, entries[i].speaker + ": " + entries[i].text, font, width);
  }
  return lines;
}

std::string crew_log_time(const CrewLogEntry& entry) {
  return std::format("{:02}:{:02}:{:02}", entry.hour, entry.minute, entry.second);
}

bool CrewLogScroll::scroll_to(int top) {
  top = std::clamp(top, 0, max_top());
  if (top == top_) {
    return false;
  }
  top_ = top;
  return true;
}

}  // namespace sh

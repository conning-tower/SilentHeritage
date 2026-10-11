// The crew message history of the extended variant (`Features::crew_log`,
// not in the original): the mission's last crew messages with their game
// times, listed by the log the command bar's message strip (or `L`) opens.
// The original's 20-slot ring (`CrewMessageLog`) is left as it is.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "assets/fnt.h"
#include "sim/clock.h"

namespace sh {

// One message: its game time (as `GameClock`: day 1-based, month 0-based,
// year since 1930), its speaker and its text after "<speaker>: ".
struct CrewLogEntry {
  std::int32_t day = 1;
  std::int32_t month = 0;
  std::int32_t year = 0;
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t second = 0;
  std::string speaker;
  std::string text;

  // Returns whether `other` falls on the same day.
  bool same_day(const CrewLogEntry& other) const {
    return day == other.day && month == other.month && year == other.year;
  }
  friend bool operator==(const CrewLogEntry&, const CrewLogEntry&) = default;
};

class CrewLog {
 public:
  // The messages kept; the oldest are dropped beyond.
  static constexpr std::size_t kMaxEntries = 100;

  // Appends a message at the time of `clock`.
  void add(const GameClock& clock, std::string speaker, std::string text);
  // The messages, oldest first.
  const std::deque<CrewLogEntry>& entries() const { return entries_; }

  // Returns the log as the "CLOG" section of the state extension
  // (game/sim_state.h): the count, then per message its six time fields
  // and the speaker and text as length-prefixed strings.
  std::vector<std::uint8_t> serialize() const;
  // Returns the log of a "CLOG" section (its newest `kMaxEntries`); throws
  // `DataError` on a short or malformed one.
  static CrewLog parse(std::span<const std::uint8_t> data);

  friend bool operator==(const CrewLog&, const CrewLog&) = default;

 private:
  std::deque<CrewLogEntry> entries_;
};

// One line of the listed log.
struct CrewLogLine {
  enum class Kind : std::uint8_t {
    kDate,          // a date heading
    kMessage,       // a message's first line, after its time
    kContinuation,  // a further line of the message
  };
  Kind kind = Kind::kMessage;
  // The entry the line belongs to (the first of its day for a heading).
  std::size_t entry = 0;
  // The text of the line (empty for a heading; a message's first line
  // starts with "<speaker>: ").
  std::string text;

  friend bool operator==(const CrewLogLine&, const CrewLogLine&) = default;
};

// Returns the lines of `entries`, oldest first: a date heading before the
// first message of each day, then each message as "<speaker>: <text>"
// word-wrapped to `width` pixels of `font`. A word wider than a line is
// cut.
std::vector<CrewLogLine> crew_log_lines(const std::deque<CrewLogEntry>& entries, const Font& font, int width);

// Returns "HH:MM:SS" of `entry`.
std::string crew_log_time(const CrewLogEntry& entry);

// The scroll position of a list of `count` lines showing `rows` at once:
// `top`, the first line shown, stays within 0..`max_top()`.
class CrewLogScroll {
 public:
  // Starts at the end (the newest lines).
  CrewLogScroll(int count, int rows) : count_(count), rows_(rows), top_(max_top()) {}

  int top() const { return top_; }
  int count() const { return count_; }
  int rows() const { return rows_; }
  int max_top() const { return count_ > rows_ ? count_ - rows_ : 0; }
  bool at_start() const { return top_ == 0; }
  bool at_end() const { return top_ == max_top(); }
  // Moves `top` to `top`, clamped; returns whether it moved.
  bool scroll_to(int top);
  // Moves `top` by `lines` (negative: back); returns whether it moved.
  bool scroll_by(int lines) { return scroll_to(top_ + lines); }

 private:
  int count_;
  int rows_;
  int top_;
};

}  // namespace sh

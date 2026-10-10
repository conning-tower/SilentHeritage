#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "sim/clock.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim_classes.h"

namespace sh {

namespace {

// Advances `clock` by `seconds` of game time at 1x (frames of 10 ticks).
void run_seconds(GameClock& clock, int seconds, ClockObserver* observer = nullptr) {
  for (int i = 0; i < seconds * 10; ++i) {
    clock.advance(10, false, observer);
  }
}

class Recorder : public ClockObserver {
 public:
  void on_minute_started(const GameClock& c) override { events.push_back("minute " + std::to_string(c.minute())); }
  void on_new_day(const GameClock& c) override { events.push_back("day " + std::to_string(c.day())); }
  void on_minute_ended(const GameClock&) override { events.push_back("end"); }
  std::vector<std::string> events;
};

}  // namespace

TEST_CASE("The clock rolls the date over at midnight", "[sim][clock]") {
  GameClock clock;
  clock.init(11, 11, 31, 23, 59, 59);  // 31 Dec 1941 23:59:59.
  CHECK(clock.weekday() == 2);         // Wednesday.
  Recorder rec;
  run_seconds(clock, 1, &rec);
  CHECK(clock.year() == 12);
  CHECK(clock.month() == 0);
  CHECK(clock.day() == 1);
  CHECK(clock.hour() == 0);
  CHECK(clock.minute() == 0);
  CHECK(clock.second() == 0);
  CHECK(clock.weekday() == 3);  // 1 Jan 1942 was a Thursday.
  CHECK(clock.minutes_elapsed() == 1);
  // The day event comes before the month rolls over, as clock_update_sky.
  CHECK(rec.events == std::vector<std::string>{"minute 60", "day 32", "end"});

  clock.init(14, 1, 28, 23, 59, 59);  // 1944: leap year.
  run_seconds(clock, 1);
  CHECK(clock.month() == 1);
  CHECK(clock.day() == 29);
  clock.init(12, 1, 28, 23, 59, 59);  // 1942.
  run_seconds(clock, 1);
  CHECK(clock.month() == 2);
  CHECK(clock.day() == 1);
  CHECK(days_in_month(10, 1) == 29);  // 1940.
}

TEST_CASE("A frame counts at most 15 timer ticks", "[sim][clock]") {
  GameClock clock;
  clock.init(12, 5, 4, 12, 0);
  clock.set_auto_adjust(false);
  CHECK(clock.advance(40, false) == 15);
  CHECK(clock.hundredths() == 15);
  CHECK(clock.advance(7, false) == 7);
}

TEST_CASE("Time compression requests double up to the cap", "[sim][clock]") {
  GameClock clock;
  clock.init(12, 5, 4, 12, 0);
  int doublings = 0;
  while (clock.double_compression()) {
    ++doublings;
  }
  CHECK(doublings == 11);
  CHECK(clock.requested_compression() == 2048);
  CHECK(clock.halve_compression());
  CHECK(clock.requested_compression() == 1024);
  CHECK(clock.reset_compression());
  CHECK(clock.requested_compression() == 1);
  CHECK_FALSE(clock.halve_compression());
  CHECK_FALSE(clock.reset_compression());
  // A lower cap stops the doubling.
  clock.set_compression_cap(64);
  while (clock.double_compression()) {
  }
  CHECK(clock.requested_compression() == 64);
}

TEST_CASE("The effective compression follows the request while frames are short", "[sim][clock]") {
  GameClock clock;
  clock.init(12, 5, 4, 12, 0);
  for (int i = 0; i < 4; ++i) {
    clock.double_compression();
  }
  // Short frames (1 tick): one doubling at the first frame, then every other.
  std::vector<std::int32_t> seen;
  for (int i = 0; i < 9; ++i) {
    clock.advance(1, false);
    seen.push_back(clock.effective_compression());
  }
  CHECK(seen == std::vector<std::int32_t>{2, 2, 4, 4, 8, 8, 16, 16, 16});
  CHECK(clock.frame_ticks() == 16);

  // Mid-length frames (10 ticks) keep it, unless a station just opened.
  clock.advance(10, false);
  clock.advance(10, false);
  CHECK(clock.effective_compression() == 16);
  CHECK(clock.reset_compression());
  clock.advance(10, false);
  clock.advance(10, false);
  CHECK(clock.effective_compression() == 1);
  clock.double_compression();
  clock.advance(10, true);
  clock.advance(10, false);
  CHECK(clock.effective_compression() == 2);
}

TEST_CASE("Unrestricted, the effective compression stays at 256 or more", "[sim][clock]") {
  GameClock clock;
  clock.init(12, 5, 4, 12, 0);
  while (clock.double_compression()) {
  }
  // A request of 256 or more jumps straight to 256, then doubles.
  std::vector<std::int32_t> seen;
  for (int i = 0; i < 7; ++i) {
    clock.advance(1, false);
    seen.push_back(clock.effective_compression());
  }
  CHECK(seen == std::vector<std::int32_t>{256, 256, 512, 512, 1024, 1024, 2048});
  // Long frames (over 20 ticks) halve it, but never below the 256 jump.
  seen.clear();
  for (int i = 0; i < 4; ++i) {
    clock.advance(50, false);
    seen.push_back(clock.effective_compression());
  }
  CHECK(seen == std::vector<std::int32_t>{1024, 512, 256, 256});
  // The game's limits (deep boat, contacts) hold only until the next
  // adjustment: their callers apply them every frame.
  clock.limit_effective_compression(64);
  CHECK(clock.effective_compression() == 64);
  clock.advance(10, false);
  clock.advance(10, false);
  CHECK(clock.effective_compression() == 256);
}

TEST_CASE("Ships cap the time compression at 64x", "[sim][clock]") {
  const FlatSeaFloor floor(-1000 * kUnitsPerFoot);
  Simulation sim(MissionConditions{}, floor);
  sim.add(std::make_unique<Ship>(test::lfreight(), fx::Vec3{500000, 0, 500000}, Angle10(0), Fixed16::from_int(8),
                                 ShipOrder::kPatrol));
  while (sim.clock().double_compression()) {
  }
  CHECK(sim.clock().requested_compression() == 2048);
  for (int i = 0; i < 20; ++i) {
    sim.step(1);
  }
  CHECK(sim.clock().compression_cap() == 64);
  CHECK(sim.clock().effective_compression() == 64);
  // A frame of 10 ticks at 64x is 6.4 s of game time for the ships.
  const std::int32_t z = sim.vehicles()[0]->position().z;
  sim.step(10);
  CHECK(sim.clock().frame_ticks() == 640);
  const double moved_yd = (sim.vehicles()[0]->position().z - z) / 48.0;
  CHECK(moved_yd > 6.4 * 8 * 0.5);
  CHECK(moved_yd < 6.4 * 8 * 0.6);
}

}  // namespace sh

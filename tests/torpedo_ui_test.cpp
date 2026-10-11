// Tests of the extended variant's torpedo type display (not in the
// original; `Features::torpedo_types`): the pictures, the status page's
// cards, the marked tube buttons, the type knob, the Mark 27's
// bearing-only TDC and the touch panel's tube icons; and that with the
// feature off the stations draw as the original.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "app/command_icons.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "game/stations/status.h"
#include "game/torpedo_art.h"
#include "game_data.h"
#include "gfx/sprite.h"
#include "sim/player_combat.h"
#include "sim/torpedo.h"
#include "sim/ship.h"
#include "torpedo_rig.h"

namespace sh {

namespace {

using test::TorpedoRig;

constexpr std::uint16_t kKeyStatus = 0x4200;  // F8
constexpr std::uint16_t kKeyTdc = 0x4000;     // F6

// Draws picture or frame on a blank image the size of the screen.
Image8 blank() { return Image8(kScreenWidth, kScreenHeight, 0); }

// Returns the colours used by `p`'s opaque pixels.
std::set<std::uint8_t> colours(const TorpedoPicture& p) {
  std::set<std::uint8_t> out;
  for (int y = 0; y < p.image.height(); ++y) {
    for (int x = 0; x < p.image.width(); ++x) {
      if (p.opaque.at(x, y) != 0) {
        out.insert(p.image.at(x, y));
      }
    }
  }
  return out;
}

// The mean grey level (0..255) of `rect` of `image` in `palette`.
double mean_grey(const Image8& image, const VgaPalette& palette, int x0, int y0, int x1, int y1) {
  double sum = 0;
  int n = 0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const auto u = static_cast<std::size_t>(image.at(x, y)) * 3;
      sum += (palette[u] + palette[u + 1] + palette[u + 2]) * 255.0 / 63 / 3;
      ++n;
    }
  }
  return sum / n;
}

StatusStation& open_torpedo_page(TorpedoRig& rig) {
  rig.frames(1);
  rig.settle();
  rig.key(kKeyStatus);
  rig.frames(1);
  rig.settle();
  INFO("page after one press " << rig.screen().ui_state().status_page);
  if (rig.screen().ui_state().status_page != 1) {
    rig.key(kKeyStatus);
    rig.frames(1);
    rig.settle();
  }
  rig.frames(2);
  auto* s = dynamic_cast<StatusStation*>(rig.screen().loop().open_station());
  REQUIRE(s != nullptr);
  REQUIRE(rig.screen().ui_state().status_page == 1);
  return *s;
}

}  // namespace

// ------------------------------------------------------------ pictures

TEST_CASE("The Mark 10 and 14 pictures are the original's frames, as drawn", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp statust = Shp::load(install, "ART/STATUST.SHP");
  const TorpedoArt art(statust, load_pcx(install, "ART/STATUSBG.PCX").palette);
  Image8 scratch;
  for (const std::uint32_t id : {kMark10Id, kMark14Id}) {
    for (const bool moving : {false, true}) {
      const SpriteFrame& f = statust.frame(0xc + 2 * (id - 1) + (moving ? 1 : 0));
      for (const bool mirror : {false, true}) {
        Image8 original = blank();
        Image8 ours = blank();
        if (mirror) {
          const Fixed16 one = Fixed16::from_int(1);
          sprite_draw_transformed(Viewport::full(original), f, 0x38, 0xa5, scratch, Angle10(0), -one, one, 0);
        } else {
          draw_sprite_clipped(Viewport::full(original), f, 0x246, 0xa5);
        }
        TorpedoArt::draw(Viewport::full(ours), art.picture(id, moving), mirror ? 0x38 : 0x246, 0xa5, mirror, scratch);
        const auto [a, b] = std::ranges::mismatch(ours.pixels(), original.pixels());
        const auto at = a - ours.pixels().begin();
        INFO("id " << id << " moving " << moving << " mirror " << mirror << " first difference at x "
                   << at % kScreenWidth << " y " << at / kScreenWidth);
        CHECK(a == ours.pixels().end());
      }
    }
  }
}

TEST_CASE("The Mark 18-1, 18-2 and 27 pictures differ and keep off the original ramps", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp statust = Shp::load(install, "ART/STATUST.SHP");
  const TorpedoArt art(statust, load_pcx(install, "ART/STATUSBG.PCX").palette);
  for (const std::uint32_t id : {kMark18_1Id, kMark18_2Id, kMark27Id}) {
    for (const std::uint8_t c : colours(art.picture(id, false))) {
      // The original types' ramps, the propellers (202) aside.
      CHECK((c < 16 || c >= 64 || c == 202));
    }
  }
  const auto& a = art.picture(kMark18_1Id, false);
  const auto& b = art.picture(kMark18_2Id, false);
  REQUIRE(a.image.width() == b.image.width());
  CHECK_FALSE(std::ranges::equal(a.image.pixels(), b.image.pixels()));
  CHECK(colours(a) != colours(art.picture(kMark10Id, false)));
  // The Mark 27 is shorter.
  CHECK(art.picture(kMark27Id, false).image.width() < a.image.width());
  CHECK_THROWS_AS(art.picture(7, false), std::out_of_range);
}

// ------------------------------------------------------------ status page

TEST_CASE("The status page shows a card per type carried at the start, which stays", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  TorpedoRig rig(install, test::mixed_scenario(install, false), Features::extended(), test::torpedo_rules(false));
  CHECK(rig.mission().start_torpedo_types() == std::vector<std::uint32_t>{2, 3, 4});
  StatusStation& s = open_torpedo_page(rig);
  CHECK(s.card_types() == std::vector<std::uint32_t>{2, 3, 4});
  rig.save("torpedo_status_mixed");
  // Every 18-1 gone: its card stays.
  TorpedoTubes& tubes = rig.mission().player_combat().tubes;
  for (std::size_t t = 0; t < static_cast<std::size_t>(tubes.count()); ++t) {
    if (tubes.content(t) == 3) {
      tubes.clear(t);
    }
  }
  for (auto* rack : {&tubes.bow_rack(), &tubes.stern_rack()}) {
    std::replace(rack->begin(), rack->end(), 3, 0);
  }
  rig.key(0x3b00);  // the conn
  rig.settle();
  StatusStation& again = open_torpedo_page(rig);
  CHECK(again.card_types() == std::vector<std::uint32_t>{2, 3, 4});
}

TEST_CASE("Without the feature the cards are the TDC table's, as the original", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  TorpedoRig rig(install, test::mixed_scenario(install, false), Features::faithful(), MissionRules{});
  StatusStation& s = open_torpedo_page(rig);
  // February 1944: the original's table MARK10 / MARK14 / MARK18-1, the
  // Mark 10 by its date.
  CHECK(s.card_types() == std::vector<std::uint32_t>{1, 2, 3});
}

TEST_CASE("Moving a torpedo frames its type's card", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  TorpedoRig rig(install, test::mixed_scenario(install, false), Features::extended(), test::torpedo_rules(false));
  StatusStation& s = open_torpedo_page(rig);
  CHECK(s.highlighted_card() == 0);
  // The first bow rack slot holds an 18-1.
  const auto it = std::ranges::find_if(s.hotspots(), [](const StatusStation::Hotspot& h) {
    return h.kind == 11 && h.index == 0;
  });
  REQUIRE(it != s.hotspots().end());
  rig.click((it->x0 + it->x1) / 2, (it->y0 + it->y1) / 2);
  rig.frames(2);
  CHECK(s.highlighted_card() == 3);
  rig.save("torpedo_status_moving");
  // White framing pixels around the 18-1's card.
  int white = 0;
  for (int x = 0; x < kScreenWidth; ++x) {
    white += rig.screen().screen().at(x, 0x158 + 1) == 240 ? 1 : 0;
  }
  CHECK(white > 100);
}

// ------------------------------------------------------------ TDC

namespace {

// Returns the TDC station's screen with `scenario`, `features` and `rules`,
// the torpedo type `index` selected.
Image8 tdc_screen(const Install& install, Scenario scenario, Features features, MissionRules rules, std::int32_t index,
                  std::string_view name) {
  TorpedoRig rig(install, std::move(scenario), features, rules);
  rig.mission().player_combat().tdc.set_type_index(index);
  rig.frames(1);
  rig.settle();
  rig.key(kKeyTdc);
  rig.settle();
  rig.frames(4);
  rig.save(name);
  return rig.screen().screen();
}

// The pixels of the knob's labels, left of the knob (46,379)-(104,437).
std::vector<std::uint8_t> knob_labels(const Image8& screen) {
  std::vector<std::uint8_t> out;
  for (int y = 379; y <= 437; ++y) {
    for (int x = 0; x < 46; ++x) {
      out.push_back(screen.at(x, y));
    }
  }
  return out;
}

}  // namespace

TEST_CASE("The knob's labels are the table's names in the art's lettering", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  // The original's table (February 1944: MARK10, MARK14, MARK18-1).
  const Image8 faithful =
      tdc_screen(install, test::mixed_scenario(install, false), Features::faithful(), {}, 1, "torpedo_tdc_faithful");
  const Image8 extended =
      tdc_screen(install, test::mixed_scenario(install, false), Features::extended(), {}, 1, "torpedo_tdc_feature");
  // The labels are written ("MARK18-1" is longer than the art's "MARK18");
  // the rest of the station, the tube buttons included, is the original's.
  CHECK(knob_labels(extended) != knob_labels(faithful));
  int differ = 0;
  for (int y = 0; y < kScreenHeight; ++y) {
    for (int x = 0; x < kScreenWidth; ++x) {
      const bool labels = x < 46 && y >= 379 && y <= 437;
      differ += !labels && extended.at(x, y) != faithful.at(x, y) ? 1 : 0;
    }
  }
  CHECK(differ == 0);
  // "MARK10" and "MARK14" drawn from the art's own letters: their pixels
  // are the art's.
  for (int y = 379 + 26; y < 379 + 26 + 7; ++y) {
    for (int x = 46 - 41; x < 46 - 41 + 33; ++x) {
      CHECK(extended.at(x, y) == faithful.at(x, y));
    }
  }
  // The selected type changes the knob, not its labels (no highlight).
  const Image8 other =
      tdc_screen(install, test::mixed_scenario(install, false), Features::extended(), {}, 2, "torpedo_tdc_feature_other");
  CHECK(knob_labels(other) == knob_labels(extended));
}

TEST_CASE("Under the torpedo rules the knob holds the types carried", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool feature : {false, true}) {
    TorpedoRig rig(install, test::mixed_scenario(install, false), feature ? Features::extended() : Features::faithful(),
                   test::torpedo_rules(false));
    const auto& types = rig.mission().player_combat().torpedo_types;
    CHECK(types[0].name == "MARK14");
    CHECK(types[1].name == "MARK18-1");
    CHECK(types[2].name == "MARK18-2");
  }
}

TEST_CASE("With fewer than three types the knob stops on those only", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  // Two types (Mark 14 and 18-2): slots 0 and 1; slot 2 an unavailable
  // straight runner.
  Scenario sc = test::mixed_scenario(install, false);
  for (std::size_t t = 0; t < sc.header.tube_torpedoes.size(); ++t) {
    sc.header.tube_torpedoes[t] = t % 2 == 0 ? 2 : 4;
  }
  sc.header.reloads = {};
  sc.header.reloads[0] = {4, 4, 10};
  sc.header.reloads[1].type = -1;
  TorpedoRig rig(install, std::move(sc), Features::extended(), test::torpedo_rules(false));
  const auto& types = rig.mission().player_combat().torpedo_types;
  CHECK(types[0].available);
  CHECK(types[1].available);
  CHECK_FALSE(types[2].available);
  Tdc& tdc = rig.mission().player_combat().tdc;
  std::set<std::int32_t> stops;
  for (int i = 0; i < 6; ++i) {
    tdc.cycle_type(rig.mission().player_combat().torpedo_types);
    stops.insert(tdc.type_index());
  }
  CHECK(stops == std::set<std::int32_t>{0, 1});
}

TEST_CASE("The Mark 27 dims the dials a bearing-only solution leaves out", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  TorpedoRig rig(install, test::mixed_scenario(install, true), Features::extended(), test::torpedo_rules(true));
  rig.frames(1);
  rig.settle();
  rig.key(kKeyTdc);
  rig.settle();
  PlayerCombat& pc = rig.mission().player_combat();
  // The Mark 14 became an 18-2 (three types at most).
  REQUIRE(pc.torpedo_types[2].name == "MARK27");
  // The range counter (320,19)-(385,37): Mark 18-2, then Mark 27.
  pc.tdc.set_type_index(1);
  rig.frames(4);
  const double straight = mean_grey(rig.screen().screen(), rig.screen().dac(), 320, 19, 385, 37);
  pc.tdc.set_type_index(2);
  rig.frames(4);
  CHECK(pc.tdc.bearing_only());
  const double homing = mean_grey(rig.screen().screen(), rig.screen().dac(), 320, 19, 385, 37);
  CHECK(homing < straight * 0.7);
  rig.save("torpedo_tdc_mark27");
}

// ------------------------------------------------------------ touch panel

TEST_CASE("The touch panel's label strips name the tubes' torpedo types", "[torpedo_ui]") {
  SH_REQUIRE_GAME_DATA(install);
  TorpedoRig rig(install, test::mixed_scenario(install, false), Features::extended(), test::torpedo_rules(false));
  PlayerCombat& pc = rig.mission().player_combat();
  pc.tubes.set_tube(1, -4);
  pc.tubes.clear(2);
  const std::array<TubeMark, kTubeIconCount> marks = tube_marks(pc);
  CHECK(marks[0] == TubeMark{3, false, "18-1"});
  CHECK(marks[1] == TubeMark{4, true, "18-2"});
  CHECK(marks[2] == TubeMark{});
  CHECK(marks[4] == TubeMark{2, false, "14"});
}

// The screenshots of the design note: run explicitly with
// SH_SCREENSHOT_DIR (and SH_PANEL_FRAME, a 640x480 BMP, for the panel).
TEST_CASE("Torpedo type screenshots", "[.torpedo_screens]") {
  SH_REQUIRE_GAME_DATA(install);
  {
    // The Mark 27 on the TDC, its bearing from the sonar: an escort heard
    // on the starboard bow.
    TorpedoRig rig(install, test::mixed_scenario(install, true), Features::extended(), test::torpedo_rules(true));
    rig.frames(1);
    rig.settle();
    rig.key(kKeyTdc);
    rig.settle();
    Mission& m = rig.mission();
    PlayerCombat& pc = m.player_combat();
    pc.tdc.set_mode(Tdc::Mode::kManual);
    pc.tdc.set_depth_ft(60);
    pc.tdc.set_type_index(2);
    Ship* escort = m.ships().front();
    const auto hear = [&] {
      escort->ai().heard.heard = true;
      escort->ai().heard.screw = ScrewSound::kHighSpeed;
      escort->ai().heard.bearing = Angle10(350);
    };
    hear();
    SimContext ctx = m.sim().context();
    pc.update_frame(ctx, m.player());
    pc.tdc.cycle_sonar_source(pc.heard);
    // The bearing follows the source while heard, and stays.
    pc.update_frame(ctx, m.player());
    rig.frames(6);
    CHECK(pc.tdc.bearing() == Angle10(350));
    rig.save("torpedo_tdc_mark27_sonar");
  }
  {
    // The TBT's TDC torpedo panel with the mixed load, tube 2 loading.
    TorpedoRig rig(install, test::mixed_scenario(install, false), Features::extended(), test::torpedo_rules(false));
    rig.mission().player_combat().tubes.set_tube(1, -4);
    rig.mission().player_combat().tubes.set_timer(1, 90 << 16);
    rig.session->services().lower_panel = LowerPanel::kTdc;
    rig.session->services().tdc_panel = TdcSubPanel::kTorpedo;
    rig.frames(1);
    rig.settle();
    rig.key(0x3d00);  // F3: the TBT
    rig.settle();
    rig.frames(6);
    rig.save("torpedo_tbt_mixed");
  }
  // One and two types aboard, under the torpedo rules: the knob's slots
  // hold the types carried, the others unavailable (no label).
  for (const auto& [types, name] : {std::pair{std::array<std::int32_t, 2>{4, 4}, "torpedo_tdc_one_type"},
                                    std::pair{std::array<std::int32_t, 2>{2, 4}, "torpedo_tdc_two_types"}}) {
    Scenario sc = test::mixed_scenario(install, false);
    for (std::size_t t = 0; t < sc.header.tube_torpedoes.size(); ++t) {
      sc.header.tube_torpedoes[t] = types[t % 2];
    }
    sc.header.reloads = {};
    sc.header.reloads[0] = {types[1], 4, 10};
    sc.header.reloads[1].type = -1;
    tdc_screen(install, std::move(sc), Features::extended(), test::torpedo_rules(false), 0, name);
  }
  // The original game's boats with one type (an S-boat's Mark 10) or two:
  // its own rendering, for comparison.
  for (const auto& [scen, name] : {std::pair{"SCEN/HIST06.SCN", "torpedo_tdc_original_one_type"}}) {
    Scenario sc = parse_scenario(install.read(scen), scen);
    TorpedoRig rig(install, std::move(sc), Features::faithful(), {});
    rig.frames(1);
    rig.settle();
    rig.key(kKeyTdc);
    rig.settle();
    rig.frames(4);
    rig.save(name);
  }
}

}  // namespace sh

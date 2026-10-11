// The main menu's INTERVIEWS and SUB TOUR screens: their data read from
// SH.EXE, the interview player's slider, and both screens driven by
// synthetic clicks (pages, videos, STOP / RESTART, parts and narrations).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/exe_image.h"
#include "assets/shp.h"
#include "assets/smacker.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_extras.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game_data.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {
namespace {

// A scratch directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_frontend_extras_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// The interview picture: where the logo and the videos are drawn.
constexpr int kPictureX = 300;
constexpr int kPictureY = 121;

// A host clicking at scripted points (one every 40 polls), recording the
// speech and the videos started, each video's first picture, and the
// screen of every frame shown.
class ScriptedHost final : public FrontendHost, public MissionRunner {
 public:
  struct Click {
    int x = 0;
    int y = 0;
  };

  // Records the video sound streams.
  class VideoRecorder final : public VideoSound {
   public:
    explicit VideoRecorder(ScriptedHost& host) : host_(host) {}
    void start(const SmackerAudioFormat&) override {
      ++starts;
      host_.capture_picture_ = true;
    }
    void queue(std::span<const std::uint8_t>) override { ++queued; }
    void stop() override { ++stops; }
    int starts = 0;
    int stops = 0;
    int queued = 0;

   private:
    ScriptedHost& host_;
  };

  std::deque<Click> script;
  std::vector<std::string> speech;
  int speech_stops = 0;
  VideoRecorder video{*this};
  // The 320x240 picture at (300, 121) when each video began.
  std::vector<Image8> first_pictures;
  // Called with the screen of every frame shown.
  std::function<void(const Image8&)> on_present;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8& screen, const VgaPalette&) override {
    tick_ += 2;
    if (++frames_ > 100'000) {
      throw std::runtime_error("the script did not end the front end");
    }
    if (capture_picture_) {
      capture_picture_ = false;
      Image8 picture(320, 240);
      for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
          picture.set(x, y, screen.at(kPictureX + x, kPictureY + y));
        }
      }
      first_pictures.push_back(std::move(picture));
    }
    if (on_present) {
      on_present(screen);
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Click c = script.front();
    script.pop_front();
    gui.mouse().update(c.x, c.y, ui::kButtonLeft, 0);
    gui.mouse().update(c.x, c.y, 0, 0);
  }
  VideoSound* video_sound() override { return &video; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  bool play_speech(std::string_view file_name) override {
    speech.emplace_back(file_name);
    return true;
  }
  void stop_speech() override { ++speech_stops; }
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view message) override { throw std::runtime_error(std::string(message)); }
  std::int32_t run(const MissionRequest&) override { throw std::runtime_error("no mission expected"); }
  MissionOutcome outcome() const override { return {}; }

 private:
  std::uint32_t tick_ = 0;
  int idle_ = 0;
  int frames_ = 0;
  bool capture_picture_ = false;
};

// The main menu (dialog at (20, 43), buttons 198 x 24 from (35, 130)).
constexpr ScriptedHost::Click kInterviews{145, 43 + 210 + 12};
constexpr ScriptedHost::Click kSubTour{145, 43 + 250 + 12};
constexpr ScriptedHost::Click kExitToDos{145, 43 + 330 + 12};
// INTERVIEWS (dialog 0x18a6e4 at (0, 0)).
constexpr ScriptedHost::Click kVideoA{0x19 + 16, 0x91 + 12};
constexpr ScriptedHost::Click kVideoB{0x19 + 16, 0xc3 + 12};
constexpr ScriptedHost::Click kNextPage{0x14 + 60, 0x172 + 12};
constexpr ScriptedHost::Click kPrevPage{0x91 + 60, 0x172 + 12};
constexpr ScriptedHost::Click kInterviewsMainMenu{0xdd + 99, 0x1ae + 12};
// The video controls (dialog 0x18ab7c at (0, 0)).
constexpr ScriptedHost::Click kStop{0x140 + 16, 0x178 + 12};
constexpr ScriptedHost::Click kRestart{0x168 + 16, 0x178 + 12};
constexpr ScriptedHost::Click kSliderTrack{0x19a + 90, 0x182 + 7};
// SUB TOUR: the panel 0x127504 at (10, 308), titled, and the part areas.
constexpr ScriptedHost::Click kToggleView{10 + 0x19 + 99, 308 + 0x25 + 12 + 3};
constexpr ScriptedHost::Click kToggleText{10 + 0x19 + 99, 308 + 0x4d + 12 + 3};
constexpr ScriptedHost::Click kSubTourMainMenu{10 + 0x19 + 99, 308 + 0x75 + 12 + 3};
// The forward torpedo room left of the bow plane's area, which lies over
// it (disabled in the internal view, it still takes the clicks there).
constexpr ScriptedHost::Click kForwardTorpedo{100, 240};
constexpr ScriptedHost::Click kGalley{0x160, 0xb0};
constexpr ScriptedHost::Click kDeckGun{0x18d, 0x91};

// Returns true when `picture` is the first picture of BUD\<video>.SMK.
bool is_first_video_picture(const Image8& picture, const Install& install, int video) {
  SmackerVideo smk = SmackerVideo::load(install, "BUD\\" + std::to_string(video) + ".SMK");
  smk.decode_next();
  const Image8& image = smk.image();
  return image.width() == picture.width() && image.height() == picture.height() &&
         std::ranges::equal(image.pixels(), picture.pixels());
}

struct FrontEndFixture {
  explicit FrontEndFixture(const Install& install)
      : paths(ShPaths::load(install)),
        exe(ExeImage::load(install)),
        saves(dir.path(), &install, paths.dir(GameDir::kSave)),
        front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1) {
    front_end.set_play_intro(false);
  }
  TempDir dir;
  ShPaths paths;
  ExeImage exe;
  SaveDir saves;
  ScriptedHost host;
  FrontEnd front_end;
};

}  // namespace

TEST_CASE("The interview pages name the BUD videos 1 to 16", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const ShPaths paths = ShPaths::load(install);
  const std::vector<InterviewEntry> first = interview_page(exe, 0);
  REQUIRE(first.size() == 4);
  CHECK(first[0].button == "A");
  CHECK(first[0].caption == "USS Skate, under the command of William \"Bud\" Gruner, sinks the Agano.");
  CHECK(first[0].video == 1);
  CHECK(first[3].button == "D");
  const std::vector<InterviewEntry> last = interview_page(exe, 3);
  CHECK(last[2].button == "O");
  CHECK(last[2].caption == "Biography of LCDR William P. \"Bud\" Gruner");
  CHECK(last[3].video == 16);
  CHECK(interview_video_path(paths, 7) == "BUD\\7.smk");
  for (int page = 0; page < kInterviewPages; ++page) {
    for (const InterviewEntry& e : interview_page(exe, page)) {
      CHECK(install.exists(interview_video_path(paths, e.video)));
    }
  }
}

TEST_CASE("The interview slider is a disabled progress bar", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  ui::GuiResources gui_res = ui::GuiResources::load(install);
  const ui::MenuResources menu_res = ui::MenuResources::load(install);
  InterviewSkin skin(gui_res, menu_res);
  // The hooks of 0xa8ce7..0xa8e2a: no frame, arrows or focus, a track 15
  // pixels high.
  CHECK(skin.slider_left_rect(0x19a, 0x182, 0xb4) == ui::Rect{});
  CHECK(skin.slider_right_rect(0x19a, 0x182, 0xb4) == ui::Rect{});
  CHECK(skin.slider_track_rect(0x19a, 0x182, 0xb4) == ui::Rect{0x19a, 0x182, 0x19a + 0xb3, 0x182 + 14});
  Image8 image(kScreenWidth, kScreenHeight, 0);
  const Viewport gfx = Viewport::full(image);
  CHECK(skin.slider_frame(gfx, 0x19a, 0x182, 0xb4, true) == ui::Rect{});
  CHECK(skin.slider_arrow(gfx, 0x19a, 0x182, 0xb4, true, true, true) == ui::Rect{});
  CHECK(skin.slider_focus(gfx, 0x19a, 0x182, 0xb4, true) == ui::Rect{});
  CHECK(image.at(0x19a - 16, 0x182) == 0);

  // The thumb moves along (w - 5) pixels: at 0 and 1000 per mille.
  const int x = 0x19a;
  const int y = 0x182;
  CHECK(skin.slider_track(gfx, x, y, 0xb4, 0, true) == ui::Rect{x - 2, y - 2, x + 0xb4 + 3, y + 18});
  CHECK(image.at(x + 10, y + 7) == 0x65);  // the menu fill
  CHECK(image.at(x - 2, y + 7) == 0x67);   // the sunken track's left edge
  CHECK(image.at(x, y + 7) == 0x4a);       // the raised thumb's left edge
  CHECK(image.at(x + 4, y + 7) == 0x67);   // and its right edge
  skin.slider_track(gfx, x, y, 0xb4, 1000, true);
  CHECK(image.at(x, y + 7) == 0x65);  // the old thumb erased
  CHECK(image.at(x + 0xaf, y + 7) == 0x4a);
  CHECK(image.at(x + 0xaf + 4, y + 7) == 0x67);

  // In a dialog, a click on the track leaves the frame number alone.
  ui::Gui gui(image, [] { return 0u; }, skin);
  int frame = 100;
  ui::Dialog d(gui, 0, 0, 0x27e, 0x1de, 0, &image);
  auto& slider = d.emplace<ui::Slider>(x, y, 0xb4, 1770, frame);
  slider.disabled = true;
  d.open();
  gui.mouse().update(kSliderTrack.x, kSliderTrack.y, ui::kButtonLeft, 0);
  gui.mouse().update(kSliderTrack.x, kSliderTrack.y, 0, 0);
  while (const std::optional<ui::Event> e = gui.events().get()) {
    d.handle_event(*e);
    d.process();
  }
  CHECK(frame == 100);
  d.close();
}

TEST_CASE("INTERVIEWS turns its pages and plays the chosen video", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  FrontEndFixture f(install);
  ScriptedHost& host = f.host;
  // Page 1 (E..H), page 2, back to page 1; its second video (F = BUD\6),
  // restarted then stopped; on page 3 NEXT is disabled: M plays BUD\13.
  host.script = {kInterviews, kNextPage, kNextPage, kPrevPage,     kVideoB,   kRestart, kStop,
                 kNextPage,   kNextPage, kNextPage, kVideoA,      kStop,     kInterviewsMainMenu, kExitToDos};
  f.front_end.run();
  CHECK(host.script.empty());
  // BUD\6 started, restarted (a new stream), then BUD\13.
  CHECK(host.video.starts == 3);
  CHECK(host.video.stops == 3);
  REQUIRE(host.first_pictures.size() == 3);
  CHECK(is_first_video_picture(host.first_pictures[0], install, 6));
  CHECK(is_first_video_picture(host.first_pictures[1], install, 6));
  CHECK(is_first_video_picture(host.first_pictures[2], install, 13));
}

TEST_CASE("The interview slider seeks in the video (not in the original)", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  FrontEndFixture f(install);
  ScriptedHost& host = f.host;
  // BUD\5, its slider clicked at its middle, then STOP.
  host.script = {kInterviews, kVideoA, kSliderTrack, kStop, kInterviewsMainMenu, kExitToDos};
  f.front_end.run();
  CHECK(host.script.empty());
  // The seek restarts the sound from the new frame.
  CHECK(host.video.starts == 2);
  CHECK(host.video.stops == 2);
}

TEST_CASE("The interviews page is kept between visits", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  FrontEndFixture f(install);
  ScriptedHost& host = f.host;
  host.script = {kInterviews, kNextPage, kInterviewsMainMenu, kInterviews, kVideoA, kStop, kInterviewsMainMenu,
                 kExitToDos};
  f.front_end.run();
  REQUIRE(host.first_pictures.size() == 1);
  CHECK(is_first_video_picture(host.first_pictures[0], install, 5));
}

TEST_CASE("SUB TOUR shows the parts of each view with their narrations", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  FrontEndFixture f(install);
  ScriptedHost& host = f.host;
  // The internal view: the forward torpedo room, the galley; the external
  // view: the galley's area does nothing there, the deck gun does.
  host.script = {kSubTour, kForwardTorpedo, kGalley, kToggleView, kGalley, kDeckGun, kToggleText, kSubTourMainMenu,
                 kSubTour, kForwardTorpedo, kSubTourMainMenu, kExitToDos};
  // The forward torpedo room's picture (SUBTOUR.SHP frame 18 at
  // (0x17f, 0xee)) appears once it is clicked.
  const Shp sprites = Shp::load(install, "ART\\SUBTOUR.SHP");
  const SpriteFrame& picture = sprites.frame(18);
  const DecodedFrame decoded = picture.decode();
  bool picture_shown = false;
  host.on_present = [&](const Image8& screen) {
    bool same = true;
    for (int y = 0; y < decoded.image.height(); y += 7) {
      for (int x = 0; x < decoded.image.width(); x += 7) {
        if (decoded.opaque.at(x, y) != 0 &&
            screen.at(0x17f + picture.x0 + x, 0xee + picture.y0 + y) != decoded.image.at(x, y)) {
          same = false;
        }
      }
    }
    picture_shown = picture_shown || same;
  };
  f.front_end.run();
  CHECK(host.script.empty());
  // The second visit keeps the external view: the forward torpedo area is
  // empty there.
  CHECK(host.speech == std::vector<std::string>{"FWRDTORP.RAW", "GALLEY.RAW", "DECKGUN.RAW"});
  CHECK(picture_shown);
  // Each narration stops the previous one; the view toggle and MAIN MENU
  // stop it, and so does leaving the screen.
  CHECK(host.speech_stops == 3 + 1 + 2 + 2);
}

TEST_CASE("The sub tour labels and parts come from SH.EXE", "[extras]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const std::vector<SubTourLabel> labels = subtour_labels(exe);
  REQUIRE(labels.size() == kSubTourLabels);
  const SubTourLabel& torpedo = labels[0];
  CHECK(torpedo.name == "Forward Torpedo");
  CHECK(torpedo.x == 53);
  CHECK(torpedo.y == 273);
  CHECK(torpedo.part);
  CHECK(torpedo.picture == 18);
  CHECK(torpedo.highlight == 2);
  CHECK(torpedo.line_x0 == 102);
  CHECK(torpedo.line_y1 == 240);
  CHECK(torpedo.narration == "FWRDTORP");
  CHECK(torpedo.highlight_x == 73);
  CHECK(torpedo.highlight_y == 187);
  CHECK(labels[12].name == "Forward Battery");
  CHECK_FALSE(labels[12].part);
  CHECK(labels[12].narration.empty());
  CHECK(labels[19].name == "Bow Plane");
  CHECK(labels[19].narration == "BOWPLANE");
  CHECK(labels[28].name == "Stern Plane");
  // Every narration is in SPEECH.
  for (const SubTourLabel& l : labels) {
    if (!l.narration.empty()) {
      CHECK(install.exists("SPEECH\\" + l.narration + ".RAW"));
    }
  }
}

}  // namespace sh

// The encounter dialogs of the single missions (convoy 0x1887bc, warship
// 0x188c08, patrol 0x189054, enemy sub 0x18946c, special mission 0x18981c):
// the generator's options, kept in the SMISSION.SAV settings.

#include <array>
#include <vector>

#include "assets/exe_image.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "ui/controls.h"

namespace sh {

namespace {

constexpr std::uint32_t kMenuPcx = 0x110940;        // "M8.PCX"
constexpr std::uint32_t kMenuPics = 0x1109b8;       // "MENUPICS.SHP"
constexpr std::uint32_t kWarshipFrames = 0x110aac;  // i32[4]
constexpr std::uint32_t kSpecialFrames = 0x110abc;  // i32[3]

// The settings an encounter dialog cycles through (`encounter_cycle_*`,
// 0xa3644..0xa3823).
enum class Setting : std::uint8_t {
  kMonth,
  kYear,
  kCrew,
  kConvoySize,
  kEscort,
  kEnemy,
  kAirCover,
  kFog,
  kTime,
  kWeather,
  kSea,
  kWarshipType,
  kPatrolSize,
  kPatrolActivity,  // the patrol size, with the special missions' strings
  kSpecialMission,
};

// The strings of a setting (a `char*` table of SH.EXE) and its value count.
struct SettingStrings {
  std::uint32_t table;
  int count;
};

SettingStrings strings_of(Setting s) {
  switch (s) {
    case Setting::kMonth:
      return {0x10e940, 12};
    case Setting::kYear:
      return {0x1109f4, 5};
    case Setting::kCrew:
      return {0x110a08, 3};
    case Setting::kConvoySize:
      return {0x110a14, 3};
    case Setting::kEscort:
      return {0x110a20, 4};
    case Setting::kEnemy:
      return {0x110a30, 3};
    case Setting::kAirCover:
      return {0x110a3c, 2};
    case Setting::kFog:
      return {0x110a44, 2};
    case Setting::kTime:
      return {0x110a80, 4};
    case Setting::kWeather:
      return {0x110a9c, 4};
    case Setting::kSea:
      return {0x110a90, 3};
    case Setting::kWarshipType:
      return {0x110a4c, 4};
    case Setting::kPatrolSize:
      return {0x110a5c, 3};
    case Setting::kPatrolActivity:
      return {0x110a68, 3};
    case Setting::kSpecialMission:
      // The control declares 2 strings, but the callback also reaches the
      // third, MISSION: ENEMY SUB ENCOUNTER.
      return {0x110a74, 3};
  }
  return {0, 0};
}

std::int32_t& value_of(EncounterSettings& e, Setting s) {
  switch (s) {
    case Setting::kMonth:
      return e.month;
    case Setting::kYear:
      return e.year;
    case Setting::kCrew:
      return e.crew;
    case Setting::kConvoySize:
      return e.convoy_size;
    case Setting::kEscort:
      return e.escort;
    case Setting::kEnemy:
      return e.enemy;
    case Setting::kAirCover:
      return e.air_cover;
    case Setting::kFog:
      return e.fog;
    case Setting::kTime:
      return e.time;
    case Setting::kWeather:
      return e.weather;
    case Setting::kSea:
      return e.sea;
    case Setting::kWarshipType:
      return e.warship_type;
    case Setting::kPatrolSize:
    case Setting::kPatrolActivity:
      return e.patrol_size;
    case Setting::kSpecialMission:
      return e.special_mission;
  }
  return e.month;
}

// One cycle button of a dialog.
struct CycleLayout {
  int x;
  int y;
  int w;
  Setting setting;
};

// One encounter dialog.
struct EncounterLayout {
  std::uint32_t title;
  std::array<std::uint32_t, 3> buttons;  // CANCEL, REALISM, BEGIN
  int palette_frame;                     // the MENUPICS.SHP frame giving the colours
  std::vector<CycleLayout> cycles;
  int group_y0;
  int group_y1;
};

EncounterLayout layout_of(int menu) {
  using S = Setting;
  switch (menu) {
    case 0x5b:  // convoy
      return {0xfbf31, {0xfbf42, 0xfbf49, 0xfbf51}, 0,
              {{147, 207, 120, S::kMonth}, {267, 207, 60, S::kYear}, {37, 243, 200, S::kCrew},
               {37, 278, 200, S::kConvoySize}, {37, 313, 200, S::kEscort}, {37, 348, 200, S::kEnemy},
               {258, 243, 200, S::kAirCover}, {258, 278, 200, S::kFog}, {258, 313, 200, S::kTime},
               {258, 348, 100, S::kWeather}, {358, 348, 100, S::kSea}},
              201, 387};
    case 0x5c:  // warship
      return {0xfbf57, {0xfbf69, 0xfbf70, 0xfbf78}, 1,
              {{147, 207, 140, S::kMonth}, {287, 207, 60, S::kYear}, {37, 243, 200, S::kCrew},
               {37, 278, 200, S::kWarshipType}, {37, 313, 200, S::kEscort}, {37, 348, 200, S::kEnemy},
               {258, 243, 200, S::kAirCover}, {258, 278, 200, S::kFog}, {258, 313, 200, S::kTime},
               {258, 348, 100, S::kWeather}, {358, 348, 100, S::kSea}},
              201, 387};
    case 0x5e:  // patrol
      return {0xfbf7e, {0xfbf8f, 0xfbf96, 0xfbf9e}, 3,
              {{37, 207, 140, S::kMonth}, {177, 207, 60, S::kYear}, {37, 243, 200, S::kCrew},
               {37, 278, 200, S::kPatrolSize}, {37, 313, 200, S::kEnemy}, {258, 207, 200, S::kAirCover},
               {258, 243, 200, S::kFog}, {258, 278, 200, S::kTime}, {258, 313, 100, S::kWeather},
               {358, 313, 100, S::kSea}},
              201, 351};
    case 0x5d:  // enemy sub
      return {0xfbfa4, {0xfbfb8, 0xfbfbf, 0xfbfc7}, 2,
              {{37, 243, 140, S::kMonth}, {177, 243, 60, S::kYear}, {37, 278, 200, S::kCrew},
               {37, 313, 200, S::kEnemy}, {258, 243, 200, S::kFog}, {258, 278, 200, S::kTime},
               {258, 313, 100, S::kWeather}, {358, 313, 100, S::kSea}},
              237, 351};
    default:  // 0x5f, special mission
      return {0xfbfcd, {0xfbfdd, 0xfbfe4, 0xfbfec}, 4,
              {{147, 207, 120, S::kMonth}, {267, 207, 60, S::kYear}, {37, 243, 200, S::kSpecialMission},
               {37, 278, 200, S::kCrew}, {37, 313, 200, S::kPatrolActivity}, {37, 348, 200, S::kEnemy},
               {258, 243, 200, S::kAirCover}, {258, 278, 200, S::kFog}, {258, 313, 200, S::kTime},
               {258, 348, 100, S::kWeather}, {358, 348, 100, S::kSea}},
              201, 387};
  }
}

// The value a cycle button shows, after advancing it when `advance`
// (`encounter_cycle_*`): month and year skip the dates outside the war,
// the year shows from 1941.
int cycle_setting(EncounterSettings& e, Setting s, bool advance) {
  std::int32_t& v = value_of(e, s);
  if (s == Setting::kMonth) {
    if (advance) {
      do {
        v = v + 1 > 11 ? 0 : v + 1;
      } while (!e.date_in_war());
    }
    return v;
  }
  if (s == Setting::kYear) {
    if (advance) {
      do {
        v = v + 1 >= WarWindow::kEndYear + 1 ? WarWindow::kStartYear : v + 1;
      } while (!e.date_in_war());
    }
    return v - WarWindow::kStartYear;
  }
  if (advance) {
    v = v + 1 >= strings_of(s).count ? 0 : v + 1;
  }
  return v;
}

}  // namespace

int FrontEnd::encounter_menu(int menu) {
  // convoy_encounter_menu (0xa3580) and its siblings.
  const EncounterLayout layout = layout_of(menu);
  load_screen_pcx(exe_string(kMenuPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);

  const Shp pictures = Shp::load(install_, paths_.path(GameDir::kArt, exe_string(kMenuPics)));
  // The picture of the dialog's current choice.
  const auto picture_frame = [&]() -> int {
    if (menu == 0x5c) {
      return exe_.i32(kWarshipFrames + static_cast<std::uint32_t>(encounter_.warship_type) * 4);
    }
    if (menu == 0x5f) {
      return exe_.i32(kSpecialFrames + static_cast<std::uint32_t>(encounter_.special_mission) * 4);
    }
    return layout.palette_frame;
  };

  ui::Dialog d(gui_, 69, 19, 502, 445, ui::kDialogTitled | ui::kDialogFramed);
  // *_encounter_draw_picture (0xa35cc..0xa3b83): the colours of one frame,
  // the frame around the picture, the picture.
  d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) {
    sprite_apply_palette(pictures.frame(static_cast<std::size_t>(layout.palette_frame)), palette_);
    menu_skin_.draw_frame(gfx, 0x1b, 0x74, 0x1bd, 0x4f);
    draw_sprite_clipped(gfx, pictures.frame(static_cast<std::size_t>(picture_frame())), 0x21, 0x7a);
  });
  d.emplace<ui::TitleBar>(exe_string(layout.title));
  d.set_back(d.emplace<ui::Button>(35, 395, 121, exe_string(layout.buttons[0]), ui::return_cancel));
  d.emplace<ui::Button>(185, 395, 121, exe_string(layout.buttons[1]), [](int) { return 0x50; });
  d.emplace<ui::Button>(335, 395, 121, exe_string(layout.buttons[2]), ui::return_accept);
  std::vector<ui::CycleButton*> greyed_by_enemy_sub;
  for (const CycleLayout& c : layout.cycles) {
    const SettingStrings strings = strings_of(c.setting);
    auto& button = d.emplace<ui::CycleButton>(
        c.x, c.y, c.w, exe_strings(strings.table, strings.count),
        [this, setting = c.setting](bool advance) { return cycle_setting(encounter_, setting, advance); });
    if (menu == 0x5f && (c.setting == Setting::kPatrolActivity || c.setting == Setting::kAirCover)) {
      greyed_by_enemy_sub.push_back(&button);
    }
  }
  d.emplace<ui::GroupBox>(26, layout.group_y0, 471, layout.group_y1);
  add_exit_area(d);

  // special_mission_menu (0xa3b1e): the patrol activity and air cover do
  // not apply to the enemy sub encounter.
  const auto grey = [&] {
    for (ui::CycleButton* b : greyed_by_enemy_sub) {
      b->disabled = encounter_.special_mission > 1;
    }
  };
  grey();
  // encounter_dialog_draw (0xa362c), warship_encounter_update_picture
  // (0xa393f), special_mission_update_picture (0xa3be6): the fade-in, and
  // a redraw when the choice the picture shows changes.
  std::int32_t shown = 0;  // 0x187230 / 0x187234
  d.set_idle([&](const Viewport&, int first) {
    const std::int32_t choice = menu == 0x5c ? encounter_.warship_type : encounter_.special_mission;
    if (first != 0) {
      shown = choice;
      palette_fade_in(palette_, 0, 10);
    }
    if ((menu == 0x5c || menu == 0x5f) && choice != shown) {
      shown = choice;
      grey();
      d.invalidate();
    }
  });
  return run_menu_dialog(d);
}

}  // namespace sh

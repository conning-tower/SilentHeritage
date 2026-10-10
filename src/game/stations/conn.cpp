#include "game/stations/conn.h"

#include "game/command_bar.h"
#include "gfx/sprite.h"

namespace sh {

namespace {

using Target = ConnHotspot::Target;

// The periscope window: the screen part (230, 12)-(409, 456) the shaft
// slides in (`conn_periscope_init`, viewport 0x118ee4).
constexpr int kScopeX = 0xe6;
constexpr int kScopeY = 0xc;
constexpr int kScopeW = 0xb4;
constexpr int kScopeH = 0x1bd;
// The cabin door sprites (CONN.SHP frames 4..13) at (221, 142).
constexpr int kDoorX = 0xdd;
constexpr int kDoorY = 0x8e;
constexpr std::size_t kDoorFrame = 4;
constexpr std::int32_t kDoorOpen = 9;

constexpr std::int32_t kSfxScopeUp = 3;
constexpr std::int32_t kSfxScopeDown = 4;
constexpr std::int32_t kSfxDoor = 5;
constexpr std::int32_t kSfxScopeThud = 0xb;

// The 1MC button of the conn's bar (0x103778).
constexpr std::uint8_t k1mcFrame = 0x22;

}  // namespace

const std::array<ConnHotspot, 9>& conn_hotspots() {
  static const std::array<ConnHotspot, 9> hotspots{{
      {Target::kPeriscope, 0x10f, 0xf, 0x170, 0xf7},
      {Target::kPeriscopeLever, 0x10f, 0xf8, 0x170, 0x1c8},
      {Target::kChart, 0x171, 0xf8, 0x25a, 0x1c8},
      {Target::kGauges, 0x86, 0x49, 0xdb, 0xd9},
      {Target::kBridge, 0x181, 0xf, 0x1f1, 0xf7},
      {Target::kCabinDoor, 0xe4, 0x68, 0x122, 0x124},
      {Target::kTdc, 0x230, 0x2e, 0x27f, 0xfb},
      {Target::kStatus, 0x2d, 0x6d, 0x8d, 0x1ae},
      {Target::kRadar, 0x1ef, 0x66, 0x220, 0xb2},
  }};
  return hotspots;
}

void ConnStation::open() {
  // conn_screen (0x2d13c)
  ++env_.ui.pause;
  load_screen_pcx(env_, "CONN.PCX");
  if (env_.ui.rig_for_red) {
    load_screen_palette(env_, "CONN.PAL");
  }
  env_.bar.open({{BarButton{k1mcFrame, k1mcFrame + 1, [this] { env_.bar.menu_1mc(); }}}, {}, true, true, true});
  dialog_ = std::make_unique<ui::Dialog>(env_.gui, 0, 0, 0x27f, 0x1df, 0, &env_.screen());
  for (const ConnHotspot& h : conn_hotspots()) {
    const Target target = h.target;
    auto& area = dialog_->emplace<ui::Area>(
        h.x0, h.y0, h.x1, h.y1,
        [this, target](ui::Dialog&, ui::Area& a, ui::Msg msg, int, const ui::Event&) {
          // hotkey_proc_simple (0x49432)
          if (msg == ui::Msg::kPress && !a.pressed) {
            click(target);
          }
          return 0;
        },
        nullptr);
    area.on_hover = [this, target](bool on, int, const ui::Event&) { hover(target, on); };
  }
  dialog_->open();
  sprites_ = Shp::load(env_.install, env_.paths.path(GameDir::kArt, "CONN.SHP"));
  could_raise_ = env_.bar.can_raise_periscope();
  was_surfaced_ = env_.bar.surfaced();

  // conn_cabin_door_init (0x2d5d4): the door shows open when the pointer
  // is already on it.
  env_.timer(GameTimer::kConnDoor) = 0;
  door_state_ = 0;
  const ui::Mouse& mouse = env_.gui.mouse();
  door_frame_ = mouse.x() >= 0xe4 && mouse.x() <= 0x122 && mouse.y() >= 0x68 && mouse.y() <= 0x124 ? kDoorOpen : 0;
  draw_door(false);

  // conn_periscope_init (0x2d30c)
  periscope_background_ = Image8(kScopeW, kScopeH);
  for (int y = 0; y < kScopeH; ++y) {
    for (int x = 0; x < kScopeW; ++x) {
      periscope_background_.set(x, y, env_.screen().at(kScopeX + x, kScopeY + y));
    }
  }
  // The animation is kept: the periscope station starts lowering it as it
  // hands over to the conn.
  env_.timer(GameTimer::kConnPeriscope) = 0;
  draw_periscope();
  raised_ = env_.mission.player().periscope_raised();

  env_.dac.fade_out(0x100);
  --env_.ui.pause;
}

void ConnStation::close() {
  // conn_station_close (0x2d1e4)
  start_periscope(0);
  dialog_->close();
  dialog_.reset();
}

void ConnStation::event(const ui::Event& event) { dialog_->handle_event(event); }

void ConnStation::process() { dialog_->process(); }

void ConnStation::update() {
  // conn_station_update (0x2d220)
  const bool surfaced = env_.bar.surfaced();
  const bool can_raise = env_.bar.can_raise_periscope();
  if (surfaced != was_surfaced_ || can_raise != could_raise_) {
    could_raise_ = can_raise;
    was_surfaced_ = surfaced;
    env_.ui.redraw = true;
  }
  animate_door();
  animate_periscope();
  if (first_frame()) {
    env_.dac.fade_in(env_.palette, 10);
  }
  if (env_.ui.redraw) {
    dialog_->invalidate();
  }
}

void ConnStation::start_periscope(std::int32_t animation) {
  // conn_periscope_start_anim (0x2d298)
  if (env_.sound.sfx_playing(kSfxScopeUp)) {
    env_.sound.stop_sfx(kSfxScopeUp);
  }
  if (env_.sound.sfx_playing(kSfxScopeDown)) {
    env_.sound.stop_sfx(kSfxScopeDown);
  }
  if (animation_ == 0 && animation != 0) {
    shaft_ = animation == 1 ? kPeriscopeDown : kPeriscopeUp;
    env_.timer(GameTimer::kConnPeriscope) = 100;
  }
  animation_ = animation;
}

void ConnStation::animate_periscope() {
  // conn_periscope_animate (0x2d3c2)
  const bool raised = env_.mission.player().periscope_raised();
  if (raised != raised_) {
    start_periscope(raised ? 1 : 2);
    env_.ui.redraw = true;
    raised_ = raised;
  }
  std::int32_t& timer = env_.timer(GameTimer::kConnPeriscope);
  if (animation_ == 0 || timer <= 4) {
    return;
  }
  timer = 0;
  draw_periscope();
  if (animation_ == 1) {
    if (!env_.sound.sfx_playing(kSfxScopeUp)) {
      env_.sound.play_sfx(kSfxScopeUp, 0x1e);
    }
    if (shaft_ < kPeriscopeUp) {
      shaft_ += 11;
    } else {
      animation_ = 0;
      env_.sound.stop_sfx(kSfxScopeUp);
    }
    shaft_ = std::min(shaft_, kPeriscopeUp);
  } else if (animation_ == 2) {
    if (!env_.sound.sfx_playing(kSfxScopeDown)) {
      env_.sound.play_sfx(kSfxScopeDown, 0x32);
    }
    if (shaft_ < 11) {
      animation_ = 0;
      env_.sound.stop_sfx(kSfxScopeDown);
      env_.sound.play_sfx(kSfxScopeThud, 100);
    } else {
      shaft_ -= 20;
    }
    shaft_ = std::max(shaft_, kPeriscopeDown);
  }
}

void ConnStation::draw_periscope() {
  // conn_draw_periscope (0x2d510): the background, then the shaft (frame
  // 1), its head (frame 3 at the top, else 2) and the well (frame 0).
  if (animation_ == 0) {
    shaft_ = env_.mission.player().periscope_raised() ? kPeriscopeUp : kPeriscopeDown;
  }
  env_.screen().blit(periscope_background_, kScopeX, kScopeY);
  const Viewport vp{&env_.screen(), kScopeX, kScopeY, kScopeX + kScopeW - 1, kScopeY + kScopeH - 1};
  draw_sprite_clipped(vp, sprites_.frame(1), 0x13, -0xc - shaft_);
  draw_sprite_clipped(vp, sprites_.frame(shaft_ >= kPeriscopeUp ? 3 : 2), 0x13, -0xc - shaft_);
  draw_sprite_clipped(vp, sprites_.frame(0), 0x13, -0xc);
}

void ConnStation::draw_door(bool into_buffer) {
  // conn_draw_cabin_door (0x2d68a): also into the periscope background,
  // which it overlaps.
  const SpriteFrame& frame = sprites_.frame(kDoorFrame + static_cast<std::size_t>(door_frame_));
  const Viewport vp{&env_.screen(), kDoorX, kDoorY, kDoorX - 1 + sprites_.frame(kDoorFrame).width(),
                    kDoorY - 1 + sprites_.frame(kDoorFrame).height()};
  draw_sprite_clipped(vp, frame, 0, 0);
  if (into_buffer) {
    draw_sprite_clipped(Viewport::full(periscope_background_), frame, kDoorX - kScopeX, kDoorY - kScopeY);
  }
}

void ConnStation::animate_door() {
  // conn_animate_cabin_door (0x2d6e3): opens frame by frame, then the
  // cabin; it gives up opening after 200 ticks.
  if (door_state_ == 0) {
    env_.timer(GameTimer::kConnDoorOpen) = 0;
    return;
  }
  std::int32_t& timer = env_.timer(GameTimer::kConnDoor);
  if (timer <= 4) {
    return;
  }
  timer = 0;
  draw_door(true);
  draw_periscope();
  if (door_state_ == 1) {
    if (door_frame_ < kDoorOpen && env_.timer(GameTimer::kConnDoorOpen) < 200) {
      ++door_frame_;
      return;
    }
    door_state_ = 0;
    env_.bar.select_station(StationId::kCabin);
  } else if (door_state_ == 2) {
    if (door_frame_ > 0) {
      --door_frame_;
      return;
    }
    door_state_ = 0;
  }
}

void ConnStation::click(ConnHotspot::Target target) {
  PlayerSub& player = env_.mission.player();
  switch (target) {
    case Target::kPeriscope:
      // conn_click_periscope (0x2d844)
      if (!player.periscope_raised()) {
        if (env_.bar.can_raise_periscope()) {
          env_.bar.toggle_periscope();
        }
      } else {
        env_.bar.select_station(StationId::kScope);
      }
      break;
    case Target::kPeriscopeLever:
      env_.bar.toggle_periscope();
      break;
    case Target::kChart:
      env_.bar.select_station(StationId::kChart);
      break;
    case Target::kGauges:
      env_.bar.select_station(StationId::kGauges);
      break;
    case Target::kBridge:
      if (env_.bar.surfaced()) {
        env_.bar.select_station(StationId::kBridge);
      }
      break;
    case Target::kCabinDoor:
      // conn_click_cabin_door (0x2d877)
      door_state_ = 1;
      env_.sound.play_sfx(kSfxDoor, 0x32);
      break;
    case Target::kTdc:
      env_.bar.select_station(StationId::kTdc);
      break;
    case Target::kStatus:
      env_.bar.select_station(StationId::kStatus);
      break;
    case Target::kRadar:
      env_.bar.select_station(StationId::kRadar);
      break;
  }
}

void ConnStation::hover(ConnHotspot::Target target, bool on) {
  // conn_hover_* (0x2d8cf..0x2d981): the station's cursor while over it.
  std::optional<ui::CursorId> cursor;
  switch (target) {
    case Target::kPeriscope:
      if (env_.bar.can_raise_periscope()) {
        cursor = env_.mission.player().periscope_raised() ? ui::CursorId::kScope : ui::CursorId::kScopeUp;
      }
      break;
    case Target::kPeriscopeLever:
      if (env_.bar.can_raise_periscope()) {
        cursor = env_.mission.player().periscope_raised() ? ui::CursorId::kScopeDown : ui::CursorId::kScopeUp;
      }
      break;
    case Target::kChart:
      cursor = ui::CursorId::kCharts;
      break;
    case Target::kGauges:
      cursor = ui::CursorId::kGauges;
      break;
    case Target::kBridge:
      if (env_.bar.surfaced()) {
        cursor = ui::CursorId::kBridge;
      }
      break;
    case Target::kCabinDoor:
      cursor = ui::CursorId::kCaptainsCabin;
      break;
    case Target::kTdc:
      cursor = ui::CursorId::kTdc;
      break;
    case Target::kStatus:
      cursor = ui::CursorId::kStatusStation;
      break;
    case Target::kRadar:
      cursor = ui::CursorId::kRadar;
      break;
  }
  if (on && cursor) {
    env_.set_cursor(static_cast<std::size_t>(*cursor));
  } else {
    env_.default_cursor();
  }
}

}  // namespace sh

// What the vehicles see of the mission while they update: the clock, the
// random generator, the sea floor, the scenario conditions and options, and
// the outputs they raise for the crew and the mission flow.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core/random.h"
#include "sim/clock.h"
#include "sim/motion.h"

namespace sh {

struct ShipClass;

// Bits of the realism/options word `0x11a85c` used by the simulation.
namespace option {
inline constexpr std::uint32_t kSailingModelMask = 0x30;  // Bits 4-5: 0 novice .. 2 expert.
inline constexpr std::uint32_t kRunAground = 0x100;       // Grounding damages the boat.
inline constexpr std::uint32_t kUnlimitedFuel = 0x200;
inline constexpr std::uint32_t kUnlimitedBattery = 0x400;
}  // namespace option

// The world area the boat may sail in, in world units from the origin (the
// map size in cells times the cell size).
struct MapExtent {
  std::int32_t max_x = 0;
  std::int32_t max_z = 0;
};

// The rule and simulation changes of the extended variant (not in the
// original), fixed for a mission: set when it is created and saved with it
// (game/mission_rules.h). The defaults are the original's rules.
//
// A rule added here adds its saved field to `kMissionRuleFields`.
struct MissionRules {
  // The corrections of the original's plain errors, changing the rules and
  // the simulation: the Mark 14 fix dates, the fog lifting on fog days, the
  // enemy subs' depth over the bottom, the AA guns' elevation, the strafing
  // aircraft's bearing, the ships' route pruning, harbour crews and
  // alert-route patrols, the career's patrol start edges, equipment and
  // boats, the encounter dates and the ID book's tabs.
  bool fix_obvious_errors = false;
  // The sound man calls a ship's screws (#41, #42) and resets the time
  // compression only when the ship has not been heard for 3 minutes since
  // it was last called, not each time its screws fade out and back in:
  // every frame a called ship is heard restarts its report timer. Detection
  // and the random draws are unchanged.
  bool quiet_sonar_reacquire = false;
  // The REALISM choice UNDEAFENED ESCORTS: the escorts' sonar is not
  // deafened by depth-charge blasts and drops its contacts as the original
  // does, for a higher realism rating. Without it, with
  // `fix_obvious_errors`, blasts deafen the escorts and their contacts are
  // cancelled by the noises near them.
  bool keen_escort_sonar = false;
  // The REALISM choice DETECTABLE RADAR: the escorts carry radar detectors
  // (behaviour: sim/radar). Off, the realism rating is lower.
  bool detectable_radar = false;
  // The realism rating counts the extended REALISM items above
  // (game/options_realism.h); false for the original's rating.
  bool extended_realism = false;
  // The Mark 18-1 and 18-2 carried together: the 18-1 replaced gradually
  // from the 18-2's date, and generated loads mixing the real 18-2; a boat
  // then carries three types at most (game/torpedo_load.h).
  bool mixed_mark18 = false;
  // The Mark 27 "Cutie" passive homing torpedo from mid-1944
  // (sim/torpedo.h, game/torpedo_load.h).
  bool mark27 = false;
  // The ST periscope radar (rule and simulation change): a boat fitted with
  // it (from November 1944, with its SJ) can use the SJ with the periscope
  // raised as well as at radar depth (sim/radar.h).
  bool st_radar = false;
  // The APR-1 radar detector (rule and simulation change): the Japanese
  // ships and aircraft transmit by type and date, and a boat fitted with the
  // APR-1 hears them (sim/radar_emissions.h).
  bool apr1 = false;

  // Returns the rules of a new extended game (the extended variant's
  // defaults, where EXTENDED.CFG or a career's rules do not set them): the
  // corrections, the quiet sonar reacquisition, the extended realism with
  // DETECTABLE RADAR (as at EXPERT), the mixed Mark 18s, the Mark 27, the ST
  // and the APR-1.
  static constexpr MissionRules extended() {
    return {.fix_obvious_errors = true,
            .quiet_sonar_reacquire = true,
            .detectable_radar = true,
            .extended_realism = true,
            .mixed_mark18 = true,
            .mark27 = true,
            .st_radar = true,
            .apr1 = true};
  }
  // Returns whether depth-charge blasts deafen the escorts' sonar, and the
  // noises near an escort's estimate cancel it by the true geometry.
  constexpr bool escorts_deafened() const { return fix_obvious_errors && !keen_escort_sonar; }

  friend bool operator==(const MissionRules&, const MissionRules&) = default;
};

// A saved field of `MissionRules`: its stable key and its member.
struct MissionRuleField {
  std::string_view key;
  std::variant<bool MissionRules::*, std::int32_t MissionRules::*> member;
};

// The saved fields of `MissionRules`; keys must stay as they are once saved.
inline const std::array<MissionRuleField, 9> kMissionRuleFields{{
    {"fix_obvious_errors", &MissionRules::fix_obvious_errors},
    {"quiet_sonar_reacquire", &MissionRules::quiet_sonar_reacquire},
    {"keen_escort_sonar", &MissionRules::keen_escort_sonar},
    {"detectable_radar", &MissionRules::detectable_radar},
    {"extended_realism", &MissionRules::extended_realism},
    {"mixed_mark18", &MissionRules::mixed_mark18},
    {"mark27", &MissionRules::mark27},
    {"st_radar", &MissionRules::st_radar},
    {"apr1", &MissionRules::apr1},
}};

// Scenario conditions and player options.
struct MissionConditions {
  std::int32_t sea_state = 0;            // 0x125f64, 0..2.
  std::uint32_t options = 0;             // 0x11a85c
  bool speech = true;                    // 0x11a8dc: crew speech (gates some crew reports).
  std::optional<MapExtent> map;          // Unset: no map-edge turn back.
  std::int32_t weather = 0;              // 0x125f60, 0 clear .. 3 overcast.
  std::int32_t fog = 0;                  // 0x125f68, the scenario's fog setting.
  std::int32_t thermal_layer_ft = 10000; // 0x11b7c8, 10000: no layer.
  std::int32_t scenario_roll = 0;        // 0x126094, rand(100) rolled per scenario.
  // 0x125ff8, the scenario's `end_on_lost_contact`: outside a career, a
  // minute without contact ends the mission.
  bool end_on_lost_contact = false;
  // `0x126d74` as the frame starts: a screen shake is running
  // (game/screen_shake.h). The mission screen keeps it current; the boat
  // grinds on the bottom while it runs.
  bool screen_shaking = false;
  // The extended variant's rules of the mission.
  MissionRules rules;

  // Returns the sailing model of realism bits 4-5 (0..2).
  std::int32_t sailing_model() const { return static_cast<std::int32_t>((options & option::kSailingModelMask) >> 4); }
};

// A crew report (`crew_message(id, arg)`; ids index the crew message table).
struct CrewReport {
  CrewReport(std::int32_t id_ = 0, std::int32_t arg_ = -1, std::string text_ = {}, std::int32_t arg2_ = -1)
      : id(id_), arg(arg_), text(std::move(text_)), arg2(arg2_) {}

  std::int32_t id = 0;
  std::int32_t arg = -1;
  std::string text;  // The `%s` argument (a damaged system's name), if any.
  std::int32_t arg2 = -1;  // A second number (minutes left of a reload).
  bool operator==(const CrewReport&) const = default;
};

// A radio message for the boat (`radio_queue_push`, 0x5e715), formatted by
// the radio log from its kind's template.
struct RadioReport {
  enum class Kind : std::int32_t {
    kAirStrike = 0,    // args: hour, minute, day, month (ETA), base index
    kDownedPilot = 1,  // no args
    kPhotoRecon = 2,   // args: location type (-1 harbor), base index, hour, minute, day, month
  };
  Kind kind = Kind::kAirStrike;
  std::array<std::int32_t, 6> args{};
  // The world position the message gives as latitude and longitude.
  std::int32_t x = 0;
  std::int32_t z = 0;
  bool operator==(const RadioReport&) const = default;
};

// Outputs of a simulation frame for the mission and UI layers.
struct MissionEvents {
  std::vector<CrewReport> crew_messages;
  // The game-over trigger `0x11a894` (2: the crew ran out of air).
  std::optional<std::int32_t> game_over;
  // The boat touched bottom this frame without the Run Aground option (the
  // screen shake; with the option the grounding damage raises
  // `screen_shake`).
  bool ran_aground = false;
  // Sound effects started (`sound_play_sfx(id, volume, pan)`; volume -1:
  // full, 0..127 by distance).
  struct Sfx {
    std::int32_t id = 0;
    std::int32_t volume = -1;
    constexpr bool operator==(const Sfx&) const = default;
  };
  std::vector<Sfx> sfx;
  // Speech files started (`play_speech_raw`, e.g. "HELM0372.RAW"), raised
  // only with the speech option on.
  std::vector<std::string> speech;
  // A hit or close blast shook the boat (`0x126d74`, the screen shake).
  bool screen_shake = false;
  // Ships the player sank this frame, for the patrol log and the tonnage
  // credit (`player_log_event(class, -1)`).
  std::vector<const ShipClass*> ships_sunk;
  // Radio messages queued this frame (air strikes, downed pilots, photo
  // recons).
  std::vector<RadioReport> radio_reports;
  // Downed pilots taken aboard (`player_pilot_rescued`), for the patrol
  // log (`player_log_event(0, 0)`) and the mission tally.
  std::int32_t pilots_rescued = 0;
  // Photo-recon targets photographed (`player_take_recon_photo`), by their
  // location type (-1 the base's harbor), for the patrol log
  // (`player_log_event(report, 1)`).
  std::vector<std::int32_t> photo_recons;
};

class Simulation;

struct SimContext {
  GameClock& clock;
  WatcomRandom& random;
  const SeaFloor& floor;
  const MissionConditions& env;
  MissionEvents& events;
  // The simulation running the vehicles (spawning, the vehicle list, the
  // player's weapons); null when a vehicle is stepped on its own.
  Simulation* sim = nullptr;

  // Posts a crew report.
  void crew_message(std::int32_t id, std::int32_t arg = -1) { events.crew_messages.push_back({id, arg, {}, -1}); }
  // Posts a crew report naming `text` (`crew_message(id, -1, -1, text)`).
  void crew_message(std::int32_t id, std::string text) { events.crew_messages.push_back({id, -1, std::move(text), -1}); }
  // Starts a sound effect.
  void sfx(std::int32_t id, std::int32_t volume = -1) { events.sfx.push_back({id, volume}); }
};

}  // namespace sh

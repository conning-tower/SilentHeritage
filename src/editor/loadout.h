// The EQUIPMENT and TORPEDOES settings without their dialogs: the working
// copies the dialogs edit with their cycle callbacks, the fit dates of
// "Limit by date: Equipment", the torpedo choices of the player's class and
// the reload capacities.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor/data.h"
#include "editor/options.h"
#include "editor/scenario.h"

namespace sh::editor {

namespace se_addr {
// The fit dates {month 0-based, day, year since 1930} of SD, SJa, SJ-1,
// PPI, equipment bit 4 and the bathythermograph (12 bytes each).
inline constexpr std::uint32_t kFitDates = 0x9ea30;
// "EMPTY", the first TORPEDOES choice.
inline constexpr std::uint32_t kEmpty = 0x9315f;
}  // namespace se_addr

// The fits with a service date, in the order of `se_addr::kFitDates`.
enum class Fit : std::uint8_t { kSd, kSja, kSj1, kPpi, kRadarDetector, kBathythermograph };

// Whether `fit` may be fitted on (`month` 0-based, `year` since 1930) when
// "Limit by date: Equipment" is on: from its month on, the day ignored.
bool fit_available(const SeExe& se, Fit fit, std::int32_t month, std::int32_t year);

// The working copy of EQUIPMENT (`cmd_equipment`, SE 0x2904f): the fields of
// the equipment word (0xa6778..0xa6788). The cycle callbacks skip the fits
// not in service at the scenario date when `limit` is set.
struct EquipmentForm {
  std::int32_t surface_radar = 0;     // bits 1-2: None, SJa, SJ-1
  std::int32_t air_radar = 0;         // bit 0: None, SD
  std::int32_t scope = 0;             // bit 3: A-Scope, PPI
  std::int32_t radar_detector = 0;    // bit 4: no control (the manual's APR-1)
  std::int32_t bathythermograph = 0;  // bit 5

  // The date the fits are checked against (the scenario's) and the option.
  std::int32_t month = 0;
  std::int32_t year = 0;
  bool limit = false;

  static EquipmentForm unpack(std::uint32_t equipment, const ScnHeader& header, const EditorOptions& options);
  // Returns `equipment` with bits 0-5 replaced.
  std::uint32_t pack(std::uint32_t equipment) const;

  // The callbacks (`advance` false only reads), each returning the index its
  // cycle button shows. The scope shows 0 ("NA") without a surface radar,
  // else its value + 1.
  std::int32_t cycle_surface_radar(const SeExe& se, bool advance);
  std::int32_t cycle_air_radar(const SeExe& se, bool advance);
  std::int32_t cycle_scope(const SeExe& se, bool advance);
  std::int32_t cycle_radar_detector(const SeExe& se, bool advance);
  std::int32_t cycle_bathythermograph(const SeExe& se, bool advance);
  // Whether the scope cycle is disabled (no surface radar).
  bool scope_disabled() const { return surface_radar == 0; }
};

// `equipment_validate_by_date` (SE 0x29133): with "Limit by date: Equipment"
// on, every fit not in service at the scenario date moves on to the next
// one that is (or none), then the visibility is updated.
void validate_equipment_by_date(Scenario& scenario, const EditorOptions& options);

// The working copy of TORPEDOES (`cmd_torpedoes`, SE 0x29677) for the
// player's class: the choice per tube and the reload counts per choice.
// Choices index the TORPEDOES list: 0 "EMPTY", then i + 1 for
// `TorpedoChoices::torpedoes[i]`.
class TorpedoForm {
 public:
  // The working copy of `scenario` (its tube torpedoes mapped to choices, 0
  // for an id not listed, and its reload counts).
  TorpedoForm(const Scenario& scenario, const EditorOptions& options);

  // Writes the tubes and reload counts back (OK).
  void commit(Scenario& scenario) const;

  // The player's class (`slist_find(0)` + `stricmp` over SUBS.DAT), or
  // nothing for an unknown class (the original reads past SUBS.DAT; here it
  // has no tubes, no reloads and no modern torpedoes).
  const SubClass* sub_class() const { return sub_; }
  std::int32_t bow_tubes() const;
  std::int32_t stern_tubes() const;
  // The "Reloads:" capacities: SUBS.DAT +0x7c forward, +0x80 aft.
  std::int32_t capacity(bool aft) const;
  // The number of TORPEDOES choices, "EMPTY" included (0xa50b0).
  std::size_t choice_count() const { return choices_.count; }
  // The WEAPONS.DAT torpedo of choice `choice` >= 1, or nothing past the
  // listed ones.
  const Torpedo* torpedo(std::size_t choice) const;

  // `torpedo_type_unavailable`: a choice the class may not carry (no modern
  // torpedoes and not "MARK10") or, with "Limit by date: Equipment", whose
  // WEAPONS.DAT month (1-based, compared with the 0-based scenario month,
  // so a month late; converted when the scenario is `corrected`, not in the
  // original) and year are after the scenario date.
  bool unavailable(std::size_t choice) const;

  // Tube `tube`: 0 .. bow - 1 the bow tubes, then the stern tubes.
  std::int32_t tube(std::size_t tube) const { return tubes_[tube]; }
  // `torpedo_tube_cycle`: the next choice, skipping the unavailable ones
  // (EMPTY always stops it).
  std::int32_t cycle_tube(std::size_t tube, bool advance);

  // The reload count of `choice` (>= 1) in the aft or forward racks.
  std::int32_t reloads(bool aft, std::size_t choice) const { return (aft ? aft_ : forward_)[choice]; }
  // `reload_<aft|fwd><n>_<tens|units>_cycle`: a digit of a reload count
  // (tens 0..2, units 0..9) keeps cycling while the racks' total of the
  // available choices exceeds the capacity. Returns the digit shown.
  std::int32_t cycle_reload(bool aft, std::size_t choice, bool tens, bool advance);
  // The extended variant's list (not in the original): whether choice
  // `choice` would be a fourth type of the load, the tube `except_tube`
  // and the reload row `except_row` (-1: none) left out. A load holds at
  // most three types (game/torpedo_load.h); `cycle_tube` and
  // `cycle_reload` skip such a choice or count.
  bool adds_fourth_type(std::size_t choice, std::int32_t except_tube, std::int32_t except_row) const;
  // Returns whether a cycle skipped a fourth type since the last call, and
  // forgets it (the dialog then explains the limit).
  bool take_refused_fourth() { return std::exchange(refused_fourth_, false); }
  // `reload_aft_over_capacity` / `reload_fwd_over_capacity`.
  bool over_capacity(bool aft) const;

 private:
  const EditorData& data_;
  const TorpedoChoices& choices_;
  const SubClass* sub_ = nullptr;
  std::int32_t month_ = 0;
  std::int32_t year_ = 0;
  bool limit_ = false;
  // The scenario's `corrected`: the torpedoes' months read right.
  bool corrected_ = false;
  std::array<std::int32_t, 10> tubes_{};  // 0xa678c
  // 0xa67b4 / 0xa67c0: by choice, entry 0 unused.
  std::vector<std::int32_t> aft_;
  std::vector<std::int32_t> forward_;
  bool refused_fourth_ = false;
};

}  // namespace sh::editor

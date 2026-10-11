#include "editor/loadout.h"

#include "editor/objects.h"

namespace sh::editor {

namespace {

// The MARK10, the only torpedo of a class without modern torpedoes.
constexpr std::uint32_t kMark10 = 0x9127e;

std::int32_t next(std::int32_t value, std::int32_t count) { return value + 1 >= count ? 0 : value + 1; }

}  // namespace

bool fit_available(const SeExe& se, Fit fit, std::int32_t month, std::int32_t year) {
  const std::uint32_t at = se_addr::kFitDates + static_cast<std::uint32_t>(fit) * 12;
  const std::int32_t fit_month = se.i32(at);
  const std::int32_t fit_year = se.i32(at + 8);
  return !(year < fit_year || (year == fit_year && month < fit_month));
}

// ------------------------------------------------------------ EQUIPMENT

EquipmentForm EquipmentForm::unpack(std::uint32_t equipment, const ScnHeader& header,
                                    const EditorOptions& options) {
  EquipmentForm f;
  f.surface_radar = static_cast<std::int32_t>((equipment & 7) >> 1);
  f.air_radar = static_cast<std::int32_t>(equipment & 1);
  f.scope = static_cast<std::int32_t>((equipment & 0xf) >> 3);
  f.radar_detector = static_cast<std::int32_t>((equipment & 0x1f) >> 4);
  f.bathythermograph = static_cast<std::int32_t>((equipment & 0x3f) >> 5);
  f.month = header.month;
  f.year = header.year;
  f.limit = options.limit_equipment != 0;
  return f;
}

std::uint32_t EquipmentForm::pack(std::uint32_t e) const {
  const auto bits = [](std::int32_t v, std::uint32_t mask) { return static_cast<std::uint32_t>(v) & mask; };
  e = (e & ~6u) | bits(surface_radar, 3) << 1;
  e = (e & ~1u) | bits(air_radar, 1);
  e = (e & ~8u) | bits(scope, 1) << 3;
  e = (e & ~0x10u) | bits(radar_detector, 1) << 4;
  e = (e & ~0x20u) | bits(bathythermograph, 1) << 5;
  return e;
}

std::int32_t EquipmentForm::cycle_surface_radar(const SeExe& se, bool advance) {
  if (advance) {
    bool refused = false;
    do {
      surface_radar = next(surface_radar, 3);
      refused = limit && surface_radar != 0 &&
                !fit_available(se, surface_radar == 1 ? Fit::kSja : Fit::kSj1, month, year);
    } while (refused);
  }
  return surface_radar;
}

std::int32_t EquipmentForm::cycle_air_radar(const SeExe& se, bool advance) {
  if (advance) {
    do {
      air_radar = next(air_radar, 2);
    } while (limit && air_radar != 0 && !fit_available(se, Fit::kSd, month, year));
  }
  return air_radar;
}

std::int32_t EquipmentForm::cycle_scope(const SeExe& se, bool advance) {
  if (advance) {
    do {
      scope = next(scope, 2);
    } while (limit && scope != 0 && !fit_available(se, Fit::kPpi, month, year));
  }
  return surface_radar == 0 ? 0 : scope + 1;
}

std::int32_t EquipmentForm::cycle_radar_detector(const SeExe& se, bool advance) {
  if (advance) {
    do {
      radar_detector = next(radar_detector, 2);
    } while (limit && radar_detector != 0 && !fit_available(se, Fit::kRadarDetector, month, year));
  }
  return radar_detector;
}

std::int32_t EquipmentForm::cycle_bathythermograph(const SeExe& se, bool advance) {
  if (advance) {
    do {
      bathythermograph = next(bathythermograph, 2);
    } while (limit && bathythermograph != 0 && !fit_available(se, Fit::kBathythermograph, month, year));
  }
  return bathythermograph;
}

void validate_equipment_by_date(Scenario& scenario, const EditorOptions& options) {
  if (options.limit_equipment == 0) {
    return;
  }
  // Each field steps back by one and its cycle runs once, so a fit in
  // service stays and another one moves on to the next in service.
  const SeExe& se = scenario.data().se;
  EquipmentForm f = EquipmentForm::unpack(scenario.header.equipment, scenario.header, options);
  --f.surface_radar;
  f.cycle_surface_radar(se, true);
  --f.air_radar;
  f.cycle_air_radar(se, true);
  --f.scope;
  f.cycle_scope(se, true);
  --f.radar_detector;
  f.cycle_radar_detector(se, true);
  --f.bathythermograph;
  f.cycle_bathythermograph(se, true);
  scenario.header.equipment = f.pack(scenario.header.equipment);
  scenario.update_all_visibility();
}

// ------------------------------------------------------------ TORPEDOES

TorpedoForm::TorpedoForm(const Scenario& scenario, const EditorOptions& options)
    : data_(scenario.data()),
      choices_(scenario.data().torpedo_choices),
      month_(scenario.header.month),
      year_(scenario.header.year),
      limit_(options.limit_equipment != 0),
      corrected_(scenario.corrected()),
      aft_(choices_.count, 0),
      forward_(choices_.count, 0) {
  if (const std::optional<std::size_t> cls = data_.find_sub(scn_text(scenario.player().rec.sub_class))) {
    sub_ = &data_.tables.subs[*cls];
  }
  for (std::size_t t = 0; t < tubes_.size(); ++t) {
    for (std::size_t c = 1; c < choices_.count; ++c) {
      const Torpedo* torpedo = this->torpedo(c);
      if (torpedo != nullptr && scenario.header.tube_torpedoes[t] == static_cast<std::int32_t>(torpedo->id)) {
        tubes_[t] = static_cast<std::int32_t>(c);
        break;
      }
    }
  }
  for (std::size_t c = 1; c < choices_.count; ++c) {
    aft_[c] = scenario.reload_aft[c - 1];
    forward_[c] = scenario.reload_forward[c - 1];
  }
}

void TorpedoForm::commit(Scenario& scenario) const {
  for (std::size_t t = 0; t < tubes_.size(); ++t) {
    const Torpedo* torpedo = tubes_[t] == 0 ? nullptr : this->torpedo(static_cast<std::size_t>(tubes_[t]));
    scenario.header.tube_torpedoes[t] = torpedo == nullptr ? 0 : static_cast<std::int32_t>(torpedo->id);
  }
  for (std::size_t c = 1; c < choices_.count; ++c) {
    scenario.reload_aft[c - 1] = aft_[c];
    scenario.reload_forward[c - 1] = forward_[c];
  }
}

std::int32_t TorpedoForm::bow_tubes() const { return sub_ == nullptr ? 0 : static_cast<std::int32_t>(sub_->bow_tubes); }

std::int32_t TorpedoForm::stern_tubes() const {
  return sub_ == nullptr ? 0 : static_cast<std::int32_t>(sub_->stern_tubes);
}

std::int32_t TorpedoForm::capacity(bool aft) const {
  if (sub_ == nullptr) {
    return 0;
  }
  return static_cast<std::int32_t>(aft ? sub_->stern_reloads : sub_->bow_reloads);
}

const Torpedo* TorpedoForm::torpedo(std::size_t choice) const {
  if (choice == 0 || choice > choices_.torpedoes.size()) {
    return nullptr;
  }
  return &data_.tables.weapons.torpedoes[choices_.torpedoes[choice - 1]];
}

bool TorpedoForm::unavailable(std::size_t choice) const {
  const Torpedo* t = torpedo(choice);
  if (t == nullptr) {
    return true;
  }
  const bool modern = sub_ != nullptr && sub_->modern_torpedoes;
  const bool refused = !modern && !equals_ignore_case(t->short_name, data_.se.str(kMark10));
  if (!limit_) {
    return refused;
  }
  const auto torpedo_year = static_cast<std::int32_t>(t->year) - kScenarioYearBase;
  // SE compares WEAPONS.DAT's 1-based month with the 0-based scenario
  // month, so each torpedo comes a month late (the MARK18-1 in November
  // 1943); corrected, its own month (not in the original).
  const auto torpedo_month = static_cast<std::int32_t>(t->month) - (corrected_ ? 1 : 0);
  return year_ < torpedo_year || (torpedo_year == year_ && month_ < torpedo_month) || refused;
}

std::int32_t TorpedoForm::cycle_tube(std::size_t tube, bool advance) {
  std::int32_t& choice = tubes_[tube];
  while (advance) {
    choice = next(choice, static_cast<std::int32_t>(choices_.count));
    if (choice == 0) {
      break;
    }
    advance = unavailable(static_cast<std::size_t>(choice));
  }
  return choice;
}

std::int32_t TorpedoForm::cycle_reload(bool aft, std::size_t choice, bool tens, bool advance) {
  std::int32_t& count = (aft ? aft_ : forward_)[choice];
  std::int32_t digit = tens ? count / 10 : count % 10;
  // Deviation: the original keeps cycling forever when the other counts
  // alone exceed the capacity (a loaded file); one full turn stops here.
  for (int turns = 0; advance && turns < 10; ++turns) {
    if (tens) {
      digit = next(digit, 3);
      count = digit * 10 + count % 10;
    } else {
      digit = next(digit, 10);
      count = count / 10 * 10 + digit;
    }
    advance = over_capacity(aft);
  }
  return digit;
}

bool TorpedoForm::over_capacity(bool aft) const {
  std::int32_t total = 0;
  for (std::size_t c = 1; c < choices_.count; ++c) {
    if (!unavailable(c)) {
      total += (aft ? aft_ : forward_)[c];
    }
  }
  return capacity(aft) < total;
}

}  // namespace sh::editor

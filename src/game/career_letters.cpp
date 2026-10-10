// The texts of the career's letters and patrol screens (modules
// campaign/patrol, homeport, newequip, mdlaward, subscrn, career): built
// like the original with strcat and `menu_pad_text_to_width`, every string
// read from SH.EXE.

#include "game/career_letters.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <format>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "game/career_rules.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

namespace addr {
constexpr std::uint32_t kBaseYear = 0x10e3a4;      // i32 1930
constexpr std::uint32_t kMonths = 0x10e910;        // char*[12] "January"..
constexpr std::uint32_t kWeekdays = 0x10e970;      // char*[7] "Monday"..
constexpr std::uint32_t kCommands = 0x10eb38;      // char*[4] "ComSubsAsiatic"..
constexpr std::uint32_t kCrews = 0x10eb1c;         // char*[3] "Green"..
constexpr std::uint32_t kDamageLevels = 0x10eb28;  // char*[4] "None"..
constexpr std::uint32_t kFates = 0x10eb00;         // char*[4] "Operational"..
constexpr std::uint32_t kStatuses = 0x10eb10;      // char*[3] "Escaped"..
constexpr std::uint32_t kShipTypes = 0x10ea04;     // {char*, char*}[24]
constexpr std::uint32_t kBaseArt = 0x10eb6c;       // {char[13], i32 month}[]
constexpr std::uint32_t kEquipment = 0x10ca58;     // 0x28-byte entries
constexpr std::uint32_t kEquipmentDates = 0x10e408;  // {month, day, year}[]
constexpr std::uint32_t kLetterTexts = 0x10c194;   // char*[]: + code * 4 (codes 100..102)
}  // namespace addr

// The equipment table entries whose date `new_equipment_init_dialogs`
// copies from the date table (index into 0x10e408 in 3-dword steps), and
// the years it sets for the torpedoes.
constexpr std::array<int, 5> kEquipmentDateIndex = {0, 1, 5, 3, 2};
constexpr std::array<std::int32_t, 2> kTorpedoYears = {1943, 1944};

// Builds a text with the original's helpers.
class Text {
 public:
  Text(const ExeImage& exe, const Font& font) : exe_(exe), font_(font) {}

  Text& s(std::uint32_t address) {
    t_ += exe_.c_string(address);
    return *this;
  }
  Text& add(std::string_view text) {
    t_ += text;
    return *this;
  }
  Text& num(std::int32_t v) {
    t_ += std::to_string(v);
    return *this;
  }
  Text& table(std::uint32_t address, std::int32_t i, std::uint32_t stride = 4) {
    t_ += exe_.string_pointer(address + static_cast<std::uint32_t>(i) * stride);
    return *this;
  }
  // `menu_pad_text_to_width`.
  Text& pad(int width) {
    ui::menu_pad_text_to_width(t_, width, font_);
    return *this;
  }
  // Pads so that `text` measured in `measure` is centred in `width`.
  Text& centre(std::string_view text, int width, const Font& measure) {
    return pad((width - measure.text_width(text)) / 2).add(text);
  }
  // "<Month> <day>, <year>" of a career date.
  Text& date(const CareerRecord& r) {
    return table(addr::kMonths, r.month).add(" ").num(r.day).add(", ").num(r.year + exe_.i32(addr::kBaseYear));
  }
  std::string& str() { return t_; }

 private:
  const ExeImage& exe_;
  const Font& font_;
  std::string t_;
};

std::string upper(std::string s) {
  std::ranges::transform(s, s.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  return s;
}

std::string command_name(const ExeImage& exe, SubCommand command) {
  return std::string(exe.string_pointer(addr::kCommands + static_cast<std::uint32_t>(command) * 4));
}

}  // namespace

std::string gcvt3(double value) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.3g", value);
  return buf;
}

std::string calendar_day_name(const ExeImage& exe, std::int32_t day, std::int32_t month, std::int32_t year) {
  // career_transit_calendar (0x1c653): Zeller's sum with January and
  // February as months 13 and 14 of the year before.
  std::int32_t m = month + 1;
  std::int32_t y = year + exe.i32(addr::kBaseYear);
  if (m == 1 || m == 2) {
    m = month + 13;
    --y;
  }
  const std::int32_t index = (y / 400 + (((m + 1) * 3) / 5 + m * 2 + day + y + y / 4) - y / 100) % 7;
  return std::string(exe.string_pointer(addr::kWeekdays + static_cast<std::uint32_t>(index) * 4));
}

std::string base_art_file(const ExeImage& exe, std::int32_t month, std::int32_t year) {
  // homeport_load_base_art (0x4d346): the last entry starting at or before
  // the month.
  const std::int32_t index = campaign_month_index_abs(month, year + exe.i32(addr::kBaseYear));
  std::uint32_t entry = 0;
  while (exe.i32(addr::kBaseArt + entry * 0x11 + 0xd) <= index) {
    ++entry;
  }
  return std::string(exe.c_string(addr::kBaseArt + (entry - 1) * 0x11));
}

std::string patrol_orders_letter(const CareerRecord& r, const PatrolOrders& orders, const ExeImage& exe,
                                 const Font& font) {
  // patrol_orders_letter (0x632f7); the text box is 0x9d wide.
  Text t(exe, font);
  t.s(0xf7c65).centre(orders.zone.name, 0x9d, font).s(0xf7c92).s(0xf7caa);
  t.s(0xf7cc2).pad(0x28).date(r).s(0xf7d51);
  t.s(0xf7d69).pad(0x28).add(command_name(exe, orders.command)).s(0xf7d9b);
  t.s(0xf7db3).pad(0x28).add(r.boat).s(0xf7de3);
  t.s(0xf7dfc).add(r.name).s(0xf7e33);
  t.s(0xf7e4c).add(orders.zone.name).s(0xf7e84);
  const double days = static_cast<double>(orders.transit_hours) * (1.0 / 24.0);  // 0xf7f67
  if (days > 0.0) {
    t.s(0xf7ec9).pad(0x60).add(gcvt3(days)).s(0xf7f07);
  }
  if (orders.transit_fuel > 0) {
    t.s(0xf7f24).pad(0x60).num(orders.transit_fuel).s(0xf7f61);
  }
  return std::move(t.str());
}

void career_realism_from_options(std::uint32_t o, CareerRecord& r) {
  // patrol_orders_realism (0x62fdc)
  auto& v = r.realism;
  const auto bit = [o](int n) { return static_cast<std::int32_t>((o >> n) & 1u); };
  v[static_cast<std::size_t>(CareerRealism::kCombatLevel)] = static_cast<std::int32_t>((o & 0xf) >> 2);
  v[static_cast<std::size_t>(CareerRealism::kSailingModel)] = static_cast<std::int32_t>((o & 0x3f) >> 4);
  v[static_cast<std::size_t>(CareerRealism::kLimitedVisibility)] = bit(7);
  v[static_cast<std::size_t>(CareerRealism::kUnlimitedFuel)] = bit(9);
  v[static_cast<std::size_t>(CareerRealism::kUnlimitedBattery)] = bit(10);
  v[static_cast<std::size_t>(CareerRealism::kUnlimitedAmmo)] = bit(11);
  v[static_cast<std::size_t>(CareerRealism::kRealisticReloads)] = bit(13);
  v[static_cast<std::size_t>(CareerRealism::kDudTorpedoes)] = bit(14);
  v[static_cast<std::size_t>(CareerRealism::kLimitedDepthDataOff)] = bit(12);
  v[static_cast<std::size_t>(CareerRealism::kInvulnerable)] = bit(15);
  v[static_cast<std::size_t>(CareerRealism::kRealisticChartsOff)] = bit(6);
  v[static_cast<std::size_t>(CareerRealism::kRunAground)] = bit(8);
}

std::string patrol_results_summary(const CareerRecord& r, const MissionResults& m, const PatrolResultsInfo& info,
                                   const ExeImage& exe, const Font& font) {
  // patrol_results_summary (0x63a69): label columns at 0 and 0xfa, values
  // at 0x6e and 0x15e.
  const auto damage_level = [](std::int32_t pct) {
    return (pct > 0 ? 1 : 0) + (pct > 30 ? 1 : 0) + (pct > 70 ? 1 : 0);
  };
  Text t(exe, font);
  t.s(0xf8009).pad(0x6e).date(r).pad(0xfa).s(0xf8099).pad(0x15e).add(m.patrol_area).s(0xf80d2);
  t.s(0xf80ea).pad(0x6e).add(m.sub).pad(0xfa).s(0xf811c).pad(0x15e).add(m.class_name).s(0xf8150);
  t.s(0xf8168).pad(0x6e).table(addr::kCrews, m.crew).pad(0xfa).s(0xf819b).pad(0x15e);
  t.table(addr::kFates, static_cast<std::int32_t>(m.fate)).s(0xf81d0);
  int realism_label = 0;
  int realism_value = 0x6e;
  if (m.fate == BoatFate::kOperational || m.fate == BoatFate::kRecalled) {
    t.s(0xf81e8).pad(0x6e).table(addr::kDamageLevels, damage_level(m.hull_damage_pct)).s(0xf8222);
    t.num(m.hull_damage_pct).s(0xf8251).pad(0xfa).s(0xf826a).pad(0x15e);
    t.table(addr::kDamageLevels, damage_level(m.system_damage_pct)).s(0xf82a6).num(m.system_damage_pct).s(0xf82d5);
    const double days = static_cast<double>(info.transit_hours + m.minutes_elapsed / 60) * (1.0 / 24.0);  // 0xf88a6
    t.s(0xf82ef).pad(0x6e).add(gcvt3(days)).s(0xf832f);
    if (days != 1.0) {
      t.s(0xf834a);
    }
    if (info.repair_days != 0) {
      t.pad(0xfa).s(0xf8362).pad(0x15e).num(info.repair_days).s(0xf839c);
      if (info.repair_days > 1) {
        t.s(0xf83b7);
      }
    }
    t.s(0xf83cf);
    const std::int32_t used = info.fuel_capacity - m.fuel;
    t.s(0xf83e7).pad(0x6e).num(used).s(0xf841f).num(used * 100 / info.fuel_capacity).s(0xf8457);
    realism_label = 0xfa;
    realism_value = 0x15e;
  }
  t.pad(realism_label).s(0xf8470).pad(realism_value).num(m.realism_pct).s(0xf84ad);
  t.s(0xf84c6).pad(0x6e).num(m.ships_sunk).pad(0xfa).s(0xf84ff).pad(0x15e).num(m.tonnage).s(0xf8535);
  t.s(0xf854d).pad(0x6e).num(m.pilots_rescued).pad(0xfa).s(0xf8588).pad(0x15e).num(m.photo_recons).s(0xf85c3);
  t.s(0xf85db).pad(0x6e).num(m.score).s(0xf860f);
  if (!m.sunk.empty()) {
    bool header = false;
    for (const SunkListEntry& e : m.sunk) {
      if (e.log.type >= PatrolLogEntry::kRescuedPilot) {
        continue;
      }
      if (!header) {
        t.s(0xf8628).pad(0x6e).s(0xf8647).pad(0x14a).s(0xf866f).pad(0x19a).s(0xf868d).s(0xf86aa);
        header = true;
      }
      const LogAngle& lat = e.log.latitude;
      const LogAngle& lon = e.log.longitude;
      t.num(lat.degrees).s(0xf86d9).add(std::format("{:02}", lat.minutes)).add(std::string(1, lat.hemisphere));
      t.s(0xf871d).num(lon.degrees).s(0xf874c).add(std::format("{:02}", lon.minutes)).add(std::string(1, lon.hemisphere));
      t.pad(0x6e).table(addr::kShipTypes, e.log.type, 8);
      if (!e.log.name.empty()) {
        t.s(0xf87a6).add(e.log.name).s(0xf87d5);
      }
      t.pad(0x14a).num(e.log.tonnage).pad(0x19a).table(addr::kStatuses, static_cast<std::int32_t>(e.status));
      if (e.status == SunkStatus::kDamaged && e.damage_pct > 0) {
        t.s(0xf8819).num(e.damage_pct).s(0xf8848);
      }
      t.s(0xf8861);
    }
    t.s(0xf8879);
  }
  t.s(0xf8891);
  return std::move(t.str());
}

std::string homeport_transfer_orders(const CareerRecord& r, const HomePort& port, bool transfer, const ExeImage& exe,
                                     const Font& font) {
  // homeport_transfer_orders (0x4d722); the text box is 0x96 wide here.
  Text t(exe, font);
  t.s(0xf54b3).centre(port.name, 0x96, font).s(0xf54e5);
  // "(SubPac)": the command without its "Com".
  std::string command = command_name(exe, port.command);
  const std::string_view prefix = exe.c_string(0xf5501);
  if (command.size() >= 3 && equals_ignore_case(std::string_view(command).substr(0, 3), prefix)) {
    command.erase(0, 3);
  }
  const std::string bracketed = std::string(exe.c_string(0xf54ff)) + command + std::string(exe.c_string(0xf554d));
  t.centre(bracketed, 0x96, font).s(0xf557f).s(0xf5599).s(0xf55b3);
  t.s(0xf55cd).pad(0x28).date(r).s(0xf5668);
  t.s(0xf5682).pad(0x28).s(0xf56a0).s(0xf56d2);
  t.s(0xf56ed).add(r.name);
  const std::string by = command_name(exe, port.command);
  if (transfer) {
    t.s(0xf5728).add(r.boat).s(0xf575c).add(port.name).s(0xf57bc).add(by).s(0xf5813);
  } else {
    t.s(0xf5838).add(port.name).s(0xf588c).add(by).s(0xf58e3);
  }
  return std::move(t.str());
}

EquipmentEntry equipment_entry(const ExeImage& exe, int i) {
  const std::uint32_t a = addr::kEquipment + static_cast<std::uint32_t>(i) * 0x28;
  EquipmentEntry e;
  e.bit = exe.u32(a);
  e.month = exe.i32(a + 4);
  e.year = exe.i32(a + 0xc);
  // new_equipment_init_dialogs (0x5b140) sets the dates at start-up.
  if (i < static_cast<int>(kEquipmentDateIndex.size())) {
    const std::uint32_t d = addr::kEquipmentDates + static_cast<std::uint32_t>(kEquipmentDateIndex[static_cast<std::size_t>(i)]) * 12;
    e.month = exe.i32(d);
    e.year = exe.i32(d + 8);
  } else {
    e.year = kTorpedoYears[static_cast<std::size_t>(i) - kEquipmentDateIndex.size()] - exe.i32(addr::kBaseYear);
  }
  e.name = exe.string_pointer(a + 0x10);
  e.text = exe.string_pointer(a + 0x14);
  e.sprite = exe.i32(a + 0x18);
  e.x = exe.i32(a + 0x1c);
  e.y = exe.i32(a + 0x20);
  e.supply = exe.i32(a + 0x24) != 0;
  return e;
}

std::optional<int> new_equipment_due(const ExeImage& exe, const CareerRecord& r, std::int32_t current_command) {
  // new_equipment_letter (0x5b5c7)
  const std::int32_t base = exe.i32(addr::kBaseYear);
  const std::int32_t now = campaign_month_index_abs(r.month, r.year + base);
  for (int i = 0; i < kEquipmentEntries; ++i) {
    const EquipmentEntry e = equipment_entry(exe, i);
    if ((e.bit & r.equipment) != 0) {
      continue;
    }
    std::int32_t due = campaign_month_index_abs(e.month, e.year + base);
    if (!e.supply && current_command != 1) {
      due += 3;
    }
    if (now >= due) {
      return i;
    }
  }
  return std::nullopt;
}

std::string new_equipment_text(const CareerRecord& r, const EquipmentEntry& e, const ExeImage& exe, const Font& font) {
  // new_equipment_letter (0x5b5c7); the text box is 0xf0 wide.
  Text t(exe, font);
  t.s(0xf756b).centre(e.name, 0xf0, font).s(0xf759c).s(0xf75b6);
  t.s(0xf75d0).pad(0x28).date(r).s(0xf766b);
  t.s(0xf7685).pad(0x28).s(e.supply ? 0xf76a3 : 0xf76d0);
  t.pad(0x28).add(r.home_port).s(0xf7715);
  t.pad(0x28).add(r.boat).s(0xf774a);
  t.s(e.supply ? 0xf7765 : 0xf77b9).add(e.name).s(0xf7825).add(e.text);
  return std::move(t.str());
}

std::string award_citation_text(LetterCode code, const CareerRecord& r, SubCommand command, std::int32_t centre_width,
                                 const ExeImage& exe, const Font& typewriter, const Font& pad_font) {
  // awards_build_citation (0x56185)
  Text t(exe, pad_font);
  const auto heading = [&](const std::string& text) { t.centre(text, centre_width, typewriter); };
  const auto c = static_cast<std::int32_t>(code);
  t.s(0xf6828);
  if (code == LetterCode::kNuc || code == LetterCode::kPuc) {
    const bool puc = code == LetterCode::kPuc;
    heading(std::string(exe.c_string(puc ? 0xf6842 : 0xf685d)));
    t.s(0xf68a4);
    heading(upper(r.boat));
    t.s(0xf68ef);
    heading(upper(r.class_title + std::string(exe.c_string(0xf6921))));
    t.s(0xf6962);
    heading(std::string(exe.c_string(0xf697c)));
    t.s(puc ? 0xf69bf : 0xf69c2).s(0xf69e0);
    t.add(exe.c_string(puc ? 0x10c088 : 0x10c1f0));
  } else if (c >= 100) {
    t.date(r).s(0xf6a81);
    t.s(0xf6a9d).pad(0x28).add(r.name).s(0xf6ad1);
    t.s(0xf6aeb).pad(0x28).add(command_name(exe, command)).s(0xf6b21);
    t.s(0xf6b3c).pad(0x28);
    t.s(code == LetterCode::kPoorPatrol ? 0xf6b59 : code == LetterCode::kPoorFuel ? 0xf6b89 : 0xf6bb6);
    t.s(0xf6bde).s(0xf6bfa);
    t.add(exe.string_pointer(addr::kLetterTexts + static_cast<std::uint32_t>(c) * 4));
  } else {
    heading(upper(r.name));
    t.s(0xf6c4f);
    heading(std::string(exe.c_string(0xf6c69)));
    t.s(0xf6ca3);
    heading(std::string(exe.c_string(0xf6cbd)));
    t.s(0xf6d00);
    if (r.status == CareerStatus::kDeceased) {
      heading(std::string(exe.c_string(0xf6d1a)));
    }
    t.s(0xf6d57).s(0xf6d74);
    static constexpr std::array<std::uint32_t, 6> kTexts = {0x10bd30, 0x10bc2c, 0x10bb28, 0x10be50, 0x10b9e0, 0x10bf68};
    t.add(exe.c_string(kTexts[static_cast<std::size_t>(c)]));
  }
  return std::move(t.str());
}

std::string new_submarine_orders(const CareerRecord& r, SubCommand command, bool offer, const ExeImage& exe,
                                 const Font& font) {
  // subscrn_compose_orders (0x72441); the text box is 0xc2 wide.
  Text t(exe, font);
  t.s(0xf98a6).centre(r.boat, 0xc2, font).s(0xf98d6).s(0xf98ef);
  t.s(0xf9908).pad(0x28).date(r).s(0xf999d);
  t.s(0xf99b6).pad(0x28).add(command_name(exe, command)).s(0xf99ea);
  t.pad(0x28).s(0xf9a03).pad(0x28).s(0xf9a32).s(0xf9a55);
  t.s(0xf9a6f).add(r.name).s(0xf9aa8);
  if (offer) {
    t.s(0xf9ac3).add(r.class_title).s(0xf9b17).add(r.boat).s(0xf9b57).table(addr::kCrews, r.rank).s(0xf9bb0);
  } else {
    t.s(0xf9be7).add(r.home_port).s(0xf9c32).add(r.class_title).s(0xf9c88).add(r.boat).s(0xf9cc8);
  }
  return std::move(t.str());
}

}  // namespace sh

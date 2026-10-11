#include "game/mission_rules.h"

#include <charconv>
#include <optional>
#include <type_traits>
#include <variant>

#include "core/byte_reader.h"
#include "game/saves_options.h"
#include "sim/state_io.h"

namespace sh {

namespace {

constexpr std::string_view kConfigPrefix = "rule.";

std::int32_t get(const MissionRules& rules, const MissionRuleField& field) {
  return std::visit(
      [&rules](auto member) { return static_cast<std::int32_t>(rules.*member); }, field.member);
}

void set(MissionRules& rules, const MissionRuleField& field, std::int32_t value) {
  std::visit(
      [&rules, value](auto member) {
        using T = std::remove_reference_t<decltype(rules.*member)>;
        if constexpr (std::is_same_v<T, bool>) {
          rules.*member = value != 0;
        } else {
          rules.*member = value;
        }
      },
      field.member);
}

const MissionRuleField* find_field(std::string_view key) {
  for (const MissionRuleField& f : kMissionRuleFields) {
    if (f.key == key) {
      return &f;
    }
  }
  return nullptr;
}

std::string_view trim(std::string_view s) {
  const std::size_t first = s.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

std::optional<std::int32_t> parse_int(std::string_view s) {
  std::int32_t v = 0;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc() || end != s.data() + s.size()) {
    return std::nullopt;
  }
  return v;
}

}  // namespace

std::vector<std::uint8_t> encode_mission_rules(const MissionRules& rules) {
  ByteWriter out;
  out.u32(static_cast<std::uint32_t>(kMissionRuleFields.size()));
  for (const MissionRuleField& f : kMissionRuleFields) {
    out.u8(static_cast<std::uint8_t>(f.key.size()));
    out.bytes({reinterpret_cast<const std::uint8_t*>(f.key.data()), f.key.size()});
    out.i32(get(rules, f));
  }
  return out.take();
}

MissionRules decode_mission_rules(std::span<const std::uint8_t> data) {
  ByteReader in(data, "mission rules");
  MissionRules rules;
  const std::uint32_t count = in.u32();
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::span<const std::uint8_t> key = in.bytes(in.u8());
    const std::int32_t value = in.i32();
    if (const MissionRuleField* f = find_field({reinterpret_cast<const char*>(key.data()), key.size()})) {
      set(rules, *f, value);
    }
  }
  return rules;
}

std::string mission_rules_config(const MissionRules& rules) {
  std::string text;
  for (const MissionRuleField& f : kMissionRuleFields) {
    text += std::string(kConfigPrefix) + std::string(f.key) + " = " + std::to_string(get(rules, f)) + "\n";
  }
  return text;
}

MissionRules parse_mission_rules_config(std::string_view text) {
  MissionRules rules;
  while (!text.empty()) {
    const std::size_t eol = text.find('\n');
    const std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    const std::string_view name = trim(line.substr(0, eq));
    if (!name.starts_with(kConfigPrefix)) {
      continue;
    }
    const MissionRuleField* f = find_field(name.substr(kConfigPrefix.size()));
    const std::optional<std::int32_t> value = parse_int(trim(line.substr(eq + 1)));
    if (f != nullptr && value) {
      set(rules, *f, *value);
    }
  }
  return rules;
}

MissionRules load_mission_rules(const SaveDir& dir) {
  const std::optional<std::vector<std::uint8_t>> data = dir.read(kExtendedConfigName);
  if (!data) {
    return {};
  }
  return parse_mission_rules_config({reinterpret_cast<const char*>(data->data()), data->size()});
}

}  // namespace sh

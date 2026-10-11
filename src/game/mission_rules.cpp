#include "game/mission_rules.h"

#include <charconv>
#include <optional>
#include <type_traits>
#include <variant>

#include "core/byte_reader.h"
#include "game/extended_config.h"
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

MissionRules parse_mission_rules_config(std::string_view text, const MissionRules& defaults) {
  MissionRules rules = defaults;
  for (const auto& [key, value] : parse_config(text)) {
    if (!key.starts_with(kConfigPrefix)) {
      continue;
    }
    const MissionRuleField* f = find_field(std::string_view(key).substr(kConfigPrefix.size()));
    const std::optional<std::int32_t> v = parse_int(value);
    if (f != nullptr && v) {
      set(rules, *f, *v);
    }
  }
  return rules;
}

std::string merge_mission_rules_config(std::string_view text, const MissionRules& rules) {
  return merge_config(text, kConfigPrefix, mission_rules_config(rules));
}

MissionRules load_mission_rules(const SaveDir& dir) {
  return parse_mission_rules_config(read_config(dir), MissionRules::extended());
}

bool save_mission_rules(const SaveDir& dir, const MissionRules& rules) {
  return write_config(dir, kConfigPrefix, mission_rules_config(rules));
}

}  // namespace sh

#include "game/extended_config.h"

#include <vector>

#include "game/saves_options.h"

namespace sh {

namespace {

constexpr std::string_view kUiPrefix = "ui.";
constexpr std::string_view kStatePrefix = "state.";
constexpr std::string_view kLastBoat = "state.last_boat";

std::string_view trim(std::string_view s) {
  const std::size_t first = s.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) {
    return {};
  }
  return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// Calls `f` with each line of `text`, without its '\n' (and '\r').
template <class F>
void for_each_line(std::string_view text, F f) {
  while (!text.empty()) {
    const std::size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    f(line);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
  }
}

}  // namespace

std::map<std::string, std::string, std::less<>> parse_config(std::string_view text) {
  std::map<std::string, std::string, std::less<>> values;
  for_each_line(text, [&values](std::string_view line) {
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      return;
    }
    const std::string_view key = trim(line.substr(0, eq));
    if (!key.empty()) {
      values[std::string(key)] = std::string(trim(line.substr(eq + 1)));
    }
  });
  return values;
}

std::string merge_config(std::string_view text, std::string_view prefix, std::string_view lines) {
  std::string merged;
  for_each_line(text, [&merged, prefix](std::string_view line) {
    const std::string_view t = trim(line);
    if (!t.empty() && !t.starts_with(prefix)) {
      merged += std::string(line) + "\n";
    }
  });
  return merged + std::string(lines);
}

std::string read_config(const SaveDir& dir) {
  const std::optional<std::vector<std::uint8_t>> data = dir.read(kExtendedConfigName);
  return data ? std::string(data->begin(), data->end()) : std::string();
}

bool write_config(const SaveDir& dir, std::string_view prefix, std::string_view lines) {
  const std::string text = merge_config(read_config(dir), prefix, lines);
  return dir.write(kExtendedConfigName, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

Features load_features(const SaveDir& dir, const Features& defaults) {
  Features f = defaults;
  const auto values = parse_config(read_config(dir));
  for (const FeatureOption& o : kFeatureOptions) {
    const auto v = values.find(std::string(kUiPrefix) + std::string(o.key));
    if (v != values.end() && (v->second == "0" || v->second == "1")) {
      f.*o.member = v->second == "1";
    }
  }
  return f;
}

bool save_features(const SaveDir& dir, const Features& features) {
  std::string lines;
  for (const FeatureOption& o : kFeatureOptions) {
    lines += std::string(kUiPrefix) + std::string(o.key) + " = " + (features.*o.member ? "1" : "0") + "\n";
  }
  return write_config(dir, kUiPrefix, lines);
}

std::optional<std::string> load_last_boat(const SaveDir& dir) {
  const auto values = parse_config(read_config(dir));
  const auto v = values.find(kLastBoat);
  if (v == values.end() || v->second.empty()) {
    return std::nullopt;
  }
  return v->second;
}

bool save_last_boat(const SaveDir& dir, std::string_view class_name) {
  const std::string text = read_config(dir);
  // The other `state.` lines are kept.
  std::string lines;
  for (const auto& [key, value] : parse_config(text)) {
    if (key.starts_with(kStatePrefix) && key != kLastBoat) {
      lines += key + " = " + value + "\n";
    }
  }
  lines += std::string(kLastBoat) + " = " + std::string(class_name) + "\n";
  return write_config(dir, kStatePrefix, lines);
}

}  // namespace sh

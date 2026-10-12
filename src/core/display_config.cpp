#include "core/display_config.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace sh {

namespace {

std::string_view trim(std::string_view s) {
  const std::size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

std::optional<bool> parse_flag(std::string_view s) {
  if (s == "0" || s == "1") {
    return s == "1";
  }
  return std::nullopt;
}

}  // namespace

DisplayConfig parse_display_config(std::string_view text) {
  DisplayConfig config;
  if (const std::optional<bool> legacy = parse_flag(trim(text))) {
    config.fullscreen = legacy;
    return config;
  }
  while (!text.empty()) {
    const std::size_t eol = text.find('\n');
    const std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    const std::string_view key = trim(line.substr(0, eq));
    const std::optional<bool> value = parse_flag(trim(line.substr(eq + 1)));
    if (!value) {
      continue;
    }
    if (key == "fullscreen") {
      config.fullscreen = value;
    } else if (key == "smoothing") {
      config.smoothing = value;
    }
  }
  return config;
}

std::string format_display_config(const DisplayConfig& config) {
  std::string text;
  if (config.fullscreen) {
    text += *config.fullscreen ? "fullscreen = 1\n" : "fullscreen = 0\n";
  }
  if (config.smoothing) {
    text += *config.smoothing ? "smoothing = 1\n" : "smoothing = 0\n";
  }
  return text;
}

DisplayConfig read_display_config(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream text;
  text << in.rdbuf();
  return parse_display_config(text.str());
}

bool write_display_config(const std::filesystem::path& file, const DisplayConfig& config) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << format_display_config(config);
  return static_cast<bool>(out.flush());
}

DisplaySettings initial_display_settings(const DisplayConfig& saved, bool extended, bool always_fullscreen) {
  DisplaySettings s;
  s.fullscreen = always_fullscreen || saved.fullscreen.value_or(extended);
  if (extended) {
    s.smoothing = saved.smoothing.value_or(true);
  }
  return s;
}

}  // namespace sh

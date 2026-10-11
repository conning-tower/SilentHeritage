#include "synthetic/exe_manifest.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "synthetic/procedural.h"

namespace sh::test {

namespace {

// Returns whether `text` is a file name or path (kept as it is): no spaces,
// upper-case letters, digits and file-name punctuation, with a dot, a
// backslash or a digit.
bool identifier(std::string_view text) {
  if (text.empty() || text.size() > 40) {
    return false;
  }
  bool marker = false;
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (std::islower(u) || std::isspace(u) || u < 0x20 || u > 0x7e) {
      return false;
    }
    if (!std::isupper(u) && !std::isdigit(u) && std::string_view("._-\\:%?*").find(c) == std::string_view::npos) {
      return false;
    }
    marker = marker || c == '.' || c == '\\' || std::isdigit(u);
  }
  return marker;
}

std::string escape(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u > 0x7e || c == '"' || c == '\\') {
      out += std::format("\\x{:02x}", u);
    } else {
      out += c;
    }
  }
  return out + "\"";
}

std::string unescape(std::string_view quoted) {
  if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"') {
    throw std::runtime_error("exe manifest: bad string");
  }
  std::string out;
  for (std::size_t i = 1; i + 1 < quoted.size(); ++i) {
    if (quoted[i] == '\\' && i + 3 < quoted.size() && quoted[i + 1] == 'x') {
      out += static_cast<char>(std::stoi(std::string(quoted.substr(i + 2, 2)), nullptr, 16));
      i += 3;
    } else {
      out += quoted[i];
    }
  }
  return out;
}

}  // namespace

std::string synthetic_text(std::string_view text, std::uint32_t seed) {
  if (identifier(text)) {
    return std::string(text);
  }
  static constexpr std::string_view kLetters = "etaoinsrhdlucmfywgpbvk";
  std::string out(text);
  SynthRandom random(seed);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const auto u = static_cast<unsigned char>(out[i]);
    // Placeholders keep their conversion letters ("%s", "%02d", "%ld").
    if (out[i] == '%') {
      std::size_t j = i + 1;
      while (j < out.size() && std::string_view("-+ #0123456789.lh").find(out[j]) != std::string_view::npos) {
        ++j;
      }
      i = j;
      continue;
    }
    if (std::isalpha(u)) {
      const char c = kLetters[static_cast<std::size_t>(random.next() % kLetters.size())];
      out[i] = std::isupper(u) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
    }
  }
  return out;
}

std::set<TracedRead> read_exe_trace(const std::filesystem::path& path, std::uint32_t image_end) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot read " + path.string());
  }
  std::set<TracedRead> out;
  for (std::string line; std::getline(in, line);) {
    std::istringstream words(line);
    std::uint32_t end = 0;
    std::uint32_t address = 0;
    std::size_t size = 0;
    int kind = 0;
    words >> std::hex >> end >> address >> size >> std::dec >> kind;
    if (words && end == image_end) {
      out.emplace(address, size, static_cast<sh::ExeRead>(kind));
    }
  }
  return out;
}

#ifdef SH_TRACE_EXE_READS
ExeManifest extract_exe_manifest(const ExeImage& image, const std::set<TracedRead>& reads,
                                 const std::vector<ExeManifest::Rule>& rules) {
  ExeManifest m;
  for (const LeObject& o : image.objects()) {
    m.objects.push_back({o.base_address, o.virtual_size, o.flags});
  }
  m.rules = rules;
  const auto in_rule = [&rules](std::uint32_t a) {
    return std::ranges::any_of(rules, [a](const ExeManifest::Rule& r) { return a >= r.address && a < r.address + r.size; });
  };
  // The strings, without those starting inside another (their bytes come
  // with it).
  std::map<std::uint32_t, std::uint32_t> string_ends;
  const auto outside = [&image](std::uint32_t address, std::size_t size) {
    return address < image.base() || address + size > image.end();  // tests of the bounds checks
  };
  for (const auto& [address, size, kind] : reads) {
    if (kind == sh::ExeRead::kString && !in_rule(address) && !outside(address, size)) {
      const auto it = string_ends.upper_bound(address);
      if (it != string_ends.begin() && std::prev(it)->second > address) {
        continue;
      }
      string_ends[address] = address + static_cast<std::uint32_t>(size);
      m.strings.push_back({address, synthetic_text(image.c_string(address), address)});
    }
  }
  const auto in_string = [&string_ends](std::uint32_t a) {
    const auto it = string_ends.upper_bound(a);
    return it != string_ends.begin() && std::prev(it)->second > a;
  };
  const std::vector<std::uint32_t>& sources = image.pointer_sources();
  std::set<std::uint32_t> pointer_done;
  for (const auto& [address, size, kind] : reads) {
    if (kind == sh::ExeRead::kString || kind == sh::ExeRead::kBytes || in_rule(address) || in_string(address) ||
        outside(address, size)) {
      continue;
    }
    const bool word = kind == sh::ExeRead::kU32 || kind == sh::ExeRead::kStringPointer;
    if (word && std::ranges::binary_search(sources, address)) {
      if (pointer_done.insert(address).second) {
        m.pointers.push_back({address, image.u32(address)});
      }
      continue;
    }
    const auto bytes = static_cast<std::uint8_t>(word ? 4 : size);
    const std::uint32_t value = bytes == 1 ? image.u8(address) : bytes == 2 ? image.u16(address) : image.u32(address);
    m.values.push_back({address, bytes, value});
  }
  return m;
}
#endif

std::string format_exe_manifest(const ExeManifest& m) {
  std::ostringstream out;
  out << "# The layout of an original executable (tests/synthetic/exe_manifest.h): synthetic text, layout numbers.\n";
  for (const auto& o : m.objects) {
    out << std::format("object {:x} {:x} {:x}\n", o.base, o.size, o.flags);
  }
  for (const auto& s : m.strings) {
    out << std::format("str {:x} {}\n", s.address, escape(s.text));
  }
  for (const auto& p : m.pointers) {
    out << std::format("ptr {:x} {:x}\n", p.address, p.target);
  }
  for (const auto& v : m.values) {
    out << std::format("u{} {:x} {:x}\n", 8 * v.size, v.address, v.value);
  }
  for (const auto& r : m.rules) {
    out << std::format("rule {:x} {:x} {}\n", r.address, r.size, r.name);
  }
  return out.str();
}

ExeManifest parse_exe_manifest(std::string_view text) {
  ExeManifest m;
  std::istringstream in{std::string(text)};
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    std::istringstream words(line);
    std::string kind;
    words >> kind >> std::hex;
    if (kind == "object") {
      ExeManifest::Object o;
      words >> o.base >> o.size >> o.flags;
      m.objects.push_back(o);
    } else if (kind == "str") {
      ExeManifest::String s;
      words >> s.address;
      std::string rest;
      std::getline(words >> std::ws, rest);
      s.text = unescape(rest);
      m.strings.push_back(std::move(s));
    } else if (kind == "ptr") {
      ExeManifest::Pointer p;
      words >> p.address >> p.target;
      m.pointers.push_back(p);
    } else if (kind == "u8" || kind == "u16" || kind == "u32") {
      ExeManifest::Value v;
      v.size = static_cast<std::uint8_t>(std::stoi(kind.substr(1)) / 8);
      words >> v.address >> v.value;
      m.values.push_back(v);
    } else if (kind == "rule") {
      ExeManifest::Rule r;
      words >> r.address >> r.size >> std::dec >> r.name;
      m.rules.push_back(std::move(r));
    } else {
      throw std::runtime_error("exe manifest: unknown line " + line);
    }
    if (!words && kind != "str") {
      throw std::runtime_error("exe manifest: bad line " + line);
    }
  }
  return m;
}

std::filesystem::path exe_manifest_path(std::string_view name) {
  return std::filesystem::path(SH_SYNTHETIC_DIR) / "manifest" / (std::string(name) + ".txt");
}

const ExeManifest& committed_exe_manifest(std::string_view name) {
  static std::map<std::string, ExeManifest, std::less<>> cache;
  if (const auto it = cache.find(name); it != cache.end()) {
    return it->second;
  }
  std::ifstream in(exe_manifest_path(name));
  if (!in) {
    throw std::runtime_error("cannot read " + exe_manifest_path(name).string());
  }
  std::ostringstream text;
  text << in.rdbuf();
  return cache.emplace(std::string(name), parse_exe_manifest(text.str())).first->second;
}

}  // namespace sh::test

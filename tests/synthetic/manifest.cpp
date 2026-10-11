#include "synthetic/manifest.h"

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "assets/cat.h"
#include "assets/mel.h"
#include "assets/objects.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "assets/smacker.h"
#include "assets/world.h"
#include "assets/environment.h"
#include "assets/fnt.h"

namespace sh::test {

namespace {

// The directories of the game files and the root's data files.
constexpr std::string_view kDirs[] = {"ART", "DATA", "SFX", "SPEECH", "ALT", "CINES", "BUD", "SCEN", "CUSTOM", "SAVE"};
constexpr std::string_view kRootFiles[] = {"EARTH.DAT", "SUN.DAT", "PLANETS.NDX", "SHPATH.INI"};

SpriteManifest sprite_manifest(std::string name, std::span<const std::uint8_t> data) {
  const Shp shp = Shp::parse(data, name);
  SpriteManifest m{.name = std::move(name), .version = shp.version(), .frames = {}};
  for (const SpriteFrame& f : shp.frames()) {
    m.frames.push_back({.empty = f.empty(),
                        .x0 = f.x0,
                        .y0 = f.y0,
                        .x1 = f.x1,
                        .y1 = f.y1,
                        .palette_entries = static_cast<std::uint32_t>(f.palette.size())});
  }
  return m;
}

std::string extension(std::string_view path) {
  const std::size_t dot = path.rfind('.');
  return dot == std::string_view::npos ? std::string() : std::string(path.substr(dot + 1));
}

FileManifest file_manifest(const Install& install, const std::string& path) {
  const std::vector<std::uint8_t> data = install.read(path);
  const std::string ext = extension(path);
  FileManifest m{.path = path, .kind = "size", .numbers = {}, .sprites = {}};
  const std::string name = path.substr(path.rfind('/') + 1);
  if (ext == "PCX" || ext == "PCC" || ext == "PAL") {
    const PcxImage pcx = decode_pcx(data, path);
    m.kind = "pcx";
    m.numbers = {pcx.image.width(), pcx.image.height()};
  } else if (ext == "SHP") {
    m.kind = "shp";
    m.sprites.push_back(sprite_manifest(name, data));
  } else if (ext == "FNT") {
    const Font font = Font::parse(data, path);
    m.kind = "fnt";
    m.numbers.push_back(font.height());
    for (std::size_t c = 0; c < font.glyph_count(); ++c) {
      m.numbers.push_back(font.glyph_width(static_cast<std::uint8_t>(c)));
    }
  } else if (ext == "CAT") {
    const CatArchive cat = CatArchive::parse(data, path);
    m.kind = "cat";
    for (std::size_t i = 0; i < cat.entries().size(); ++i) {
      m.sprites.push_back(sprite_manifest(cat.entries()[i].name, cat.member(i)));
    }
  } else if (ext == "WLD") {
    const World w = parse_world(data, path);
    m.kind = "wld";
    m.numbers = {w.version,     w.scale_shift, static_cast<std::int64_t>(w.locations.size()),
                 w.grid.width,  w.grid.height, w.chart.width,
                 w.chart.height, w.bounds ? 1 : 0};
    if (!w.coast_tiles_shp.empty()) {
      m.sprites.push_back(sprite_manifest("tiles", w.coast_tiles_shp));
    }
  } else if (ext == "OBS") {
    m.kind = "obs";
    for (const MapObject& o : parse_objects(data, path)) {
      m.numbers.push_back(static_cast<std::int64_t>(o.render_type));
      m.numbers.push_back(o.size);
      m.sprites.push_back(sprite_manifest(std::format("object{}", m.sprites.size()), o.shp));
    }
  } else if (ext == "ENV") {
    const Environment env = parse_environment(data, path);
    const PcxImage sky = decode_pcx(env.sky_pcx, path);
    m.kind = "env";
    m.numbers = {env.version, sky.image.width(), sky.image.height()};
  } else if (ext == "SMK") {
    const SmackerVideo video(data, path);
    m.kind = "smk";
    m.numbers = {video.width(), video.height(), static_cast<std::int64_t>(video.y_scale()), video.frame_count(),
                 video.frame_duration_us()};
    for (const SmackerAudioFormat& a : video.audio_tracks()) {
      m.numbers.insert(m.numbers.end(), {a.rate, a.channels, a.bits});
    }
  } else if (ext == "MEL") {
    const MelSample mel = parse_mel(data, path);
    m.kind = "mel";
    m.numbers = {mel.header.rate,       mel.header.rate_max,      mel.header.rate_min,
                 mel.header.loop_count, mel.header.volume_offset, static_cast<std::int64_t>(mel.pcm.size())};
  } else if (ext == "BNK") {
    m.kind = "bnk";
    for (const BankEntry& e : parse_bnk(data, path)) {
      m.numbers.push_back(e.sample_id);
    }
  } else {
    m.numbers = {static_cast<std::int64_t>(data.size())};
  }
  return m;
}

void format_sprite(std::ostringstream& out, const SpriteManifest& s) {
  out << std::format("  sprite {} {} {}\n", s.name, s.version, s.frames.size());
  for (const SpriteManifest::Frame& f : s.frames) {
    out << std::format("    {} {} {} {} {} {}\n", f.empty ? "empty" : "frame", f.x0, f.y0, f.x1, f.y1,
                       f.palette_entries);
  }
}

}  // namespace

Manifest extract_manifest(const Install& install) {
  Manifest out;
  for (const std::string_view dir : kDirs) {
    for (const std::string& path : install.list(dir, "")) {
      const std::string ext = extension(path);
      if (ext == "EXE" || ext == "INI" || ext == "001" || path.find(' ') != std::string::npos) {
        continue;  // programs, desktop.ini, third-party data, copies
      }
      out.emplace(path, file_manifest(install, path));
    }
  }
  for (const std::string_view name : kRootFiles) {
    out.emplace(std::string(name), file_manifest(install, std::string(name)));
  }
  return out;
}

std::string format_manifest(const Manifest& manifest) {
  std::ostringstream out;
  out << "# The structure of the original game's files (tests/synthetic/manifest.h): no pixels, text or sound.\n";
  for (const auto& [path, m] : manifest) {
    out << path << ' ' << m.kind;
    for (const std::int64_t v : m.numbers) {
      out << ' ' << v;
    }
    out << '\n';
    for (const SpriteManifest& s : m.sprites) {
      format_sprite(out, s);
    }
  }
  return out.str();
}

Manifest parse_manifest(std::string_view text) {
  Manifest out;
  std::istringstream in{std::string(text)};
  FileManifest* file = nullptr;
  SpriteManifest* sprite = nullptr;
  int line_number = 0;
  const auto fail = [&line_number](std::string_view what) {
    throw std::runtime_error(std::format("manifest line {}: {}", line_number, what));
  };
  for (std::string line; std::getline(in, line);) {
    ++line_number;
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    std::istringstream words(line);
    std::string first;
    words >> first;
    if (!line.starts_with(' ')) {
      FileManifest m{.path = first, .kind = {}, .numbers = {}, .sprites = {}};
      words >> m.kind;
      for (std::int64_t v; words >> v;) {
        m.numbers.push_back(v);
      }
      file = &out.insert_or_assign(first, std::move(m)).first->second;
      sprite = nullptr;
    } else if (first == "sprite") {
      if (file == nullptr) {
        fail("sprite before any file");
      }
      SpriteManifest s;
      std::size_t count = 0;
      words >> s.name >> s.version >> count;
      s.frames.reserve(count);
      file->sprites.push_back(std::move(s));
      sprite = &file->sprites.back();
    } else if (first == "frame" || first == "empty") {
      if (sprite == nullptr) {
        fail("frame before any sprite");
      }
      SpriteManifest::Frame f;
      f.empty = first == "empty";
      if (!(words >> f.x0 >> f.y0 >> f.x1 >> f.y1 >> f.palette_entries)) {
        fail("bad frame");
      }
      sprite->frames.push_back(f);
    } else {
      fail("unknown line");
    }
  }
  return out;
}

std::filesystem::path manifest_path() { return std::filesystem::path(SH_SYNTHETIC_DIR) / "manifest" / "files.txt"; }

const Manifest& committed_manifest() {
  static const Manifest manifest = [] {
    std::ifstream in(manifest_path());
    if (!in) {
      throw std::runtime_error("cannot read " + manifest_path().string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_manifest(text.str());
  }();
  return manifest;
}

}  // namespace sh::test

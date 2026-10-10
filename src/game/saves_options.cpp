#include "game/saves_options.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

#include "core/byte_reader.h"
#include "core/install.h"

namespace sh {

namespace {

std::string upper(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  return out;
}

// Appends `v` little-endian.
void put_i32(std::vector<std::uint8_t>& out, std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(u >> shift));
  }
}

std::vector<std::uint8_t> file_image(std::uint8_t version, std::span<const std::int32_t> values) {
  std::vector<std::uint8_t> out{version};
  for (const std::int32_t v : values) {
    put_i32(out, v);
  }
  return out;
}

// Reads the version byte, then up to `fields.size()` dwords into `fields`
// in order, stopping at the first short read like the original loaders.
// Returns false, writing nothing, when the version byte is missing or not
// `version`.
bool read_fields(std::span<const std::uint8_t> data, std::uint8_t version, std::span<std::int32_t* const> fields) {
  if (data.empty() || data[0] != version) {
    return false;
  }
  ByteReader r(data.subspan(1));
  for (std::int32_t* field : fields) {
    if (r.remaining() < 4) {
      break;
    }
    *field = r.i32();
  }
  return true;
}

// The shared flow of the loaders: read the file into `value`, or write
// `value` (the defaults) when it is missing or of another version.
template <class T>
T load_or_create(const SaveDir& dir, T value) {
  const std::optional<std::vector<std::uint8_t>> data = dir.read(T::kFileName);
  if (!data || !value.read_from(*data)) {
    value.save(dir);
  }
  return value;
}

}  // namespace

bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
  std::error_code ec;
  const bool in_place = std::filesystem::exists(path, ec) && !std::filesystem::is_regular_file(path, ec);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  std::ofstream out(in_place ? path : tmp, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  out.close();
  if (in_place || !out) {
    if (!in_place) {
      std::filesystem::remove(tmp, ec);
    }
    return static_cast<bool>(out);
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    return false;
  }
  return true;
}

SaveDir::SaveDir(std::filesystem::path dir, const Install* install, std::string install_dir)
    : dir_(std::move(dir)), install_(install), install_dir_(std::move(install_dir)) {}

std::filesystem::path SaveDir::own_path(std::string_view name) const {
  const std::filesystem::path exact = dir_ / name;
  std::error_code ec;
  if (std::filesystem::exists(exact, ec)) {
    return exact;
  }
  const std::string wanted = upper(name);
  for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
    if (upper(entry.path().filename().string()) == wanted) {
      return entry.path();
    }
  }
  return exact;
}

std::optional<std::vector<std::uint8_t>> SaveDir::read(std::string_view name) const {
  const std::filesystem::path own = own_path(name);
  std::error_code ec;
  if (std::filesystem::is_regular_file(own, ec)) {
    return read_file(own);
  }
  if (install_ != nullptr) {
    if (auto data = install_->try_read(install_dir_ + std::string(name))) {
      return data;
    }
  }
  return std::nullopt;
}

bool SaveDir::write(std::string_view name, std::span<const std::uint8_t> data) const {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  return write_file_atomic(own_path(name), data);
}

std::vector<std::string> SaveDir::list(std::string_view extension) const {
  std::vector<std::string> names;
  const std::string ext = upper(extension);
  std::error_code ec;
  if (std::filesystem::is_directory(dir_, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
      std::string name = upper(entry.path().filename().string());
      if (entry.is_regular_file() && name.ends_with(ext)) {
        names.push_back(std::move(name));
      }
    }
  }
  if (install_ != nullptr) {
    std::string dir = install_dir_;
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) {
      dir.pop_back();
    }
    for (const std::string& path : install_->list(dir, ext)) {
      names.push_back(path.substr(path.rfind('/') + 1));
    }
  }
  std::ranges::sort(names);
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

void SaveDir::remove(std::string_view name) const {
  std::error_code ec;
  std::filesystem::remove(own_path(name), ec);
  if (install_ != nullptr && install_->exists(install_dir_ + std::string(name))) {
    write(name, {});
  }
}

// GOPTION.SAV

std::vector<std::uint8_t> GameOptions::to_bytes() const {
  const std::array values{auto_torp_load, auto_sj_radar,   auto_sd_radar,  sound_effects,
                          engine_sounds,  speech,          collision_alarm, effects_volume,
                          speech_volume,  auto_red_lights, chart_follow_sub};
  return file_image(kVersion, values);
}

bool GameOptions::read_from(std::span<const std::uint8_t> data) {
  // The version must be 3, so the fields of versions 2 and 3 are always
  // read (the original's version tests after it are dead code).
  const std::array fields{&auto_torp_load, &auto_sj_radar,   &auto_sd_radar,  &sound_effects,
                          &engine_sounds,  &speech,          &collision_alarm, &effects_volume,
                          &speech_volume,  &auto_red_lights, &chart_follow_sub};
  return read_fields(data, kVersion, fields);
}

GameOptions GameOptions::load(const SaveDir& dir) { return load_or_create(dir, GameOptions{}); }

// GDETAIL.SAV

std::vector<std::uint8_t> DetailOptions::to_bytes() const {
  const std::array values{static_cast<std::int32_t>(detail_bits), torpedo_wakes, ship_wakes};
  return file_image(kVersion, values);
}

bool DetailOptions::read_from(std::span<const std::uint8_t> data) {
  auto bits = static_cast<std::int32_t>(detail_bits);
  const std::array fields{&bits, &torpedo_wakes, &ship_wakes};
  if (!read_fields(data, kVersion, fields)) {
    return false;
  }
  detail_bits = static_cast<std::uint32_t>(bits);
  return true;
}

DetailOptions DetailOptions::load(const SaveDir& dir) { return load_or_create(dir, DetailOptions{}); }

// REALISM.SAV

namespace {

// One value of REALISM.SAV: its bit position and width in the options word,
// in file order.
struct RealismField {
  int shift;
  std::uint32_t mask;
};

constexpr std::array<RealismField, 12> kRealismFields{{
    {2, 3},   // combat level
    {4, 3},   // sailing model
    {7, 1},   // limited visibility
    {9, 1},   // unlimited fuel
    {10, 1},  // unlimited battery
    {11, 1},  // unlimited ammo
    {12, 1},  // limited depth data off
    {13, 1},  // realistic reloads
    {14, 1},  // dud torpedoes
    {15, 1},  // invulnerable
    {6, 1},   // realistic charts off
    {8, 1},   // run aground
}};

}  // namespace

std::vector<std::uint8_t> RealismFile::to_bytes(std::uint32_t options) {
  std::array<std::int32_t, kRealismFields.size()> values{};
  for (std::size_t i = 0; i < kRealismFields.size(); ++i) {
    values[i] = static_cast<std::int32_t>((options >> kRealismFields[i].shift) & kRealismFields[i].mask);
  }
  return file_image(kVersion, values);
}

bool RealismFile::read_into(std::span<const std::uint8_t> data, std::uint32_t& options) {
  if (data.empty() || data[0] != kVersion) {
    return false;
  }
  ByteReader r(data.subspan(1));
  for (const RealismField& f : kRealismFields) {
    if (r.remaining() < 4) {
      break;
    }
    const std::uint32_t v = r.u32();
    options = (options & ~(f.mask << f.shift)) | ((v & f.mask) << f.shift);
  }
  return true;
}

void RealismFile::load(const SaveDir& dir, std::uint32_t& options) {
  options = (options & ~option::kRealismMask) | kDefaultBits;
  const std::optional<std::vector<std::uint8_t>> data = dir.read(kFileName);
  if (!data || !read_into(*data, options)) {
    save(dir, options);
  }
}

// SMISSION.SAV

std::vector<std::uint8_t> EncounterSettings::to_bytes() const {
  const std::array values{year,  month, time,      weather, sea,          convoy_size, escort,
                          enemy, crew,  air_cover, fog,     warship_type, patrol_size, special_mission};
  return file_image(kVersion, values);
}

bool EncounterSettings::read_from(std::span<const std::uint8_t> data) {
  const std::array fields{&year,  &month, &time,      &weather, &sea,          &convoy_size, &escort,
                          &enemy, &crew,  &air_cover, &fog,     &warship_type, &patrol_size, &special_mission};
  return read_fields(data, kVersion, fields);
}

EncounterSettings EncounterSettings::load(const SaveDir& dir) { return load_or_create(dir, EncounterSettings{}); }

bool EncounterSettings::date_in_war() const {
  return year <= WarWindow::kEndYear && (year != WarWindow::kEndYear || month <= WarWindow::kEndMonth) &&
         WarWindow::kStartYear <= year && (year != WarWindow::kStartYear || WarWindow::kStartMonth <= month);
}

}  // namespace sh

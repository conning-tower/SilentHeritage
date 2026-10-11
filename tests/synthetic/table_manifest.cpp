#include "synthetic/table_manifest.h"

#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "core/byte_reader.h"

namespace sh::test {

namespace {

std::string quote(std::string_view text) {
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

std::string num(std::int64_t v) { return std::to_string(v); }

}  // namespace

const std::string& TableRecord::text(std::string_view key) const {
  static const std::string kEmpty;
  const auto it = fields.find(key);
  return it == fields.end() ? kEmpty : it->second;
}

std::int64_t TableRecord::number(std::string_view key) const {
  const std::string& t = text(key);
  return t.empty() ? 0 : std::stoll(t);
}

TableManifest extract_table_manifest(const Install& install) {
  const GameTables tables = GameTables::load(install);
  TableManifest out;
  const auto add = [&out](std::string table, std::map<std::string, std::string, std::less<>> fields) {
    out.push_back({std::move(table), std::move(fields)});
  };
  for (const ShipClass& s : tables.ships) {
    add("SHIPS.DAT", {{"index", s.index_name},
                      {"name", s.name},
                      {"type", num(static_cast<int>(s.ship_type))},
                      {"role", num(static_cast<int>(s.role))},
                      {"sprite1", s.sprite1},
                      {"sprite2", s.sprite2},
                      {"idbook", s.idbook_picture},
                      {"start_year", num(s.start_year)},
                      {"start_month", num(s.start_month)},
                      {"end_year", num(s.end_year)},
                      {"end_month", num(s.end_month)},
                      {"armed", num(s.main_gun_count > 0 ? 1 : 0)}});
  }
  for (const SubClass& s : tables.subs) {
    add("SUBS.DAT", {{"index", s.index_name},
                     {"name", s.name},
                     {"display", s.display_name},
                     {"class_index", num(s.class_index)},
                     {"picture", num(s.picture_frame)},
                     {"silhouette", s.sprite_silhouette},
                     {"profile", s.sprite_profile},
                     {"intro_year", num(s.intro_year)},
                     {"intro_month", num(s.intro_month)},
                     {"end_year", num(s.end_year)},
                     {"end_month", num(s.end_month)},
                     // The career's use of the class (+0x58: 0 none, 1 or 2)
                     // and its last month (+0x5c, +0x60).
                     {"career", num(static_cast<std::int32_t>(read_u32le(s.raw, 0x58)))},
                     {"career_end_month", num(static_cast<std::int32_t>(read_u32le(s.raw, 0x5c)))},
                     {"career_end_year", num(static_cast<std::int32_t>(read_u32le(s.raw, 0x60)))}});
  }
  for (const AircraftType& a : tables.aircraft) {
    add("AIRCRAFT.DAT", {{"index", a.index_name},
                         {"name", a.name},
                         {"long_name", a.long_name},
                         {"role", num(static_cast<int>(a.role))},
                         {"sprite", a.sprite},
                         {"intro_year", num(a.intro_year)},
                         {"end_year", num(a.end_year)}});
  }
  for (const Torpedo& t : tables.weapons.torpedoes) {
    add("WEAPONS.DAT", {{"torpedo", t.short_name},
                        {"long_name", t.long_name},
                        {"id", num(t.id)},
                        {"nation", num(static_cast<int>(t.nation))},
                        {"year", num(t.year)},
                        {"month", num(t.month)}});
  }
  for (const HomePort& p : tables.home_ports) {
    add("HOMEPORT.DAT", {{"name", p.name},
                         {"command", num(static_cast<int>(p.command))},
                         {"open", std::format("{}/{}/{}", p.open_date.year, p.open_date.month, p.open_date.day)},
                         {"close", std::format("{}/{}/{}", p.close_date.year, p.close_date.month, p.close_date.day)}});
  }
  for (const PatrolZone& z : parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT")) {
    add("PZ.DAT", {{"name", z.name}, {"world", z.world}, {"routes", z.routes}, {"bases", z.bases},
                   {"command", num(static_cast<int>(z.command))}});
  }
  return out;
}

std::string format_table_manifest(const TableManifest& manifest) {
  std::ostringstream out;
  out << "# The identifiers of the original's tables (tests/synthetic/table_manifest.h): names, kinds, dates.\n";
  for (const TableRecord& r : manifest) {
    out << r.table;
    for (const auto& [k, v] : r.fields) {
      out << ' ' << k << '=' << quote(v);
    }
    out << '\n';
  }
  return out.str();
}

TableManifest parse_table_manifest(std::string_view text) {
  TableManifest out;
  std::istringstream in{std::string(text)};
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || line.starts_with('#')) {
      continue;
    }
    TableRecord r;
    std::size_t i = line.find(' ');
    r.table = line.substr(0, i);
    while (i != std::string::npos && i < line.size()) {
      const std::size_t eq = line.find('=', i);
      if (eq == std::string::npos || eq + 1 >= line.size() || line[eq + 1] != '"') {
        throw std::runtime_error("table manifest: bad line " + line);
      }
      const std::string key = line.substr(i + 1, eq - i - 1);
      std::string value;
      std::size_t j = eq + 2;
      for (; j < line.size() && line[j] != '"'; ++j) {
        if (line[j] == '\\' && j + 3 < line.size()) {
          value += static_cast<char>(std::stoi(line.substr(j + 2, 2), nullptr, 16));
          j += 3;
        } else {
          value += line[j];
        }
      }
      r.fields.emplace(key, value);
      i = j + 1 < line.size() ? j + 1 : std::string::npos;
    }
    out.push_back(std::move(r));
  }
  return out;
}

const TableManifest& committed_table_manifest() {
  static const TableManifest manifest = [] {
    const std::filesystem::path path = std::filesystem::path(SH_SYNTHETIC_DIR) / "manifest" / "tables.txt";
    std::ifstream in(path);
    if (!in) {
      throw std::runtime_error("cannot read " + path.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parse_table_manifest(text.str());
  }();
  return manifest;
}

std::vector<const TableRecord*> table_records(std::string_view table) {
  std::vector<const TableRecord*> out;
  for (const TableRecord& r : committed_table_manifest()) {
    if (r.table == table) {
      out.push_back(&r);
    }
  }
  return out;
}

}  // namespace sh::test

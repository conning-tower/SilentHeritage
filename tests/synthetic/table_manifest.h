// The identifiers of the original's game tables: the class, weapon, port
// and zone names (historical names the code and the tests look up), their
// kinds (ship type, role, nation) and service dates, without their numbers
// (speeds, sizes, strengths, weights), which the synthetic install
// generates by rules. Extracted by "[.synthetic_manifest]" into
// tests/synthetic/manifest/tables.txt, one record per line:
//
//   SHIPS.DAT index="FUSO" name="FUSO" type=3 role=2 ...
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/install.h"

namespace sh::test {

struct TableRecord {
  std::string table;  // e.g. "SHIPS.DAT"
  std::map<std::string, std::string, std::less<>> fields;

  // Returns field `key` (an empty string when absent), or as a number.
  const std::string& text(std::string_view key) const;
  std::int64_t number(std::string_view key) const;

  bool operator==(const TableRecord&) const = default;
};

using TableManifest = std::vector<TableRecord>;

// Returns the identifiers of the tables of `install`.
TableManifest extract_table_manifest(const Install& install);

std::string format_table_manifest(const TableManifest& manifest);
TableManifest parse_table_manifest(std::string_view text);

// Returns the committed manifest (tests/synthetic/manifest/tables.txt), and
// its records of `table`.
const TableManifest& committed_table_manifest();
std::vector<const TableRecord*> table_records(std::string_view table);

}  // namespace sh::test

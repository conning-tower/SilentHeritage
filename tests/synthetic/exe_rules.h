// The tables of the executables the synthetic ones generate instead of
// keeping the original's values (synthetic/exe_manifest.h): the
// fixed-point math tables from their formulas, and the gameplay tables
// from rules of their own.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "synthetic/exe_manifest.h"

namespace sh::test {

// Returns the generated ranges of SH.EXE ("sh_exe") or SE.EXE ("se_exe").
std::vector<ExeManifest::Rule> exe_rules(std::string_view exe);

// Fills `out` (the rule's bytes) for rule `name`; throws
// `std::runtime_error` for an unknown rule or a size it cannot fill.
void fill_exe_rule(std::string_view name, std::span<std::uint8_t> out);

}  // namespace sh::test

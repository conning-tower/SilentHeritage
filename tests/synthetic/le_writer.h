// A DOS/4GW-style executable (an MZ stub bound to a Watcom LE module)
// holding the synthetic image of an exe manifest (synthetic/exe_manifest.h)
// at its linear addresses, with a fixup record for each pointer, which
// `ExeImage` loads like the original's.
#pragma once

#include <cstdint>
#include <vector>

#include "synthetic/exe_manifest.h"

namespace sh::test {

// Returns the executable of `manifest`: its strings, pointers, values and
// rules (synthetic/exe_rules.h) placed in its objects; every other byte 0.
std::vector<std::uint8_t> write_le_executable(const ExeManifest& manifest);

}  // namespace sh::test

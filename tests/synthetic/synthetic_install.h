// A complete fake install of the game made by code: the files of the
// manifest's structure (synthetic/manifest.h) filled with procedural
// content, so that the game and the tests run without the original's
// files. No byte comes from the original game.
//
//   const Install& install = synthetic_install();          // in memory, cached
//   write_install_files(synthetic_install_files({}), dir);  // or on disk
#pragma once

#include <cstdint>

#include "core/install.h"
#include "synthetic/install_files.h"

namespace sh::test {

struct SyntheticOptions {
  std::uint64_t seed = 1;
};

// Returns the files of the synthetic install for `options`.
InstallFiles synthetic_install_files(const SyntheticOptions& options);

// Returns the synthetic install of the default options, built once per
// process, or read from the directory SH_SYNTHETIC_INSTALL names (written by
// make_synthetic_install with the default seed: CTest writes it once for
// all the test processes).
const Install& synthetic_install();

}  // namespace sh::test

// The password key of SE.EXE, for the tests that encode scenario passwords.
#pragma once

#include "assets/scn_file.h"
#include "editor/se_strings.h"
#include "core/install.h"

namespace sh::test {

// Returns the key read from the SE.EXE of `install`.
inline ScnPasswordKey se_password_key(const Install& install) {
  return read_scn_password_key(editor::SeExe::load(install).image());
}

}  // namespace sh::test

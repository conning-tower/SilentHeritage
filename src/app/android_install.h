// The game install embedded in the Android package (android/build_apk.sh):
// the original files stored uncompressed under the APK's "bundle" asset
// directory, listed by the "bundle_index.txt" asset (the index of
// `Install::packaged`), read in place through the NDK's asset manager.
#pragma once

#include <optional>

#include "core/install.h"

namespace sh {

// Returns the install of the APK, nothing when the APK embeds none (no
// index: the assets-free package, whose game files come from the asset
// setup). Needs SDL's Android activity (call from SDL_main). Throws
// `std::runtime_error` when the asset manager cannot be opened,
// `DataError` on a malformed index.
std::optional<Install> apk_install();

}  // namespace sh

// The Android side of the asset setup (app/install_setup.h): the system
// folder picker and the folders opened through the Storage Access Framework
// as `Install::DocumentTree`s, through the static methods of
// android/java/.../GameActivity.java. Need SDL's Android activity
// (call from SDL_main).
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "core/install.h"

namespace sh {

// Shows the system folder picker and waits until it is closed; returns the
// tree URI chosen (its read permission persisted), nothing when cancelled.
// Throws `std::runtime_error` when the activity cannot be reached.
std::optional<std::string> pick_android_folder();

// Returns the document tree of `tree_uri`. Throws `DataError` when it is
// not a tree URI; listing and reading throw it when the folder is no longer
// accessible.
std::shared_ptr<const Install::DocumentTree> android_document_tree(const std::string& tree_uri);

}  // namespace sh

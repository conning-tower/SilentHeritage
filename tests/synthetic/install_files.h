// Installs held in memory (the synthetic install, synthetic_install.h) and
// an install whose reads are logged, to learn which files the game and the
// tests read.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/install.h"

namespace sh::test {

// The files of an install by path ('/' separated, as the game spells them,
// e.g. "DATA/SUBS.DAT").
using InstallFiles = std::map<std::string, std::vector<std::uint8_t>>;

// Returns an install reading `files` (which it keeps), described as `name`.
Install install_from_files(std::shared_ptr<const InstallFiles> files, std::string name);

// Returns the install in host directory `root` whose file reads append
// their path (one per line, upper case) to `log`.
Install traced_install(const std::filesystem::path& root, const std::filesystem::path& log);

// Writes `files` under directory `dir` (created); throws
// `std::runtime_error` when a file cannot be written.
void write_install_files(const InstallFiles& files, const std::filesystem::path& dir);

}  // namespace sh::test

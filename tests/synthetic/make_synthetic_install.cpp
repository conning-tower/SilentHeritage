// Writes the synthetic install (synthetic_install.h) into a directory, for
// running the game without the original's files:
//
//   make_synthetic_install <dir> [seed]
//   silent_heritage --data <dir> --nointro
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

#include "synthetic/synthetic_install.h"

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: %s <dir> [seed]\n", argv[0]);
    return 2;
  }
  try {
    sh::test::SyntheticOptions options;
    if (argc == 3) {
      options.seed = std::stoull(argv[2]);
    }
    const sh::test::InstallFiles files = sh::test::synthetic_install_files(options);
    sh::test::write_install_files(files, argv[1]);
    std::printf("%zu files written to %s\n", files.size(), argv[1]);
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s: %s\n", argv[0], e.what());
    return 1;
  }
}

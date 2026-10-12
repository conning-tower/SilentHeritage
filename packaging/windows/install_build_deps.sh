#!/usr/bin/env bash
# Installs what the Windows build needs on Ubuntu 24.04 (the release
# workflow's runner, .github/workflows/build.yml), as root: clang and lld
# with the mingw-w64 runtime (cmake/mingw-clang.cmake), CMake and Ninja,
# and for the icon (packaging/build_desktop.sh) librsvg, cairo and the
# Liberation fonts.
#
# Usage: sudo packaging/windows/install_build_deps.sh
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates git cmake ninja-build python3 zip \
  clang lld llvm mingw-w64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
  librsvg2-2 libcairo2 fonts-liberation-sans-narrow fontconfig

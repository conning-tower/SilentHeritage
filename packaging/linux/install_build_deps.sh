#!/usr/bin/env bash
# Installs what the Linux release build needs on Ubuntu 22.04 (the release
# workflow's container, .github/workflows/build.yml), as root: the oldest
# distribution we build on, so that the executable runs on any glibc from
# 2.35 on. LLVM's clang and libc++ come from apt.llvm.org (the code needs
# C++20's <format>, missing from Ubuntu 22.04's libstdc++), CMake from PyPI
# (Ubuntu 22.04 has 3.22). SDL's development packages are those of
# https://wiki.libsdl.org/SDL3/README-linux: SDL opens the libraries at run
# time, the executable depends on none of them. librsvg, cairo and the
# Liberation fonts render the icons (packaging/build_desktop.sh).
#
# The builds then use libc++: set CXXFLAGS=-stdlib=libc++ in the
# environment before configuring (the workflow does).
#
# Usage: packaging/linux/install_build_deps.sh
set -euo pipefail

readonly llvm=20
readonly cmake_version=3.31.6

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates curl gnupg git file patchelf \
  python3 python3-pip ninja-build pkg-config squashfs-tools zip xz-utils \
  librsvg2-2 libcairo2 fonts-liberation fontconfig \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
  libwayland-dev wayland-protocols libdecor-0-dev libdbus-1-dev libibus-1.0-dev libudev-dev

curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --batch --yes --dearmor -o /usr/share/keyrings/llvm.gpg
echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/jammy/ llvm-toolchain-jammy-${llvm} main" \
  > /etc/apt/sources.list.d/llvm.list
apt-get update
apt-get install -y --no-install-recommends "clang-${llvm}" "lld-${llvm}" "libc++-${llvm}-dev" "libc++abi-${llvm}-dev"
for tool in clang clang++ ld.lld; do
  ln -sf "/usr/bin/${tool}-${llvm}" "/usr/local/bin/${tool}"
done

python3 -m pip install --no-cache-dir "cmake==${cmake_version}"

#!/usr/bin/env bash
# Builds the desktop packages of a release, both variants (faithful and
# extended, see CMakeLists.txt), into dist/:
#   linux    SilentHeritage-<version>-linux-<arch>.tar.gz: the executable, its
#            .desktop file and icon, README.txt and the licences; and
#            SilentHeritage-<version>-<arch>.AppImage, the same as a single
#            file (<arch> the host's: x86_64 or aarch64).
#   windows  SilentHeritage-<version>-windows-x64.zip: the executable (its
#            icon, version and manifest in its resources), README.txt and
#            the licences; cross-compiled (cmake/mingw-clang.cmake).
# and likewise SilentHeritageExtended-... for the extended variant. The
# release workflow runs it (.github/workflows/build.yml), on Ubuntu 22.04
# for Linux (packaging/linux/install_build_deps.sh), 24.04 for Windows
# (packaging/windows/install_build_deps.sh).
#
# The executables run from any folder and keep nothing next to themselves:
# the game asks for the install's folder on the first start and keeps its
# saves in the per-user application folder.
#
# Usage: packaging/build_desktop.sh linux|windows
# Environment (defaults in brackets):
#   SH_VERSION         the version, MAJOR.MINOR.PATCH[-suffix] [0.0.0-dev]
#   SOURCE_DATE_EPOCH  the time stamp of the packages' files [the last commit's]
#   SH_APPIMAGE_RUNTIME  the AppImage runtime [downloaded, its SHA-256 checked]
#   SH_CMAKE_ARGS      extra arguments of the CMake configurations
#
# The icons are rendered from android/icon/*.svg as android/build_apk.sh does
# (python3 with the librsvg and cairo libraries, the Liberation Sans Narrow
# font) and made by tools/make_launcher_icon --desktop.
set -euo pipefail

readonly platform="${1:-}"
case "${platform}" in
  linux | windows) ;;
  *)
    echo "usage: $0 linux|windows" >&2
    exit 1
    ;;
esac

readonly root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly version="${SH_VERSION:-0.0.0-dev}"
if [[ -z "${SOURCE_DATE_EPOCH:-}" ]]; then
  SOURCE_DATE_EPOCH="$(git -C "${root}" log -1 --format=%ct)"
fi
export SOURCE_DATE_EPOCH
readonly dist="${root}/dist"
readonly work="${root}/build/package-${platform}"

# The AppImage runtime (https://github.com/AppImage/type2-runtime), pinned:
# its release and the SHA-256 of each architecture's file.
readonly appimage_runtime_release=20251108
declare -A appimage_runtime_sha256=(
  [x86_64]=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
  [aarch64]=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444
)

cmake_extra=()
read -r -a cmake_extra <<< "${SH_CMAKE_ARGS:-}"
readonly cmake_extra

# The icon tool, for the host (as android/build_apk.sh builds it; SDL
# without the display, whose development packages the Windows build lacks).
readonly host_tools="${root}/build/host-tools-desktop"
cmake -S "${root}" -B "${host_tools}" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF
ninja -C "${host_tools}" make_launcher_icon
readonly icon_tool="${host_tools}/tools/make_launcher_icon"

# make_icons <variant> <dir>: the desktop icons of the variant into <dir>
# (icon-<size>.png, icon.ico): the SH plate, and for the extended variant
# its oxblood plate and the "EXT" ribbon (as android/build_apk.sh).
make_icons() {
  local variant="$1" dir="$2"
  local plate_args=() overlay_args=()
  mkdir -p "${dir}"
  if [[ "${variant}" == extended ]]; then
    plate_args=(--css '#plate{fill:#3c1416}')
    python3 "${root}/android/icon/render_svg.py" "${root}/android/icon/extended_ribbon.svg" "${dir}/ribbon.pam" 864
    overlay_args=(--overlay "${dir}/ribbon.pam")
  fi
  python3 "${root}/android/icon/render_svg.py" "${plate_args[@]}" "${root}/android/icon/sh_plate.svg" \
    "${dir}/plate.pam" 864
  "${icon_tool}" --desktop "${dir}" --foreground "${dir}/plate.pam" "${overlay_args[@]}"
  rm -f "${dir}"/*.pam
}

# build <variant> <build dir> [cmake args...]: the game's executable in the
# variant, release build. Sets edition_name, edition_executable and
# edition_app_id (the build's edition.sh).
build() {
  local variant="$1" dir="$2"
  shift 2
  cmake -S "${root}" -B "${dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF \
    -DSH_EDITION=heritage -DSH_VARIANT="${variant}" -DSH_VERSION="${version}" "$@" "${cmake_extra[@]}"
  source "${dir}/edition.sh"
  ninja -C "${dir}" "${edition_executable}"
}

# stage_docs <dir>: the README and the licences of a package.
stage_docs() {
  local dir="$1"
  sed -e "s/@NAME@/${edition_name}/g" -e "s/@VERSION@/${version}/g" -e "s/@EXECUTABLE@/${edition_executable}/g" \
    "${root}/packaging/README-${platform}.txt" > "${dir}/README.txt"
  mkdir -p "${dir}/licenses"
  cp "${root}/LICENSE" "${dir}/licenses/SilentHeritage.txt"
  cp "${sdl_src}/LICENSE.txt" "${dir}/licenses/SDL3.txt"
}

# fix_times <dir>: every file and folder under <dir> stamped SOURCE_DATE_EPOCH.
fix_times() {
  find "$1" -exec touch -h -d "@${SOURCE_DATE_EPOCH}" {} +
}

# package_name <variant>: the packages' base name.
package_name() {
  if [[ "$1" == extended ]]; then
    echo "SilentHeritageExtended-${version}"
  else
    echo "SilentHeritage-${version}"
  fi
}

# appimage_runtime <arch>: the path of the AppImage runtime for <arch>.
appimage_runtime() {
  local arch="$1"
  if [[ -n "${SH_APPIMAGE_RUNTIME:-}" ]]; then
    echo "${SH_APPIMAGE_RUNTIME}"
    return
  fi
  local file="${work}/runtime-${arch}"
  if [[ ! -f "${file}" ]]; then
    curl -fsSL -o "${file}.part" \
      "https://github.com/AppImage/type2-runtime/releases/download/${appimage_runtime_release}/runtime-${arch}"
    echo "${appimage_runtime_sha256[${arch}]}  ${file}.part" | sha256sum -c - >&2
    mv "${file}.part" "${file}"
  fi
  echo "${file}"
}

package_linux() {
  local variant="$1" arch
  arch="$(uname -m)"
  local dir="${work}/${variant}"
  build "${variant}" "${dir}/build" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD
  local name
  name="$(package_name "${variant}")"
  local icons="${dir}/icons"
  make_icons "${variant}" "${icons}"
  local desktop
  desktop="$(sed -e "s/@NAME@/${edition_name}/g" -e "s/@EXECUTABLE@/${edition_executable}/g" \
    -e "s/@APP_ID@/${edition_app_id}/g" "${root}/packaging/linux/app.desktop.in")"

  # The tarball: the executable and, for a desktop menu entry, its .desktop
  # file and icon (README.txt says where to put them).
  local tree="${dir}/${name}-linux-${arch}"
  rm -rf "${tree}"
  mkdir -p "${tree}"
  install -m 755 -s "${dir}/build/src/app/${edition_executable}" "${tree}/"
  echo "${desktop}" > "${tree}/${edition_app_id}.desktop"
  cp "${icons}/icon-256.png" "${tree}/${edition_app_id}.png"
  stage_docs "${tree}"
  fix_times "${tree}"
  tar -C "${dir}" --sort=name --owner=0 --group=0 --numeric-owner --mode='go-w' -cf - "$(basename "${tree}")" |
    gzip -9 -n > "${dist}/${name}-linux-${arch}.tar.gz"

  # The AppImage: an AppDir (the executable in usr/bin, AppRun its link,
  # the .desktop file and icon at the top and in usr/share) as a squashfs
  # after the runtime.
  local appdir="${dir}/AppDir"
  rm -rf "${appdir}"
  mkdir -p "${appdir}/usr/bin" "${appdir}/usr/share/applications" "${appdir}/usr/share/doc/${edition_executable}"
  install -m 755 -s "${dir}/build/src/app/${edition_executable}" "${appdir}/usr/bin/"
  ln -s "usr/bin/${edition_executable}" "${appdir}/AppRun"
  echo "${desktop}" > "${appdir}/usr/share/applications/${edition_app_id}.desktop"
  ln -s "usr/share/applications/${edition_app_id}.desktop" "${appdir}/${edition_app_id}.desktop"
  local size
  for size in 16 24 32 48 64 128 256; do
    mkdir -p "${appdir}/usr/share/icons/hicolor/${size}x${size}/apps"
    cp "${icons}/icon-${size}.png" "${appdir}/usr/share/icons/hicolor/${size}x${size}/apps/${edition_app_id}.png"
  done
  ln -s "usr/share/icons/hicolor/256x256/apps/${edition_app_id}.png" "${appdir}/${edition_app_id}.png"
  ln -s "${edition_app_id}.png" "${appdir}/.DirIcon"
  stage_docs "${appdir}/usr/share/doc/${edition_executable}"
  fix_times "${appdir}"
  rm -f "${dir}/image.squashfs"
  # (mksquashfs stamps every file and the image SOURCE_DATE_EPOCH.)
  mksquashfs "${appdir}" "${dir}/image.squashfs" -root-owned -noappend -comp zstd -quiet
  local appimage="${dist}/${name}-${arch}.AppImage"
  cat "$(appimage_runtime "${arch}")" "${dir}/image.squashfs" > "${appimage}"
  chmod 755 "${appimage}"
}

package_windows() {
  local variant="$1"
  local dir="${work}/${variant}"
  local icons="${dir}/icons"
  make_icons "${variant}" "${icons}"
  build "${variant}" "${dir}/build" -DCMAKE_TOOLCHAIN_FILE="${root}/cmake/mingw-clang.cmake" \
    -DSH_WINDOWS_ICON="${icons}/icon.ico"
  local name
  name="$(package_name "${variant}")"
  local tree="${dir}/${name}-windows-x64"
  rm -rf "${tree}"
  mkdir -p "${tree}"
  llvm-strip -o "${tree}/${edition_executable}.exe" "${dir}/build/src/app/${edition_executable}.exe" 2>/dev/null ||
    x86_64-w64-mingw32-strip -o "${tree}/${edition_executable}.exe" "${dir}/build/src/app/${edition_executable}.exe"
  stage_docs "${tree}"
  # Windows line endings for the texts, read with Notepad.
  find "${tree}" -name '*.txt' -exec sed -i 's/\r*$/\r/' {} +
  fix_times "${tree}"
  local zip="${dist}/${name}-windows-x64.zip"
  rm -f "${zip}"
  (
    cd "${dir}"
    find "$(basename "${tree}")" | LC_ALL=C sort | TZ=UTC zip -q -X -9 "${zip}" -@
  )
}

mkdir -p "${dist}" "${work}"
for variant in faithful extended; do
  sdl_src="${work}/${variant}/build/_deps/sdl3-src"
  "package_${platform}" "${variant}"
done
ls -l "${dist}"

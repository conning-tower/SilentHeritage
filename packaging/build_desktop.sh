#!/usr/bin/env bash
# Builds the desktop packages of a release, both variants (classic and
# extended, see CMakeLists.txt), into dist/:
#   linux    SilentHeritage-<version>-linux-<arch>.tar.gz: the executable, its
#            .desktop file and icon, README.txt and the licences; and
#            SilentHeritage-<version>-linux-<arch>.AppImage, the same as a single
#            file (<arch> the host's: x86_64 or aarch64).
#   windows  SilentHeritage-<version>-windows-x64.zip: the executable (its
#            icon, version and manifest in its resources), README.txt and
#            the licences; cross-compiled (cmake/mingw-clang.cmake).
#   macos    SilentHeritage-<version>-macos.dmg: the app (one universal
#            executable, arm64 and x86_64, for macOS 13.3 and later; its
#            icon; README.txt and the licences in its resources), ad-hoc
#            signed, next to a link to /Applications and README.txt.
#   icons    no package: the desktop icons of both variants, into
#            build/desktop-icons/<variant>/ (for SH_ICONS_DIR).
# and likewise SilentHeritageExtended-... for the extended variant. The
# release workflow runs it (.github/workflows/build.yml), on Ubuntu 22.04
# for Linux (packaging/linux/install_build_deps.sh), 24.04 for Windows
# (packaging/windows/install_build_deps.sh) and the icons, macOS 15 for
# macOS (with the icons made on Linux: SH_ICONS_DIR).
#
# The executables run from any folder and keep nothing next to themselves:
# the game asks for the install's folder on the first start and keeps its
# saves in the per-user application folder.
#
# Usage: packaging/build_desktop.sh linux|windows|macos|icons
# Environment (defaults in brackets):
#   SH_VERSION         the version, MAJOR.MINOR.PATCH[-suffix] [0.0.0-dev]
#   SOURCE_DATE_EPOCH  the time stamp of the packages' files [the last commit's]
#   SH_APPIMAGE_RUNTIME  the AppImage runtime [downloaded, its SHA-256 checked]
#   SH_CMAKE_ARGS      extra arguments of the CMake configurations
#   SH_ICONS_DIR       the desktop icons already made (`icons`), in
#                      <dir>/<variant>/ [made here]
#
# The icons are rendered from android/icon/*.svg as android/build_apk.sh does
# (python3 with the librsvg and cairo libraries, the Liberation Sans Narrow
# font) and made by tools/make_launcher_icon --desktop.
#
# It runs with macOS's bash 3.2: no associative arrays, and the arrays that
# can be empty are expanded as ${a[@]+"${a[@]}"} (set -u).
set -euo pipefail

readonly platform="${1:-}"
case "${platform}" in
  linux | windows | macos | icons) ;;
  *)
    echo "usage: $0 linux|windows|macos|icons" >&2
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
appimage_runtime_sha256() {
  case "$1" in
    x86_64) echo 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d ;;
    aarch64) echo 00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444 ;;
  esac
}

# The oldest macOS the app runs on (libc++'s <format>).
readonly macos_minimum=13.3

cmake_extra=()
read -r -a cmake_extra <<< "${SH_CMAKE_ARGS:-}"
readonly cmake_extra

# The icon tool, for the host (as android/build_apk.sh builds it; SDL
# without the display, whose development packages the Windows build lacks),
# built (or brought up to date) on first use.
readonly host_tools="${root}/build/host-tools-desktop"
readonly icon_tool="${host_tools}/tools/make_launcher_icon"
icon_tool_built=false
build_icon_tool() {
  if [[ "${icon_tool_built}" == false ]]; then
    cmake -S "${root}" -B "${host_tools}" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF \
      -DSDL_WAYLAND=OFF
    ninja -C "${host_tools}" make_launcher_icon
    icon_tool_built=true
  fi
}

# make_icons <variant> <dir>: the desktop icons of the variant into <dir>
# (icon-<size>.png, icon.ico, icon.icns): the SH plate, and for the extended
# variant its oxblood plate and the "EXT" ribbon (as android/build_apk.sh),
# rendered at 1536 px for the 1024 px of the .icns; or those of
# SH_ICONS_DIR.
make_icons() {
  local variant="$1" dir="$2"
  mkdir -p "${dir}"
  if [[ -n "${SH_ICONS_DIR:-}" ]]; then
    cp "${SH_ICONS_DIR}/${variant}/"* "${dir}/"
    return
  fi
  build_icon_tool
  local plate_args=() overlay_args=()
  if [[ "${variant}" == extended ]]; then
    plate_args=(--css '#plate{fill:#3c1416}')
    python3 "${root}/android/icon/render_svg.py" "${root}/android/icon/extended_ribbon.svg" "${dir}/ribbon.pam" 1536
    overlay_args=(--overlay "${dir}/ribbon.pam")
  fi
  python3 "${root}/android/icon/render_svg.py" ${plate_args[@]+"${plate_args[@]}"} \
    "${root}/android/icon/sh_plate.svg" "${dir}/plate.pam" 1536
  "${icon_tool}" --desktop "${dir}" --foreground "${dir}/plate.pam" ${overlay_args[@]+"${overlay_args[@]}"}
  rm -f "${dir:?}"/*.pam
}

# build <variant> <build dir> [cmake args...]: the game's executable in the
# variant, release build. Sets edition_name, edition_executable and
# edition_app_id (the build's edition.sh).
build() {
  local variant="$1" dir="$2"
  shift 2
  cmake -S "${root}" -B "${dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF \
    -DSH_EDITION=heritage -DSH_VARIANT="${variant}" -DSH_VERSION="${version}" "$@" \
    ${cmake_extra[@]+"${cmake_extra[@]}"}
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

# fix_times <dir>: every file and folder under <dir> stamped SOURCE_DATE_EPOCH
# (GNU date, else BSD date).
fix_times() {
  local stamp
  stamp="$(date -u -d "@${SOURCE_DATE_EPOCH}" +%Y%m%d%H%M.%S 2>/dev/null ||
    date -u -r "${SOURCE_DATE_EPOCH}" +%Y%m%d%H%M.%S)"
  find "$1" -exec env TZ=UTC touch -h -t "${stamp}" {} +
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
    echo "$(appimage_runtime_sha256 "${arch}")  ${file}.part" | sha256sum -c - >&2
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
  local appimage="${dist}/${name}-linux-${arch}.AppImage"
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

package_macos() {
  local variant="$1"
  local dir="${work}/${variant}"
  local icons="${dir}/icons"
  make_icons "${variant}" "${icons}"
  build "${variant}" "${dir}/build" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${macos_minimum}"
  local name
  name="$(package_name "${variant}")"

  # The app: the executable, its Info.plist and icon, the README and the
  # licences in its resources.
  local stage="${dir}/dmg"
  rm -rf "${stage:?}"
  local app="${stage}/${edition_name}.app"
  mkdir -p "${app}/Contents/MacOS" "${app}/Contents/Resources"
  cp "${dir}/build/src/app/${edition_executable}" "${app}/Contents/MacOS/"
  strip -x "${app}/Contents/MacOS/${edition_executable}"
  cp "${icons}/icon.icns" "${app}/Contents/Resources/AppIcon.icns"
  # CFBundleShortVersionString and CFBundleVersion: numbers only.
  sed -e "s/@NAME@/${edition_name}/g" -e "s/@EXECUTABLE@/${edition_executable}/g" \
    -e "s/@APP_ID@/${edition_app_id}/g" -e "s/@VERSION@/${version%%-*}/g" \
    -e "s/@MINIMUM_SYSTEM@/${macos_minimum}/g" "${root}/packaging/macos/Info.plist.in" > "${app}/Contents/Info.plist"
  printf 'APPL????' > "${app}/Contents/PkgInfo"
  stage_docs "${app}/Contents/Resources"
  # The disk image's window: the app, where to drag it, the README.
  cp "${app}/Contents/Resources/README.txt" "${stage}/README.txt"
  ln -s /Applications "${stage}/Applications"
  fix_times "${stage}"
  # Ad-hoc signed (no certificate): Apple Silicon runs no unsigned code.
  codesign --force --deep --sign - "${app}"
  codesign --verify --deep --strict "${app}"
  local dmg="${dist}/${name}-macos.dmg"
  rm -f "${dmg}"
  hdiutil create -quiet -volname "${edition_name} ${version}" -srcfolder "${stage}" -fs HFS+ -format UDZO "${dmg}"
}

package_icons() {
  make_icons "$1" "${root}/build/desktop-icons/$1"
}

mkdir -p "${dist}" "${work}"
for variant in classic extended; do
  sdl_src="${work}/${variant}/build/_deps/sdl3-src"
  "package_${platform}" "${variant}"
done
ls -l "${dist}"

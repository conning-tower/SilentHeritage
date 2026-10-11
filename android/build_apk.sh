#!/usr/bin/env bash
# Builds the debug-signed Android packages (arm64-v8a), without Gradle: the
# steps Bazel's rules_android run (CMake for the native library, javac, d8,
# aapt2, zipalign, apksigner). One package per edition (the native library
# built with SH_EDITION, see CMakeLists.txt), signed with the same debug key:
#   build/android/SilentHeritage-debug.apk  Silent Heritage
#       (com.conningtower.silentheritage): no game files, the asset setup
#       asks for the install's folder on the first start
#       (app/install_setup.h); the SH round plate icon
#   build/android/SilentHunter-debug.apk    Silent Hunter
#       (com.conningtower.silenthunter): the original game files embedded;
#       the icon cut from the game's art. A local testing build only: it
#       contains the original game's files, never distribute it.
#
# With --variant=extended, the packages are the extended variant's
# (SH_VARIANT): build/android/SilentHeritageExtended-debug.apk
# (com.conningtower.silentheritage.extended), and likewise for Silent Hunter,
# installed next to the faithful ones. Their launcher icons carry the
# extended mark, a brass "EXT" ribbon (android/icon/extended_ribbon.svg)
# drawn over the edition's icon, whatever --icon says; the SH plate is
# oxblood instead of dark green.
#
# Usage: android/build_apk.sh [--edition=heritage|hunter] [--variant=faithful|extended] [--icon=game|sh-plate]
#   --edition  the edition's package to build [heritage]; --edition=hunter
#              builds the local Silent Hunter testing package
#   --variant  the variant built [faithful]
#   --icon     the launcher icon of every package built, instead of each
#              edition's own; made at build time by tools/make_launcher_icon
#              (built for the host in build/host-tools): `game` cuts it from
#              the install's ART/RTNPORT.PCX; `sh-plate` renders
#              android/icon/sh_plate.svg (needs python3
#              with the librsvg and cairo libraries, and the Liberation Sans
#              Narrow font, as does the extended variant's ribbon)
# Environment (defaults in brackets):
#   ANDROID_HOME  the Android SDK (required; ANDROID_SDK_ROOT is accepted
#                 too), holding platforms/android-36 and build-tools/36.1.0
#   ANDROID_NDK_HOME  the NDK [$ANDROID_HOME/ndk/29.0.14206865]
#   JAVA_HOME     a JDK 17+ [the JDK of the javac in PATH]
#   SH_DATA       the original install embedded in the Silent Hunter package
#                 and the source of the game-art launcher icon (required by
#                 those only)
#   SH_KEYSTORE   the debug keystore, created if missing (a standard
#                 Android debug key) [debug.keystore at the repository root,
#                 git-ignored]
#   SH_VERSION_CODE, SH_VERSION_NAME  the package version [1, 0.1]
#   SH_CMAKE_ARGS extra arguments of the native library's CMake configuration
set -euo pipefail

editions=(heritage)
variant=faithful
icon_override=
for arg in "$@"; do
  case "${arg}" in
    --edition=heritage | --edition=hunter) editions=("${arg#--edition=}") ;;
    --variant=faithful | --variant=extended) variant="${arg#--variant=}" ;;
    --icon=game | --icon=sh-plate) icon_override="${arg#--icon=}" ;;
    *)
      echo "usage: $0 [--edition=heritage|hunter] [--variant=faithful|extended] [--icon=game|sh-plate]" >&2
      exit 1
      ;;
  esac
done
readonly editions variant icon_override

# The packaging of edition $1: whether the game files are embedded, the
# launcher icon, the package's file name. Its name and applicationId come
# from the native build's edition.sh (CMakeLists.txt, SH_EDITION).
edition_assets() { [[ "$1" == hunter ]]; }
edition_icon() {
  if [[ -n "${icon_override}" ]]; then
    echo "${icon_override}"
  elif [[ "$1" == hunter ]]; then
    echo game
  else
    echo sh-plate
  fi
}
edition_apk() {
  local suffix=
  if [[ "${variant}" == extended ]]; then
    suffix=Extended
  fi
  case "$1" in
    heritage) echo "SilentHeritage${suffix}-debug.apk" ;;
    hunter) echo "SilentHunter${suffix}-debug.apk" ;;
  esac
}

readonly reimpl="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly sdk="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "${sdk}" ]]; then
  echo "build_apk.sh: set ANDROID_HOME to the Android SDK" >&2
  exit 1
fi
readonly platform_jar="${sdk}/platforms/android-36/android.jar"
readonly build_tools="${sdk}/build-tools/36.1.0"
readonly ndk="${ANDROID_NDK_HOME:-${sdk}/ndk/29.0.14206865}"
readonly ndk_bin="${ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [[ -z "${JAVA_HOME:-}" ]] && command -v javac > /dev/null; then
  JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")"
fi
export JAVA_HOME="${JAVA_HOME:-}"
export PATH="${JAVA_HOME}/bin:${PATH}"
readonly data="${SH_DATA:-}"
readonly keystore="${SH_KEYSTORE:-${reimpl}/debug.keystore}"
readonly version_code="${SH_VERSION_CODE:-1}"
readonly version_name="${SH_VERSION_NAME:-0.1}"
readonly min_sdk=24
readonly target_sdk=36

readonly android="${reimpl}/android"
readonly out="${reimpl}/build/android"
readonly native="${out}/native"
readonly stage="${out}/stage"
# The extended variant's launcher icon: the extra arguments of
# make_launcher_icon (the ribbon drawn over any edition's icon, rendered
# before step 1's native build) and of render_svg.py for the SH plate (its colour).
variant_icon_args=()
variant_plate_args=()
if [[ "${variant}" == extended ]]; then
  variant_icon_args=(--overlay "${stage}/extended_ribbon.pam")
  variant_plate_args=(--css '#plate{fill:#3c1416}')
fi
readonly variant_icon_args variant_plate_args
# SDL's sources: those of the Linux build when present, else CMake fetches them.
sdl_src="${reimpl}/build/linux/_deps/sdl3-src"

# The install is read for the embedded game files and the game-art icon.
required=("${platform_jar}" "${build_tools}/aapt2" "${ndk}/build/cmake/android.toolchain.cmake"
  "${JAVA_HOME}/bin/javac")
for e in "${editions[@]}"; do
  if edition_assets "${e}" || [[ "$(edition_icon "${e}")" == game ]]; then
    if [[ -z "${data}" ]]; then
      echo "build_apk.sh: set SH_DATA to the original install (edition ${e}, icon $(edition_icon "${e}"))" >&2
      exit 1
    fi
    required+=("${data}/SH.EXE")
  fi
done
for f in "${required[@]}"; do
  if [[ ! -e "${f}" ]]; then
    echo "build_apk.sh: missing ${f}" >&2
    exit 1
  fi
done
for tool in ninja rsync zip clang++; do
  if ! command -v "${tool}" > /dev/null; then
    echo "build_apk.sh: ${tool} not found" >&2
    exit 1
  fi
done

# 0. The launcher icon tool, for the host.
readonly host_tools="${reimpl}/build/host-tools"
host_args=()
if [[ -d "${sdl_src}" ]]; then
  host_args+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl_src}")
fi
cmake -S "${reimpl}" -B "${host_tools}" -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF "${host_args[@]}"
ninja -C "${host_tools}" make_launcher_icon
readonly icon_tool="${host_tools}/tools/make_launcher_icon"

# build_native <edition>: the native library of the edition in the variant,
# the game with SDL linked in, as libmain.so (one build directory,
# reconfigured: only the files of the edition's identity are rebuilt). Sets edition_name,
# edition_executable and edition_app_id.
build_native() {
  local cmake_args
  read -r -a cmake_args <<< "${SH_CMAKE_ARGS:-}"
  if [[ -d "${sdl_src}" ]]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_SDL3=${sdl_src}")
  fi
  cmake -S "${reimpl}" -B "${native}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${reimpl}/cmake/android-arm64.cmake" -DSH_ANDROID_NDK="${ndk}" \
    -DCMAKE_BUILD_TYPE=Release -DSH_BUILD_TESTS=OFF -DSH_SANITIZE=OFF -DSH_EDITION="$1" -DSH_VARIANT="${variant}" "${cmake_args[@]}"
  source "${native}/edition.sh"
  ninja -C "${native}" "${edition_executable}"
}

# 1. Java: SDL's activity and ours, compiled to classes.dex (the same in
# every package; the first native build fetches SDL's sources if needed).
rm -rf "${stage}"
mkdir -p "${stage}/classes"
# The extended variant's ribbon (variant_icon_args), for every package.
if [[ "${variant}" == extended ]]; then
  python3 "${android}/icon/render_svg.py" "${android}/icon/extended_ribbon.svg" "${stage}/extended_ribbon.pam" 864
fi
build_native "${editions[0]}"
if [[ ! -d "${sdl_src}" ]]; then
  sdl_src="${native}/_deps/sdl3-src"
fi
mapfile -t java_sources < <(find "${sdl_src}/android-project/app/src/main/java" "${android}/java" -name '*.java')
"${JAVA_HOME}/bin/javac" -source 8 -target 8 -bootclasspath "${platform_jar}" -Xlint:-options -nowarn \
  -d "${stage}/classes" "${java_sources[@]}"
mapfile -t classes < <(find "${stage}/classes" -name '*.class')
"${build_tools}/d8" --release --min-api "${min_sdk}" --lib "${platform_jar}" --output "${stage}" "${classes[@]}"

if [[ ! -f "${keystore}" ]]; then
  mkdir -p "$(dirname "${keystore}")"
  "${JAVA_HOME}/bin/keytool" -genkeypair -keystore "${keystore}" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
fi

for edition in "${editions[@]}"; do
  if [[ "${edition}" != "${editions[0]}" ]]; then
    build_native "${edition}"
  fi
  dir="${stage}/${edition}"
  mkdir -p "${dir}/lib/arm64-v8a" "${dir}/res"
  cp "${stage}/classes.dex" "${dir}/"
  "${ndk_bin}/llvm-strip" --strip-unneeded -o "${dir}/lib/arm64-v8a/libmain.so" "${native}/src/app/libmain.so"
  stored=(lib/arm64-v8a/libmain.so)

  # 2. The game files (app/android_install.h), read in place from the APK:
  # the install without the DOS programs other than SH.EXE and SE.EXE (read
  # for their data), the sound drivers, Windows' desktop.ini files and the
  # player's SAVE folder (saves, options, best missions: a fresh package
  # starts with none; the game writes its own saves in the app's storage),
  # and their index (sizes and paths).
  if edition_assets "${edition}"; then
    mkdir -p "${dir}/assets/bundle"
    rsync -a --exclude='[Dd][Ee][Ss][Kk][Tt][Oo][Pp].[Ii][Nn][Ii]' --exclude='/[Ss][Aa][Vv][Ee]/' \
      --include='/SH.EXE' --include='/SE.EXE' --exclude='*.EXE' \
      --exclude='*.DIG' --exclude='*.DLL' --exclude='/AILDRVR.LST' --exclude='/DIG.INI' --exclude='/MDI.INI' \
      --exclude='/SAMPLE.AD' --exclude='/SAMPLE.OPL' --exclude='/BREAK.LOG' \
      "${data}/" "${dir}/assets/bundle/"
    {
      echo "SH-BUNDLE 2"
      (cd "${dir}/assets/bundle" && find . -type f -printf '%s %P\n' | LC_ALL=C sort -k2)
    } > "${dir}/assets/bundle_index.txt"
    stored+=(assets)
  fi

  # 3. Resources: the edition's name and the launcher icon's PNGs (none is
  # stored in the repository). The adaptive icon (res/mipmap-anydpi-v26,
  # API 26+) takes a full-bleed 108 dp foreground, of which the launcher
  # masks about the central 72 dp, and a monochrome layer (themed icons);
  # below API 26, the legacy 48 dp icons are that central part, square and
  # round. The manifest's package (the Java package) becomes the edition's
  # applicationId.
  cp -r "${android}/res/." "${dir}/res/"
  mkdir -p "${dir}/res/values"
  printf '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <string name="app_name">%s</string>\n</resources>\n' \
    "${edition_name}" > "${dir}/res/values/strings.xml"
  case "$(edition_icon "${edition}")" in
    game)
      "${icon_tool}" --res "${dir}/res" --install "${data}" "${variant_icon_args[@]}"
      ;;
    sh-plate)
      python3 "${android}/icon/render_svg.py" "${variant_plate_args[@]}" "${android}/icon/sh_plate.svg" \
        "${dir}/icon.pam" 864
      "${icon_tool}" --res "${dir}/res" --foreground "${dir}/icon.pam" "${variant_icon_args[@]}"
      rm "${dir}/icon.pam"
      ;;
  esac
  "${build_tools}/aapt2" compile --dir "${dir}/res" -o "${dir}/res.zip"
  "${build_tools}/aapt2" link -o "${dir}/unsigned.apk" -I "${platform_jar}" \
    --manifest "${android}/AndroidManifest.xml" --rename-manifest-package "${edition_app_id}" \
    --min-sdk-version "${min_sdk}" --target-sdk-version "${target_sdk}" \
    --version-code "${version_code}" --version-name "${version_name}" \
    "${dir}/res.zip"

  # 4. The package: classes.dex, the native library (loaded from the APK in
  # place: extractNativeLibs="false") and the game files, both stored
  # uncompressed, aligned and signed.
  (
    cd "${dir}"
    zip -q unsigned.apk classes.dex
    zip -q -0 -r -D unsigned.apk "${stored[@]}"
  )
  "${build_tools}/zipalign" -f -P 16 4 "${dir}/unsigned.apk" "${dir}/aligned.apk"
  apk="${out}/$(edition_apk "${edition}")"
  "${build_tools}/apksigner" sign --ks "${keystore}" --ks-pass pass:android --key-pass pass:android \
    --ks-key-alias androiddebugkey --out "${apk}" "${dir}/aligned.apk"
  echo "${apk}: ${edition_name} (${edition_app_id})"
done
rm -rf "${stage}"

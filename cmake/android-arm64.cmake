# Cross-compiles the game for Android (arm64-v8a, API level 24 and up) with
# the NDK's clang: src/app then builds libmain.so for SDL's Java activity.
# Usage (android/build_apk.sh does it):
#   cmake -B build/android-native -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/android-arm64.cmake \
#     -DSH_ANDROID_NDK=<sdk>/ndk/<version>
# The NDK may also be given by the ANDROID_NDK_HOME environment variable.
if(SH_ANDROID_NDK)
  # Seen by the try_compile projects, which re-read this file.
  set(ENV{SH_ANDROID_NDK} "${SH_ANDROID_NDK}")
elseif(DEFINED ENV{SH_ANDROID_NDK})
  set(SH_ANDROID_NDK "$ENV{SH_ANDROID_NDK}")
elseif(DEFINED ENV{ANDROID_NDK_HOME})
  set(SH_ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
else()
  message(FATAL_ERROR "android-arm64.cmake: set SH_ANDROID_NDK (or ANDROID_NDK_HOME) to the NDK directory")
endif()

set(ANDROID_ABI arm64-v8a)
set(ANDROID_PLATFORM android-24)
set(ANDROID_STL c++_static)
include("${SH_ANDROID_NDK}/build/cmake/android.toolchain.cmake")

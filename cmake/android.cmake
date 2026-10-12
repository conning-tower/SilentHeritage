# Cross-compiles the game for Android (API level 24 and up) with the NDK's
# clang, for one ABI: arm64-v8a (the default) or x86_64 (SH_ANDROID_ABI).
# src/app then builds libmain.so for SDL's Java activity.
# Usage (android/build_apk.sh does it, once per ABI of the package):
#   cmake -B build/android-native -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/android.cmake \
#     -DSH_ANDROID_NDK=<sdk>/ndk/<version> [-DSH_ANDROID_ABI=x86_64]
# The NDK may also be given by the ANDROID_NDK_HOME environment variable.
if(SH_ANDROID_NDK)
  # Seen by the try_compile projects, which re-read this file.
  set(ENV{SH_ANDROID_NDK} "${SH_ANDROID_NDK}")
elseif(DEFINED ENV{SH_ANDROID_NDK})
  set(SH_ANDROID_NDK "$ENV{SH_ANDROID_NDK}")
elseif(DEFINED ENV{ANDROID_NDK_HOME})
  set(SH_ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
else()
  message(FATAL_ERROR "android.cmake: set SH_ANDROID_NDK (or ANDROID_NDK_HOME) to the NDK directory")
endif()
if(SH_ANDROID_ABI)
  set(ENV{SH_ANDROID_ABI} "${SH_ANDROID_ABI}")
elseif(DEFINED ENV{SH_ANDROID_ABI})
  set(SH_ANDROID_ABI "$ENV{SH_ANDROID_ABI}")
else()
  set(SH_ANDROID_ABI arm64-v8a)
endif()
if(NOT SH_ANDROID_ABI MATCHES "^(arm64-v8a|x86_64)$")
  message(FATAL_ERROR "android.cmake: SH_ANDROID_ABI must be arm64-v8a or x86_64, not '${SH_ANDROID_ABI}'")
endif()

set(ANDROID_ABI ${SH_ANDROID_ABI})
set(ANDROID_PLATFORM android-24)
set(ANDROID_STL c++_static)
include("${SH_ANDROID_NDK}/build/cmake/android.toolchain.cmake")

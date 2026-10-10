# Build-time tools, for the host only (android/build_apk.sh builds them in
# build/host-tools).

# The Android launcher icon from the install's art (a library for the tests).
sh_add_module(sh_launcher_icon
  SOURCES launcher_icon.cpp
  DEPS sh_core sh_assets)
# Its header is included as "tools/launcher_icon.h".
target_include_directories(sh_launcher_icon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_executable(make_launcher_icon make_launcher_icon.cpp)
target_link_libraries(make_launcher_icon PRIVATE sh_launcher_icon)

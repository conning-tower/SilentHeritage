# The asset viewer and the command line, a library so the tests
# can use its parts.
sh_add_module(sh_viewer
  SOURCES
    # The command line of the game's executable.
    command_line.cpp
    # The pictures of the floating command button, from the install.
    command_icons.cpp
    # The live state the touch command panel shows (extended variant).
    panel_status.cpp
    # The remembered source of the game files and the start-up choice.
    install_source.cpp
    draw_util.cpp
    palettes.cpp
    sound_system.cpp
    ui_text.cpp
    viewer.cpp
    view_fonts.cpp
    view_maps.cpp
    view_messages.cpp
    view_pictures.cpp
    view_skies.cpp
    view_sounds.cpp
    view_sprites.cpp
    view_videos.cpp
  DEPS sh_core sh_gfx sh_assets sh_audio sh_platform sh_game)
target_link_libraries(sh_viewer PRIVATE SDL3::SDL3-static)

# The game, named after the edition (SH_EDITION): silent_heritage or
# silent_hunter.
if(ANDROID)
  # SDL's Java activity loads the program as libmain.so and calls SDL_main.
  add_library(${SH_EDITION_EXECUTABLE} SHARED
    # The install embedded in the APK.
    android_install.cpp
    # The asset setup's folder picker and Storage Access Framework folders.
    android_documents.cpp
    game_app.cpp
    install_setup.cpp
    main.cpp)
  set_target_properties(${SH_EDITION_EXECUTABLE} PROPERTIES OUTPUT_NAME main)
  target_link_libraries(${SH_EDITION_EXECUTABLE} PRIVATE android)
else()
  add_executable(${SH_EDITION_EXECUTABLE}
    game_app.cpp
    install_setup.cpp
    main.cpp)
endif()
if(WIN32)
  # A windowed program (no console window; app/main.cpp attaches to the
  # console it is started from), with its icon, version and manifest.
  set(SH_WINDOWS_ICON "" CACHE FILEPATH
    "The executable's icon (.ico), made by tools/make_launcher_icon --desktop; none when empty")
  set(SH_WINDOWS_ICON_RESOURCE "")
  if(SH_WINDOWS_ICON)
    set(SH_WINDOWS_ICON_RESOURCE "1 ICON \"${SH_WINDOWS_ICON}\"")
  endif()
  set(SH_WINDOWS_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/windows.manifest)
  configure_file(windows.rc.in ${CMAKE_CURRENT_BINARY_DIR}/windows.rc @ONLY)
  target_sources(${SH_EDITION_EXECUTABLE} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/windows.rc)
  set_target_properties(${SH_EDITION_EXECUTABLE} PROPERTIES WIN32_EXECUTABLE TRUE)
  set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/windows.rc APPEND PROPERTY OBJECT_DEPENDS
    ${SH_WINDOWS_MANIFEST} ${SH_WINDOWS_ICON})
endif()
target_link_libraries(${SH_EDITION_EXECUTABLE} PRIVATE sh_core sh_gfx sh_assets sh_audio sh_platform sh_sim sh_ui sh_game
  sh_viewer sh_editor SDL3::SDL3-static)

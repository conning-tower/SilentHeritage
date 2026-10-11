# Tests of the scenario editor library, one block per module.
# Game files come from SH_DATA as for sh_tests.
add_executable(sh_editor_tests
  # Loads SH.EXE (or the math tables) before the tests.
  ../game_exe_setup.cpp
  ../temp_dir_setup.cpp
  # Lossless .SCN files.
  scn_file_test.cpp
  # SBOPTION.SAV.
  options_test.cpp
  # Object model and scenario document.
  scenario_test.cpp
  # Settings and selection dialogs.
  environment_test.cpp
  loadout_test.cpp
  dialogs_settings_test.cpp
  class_sheets_test.cpp
  dialogs_select_test.cpp
  dialogs_file_test.cpp
  dialogs_patrol_test.cpp
  # The chart screen.
  chart_screen_test.cpp
  # The toolbox and its object panels.
  toolbox_test.cpp
  # The File menu and the editor program.
  file_commands_test.cpp
  editor_test.cpp
  # QA sessions through the whole editor.
  session_test.cpp
  robustness_test.cpp
  # The screens of the DOSBox captures.
  dosbox_screens_test.cpp
)
target_include_directories(sh_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sh_editor_tests PRIVATE sh_test_support sh_editor Catch2::Catch2WithMain)
# The dialog tests' screenshots (platform/screenshot).
target_link_libraries(sh_editor_tests PRIVATE sh_platform)
if(NOT CMAKE_CROSSCOMPILING)
  include(Catch)
  catch_discover_tests(sh_editor_tests)
endif()

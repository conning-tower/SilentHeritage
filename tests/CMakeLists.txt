# The tests' own library: in-memory and traced installs, and the synthetic
# install (synthetic/).
add_library(sh_test_support STATIC
  synthetic/install_files.cpp
  # The writers of the game's file formats.
  synthetic/encoders.cpp
  synthetic/table_encoders.cpp
  # The structure of the original's files.
  synthetic/manifest.cpp
  # The synthetic install and its generators.
  synthetic/procedural.cpp
  synthetic/synthetic_install.cpp
  synthetic/gen_tables.cpp
  synthetic/gen_art.cpp
  synthetic/gen_world.cpp
  synthetic/gen_media.cpp
  synthetic/gen_scenarios.cpp
  synthetic/gen_misc.cpp
  # The synthetic executables.
  synthetic/exe_manifest.cpp
  synthetic/exe_rules.cpp
  synthetic/le_writer.cpp
)
target_include_directories(sh_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sh_test_support PRIVATE SH_SYNTHETIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/synthetic")
# The synthetic install written to a directory: make_synthetic_install <dir>.
add_executable(make_synthetic_install synthetic/make_synthetic_install.cpp)
target_link_libraries(make_synthetic_install PRIVATE sh_test_support)
target_link_libraries(sh_test_support PUBLIC sh_core sh_assets)

# Unit tests, one block per area. Tests needing the original game files read
# the install from the SH_DATA environment variable and are skipped when it is
# unset (game_data.h).
add_executable(sh_tests
  # Loads SH.EXE (or the math tables) before the tests.
  game_exe_setup.cpp
  # A temporary directory per test run.
  temp_dir_setup.cpp
  core_test.cpp
  fixed_math_test.cpp
  # Graphics formats and drawing.
  gfx_assets_test.cpp
  # World formats.
  world_assets_test.cpp
  # Game tables.
  tables_test.cpp
  # Sound and video.
  video_test.cpp
  audio_test.cpp
  # The SDL sound output reopened when the system loses it.
  audio_device_test.cpp
  # Simulation.
  sim_clock_test.cpp
  sim_geometry_test.cpp
  sim_motion_test.cpp
  sim_player_test.cpp
  # Weapons and damage.
  sim_weapons_test.cpp
  sim_damage_test.cpp
  # Sensors and ship AI.
  sim_sensors_test.cpp
  sim_ai_test.cpp
  sonar_reacquire_test.cpp
  # Aircraft and enemy submarines.
  sim_air_test.cpp
  # Simulation regressions (draw order, original quirks).
  sim_review_test.cpp
  # The aircraft, enemy submarines and air reports in the views and logs.
  airview_test.cpp
  # Sky, light and weather.
  sim_sky_test.cpp
  # 3D world view.
  view_test.cpp
  # UI and game.
  ui_test.cpp
  # Layout-aware keyboard words.
  keys_layout_test.cpp
  mission_test.cpp
  # Control-room stations and the mission screen (command bar, conn, chart,
  # gauges, status, captain's quarters).
  stations_control_test.cpp
  crew_log_test.cpp
  stations_control_chart_test.cpp
  chart_groups_test.cpp
  chart_weapons_test.cpp
  # User-reported chart regressions.
  chart_bugs_test.cpp
  stations_control_gauges_test.cpp
  stations_control_status_test.cpp
  stations_control_cabin_test.cpp
  # Outside and attack stations (bridge, periscope, TBT, deck gun, TDC, radar).
  stations_attack_test.cpp
  stations_tdc_test.cpp
  stations_deckgun_test.cpp
  stations_radar_test.cpp
  # Station regressions (deck gun bar, redraws, conn periscope).
  stations_review_test.cpp
  # The recognition manual.
  idbook_test.cpp
  # The front end: menus, option files, popup menus.
  frontend_test.cpp
  # Single-mission screens: sub selection, results, best missions.
  frontend2_test.cpp
  # The MISSION BRIEFING / PATROL BRIEFING screen.
  briefing_test.cpp
  # The main menu's INTERVIEWS and SUB TOUR.
  frontend_extras_test.cpp
  # The single-mission scenario generator.
  scengen_test.cpp
  # The career: saves, rules and awards, the patrol, the career screens.
  career_save_test.cpp
  career_rules_test.cpp
  career_contacts_test.cpp
  career_patrol_test.cpp
  career_letters_test.cpp
  career_menus_test.cpp
  # Main program and asset viewer.
  app_test.cpp
  # The asset setup: remembered source, folder checks, document trees.
  install_setup_test.cpp
  # Integration: a mission with every station, end to end.
  integration_test.cpp
  # Gameplay QA: regressions of bugs found playing the game.
  qa_test.cpp
  aground_test.cpp
  sitduck_test.cpp
  # Mission saves: MISSn.SAV, the state lists, the career saved on patrol.
  mission_rules_test.cpp
  sim_rules_test.cpp
  mission_save_test.cpp
  # Photo reconnaissance: the target in view, the photo, the mission's end.
  recon_test.cpp
  # Late ports: recon marker, debris, fuel needle, screen shake, FPS.
  gaps_test.cpp
  # I/O regressions: atomic saves, parser bounds, audio reads outside the lock.
  io_review_test.cpp
  # Game regressions: career resume, route groups, damaged saves, chart view.
  game_review_test.cpp
  # Touch screens and suspension: soft keyboard text, the resume snapshot.
  resume_test.cpp
  # The floating command button of touch screens and its icons.
  command_panel_test.cpp
  # The extended variant's controls: view drags, precise dials, the live
  # command panel.
  controls_gesture_test.cpp
  controls_stations_test.cpp
  controls_panel_test.cpp
  # Modern conveniences: the chart's wider edge scroll, Alt+Enter fullscreen.
  chart_edge_scroll_test.cpp
  display_fullscreen_test.cpp
  # The Android launcher icon tool (tools/).
  launcher_icon_test.cpp
  # Golden traces of the faithful game (tests/golden/).
  golden_trace.cpp
  golden_rig.cpp
  golden_game_rig.cpp
  golden_test.cpp
  golden_game_test.cpp
  # The synthetic install: its format writers.
  synthetic_encoders_test.cpp
  synthetic_manifest_test.cpp
  synthetic_install_test.cpp
)
# The committed golden traces, recorded in place with SH_GOLDEN_RECORD.
target_compile_definitions(sh_tests PRIVATE SH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
target_link_libraries(sh_tests PRIVATE sh_test_support sh_core sh_gfx sh_assets sh_audio sh_sim sh_ui sh_game
  Catch2::Catch2WithMain)
# Main program and asset viewer.
target_link_libraries(sh_tests PRIVATE sh_platform sh_viewer)
# 3D world view.
target_link_libraries(sh_tests PRIVATE sh_view)
# The Android launcher icon tool.
target_link_libraries(sh_tests PRIVATE sh_launcher_icon)
# The golden traces of the whole program run the scenario editor.
target_link_libraries(sh_tests PRIVATE sh_editor)
# UI and game: the BIOS key tests name SDL keycodes.
target_link_libraries(sh_tests PRIVATE SDL3::Headers)
if(NOT CMAKE_CROSSCOMPILING)
  include(Catch)
  catch_discover_tests(sh_tests)
endif()

# The scenario editor: its own executable, sh_editor_tests.
add_subdirectory(editor)

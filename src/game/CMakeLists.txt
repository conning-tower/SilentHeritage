sh_add_module(sh_game
  SOURCES
    # Data directories (SHPATH.INI).
    shpath.cpp
    # Mission integration.
    class_params.cpp
    headless.cpp
    mission.cpp
    mission_loop.cpp
    screen_shake.cpp
    torpedo_load.cpp
    mission_session.cpp
    # Control-room stations and the in-mission screen host: the command
    # bar, conn, chart, gauges, status, captain's quarters.
    station_env.cpp
    command_bar.cpp
    mission_screen.cpp
    engine_sounds.cpp
    stations/conn.cpp
    stations/chart.cpp
    stations/chart_view.cpp
    stations/chart_panels.cpp
    stations/chart_reports.cpp
    stations/gauges.cpp
    stations/nav_panel.cpp
    stations/status.cpp
    stations/cabin.cpp
    # The radio messages for the captain's radio log.
    radio_room.cpp
    # Outside and attack stations: the 3D view adapter, bridge, periscope,
    # TBT, deck gun, TDC, radar.
    world_view_adapter.cpp
    stations/attack_services.cpp
    stations/tdc.cpp
    stations/deckgun.cpp
    stations/radar.cpp
    stations/bridge.cpp
    stations/scope.cpp
    stations/tbt.cpp
    # The recognition manual (scope, TBT, captain's quarters).
    stations/idbook.cpp
    # The front end: start-up, intro videos, menus, option files.
    frontend.cpp
    frontend_credits.cpp
    frontend_encounter.cpp
    frontend_realism.cpp
    frontend_screen.cpp
    frontend_video.cpp
    # The main menu's INTERVIEWS and SUB TOUR.
    frontend_interviews.cpp
    frontend_subtour.cpp
    # Mission screens: sub selection, briefing, results, best missions.
    frontend_subsel.cpp
    frontend_results.cpp
    frontend_results_screen.cpp
    frontend_briefing.cpp
    frontend_best.cpp
    options_realism.cpp
    saves_options.cpp
    # The single-mission scenario generator.
    scengen.cpp
    # The career: records and saves, rules and awards, the patrol, the
    # career screens.
    career.cpp
    career_save.cpp
    career_boats.cpp
    career_rules.cpp
    patrol_contacts.cpp
    patrol.cpp
    awards.cpp
    career_letters.cpp
    career_letters_screens.cpp
    career_menus.cpp
    career_menus_view.cpp
    career_flow.cpp
    career_patrol_flow.cpp
    # Mission saves: MISSn.SAV, the simulation state lists, the career
    # saved on patrol.
    vehicle_state.cpp
    sim_state.cpp
    mission_save.cpp
    frontend_continue.cpp
    save_commands.cpp
    # The mission layer of the program (its missions and patrols with every
    # station) and the resume snapshot of a suspended game, kept through
    # the system's lifecycle events with their diagnostic log.
    game_runner.cpp
    resume.cpp
    resume_keeper.cpp
    lifecycle_log.cpp
  DEPS sh_core sh_gfx sh_assets sh_audio sh_sim sh_ui sh_view)

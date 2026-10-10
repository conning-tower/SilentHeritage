# The scenario editor SE.EXE as a library (docs/editor-design.md), one
# block per module.
sh_add_module(sh_editor
  SOURCES
    # SBOPTION.SAV.
    options.cpp
    # SE.EXE strings, data, objects and the scenario document.
    se_strings.cpp
    data.cpp
    objects.cpp
    scenario.cpp
    # Settings and selection dialogs.
    editor_ui.cpp
    environment.cpp
    loadout.cpp
    dialogs_boxes.cpp
    dialogs_settings.cpp
    class_sheets.cpp
    dialogs_select.cpp
    dialogs_file.cpp
    dialogs_patrol.cpp
    # The chart screen.
    chart_screen.cpp
    # The File menu and the editor program.
    file_commands.cpp
    editor.cpp
    # The toolbox and its object panels.
    toolbox.cpp
  DEPS sh_assets sh_game)

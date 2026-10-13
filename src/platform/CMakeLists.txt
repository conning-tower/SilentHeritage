sh_add_module(sh_platform
  SOURCES
    display.cpp
    # Whether the system honours the pointer warps.
    pointer_warp.cpp
    screenshot.cpp
    # The lifecycle events SDL hands only to event watches.
    lifecycle_watch.cpp
    # BIOS keyboard words (UI area).
    bios_keys.cpp
    # The floating command button of touch screens.
    command_panel.cpp
    command_panel_view.cpp
  DEPS sh_gfx)
target_link_libraries(sh_platform PRIVATE SDL3::SDL3-static)

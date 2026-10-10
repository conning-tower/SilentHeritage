sh_add_module(sh_ui
  SOURCES
    # Events, mouse and cursor.
    event.cpp
    mouse.cpp
    # The dialog engine and its controls.
    hotspot.cpp
    dialog.cpp
    controls.cpp
    dial_control.cpp
    # Skins and their drawing primitives.
    draw.cpp
    skin.cpp
    default_skin.cpp
    menu_skin.cpp
    # Popup menus and the menu bar hooks.
    popup_menu.cpp
    # The menu bar (the scenario editor's).
    menu_bar.cpp
  DEPS sh_core sh_gfx sh_assets)

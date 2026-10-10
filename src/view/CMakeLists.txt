sh_add_module(sh_view
  SOURCES
    # The 3D world view: camera, render list, palette, sky, sea, terrain, objects.
    backdrop.cpp
    object_draw.cpp
    polygon.cpp
    render_list.cpp
    terrain.cpp
    world_palette.cpp
    world_view.cpp
  DEPS sh_core sh_gfx sh_assets)

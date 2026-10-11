# Parsers for the original file formats, one block per area.
set(SH_ASSETS_GFX_SOURCES
  # Graphics formats: CAT, SHP, PCX, PAL, FNT.
  cat.cpp
  fnt.cpp
  pcx.cpp
  shp.cpp
)
set(SH_ASSETS_WORLD_SOURCES
  # World formats: compressed blocks, WLD, OBS, ENV, SCN (the game's view and
  # the lossless editor model), CR, PZ, map bases.
  bases.cpp
  compressed.cpp
  environment.cpp
  objects.cpp
  patrol_zones.cpp
  routes.cpp
  scenario.cpp
  scn_file.cpp
  world.cpp
)
set(SH_ASSETS_TABLE_SOURCES
  # Game tables: DATA/*.DAT, ephemeris, SH.EXE data.
  crew_messages.cpp
  dat_tables.cpp
  ephemeris.cpp
  exe_image.cpp
  game_exe.cpp
)
set(SH_ASSETS_SOUND_SOURCES
  # Sound formats: MEL, BNK, RAW.
  mel.cpp
  speech.cpp
)
set(SH_ASSETS_VIDEO_SOURCES
  # Smacker video.
  smacker.cpp
)
sh_add_module(sh_assets
  SOURCES
    ${SH_ASSETS_GFX_SOURCES}
    ${SH_ASSETS_WORLD_SOURCES}
    ${SH_ASSETS_TABLE_SOURCES}
    ${SH_ASSETS_SOUND_SOURCES}
    ${SH_ASSETS_VIDEO_SOURCES}
  DEPS sh_core)
if(SH_TRACE_EXE_READS)
  target_compile_definitions(sh_assets PUBLIC SH_TRACE_EXE_READS)
endif()

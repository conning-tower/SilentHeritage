sh_add_module(sh_audio
  SOURCES
    sample_table.cpp
    mel_layer.cpp
    audio_device.cpp
  DEPS sh_core sh_assets)
target_link_libraries(sh_audio PRIVATE SDL3::SDL3-static)

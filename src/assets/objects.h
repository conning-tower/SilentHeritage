// `.OBS` map objects (`DATA/WORLD-1.OBS`): the 3D sprites of the `.WLD`
// landmarks, indexed by `LocationType`. Layout from `world_load_aobj`
// (0x90f2e); docs/formats.md ".OBS".
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>


namespace sh {

// How an object is drawn.
enum class ObjectRenderType : std::uint32_t {
  kBillboard = 0,  // one view, always facing the camera
  kOriented = 1,   // a view per heading (the moored submarine)
};

// One `.OBS` entry.
struct MapObject {
  ObjectRenderType render_type = ObjectRenderType::kBillboard;
  std::int32_t size = 0;   // render size, an integer the renderer shifts into 16.16
  // The object's sprites: an embedded `.SHP` file kept as raw bytes.
  std::vector<std::uint8_t> shp;
};

// Parses a whole `.OBS` file (version 2); `context` names it in errors.
// Throws `DataError` if it is malformed.
std::vector<MapObject> parse_objects(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh

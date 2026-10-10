#include "assets/objects.h"

#include <format>

#include "assets/compressed.h"
#include "assets/format_header.h"
#include "core/byte_reader.h"

namespace sh {
namespace {

constexpr std::uint8_t kObjectsVersion = 2;  // the only version the loader takes
constexpr std::int16_t kMaxObjects = 51;     // the game's table 0x16e190

}  // namespace

std::vector<MapObject> parse_objects(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  if (read_format_header(r, "AOBJ", kObjectsVersion) != kObjectsVersion) {
    r.fail("unsupported AOBJ version");
  }
  const std::int16_t count = r.i16();
  if (count < 0 || count > kMaxObjects) {
    r.fail(std::format("{} objects, the game holds {}", count, kMaxObjects));
  }
  std::vector<MapObject> objects(static_cast<std::size_t>(count));
  std::vector<std::uint32_t> sizes;
  for (MapObject& obj : objects) {
    obj.render_type = static_cast<ObjectRenderType>(r.u32());
    sizes.push_back(r.u32());
    r.skip(4);  // runtime pointer
    obj.size = r.i32();
    r.skip(4);  // +0x10: 0 in the shipped file; `world_load_aobj` loads it, no code reads it
  }
  for (std::size_t i = 0; i < objects.size(); ++i) {
    objects[i].shp = read_compressed(r);
    if (objects[i].shp.size() != sizes[i]) {
      r.fail(std::format("object {} size disagrees with its block", i));
    }
  }
  return objects;
}

}  // namespace sh

#include "editor/se_strings.h"

#include <vector>

namespace sh::editor {

SeExe SeExe::load(const Install& install) {
  const std::vector<std::uint8_t> data = install.read("SE.EXE");
  return SeExe(data);
}

std::string_view SeExe::table_str(std::uint32_t table, std::size_t index, std::size_t stride) const {
  return image_.string_pointer(table + static_cast<std::uint32_t>(index * stride));
}

}  // namespace sh::editor

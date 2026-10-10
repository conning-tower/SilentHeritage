#include "assets/cat.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"

namespace sh {

namespace {

bool iequals(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
  });
}

}  // namespace

CatArchive CatArchive::parse(std::vector<std::uint8_t> data, std::string_view context) {
  CatArchive cat;
  cat.data_ = std::move(data);
  cat.context_ = std::string(context);
  ByteReader r(cat.data_, context);
  const std::uint16_t count = r.u16();
  cat.entries_.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    CatEntry e;
    std::string name = r.fixed_string(12);
    while (!name.empty() && name.back() == ' ') {
      name.pop_back();
    }
    e.name = std::move(name);
    e.flags = r.u16();
    e.timestamp = r.u32();
    e.size = r.u32();
    e.offset = r.u32();
    if (static_cast<std::uint64_t>(e.offset) + e.size > cat.data_.size()) {
      throw DataError(std::format("{}: member {} runs past the end of the archive", context, e.name));
    }
    cat.entries_.push_back(std::move(e));
  }
  return cat;
}

CatArchive CatArchive::load(const Install& install, std::string_view dos_path) {
  return parse(install.read(dos_path), dos_path);
}

std::optional<std::size_t> CatArchive::find(std::string_view name) const {
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (iequals(entries_[i].name, name)) {
      return i;
    }
  }
  return std::nullopt;
}

std::span<const std::uint8_t> CatArchive::member(std::size_t index) const {
  const CatEntry& e = entries_.at(index);
  return std::span(data_).subspan(e.offset, e.size);
}

std::span<const std::uint8_t> CatArchive::member(std::string_view name) const {
  const auto index = find(name);
  if (!index) {
    throw DataError(std::format("{}: no member named {}", context_, name));
  }
  return member(*index);
}

}  // namespace sh

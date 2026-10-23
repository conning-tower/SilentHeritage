// The CRC-32 of ISO 3309, as zip, PNG and ARJ files use it.
#pragma once

#include <cstdint>
#include <span>

namespace sh {

// Returns the CRC-32 of `data`.
std::uint32_t crc32(std::span<const std::uint8_t> data);

}  // namespace sh

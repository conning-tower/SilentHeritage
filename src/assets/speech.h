// Speech clips: headerless unsigned 8-bit mono PCM `.RAW` files in `SPEECH/`
// (crew voices, narrations) with `ALT/` as the fallback directory. The rate is
// not stored: the speech stream plays at `kSpeechRate` (init_sound, 0x10889).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/install.h"

namespace sh {

inline constexpr std::int32_t kSpeechRate = 22050;
inline constexpr std::string_view kSpeechDir = "SPEECH/";
// Directory tried when a clip cannot be opened from the speech directory
// (play_speech_raw, 0x10b8b).
inline constexpr std::string_view kAltSpeechDir = "ALT/";

// Returns the file name of the speech sample `sound` (a crew-message sound
// name such as "2DEEP"): "<sound>.RAW", as crew_message (0x4c843) builds it.
std::string speech_file_name(std::string_view sound);

// Returns the PCM of `file_name` (e.g. "2DEEP.RAW") read from `speech_dir`,
// or from `kAltSpeechDir` when it is not there, or nothing if neither has it.
std::optional<std::vector<std::uint8_t>> read_speech_raw(const Install& install, std::string_view file_name,
                                                         std::string_view speech_dir = kSpeechDir);

}  // namespace sh

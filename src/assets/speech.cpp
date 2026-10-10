#include "assets/speech.h"

namespace sh {

std::string speech_file_name(std::string_view sound) { return std::string(sound) + ".RAW"; }

std::optional<std::vector<std::uint8_t>> read_speech_raw(const Install& install, std::string_view file_name,
                                                         std::string_view speech_dir) {
  for (const std::string_view dir : {speech_dir, kAltSpeechDir}) {
    if (auto data = install.try_read(std::string(dir) + std::string(file_name))) {
      return data;
    }
  }
  return std::nullopt;
}

}  // namespace sh

#include "assets/crew_messages.h"

#include <format>

#include "core/error.h"

namespace sh {

namespace {

constexpr std::uint32_t kRecordSize = 12;
constexpr std::uint32_t kSpeakerRecordSize = 8;
constexpr std::uint8_t kEndId = 0xff;
constexpr std::uint8_t kSpeakerCount = 9;
// Guards against reading a foreign executable forever.
constexpr std::uint32_t kMaxMessages = 256;

}  // namespace

CrewMessages::CrewMessages(const ExeImage& exe) {
  for (std::uint32_t i = 0;; ++i) {
    if (i == kMaxMessages) {
      throw DataError("SH.EXE: crew-message table has no terminator");
    }
    const std::uint32_t address = kTableAddress + i * kRecordSize;
    const std::uint8_t id = exe.u8(address);
    if (id == kEndId) {
      break;
    }
    const std::uint8_t speaker = exe.u8(address + 1);
    if (speaker >= kSpeakerCount) {
      throw DataError(std::format("SH.EXE: crew message {} has speaker {}", id, speaker));
    }
    messages_.push_back(CrewMessage{
        .id = id,
        .speaker = static_cast<CrewSpeaker>(speaker),
        .speaker_name = std::string(exe.string_pointer(kSpeakersAddress + speaker * kSpeakerRecordSize)),
        .sound = std::string(exe.string_pointer(address + 4)),
        .text = std::string(exe.string_pointer(address + 8)),
    });
  }
}

const CrewMessage* CrewMessages::find(std::uint8_t id) const {
  for (const CrewMessage& m : messages_) {
    if (m.id == id) {
      return &m;
    }
  }
  for (const CrewMessage& m : extended_crew_messages()) {
    if (m.id == id) {
      return &m;
    }
  }
  return nullptr;
}

const std::vector<CrewMessage>& extended_crew_messages() {
  static const std::vector<CrewMessage> messages{
      {0x80, CrewSpeaker::kExecutiveOfficer, "Executive Officer", "", "No firing solution, sir!"},
  };
  return messages;
}

}  // namespace sh

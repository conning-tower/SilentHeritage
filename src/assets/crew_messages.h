// The crew-message table of SH.EXE: the spoken and printed crew reports
// ("Up Periscope, aye.") that every `crew_message(id, ...)` call (0x4c843)
// looks up by id.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/exe_image.h"

namespace sh {

// Who speaks a crew message (index into the speaker table 0x108e38).
enum class CrewSpeaker : std::uint8_t {
  kOod = 0,
  kDivingOfficer = 1,
  kHelmsman = 2,
  kPlanesman = 3,
  kSound = 4,
  kRadarOperator = 5,
  kExecutiveOfficer = 6,
  kRadioOperator = 7,
  kDebug = 8,
};

// One crew message. In `text`, the formatter's placeholders are kept as raw
// bytes: 0x01/0x02 the numeric arguments, 0x03 a system name, '\t' a bearing,
// 0x11/0x12 a plural "s" for the first/second argument.
struct CrewMessage {
  std::uint8_t id = 0;
  CrewSpeaker speaker = CrewSpeaker::kOod;
  std::string speaker_name;  // "OOD", "Diving Officer", ...
  std::string sound;         // speech sample stem (SPEECH\<sound>.RAW), empty if none
  std::string text;
};

// The crew-message table in its original order.
class CrewMessages {
 public:
  // Linear address of the first 12-byte record {u8 id, u8 speaker, u16 pad,
  // char* sound, char* text}; the table ends at id 0xff.
  static constexpr std::uint32_t kTableAddress = 0x108a8c;
  // Linear address of the speaker table {char* name, u32 id}[].
  static constexpr std::uint32_t kSpeakersAddress = 0x108e38;

  // Reads the table from the SH.EXE image; throws `DataError` on a speaker
  // index outside the speaker table or a missing terminator.
  explicit CrewMessages(const ExeImage& exe);

  const std::vector<CrewMessage>& all() const { return messages_; }
  // Returns the first message with `id` (as `crew_message` searches), then
  // the extended variant's, or nullptr if there is none (the original then
  // says nothing).
  const CrewMessage* find(std::uint8_t id) const;

 private:
  std::vector<CrewMessage> messages_;
};

// The extended variant's crew messages (not in the original), with ids
// from 0x80, past the original's (0..76): 0x80 "No firing solution, sir!"
// (the TDC has only a bearing for a straight-running torpedo).
const std::vector<CrewMessage>& extended_crew_messages();

}  // namespace sh

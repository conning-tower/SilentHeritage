// The medals, unit citations and letters of the career (module
// campaign/mdlaward): the codes `awards_present` (0x55ecd) takes and what
// `awards_build_citation` (0x56185) shows for each: the picture, the
// speech, the body text and the box it is typed in. The texts are SH.EXE
// strings, given by address.
#pragma once

#include <cstdint>
#include <optional>

#include "game/career.h"

namespace sh {

// The codes of `awards_present`: the medals 0..5 and the unit citations
// 6..7 are the `Award` counters; 100..102 are letters from the command.
enum class AwardCode : std::int32_t {
  kBronzeStar = 0,
  kSilverStar = 1,
  kNavyCross = 2,
  kDsm = 3,
  kMedalOfHonor = 4,
  kAsiaticPacific = 5,
  kNuc = 6,
  kPuc = 7,
  kPoorPatrol = 100,   // POOR PATROL PERFORMANCE
  kPoorFuel = 101,     // POOR FUEL MANAGEMENT
  kRelieved = 102,     // LOSS OF COMMAND
};

// Returns the counter of a medal or citation code, nothing for a letter.
std::optional<Award> award_of(AwardCode code);

// How `awards_build_citation` lays a code out.
enum class CitationLayout : std::uint8_t {
  // The medal citations: "<NAME>" / "COMMANDER" / "UNITED STATES NAVY"
  // (and "(Posthumous)" when deceased), "CITATION:", then the text, over
  // mdlaward.pcx with the medal sprite of AWARDS.SHP.
  kMedal,
  // The unit citations: the citation's title, the boat, "<CLASS> Class
  // Submarine", "UNITED STATES NAVY", "CITATION:", then the text.
  kUnit,
  // The letters: date, "To:" the commander, "From:" the command, "Re:"
  // the subject, "Commander,", then the text.
  kLetter,
};

// What a code shows.
struct AwardPresentation {
  CitationLayout layout = CitationLayout::kMedal;
  std::uint32_t picture = 0;  // SH.EXE address of the PCX name, e.g. "mdlaward.pcx"
  std::uint32_t speech = 0;   // SH.EXE address of the .RAW name read aloud
  std::uint32_t text = 0;     // SH.EXE address of the body text
  // The text box (0x124248..0x124254): x, y, width, height.
  std::int32_t box_x = 0;
  std::int32_t box_y = 0;
  std::int32_t box_width = 0;
  std::int32_t box_height = 0;
  bool medal_sprite = false;  // the medal is drawn over the picture
};

// Returns the presentation of `code`.
AwardPresentation award_presentation(AwardCode code);

}  // namespace sh

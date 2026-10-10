#include "game/awards.h"

namespace sh {

std::optional<Award> award_of(AwardCode code) {
  const auto c = static_cast<std::int32_t>(code);
  if (c >= 0 && c < static_cast<std::int32_t>(kAwardCount)) {
    return static_cast<Award>(c);
  }
  return std::nullopt;
}

AwardPresentation award_presentation(AwardCode code) {
  // awards_build_citation (0x56185)
  constexpr std::uint32_t kMedalPcx = 0x10c330;    // "mdlaward.pcx"
  constexpr std::uint32_t kNavyCitPcx = 0x10c34c;  // "navycit.pcx"
  constexpr std::uint32_t kPresCitPcx = 0x10c340;  // "prescit.pcx"
  const auto medal = [](std::uint32_t speech, std::uint32_t text) {
    return AwardPresentation{.layout = CitationLayout::kMedal,
                             .picture = kMedalPcx,
                             .speech = speech,
                             .text = text,
                             .box_x = 0x151,
                             .box_y = 0x82,
                             .box_width = 0xfc,
                             .box_height = 0xeb,
                             .medal_sprite = true};
  };
  const auto citation = [](CitationLayout layout, std::uint32_t speech, std::uint32_t text) {
    return AwardPresentation{.layout = layout,
                             .picture = kNavyCitPcx,
                             .speech = speech,
                             .text = text,
                             .box_x = 0xc3,
                             .box_y = 0x7d,
                             .box_width = 0xf9,
                             .box_height = 300};
  };
  switch (code) {
    case AwardCode::kBronzeStar:
      return medal(0xf6dec, 0x10bd30);  // BRNZSTAR.RAW
    case AwardCode::kSilverStar:
      return medal(0xf6dc8, 0x10bc2c);  // SEC0003.RAW
    case AwardCode::kNavyCross:
      return medal(0xf6da4, 0x10bb28);  // SEC0002.RAW
    case AwardCode::kDsm:
      return medal(0xf6e11, 0x10be50);  // DSM.RAW
    case AwardCode::kMedalOfHonor:
      return medal(0xf6d80, 0x10b9e0);  // SEC0001.RAW
    case AwardCode::kAsiaticPacific:
      return medal(0xf6e31, 0x10bf68);  // APCM.RAW
    case AwardCode::kNuc:
      return citation(CitationLayout::kUnit, 0xf6e72, 0x10c1f0);  // SEC0004.RAW
    case AwardCode::kPuc:
      return {.layout = CitationLayout::kUnit,
              .picture = kPresCitPcx,
              .speech = 0xf6e52,  // PUC.RAW
              .text = 0x10c088,
              .box_x = 0xc3,
              .box_y = 0xaf,
              .box_width = 0xf9,
              .box_height = 0xf0};
    // The letters' texts: the pointer table 0x10c194 + code x 4.
    case AwardCode::kPoorPatrol:
      return citation(CitationLayout::kLetter, 0xf6e96, 0xf6f04);  // NOATTACK.RAW
    case AwardCode::kPoorFuel:
      return citation(CitationLayout::kLetter, 0xf6ea3, 0xf7058);  // RUNFUEL.RAW
    case AwardCode::kRelieved:
      return citation(CitationLayout::kLetter, 0xf6eaf, 0xf7187);  // RELIEVED.RAW
  }
  return {};
}

}  // namespace sh

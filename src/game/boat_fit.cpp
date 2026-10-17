#include "game/boat_fit.h"

#include <algorithm>

#include "core/byte_reader.h"
#include "sim/state_io.h"

namespace sh {

namespace {

constexpr std::string_view kGatoProfile = "P_GATO.SHP";
constexpr std::string_view kTenchProfile = "P_TENCH.SHP";

}  // namespace

bool fleet_boat_with_one_gun(const SubClass& sub) { return sub.modern_torpedoes && sub.deck_gun_count == 1; }

SubClass fitted_sub_class(const SubClass& sub, const BoatFit& fit) {
  SubClass out = sub;
  if (!fleet_boat_with_one_gun(sub)) {
    return out;
  }
  if (fit.second_gun) {
    out.deck_gun_count = 2;
    out.deck_gun_rounds = std::max(sub.deck_gun_rounds, kTwoGunRounds);
    if (equals_ignore_case(sub.sprite_profile, kGatoProfile)) {
      out.sprite_profile = std::string(kTenchProfile);
    }
  } else if (fit.gun_mount) {
    out.single_gun_mount = *fit.gun_mount != 0 ? 1 : 0;
  }
  return out;
}

std::vector<std::uint8_t> encode_boat_fit(const BoatFit& fit) {
  ByteWriter out;
  out.i32(fit.gun_mount ? static_cast<std::int32_t>(*fit.gun_mount) : -1);
  out.i32(fit.second_gun ? 1 : 0);
  return out.take();
}

BoatFit decode_boat_fit(std::span<const std::uint8_t> data) {
  ByteReader in(data, "boat fit");
  BoatFit fit;
  if (const std::int32_t mount = in.i32(); mount >= 0) {
    fit.gun_mount = mount != 0 ? 1u : 0u;
  }
  fit.second_gun = in.i32() != 0;
  return fit;
}

}  // namespace sh

// The campaign calendar shared by the career tables.
#pragma once

namespace sh {

// Campaign months: index 0 is December 1941, 44 is August 1945.
inline constexpr int kCampaignMonths = 45;

// Returns the campaign-month index of 1-based `month` of `year`; it is
// outside 0..44 outside the war.
constexpr int campaign_month_index(int year, int month) {
  return (year - 1941) * 12 + month - 12;
}

}  // namespace sh

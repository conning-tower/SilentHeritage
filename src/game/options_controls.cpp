#include "game/options_controls.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ui/controls.h"

namespace sh {

void add_feature_control(ui::Dialog& d, int x, int y, int cycle_w, const FeatureOption& option, Features& features,
                         bool touch_panel, bool disabled, std::function<void()> changed) {
  if (option.choices.empty()) {
    auto& box = d.emplace<ui::CheckBox>(x, y, std::string(option.label),
                                        [&features, &option, changed = std::move(changed)](bool flip) {
                                          if (flip) {
                                            option.set(features, option.get(features) == 0 ? 1 : 0);
                                            if (changed) {
                                              changed();
                                            }
                                          }
                                          return option.get(features) != 0;
                                        });
    box.disabled = disabled;
    return;
  }
  if (!touch_panel && !option.desktop_choices.empty()) {
    const std::span<const FeatureChoice> choices = option.desktop_choices;
    std::vector<std::string> labels;
    for (const FeatureChoice& c : choices) {
      labels.emplace_back(c.label);
    }
    auto& cycle = d.emplace<ui::CycleButton>(
        x, y, cycle_w, std::move(labels), [&features, &option, choices, changed = std::move(changed)](bool advance) {
          if (advance) {
            const int value = option.get(features);
            const auto next =
                std::ranges::find_if(choices, [value](const FeatureChoice& c) { return c.value > value; });
            option.set(features, (next != choices.end() ? *next : choices.front()).value);
            if (changed) {
              changed();
            }
          }
          // The last choice not above the value.
          int shown = 0;
          while (shown + 1 < static_cast<int>(choices.size()) && choices[shown + 1].value <= option.get(features)) {
            ++shown;
          }
          return shown;
        });
    cycle.disabled = disabled;
    return;
  }
  auto& cycle = d.emplace<ui::CycleButton>(
      x, y, cycle_w, std::vector<std::string>(option.choices.begin(), option.choices.end()),
      [&features, &option, changed = std::move(changed)](bool advance) {
        if (advance) {
          option.set(features, (option.get(features) + 1) % option.values());
          if (changed) {
            changed();
          }
        }
        return option.get(features);
      });
  cycle.disabled = disabled;
}

void add_setting_check_box(ui::Dialog& d, int x, int y, std::string_view label, bool& value,
                           std::optional<bool> fixed, std::function<void()> changed) {
  const auto toggle = [&value, fixed, changed = std::move(changed)](bool flip) {
    if (fixed) {
      return *fixed;
    }
    if (flip) {
      value = !value;
      if (changed) {
        changed();
      }
    }
    return value;
  };
  auto& box = d.emplace<ui::CheckBox>(x, y, std::string(label), toggle);
  box.disabled = fixed.has_value();
}

}  // namespace sh

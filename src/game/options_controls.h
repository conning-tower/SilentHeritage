// The controls of the extended options pages (not in the original): an
// improvement of `kFeatureOptions` as a check box or a cycle button, and a
// check box of a window setting.
#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "core/features.h"
#include "game/extended_config.h"
#include "ui/dialog.h"

namespace sh {

// Adds the control of `option` at (`x`, `y`) to `d`, editing `features`
// (which must outlive the dialog): a check box, or a cycle button `cycle_w`
// wide, through `option.desktop_choices` unless `touch_panel` (the touch
// command panel exists). `disabled` greys it. `changed` runs after each
// change.
void add_feature_control(ui::Dialog& d, int x, int y, int cycle_w, const FeatureOption& option, Features& features,
                         bool touch_panel, bool disabled, std::function<void()> changed = {});

// Adds a check box labelled `label` at (`x`, `y`) to `d`, editing `value`
// (which must outlive the dialog). With `fixed`, it is greyed and shows
// that value. `changed` runs after each change.
void add_setting_check_box(ui::Dialog& d, int x, int y, std::string_view label, bool& value,
                           std::optional<bool> fixed, std::function<void()> changed = {});

}  // namespace sh

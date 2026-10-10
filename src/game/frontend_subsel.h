// The SUB CLASS selection screen (module subsel, `subsel_screen` 0x72cfb,
// dialog 0x1279a4): the SUBS.DAT classes the screen's filter keeps, each
// with its SUBPICS.SHP picture and its statistics or description. The
// encounters keep the classes in service at the mission date
// (`encounter_sub_class_in_service`); the screen itself is
// `FrontEnd::subsel_screen`.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "assets/dat_tables.h"

namespace sh {

class ExeImage;

// The filter `0x1279a0` of the selection: true keeps a class.
using SubClassFilter = std::function<bool(const SubClass&)>;

// `encounter_sub_class_in_service` (0xa1902): true when the date (`year`
// since 1930, `month` 0-based) lies inside the class's service dates
// (SUBS.DAT +0x48 month and +0x4c year of entry, +0x50 / +0x54 of
// withdrawal, months 1-based), both months included.
bool sub_class_in_service(const SubClass& sub, std::int32_t year, std::int32_t month);

// `subsel_load_subs_dat` (0x72ebe): the classes of `subs` (SUBS.DAT order)
// that `filter` keeps, all of them without a filter.
std::vector<const SubClass*> subsel_classes(std::span<const SubClass> subs, const SubClassFilter& filter);

// One text of the statistics page, at (x, y) of the info panel.
struct SubInfoText {
  int x = 0;
  int y = 0;
  std::string text;
};

// `subsel_draw_statistics` (0x73322): the label and value texts of `sub`
// (displacement, dimensions, machinery, power, speed, test depth, range,
// tubes and guns), with their strings from SH.EXE.
std::vector<SubInfoText> sub_statistics(const SubClass& sub, const ExeImage& exe);

}  // namespace sh

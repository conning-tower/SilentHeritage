// The data of the main menu's INTERVIEWS (module intrview: Bud Gruner's
// videos BUD\<n>.SMK, 4 pages of 4) and SUB TOUR (module subtour: the Gato
// class cutaway B-GRND-1/2.PCX with its labelled parts, pictures and
// narrations) screens, read from SH.EXE, and the slider skin of the
// interview player. The screens themselves are `FrontEnd::interviews_screen`
// and `FrontEnd::subtour_screen`.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/frontend.h"

namespace sh {

class ExeImage;
class ShPaths;

inline constexpr int kInterviewPages = 4;
inline constexpr int kInterviewsPerPage = 4;

// One interview of a page: its button label ("A".."P", the strings of
// 0x11174c) and the caption shown beside it (the 200-byte strings of
// 0x110acc), and its video number (1..16).
struct InterviewEntry {
  std::string button;
  std::string caption;
  int video = 0;
};

// `interviews_set_page` (0xa8abd): the four interviews of `page` (0..3).
std::vector<InterviewEntry> interview_page(const ExeImage& exe, int page);

// Returns the DOS path of interview video `video` (1..16), the
// "%s%d.smk" of `play_interview_video` (0xa8ef1) in the BUD directory,
// e.g. "BUD\7.smk".
std::string interview_video_path(const ShPaths& paths, int video);

// The interview player's hooks (`interviews_install_gui_hooks`, 0xa8e5a):
// the MENU skin whose slider (0x108a00..0x108a18) is a bare progress bar:
// no frame, arrows or focus (empty rectangles), a track 15 pixels high and
// a 5-pixel thumb (0xa8ce7..0xa8e2a).
class InterviewSkin : public FrontendMenuSkin {
 public:
  using FrontendMenuSkin::FrontendMenuSkin;

  ui::Rect slider_frame(const Viewport& gfx, int x, int y, int w, bool disabled) override;
  ui::Rect slider_left_rect(int x, int y, int w) override;
  ui::Rect slider_right_rect(int x, int y, int w) override;
  ui::Rect slider_track_rect(int x, int y, int w) override;
  ui::Rect slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool disabled) override;
  ui::Rect slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool disabled) override;
  ui::Rect slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) override;
};

inline constexpr int kSubTourLabels = 29;
// Labels 0..15 belong to the internal view, 16..28 to the external one;
// the parts that can be clicked are 0..11 and 16..19.
inline constexpr int kSubTourFirstExternal = 16;

// One 0x4c-byte record of the label table 0x10d958, with the part's
// tables 0x10d834 / 0x10d74c / 0x10d7c0 for the first 20.
struct SubTourLabel {
  std::string name;   // e.g. "Forward Torpedo"
  int x = 0;          // +0x28: the text position
  int y = 0;          // +0x2c
  bool part = false;  // +0x30: a part with a picture (the speaker icon)
  int picture = -1;   // +0x34: its SUBTOUR.SHP picture frame
  int highlight = -1;  // +0x38: its SUBTOUR.SHP highlight frame
  int line_x0 = 0;    // +0x3c..+0x48: the line to the part
  int line_y0 = 0;
  int line_x1 = 0;
  int line_y1 = 0;
  // The narration "<name>.RAW" (0x10d834, 10-byte names; empty for a label
  // without a part) and where the highlight frame is drawn (0x10d74c,
  // 0x10d7c0).
  std::string narration;
  int highlight_x = 0;
  int highlight_y = 0;
};

// Returns the 29 labels of the sub tour.
std::vector<SubTourLabel> subtour_labels(const ExeImage& exe);

}  // namespace sh

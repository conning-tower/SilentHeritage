// The structure of the original's files, without their content: picture
// sizes, sprite frame boxes, glyph widths, map and video dimensions, sound
// lengths. Extracted once from an install by the hidden test
// "[.synthetic_manifest]" into tests/synthetic/manifest/files.txt, it tells
// the synthetic install (synthetic_install.h) what shapes to give its own
// procedural content, so that the game's layouts work on it.
//
// The file is text, one file per line ("<path> <kind> <numbers...>"), its
// sprites on indented lines below it:
//
//   ART/CONN.PCX pcx 640 480
//   ART/GUISPR.SHP shp
//     sprite GUISPR.SHP 1.10 2
//       frame -4 -4 3 3 0
//       empty 0 0 -1 -1 0
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/install.h"

namespace sh::test {

struct SpriteManifest {
  struct Frame {
    bool empty = false;
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::uint32_t palette_entries = 0;  // the frame's own colours

    bool operator==(const Frame&) const = default;
  };

  std::string name;
  std::string version;  // "1.10" or "1.00"
  std::vector<Frame> frames;

  bool operator==(const SpriteManifest&) const = default;
};

// One file. `numbers` by kind:
//   pcx  width height
//   fnt  height glyph-widths...
//   cat  (its members in `sprites`, `sprites[i].name` the member name)
//   wld  version scale-shift locations grid-width grid-height chart-width chart-height bounds(0/1)
//        (its coast tiles in `sprites`)
//   obs  per object: render-type size (their sprites in `sprites`)
//   env  version sky-width sky-height
//   smk  width height y-scale frames frame-us, then per audio track: rate channels bits
//   mel  rate rate-max rate-min loop-count volume-offset pcm-bytes
//   bnk  sample ids...
//   any other kind: the size in bytes
struct FileManifest {
  std::string path;
  std::string kind;
  std::vector<std::int64_t> numbers;
  std::vector<SpriteManifest> sprites;

  bool operator==(const FileManifest&) const = default;
};

using Manifest = std::map<std::string, FileManifest>;

// Returns the manifest of the game files of `install` (the directories the
// game reads and the root's data files; not the programs and drivers).
Manifest extract_manifest(const Install& install);

// Returns the manifest text of `manifest`, and parses one; parsing throws
// `std::runtime_error` on a malformed line.
std::string format_manifest(const Manifest& manifest);
Manifest parse_manifest(std::string_view text);

// Returns the committed manifest (tests/synthetic/manifest/files.txt).
const Manifest& committed_manifest();
std::filesystem::path manifest_path();

}  // namespace sh::test

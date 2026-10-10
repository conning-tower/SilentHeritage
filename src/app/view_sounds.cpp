// Viewer category "Sounds": the effects (SFX/<id>.MEL) and the speech clips
// (SPEECH/*.RAW, ALT/*.RAW), with their waveform; Enter plays them through
// the MEL sound layer like the game (`sound_play_sfx`, `play_speech_raw`).

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "assets/mel.h"
#include "assets/speech.h"
#include "core/install.h"

namespace sh {

namespace {

struct SoundItem {
  GameDir dir = GameDir::kSfx;
  std::string file;
  std::int32_t sfx_id = -1;  // for SFX
};

// Draws the u8 PCM `pcm` as a min/max waveform filling the w x h box at
// (x, y) of `vp`.
void draw_waveform(const Viewport& vp, std::span<const std::uint8_t> pcm, int x, int y, int w, int h,
                   std::uint8_t colour, std::uint8_t axis) {
  fill_rect(vp, x, y + h / 2, w, 1, axis);
  if (pcm.empty()) {
    return;
  }
  for (int col = 0; col < w; ++col) {
    const std::size_t begin = pcm.size() * static_cast<std::size_t>(col) / static_cast<std::size_t>(w);
    const std::size_t end =
        std::max(begin + 1, pcm.size() * static_cast<std::size_t>(col + 1) / static_cast<std::size_t>(w));
    const auto [lo, hi] = std::minmax_element(pcm.begin() + static_cast<std::ptrdiff_t>(begin),
                                              pcm.begin() + static_cast<std::ptrdiff_t>(std::min(end, pcm.size())));
    const int top = y + (255 - *hi) * (h - 1) / 255;
    const int bottom = y + (255 - *lo) * (h - 1) / 255;
    fill_rect(vp, x + col, top, 1, bottom - top + 1, colour);
  }
}

class SoundsCategory final : public Category {
 public:
  explicit SoundsCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Sounds"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      list_items();
    }
    return items_.size();
  }

  std::string item_name(std::size_t index) override {
    const SoundItem& item = items_[index];
    return std::string(services_.paths.dir_name(item.dir)) + "/" + item.file;
  }

  void select(std::size_t index) override {
    item_ = items_[index];
    const std::string path = services_.paths.path(item_.dir, item_.file);
    const std::vector<std::uint8_t> data = services_.install.read(path);
    if (item_.dir == GameDir::kSfx) {
      MelSample s = parse_mel(data, path);
      header_ = s.header;
      pcm_ = std::move(s.pcm);
    } else {
      header_.reset();
      pcm_ = data;
    }
  }

  void activate() override {
    if (item_.dir == GameDir::kSfx) {
      services_.sound.play_sfx(item_.sfx_id);
    } else {
      services_.sound.start_speech(read_speech_raw(services_.install, item_.file, services_.paths.dir(item_.dir)));
    }
  }

  void draw(Canvas& canvas) override {
    const UiColours colours = UiColours::pick(canvas.palette);
    const UiText& text = services_.text;
    const int line = text.line_height() + 2;
    int y = 16;
    text.draw(canvas.area, 16, y, item_name_text(), colours.accent);
    y += 2 * line;
    for (const std::string& s : details()) {
      text.draw(canvas.area, 16, y, s, colours.text, canvas.width - 32);
      y += line;
    }
    y += line;
    draw_waveform(canvas.area, pcm_, 16, y, canvas.width - 32, 160, colours.text, colours.dim);
    y += 160 + line;
    text.draw(canvas.area, 16, y, services_.sound.has_device() ? "Enter: play" : "Enter: play (no audio device)",
              colours.dim);
  }

  std::string status() const override {
    return std::format("{} bytes  {:.2f} s", pcm_.size(), static_cast<double>(pcm_.size()) / rate());
  }

 private:
  void list_items() {
    const ShPaths& paths = services_.paths;
    std::vector<SoundItem> effects;
    for (const std::string& path : services_.install.list(paths.dir_name(GameDir::kSfx), ".MEL")) {
      SoundItem item{GameDir::kSfx, path.substr(path.rfind('/') + 1), -1};
      const std::string_view stem = std::string_view(item.file).substr(0, item.file.rfind('.'));
      if (std::from_chars(stem.data(), stem.data() + stem.size(), item.sfx_id).ec == std::errc{}) {
        effects.push_back(std::move(item));
      }
    }
    std::ranges::sort(effects, {}, &SoundItem::sfx_id);
    items_ = std::move(effects);
    for (const GameDir dir : {GameDir::kSpeech, GameDir::kAlt}) {
      for (const std::string& path : services_.install.list(paths.dir_name(dir), ".RAW")) {
        items_.push_back({dir, path.substr(path.rfind('/') + 1), -1});
      }
    }
  }

  std::string item_name_text() const {
    return item_.dir == GameDir::kSfx ? std::format("Sound effect {}", item_.sfx_id) : "Speech " + item_.file;
  }

  double rate() const { return header_ ? header_->nominal_rate() : kSpeechRate; }

  std::vector<std::string> details() const {
    std::vector<std::string> out;
    if (header_) {
      out.push_back(header_->random_rate()
                        ? std::format("Rate: random {}..{} Hz", header_->rate_min, header_->rate_max)
                        : std::format("Rate: {} Hz", header_->rate));
      out.push_back(header_->loop_count < 0 ? std::string("Loops until stopped")
                                            : std::format("Plays {} time(s)", header_->loop_count));
      out.push_back(std::format("Volume offset: {}", header_->volume_offset));
    } else {
      out.push_back(std::format("Rate: {} Hz (speech stream)", kSpeechRate));
    }
    out.push_back(std::format("{} samples, unsigned 8-bit mono", pcm_.size()));
    return out;
  }

  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<SoundItem> items_;
  SoundItem item_;
  std::optional<MelHeader> header_;
  std::vector<std::uint8_t> pcm_;
};

}  // namespace

std::unique_ptr<Category> make_sounds_category(const ViewerServices& services) {
  return std::make_unique<SoundsCategory>(services);
}

}  // namespace sh

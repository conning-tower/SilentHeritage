// Viewer category "Videos": the Smacker cinematics (CINES/*.SMK) and Bud
// Gruner interviews (BUD/*.SMK), played at the decoder's frame rate with the
// first audio track through an SDL audio stream.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/category.h"
#include "app/draw_util.h"
#include "app/names.h"
#include "assets/smacker.h"
#include "core/edition.h"
#include "core/install.h"

namespace sh {

namespace {

// A playback stream for one video's audio track.
class VideoAudio {
 public:
  VideoAudio() = default;
  ~VideoAudio() { close(); }
  VideoAudio(const VideoAudio&) = delete;
  VideoAudio& operator=(const VideoAudio&) = delete;

  // Opens a paused stream for `format`; returns false (with SDL's reason
  // in `error()`) if there is no audio device.
  bool open(const SmackerAudioFormat& format) {
    close();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      error_ = SDL_GetError();
      return false;
    }
    initialized_ = true;
    const SDL_AudioSpec spec{.format = format.bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8,
                             .channels = format.channels,
                             .freq = static_cast<int>(format.rate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
      error_ = SDL_GetError();
      close();
      return false;
    }
    return true;
  }

  void close() {
    if (stream_ != nullptr) {
      SDL_DestroyAudioStream(stream_);
      stream_ = nullptr;
    }
    if (initialized_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      initialized_ = false;
    }
  }

  bool is_open() const { return stream_ != nullptr; }
  const std::string& error() const { return error_; }

  void put(std::span<const std::uint8_t> pcm) {
    if (stream_ != nullptr && !pcm.empty()) {
      SDL_PutAudioStreamData(stream_, pcm.data(), static_cast<int>(pcm.size()));
    }
  }
  void clear() {
    if (stream_ != nullptr) {
      SDL_ClearAudioStream(stream_);
    }
  }
  void set_playing(bool playing) {
    if (stream_ != nullptr) {
      if (playing) {
        SDL_ResumeAudioStreamDevice(stream_);
      } else {
        SDL_PauseAudioStreamDevice(stream_);
      }
    }
  }

 private:
  SDL_AudioStream* stream_ = nullptr;
  bool initialized_ = false;
  std::string error_;
};

class VideosCategory final : public Category {
 public:
  explicit VideosCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Videos"; }

  std::size_t item_count() override {
    if (!listed_) {
      listed_ = true;
      for (const GameDir dir : {GameDir::kCines, GameDir::kBud}) {
        std::vector<std::string> files;
        for (const std::string& path : services_.install.list(services_.paths.dir_name(dir), ".SMK")) {
          files.push_back(path.substr(path.rfind('/') + 1));
        }
        // BUD/1..16 in order rather than by name.
        std::ranges::stable_sort(files, numbered_name_less);
        for (std::string& f : files) {
          items_.push_back({dir, std::move(f)});
        }
      }
    }
    return items_.size();
  }

  std::string item_name(std::size_t index) override {
    return std::string(services_.paths.dir_name(items_[index].first)) + "/" + items_[index].second;
  }

  void select(std::size_t index) override {
    set_playing(false);
    video_.reset();
    video_.emplace(SmackerVideo::load(services_.install, services_.paths.path(items_[index].first, items_[index].second)));
    video_->decode_next();
    audio_.close();
    if (!video_->audio_tracks().empty() && !audio_.open(video_->audio_tracks().front())) {
      std::fprintf(stderr, "%s: no video sound: %s\n", edition::kExecutable, audio_.error().c_str());
    }
  }

  int frame_count() const override { return static_cast<int>(video_->frame_count()); }
  int frame() const override { return video_->frame_index(); }
  void set_frame(int frame) override {
    set_playing(false);
    if (frame < video_->frame_index()) {
      video_->rewind();
    }
    while (video_->frame_index() < frame && video_->decode_next()) {
    }
  }

  void activate() override {
    if (playing_) {
      set_playing(false);
      return;
    }
    if (video_->frame_index() + 1 >= static_cast<int>(video_->frame_count())) {
      video_->rewind();
      video_->decode_next();
    }
    audio_.clear();
    push_audio();
    start_us_ = now_us_;
    start_frame_ = video_->frame_index();
    set_playing(true);
  }

  void update(std::uint64_t now_us) override {
    now_us_ = now_us;
    if (!playing_) {
      return;
    }
    const auto due = start_frame_ + static_cast<int>((now_us - start_us_) / video_->frame_duration_us());
    while (video_->frame_index() < due) {
      if (!video_->decode_next()) {
        set_playing(false);
        return;
      }
      push_audio();
    }
  }

  void draw(Canvas& canvas) override {
    canvas.palette = video_->palette();
    const Image8& image = video_->image();
    canvas.content_width = image.width();
    canvas.content_height = image.height();
    blit_image(canvas.area, image, std::max(0, (canvas.width - image.width()) / 2) - canvas.pan_x,
               std::max(0, (canvas.height - image.height()) / 2) - canvas.pan_y);
  }

  std::string status() const override {
    std::string s = std::format("{}x{}  {:.2f} fps", video_->width(), video_->height(),
                                1e6 / video_->frame_duration_us());
    if (!video_->audio_tracks().empty()) {
      const SmackerAudioFormat& a = video_->audio_tracks().front();
      s += std::format("  {} Hz {}-bit {}", a.rate, a.bits, a.channels == 2 ? "stereo" : "mono");
      if (!audio_.is_open()) {
        s += " (no device)";
      }
    }
    s += playing_ ? "  playing" : "  paused";
    return s;
  }

  std::string_view keys_help() const override { return "Enter play/pause"; }

 private:
  void set_playing(bool playing) {
    playing_ = playing;
    audio_.set_playing(playing);
  }

  void push_audio() {
    if (!video_->audio_tracks().empty()) {
      audio_.put(video_->audio(0));
    }
  }

  const ViewerServices& services_;
  bool listed_ = false;
  std::vector<std::pair<GameDir, std::string>> items_;
  std::optional<SmackerVideo> video_;
  VideoAudio audio_;
  bool playing_ = false;
  std::uint64_t now_us_ = 0;
  std::uint64_t start_us_ = 0;
  int start_frame_ = 0;
};

}  // namespace

std::unique_ptr<Category> make_videos_category(const ViewerServices& services) {
  return std::make_unique<VideosCategory>(services);
}

}  // namespace sh

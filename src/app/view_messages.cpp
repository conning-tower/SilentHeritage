// Viewer category "Messages": the crew-message table of SH.EXE (speaker,
// text, speech clip); Enter speaks it like `crew_message`.

#include <format>
#include <memory>
#include <string>
#include <vector>

#include "app/category.h"
#include "assets/crew_messages.h"
#include "assets/exe_image.h"
#include "assets/speech.h"
#include "core/install.h"

namespace sh {

namespace {

// Returns `text` with the formatter's placeholder bytes spelled out.
std::string readable(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '\x01':
        out += "<1>";
        break;
      case '\x02':
        out += "<2>";
        break;
      case '\x03':
        out += "<system>";
        break;
      case '\t':
        out += "<bearing>";
        break;
      case '\x11':
      case '\x12':
        out += "(s)";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

// Splits `text` into lines of at most `width` pixels at spaces.
std::vector<std::string> wrap(const UiText& ui, std::string_view text, int width) {
  std::vector<std::string> lines;
  std::string line;
  std::size_t pos = 0;
  while (pos < text.size()) {
    std::size_t end = text.find(' ', pos);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    const std::string_view word = text.substr(pos, end - pos);
    const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
    if (!line.empty() && ui.width(candidate) > width) {
      lines.push_back(line);
      line = std::string(word);
    } else {
      line = candidate;
    }
    pos = end + 1;
  }
  if (!line.empty()) {
    lines.push_back(line);
  }
  return lines;
}

class MessagesCategory final : public Category {
 public:
  explicit MessagesCategory(const ViewerServices& services) : services_(services) {}

  std::string_view name() const override { return "Messages"; }

  std::size_t item_count() override {
    if (!messages_) {
      messages_ = std::make_unique<CrewMessages>(ExeImage::load(services_.install));
    }
    return messages_->all().size();
  }

  std::string item_name(std::size_t index) override {
    const CrewMessage& m = messages_->all()[index];
    return std::format("{:02X} {}", m.id, readable(m.text));
  }

  void select(std::size_t index) override { message_ = &messages_->all()[index]; }

  void activate() override {
    if (!message_->sound.empty()) {
      services_.sound.start_speech(read_speech_raw(services_.install, speech_file_name(message_->sound),
                                                   services_.paths.dir(GameDir::kSpeech)));
    }
  }

  void draw(Canvas& canvas) override {
    const UiColours colours = UiColours::pick(canvas.palette);
    const UiText& text = services_.text;
    const int line = text.line_height() + 2;
    int y = 16;
    text.draw(canvas.area, 16, y, std::format("Message {:02X}: {}", message_->id, message_->speaker_name),
              colours.accent);
    y += 2 * line;
    for (const std::string& l : wrap(text, readable(message_->text), canvas.width - 32)) {
      text.draw(canvas.area, 16, y, l, colours.text);
      y += line;
    }
    y += line;
    text.draw(canvas.area, 16, y,
              message_->sound.empty() ? std::string("No speech")
                                      : std::format("Speech: {}{}", services_.paths.dir(GameDir::kSpeech),
                                                    speech_file_name(message_->sound)),
              colours.dim);
  }

  std::string status() const override {
    return message_->sound.empty() ? std::string("no speech") : speech_file_name(message_->sound);
  }

 private:
  const ViewerServices& services_;
  std::unique_ptr<CrewMessages> messages_;
  const CrewMessage* message_ = nullptr;
};

}  // namespace

std::unique_ptr<Category> make_messages_category(const ViewerServices& services) {
  return std::make_unique<MessagesCategory>(services);
}

}  // namespace sh

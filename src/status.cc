#include "src/status.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

namespace afc {
namespace editor {

wchar_t Braille(size_t counter) {
  wchar_t c = L'⠀';
  c += (counter & 0x80 ? 0x01 : 0) + (counter & 0x40 ? 0x08 : 0) +
       (counter & 0x20 ? 0x02 : 0) + (counter & 0x10 ? 0x10 : 0) +
       (counter & 0x08 ? 0x04 : 0) + (counter & 0x04 ? 0x20 : 0) +
       (counter & 0x02 ? 0x40 : 0) + (counter & 0x01 ? 0x80 : 0);
  return c;
}

size_t HandleOverflow(size_t counter, OverflowBehavior overflow_behavior,
                      size_t largest_value) {
  switch (overflow_behavior) {
    case OverflowBehavior::kModulo:
      counter = counter % largest_value;
      break;
    case OverflowBehavior::kMaximum:
      counter = std::min(counter, largest_value);
      break;
  }
  return counter;
}

std::wstring ProgressString(size_t counter,
                            OverflowBehavior overflow_behavior) {
  static const std::vector<wchar_t> values = {
      // From the top left, to the bottom right.
      Braille(0x80),
      Braille(0xC0),
      Braille(0xD0),
      Braille(0xD4),
      Braille(0xD5),
      // Now the tail from the top left is erased.
      Braille(0x55),
      Braille(0x15),
      Braille(0x05),
      Braille(0x01),
      Braille(0x00),
      // From the botom right, to the top left.
      Braille(0x01),
      Braille(0x03),
      Braille(0x0B),
      Braille(0x2B),
      Braille(0xAB),
      // Now the tail from the bottom right is erased.
      Braille(0xAA),
      Braille(0xA8),
      Braille(0xA0),
      Braille(0x80),
      Braille(0x00),
  };

  static const size_t kLargestValue = values.size();

  return std::wstring(
      1, values[HandleOverflow(counter, overflow_behavior, kLargestValue)]);
}

std::wstring ProgressStringFillUp(size_t lines,
                                  OverflowBehavior overflow_behavior) {
  std::wstring output = L" ▁▂▃▄▅▆▇█";
  size_t kInitial = 32;
  if (lines < kInitial) {
    return L" ";
  }
  return std::wstring(
      1, output[HandleOverflow(floor(log2(lines / kInitial)), overflow_behavior,
                               output.size())]);
}

Status::Status(std::shared_ptr<OpenBuffer> console, AudioPlayer* audio_player,
               std::function<void()> updates_listener)
    : console_(std::move(console)),
      audio_player_(audio_player),
      updates_listener_(std::move(updates_listener)) {
  ValidatePreconditions();
}

Status::Type Status::GetType() const {
  ValidatePreconditions();
  return type_;
}

LineNumberDelta Status::DesiredLines() const {
  ValidatePreconditions();
  return type_ != Type::kPrompt && text_.empty() ? LineNumberDelta(0)
                                                 : LineNumberDelta(1);
}

void Status::set_prompt(std::wstring text, std::shared_ptr<OpenBuffer> buffer) {
  CHECK(buffer != nullptr);
  ValidatePreconditions();

  type_ = Status::Type::kPrompt;
  prompt_buffer_ = std::move(buffer);
  text_ = std::move(text);
  updates_listener_();

  ValidatePreconditions();
}

const OpenBuffer* Status::prompt_buffer() const {
  ValidatePreconditions();
  return prompt_buffer_.get();
}

void Status::SetInformationText(std::wstring text) {
  ValidatePreconditions();

  CHECK((prompt_buffer_ != nullptr) == (type_ == Type::kPrompt));
  if (prompt_buffer_ != nullptr) {
    return;
  }
  type_ = Type::kInformation;
  text_ = std::move(text);
  updates_listener_();

  ValidatePreconditions();
}

void Status::SetWarningText(std::wstring text) {
  ValidatePreconditions();

  GenerateAlert(audio_player_);
  if (prompt_buffer_ != nullptr) {
    return;
  }
  type_ = Type::kWarning;
  text_ = std::move(text);
  updates_listener_();

  ValidatePreconditions();
}

void Status::Reset() {
  ValidatePreconditions();

  prompt_buffer_ = nullptr;
  type_ = Type::kInformation;
  text_ = L"";
  updates_listener_();

  ValidatePreconditions();
}

void Status::Bell() {
  ValidatePreconditions();

  if (!all_of(text_.begin(), text_.end(), [](const wchar_t& c) {
        return c == L'♪' || c == L'♫' || c == L'…' || c == L' ' || c == L'𝄞';
      })) {
    text_ = L" 𝄞";
  } else if (text_.size() >= 40) {
    text_ = L"…" + text_.substr(text_.size() - 40, text_.size());
  }
  text_ += +L" " + std::wstring(text_.back() == L'♪' ? L"♫" : L"♪");
  updates_listener_();
}

const std::wstring& Status::text() const {
  ValidatePreconditions();
  return text_;
}

void Status::ValidatePreconditions() const {
  CHECK((prompt_buffer_ != nullptr) == (type_ == Type::kPrompt));
}

}  // namespace editor
}  // namespace afc

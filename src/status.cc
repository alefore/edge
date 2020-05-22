#include "src/status.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/line.h"
#include "src/line_modifier.h"

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
  if (lines <= 1) {
    return L"∅";
  }
  std::wstring output = L" _▁▂▃▄▅▆▇█";
  size_t kInitial = 32;
  if (lines < kInitial) {
    return L" ";
  }
  int index = HandleOverflow(floor(log2(lines / kInitial)), overflow_behavior,
                             output.size());
  return {output[index]};
}

int StatusPromptExtraInformation::StartNewVersion() {
  version_++;
  last_version_state_ = VersionExecution::kRunning;
  return version_;
}

void StatusPromptExtraInformation::SetValue(std::wstring key, int version,
                                            std::wstring value) {
  auto& entry = information_[key];
  if (entry.version <= version) {
    entry = {.version = version, .value = value};
  }
}

void StatusPromptExtraInformation::SetValue(std::wstring key, int version,
                                            int value) {
  return SetValue(key, version, std::to_wstring(value));
}

std::shared_ptr<Line> StatusPromptExtraInformation::GetLine() const {
  Line::Options options;
  static const auto dim = LineModifierSet{LineModifier::DIM};
  static const auto empty = LineModifierSet{};

  if (!information_.empty()) {
    options.AppendString(L"    -- ", dim);
    bool need_separator = false;
    for (const auto [key, value] : information_) {
      if (need_separator) {
        options.AppendString(L" ", empty);
      }
      need_separator = true;

      const auto& modifiers = value.version < version_ ? dim : empty;
      options.AppendString(key, modifiers);
      options.AppendString(L":", dim);
      options.AppendString(value.value, modifiers);
    }
  }
  switch (last_version_state_) {
    case VersionExecution::kDone:
      break;
    case VersionExecution::kRunning:
      options.AppendString(L" …", dim);
      break;
  }

  return std::make_shared<Line>(std::move(options));
}

void StatusPromptExtraInformation::MarkVersionDone(int version) {
  auto it = information_.begin();
  while (it != information_.end()) {
    if (it->second.version < version_) {
      it = information_.erase(it);
    } else {
      ++it;
    }
  }
  if (version == version_) {
    last_version_state_ = VersionExecution::kDone;
  }
}

Status::Status(std::shared_ptr<OpenBuffer> console, AudioPlayer* audio_player)
    : console_(std::move(console)), audio_player_(audio_player) {
  ValidatePreconditions();
}

void Status::CopyFrom(const Status& status) { data_ = status.data_; }

Status::Type Status::GetType() const {
  ValidatePreconditions();
  return data_->type;
}

void Status::set_prompt(std::wstring text, std::shared_ptr<OpenBuffer> buffer) {
  CHECK(buffer != nullptr);
  ValidatePreconditions();
  data_ = std::make_shared<Data>(Data{
      .type = Status::Type::kPrompt,
      .text = std::move(text),
      .prompt_buffer = std::move(buffer),
      .extra_information = std::make_unique<StatusPromptExtraInformation>()});
  ValidatePreconditions();
}

void Status::set_context(std::shared_ptr<OpenBuffer> context) {
  ValidatePreconditions();
  data_->context = std::move(context);
  ValidatePreconditions();
}

const std::shared_ptr<OpenBuffer>& Status::prompt_buffer() const {
  ValidatePreconditions();
  return data_->prompt_buffer;
}

const std::shared_ptr<OpenBuffer>& Status::context() const {
  ValidatePreconditions();
  return data_->context;
}

StatusPromptExtraInformation* Status::prompt_extra_information() {
  return data_->extra_information.get();
}

const StatusPromptExtraInformation* Status::prompt_extra_information() const {
  return data_->extra_information.get();
}

void Status::SetInformationText(std::wstring text) {
  ValidatePreconditions();
  if (data_->prompt_buffer != nullptr) {
    return;
  }
  LOG(INFO) << "SetInformationText: " << text;
  data_ = std::make_shared<Data>(
      Data{.type = Type::kInformation, .text = std::move(text)});
  ValidatePreconditions();
}

struct StatusExpirationControl {
  std::weak_ptr<Status::Data> data;
};

std::unique_ptr<StatusExpirationControl,
                std::function<void(StatusExpirationControl*)>>
Status::SetExpiringInformationText(std::wstring text) {
  ValidatePreconditions();
  SetInformationText(text);
  ValidatePreconditions();
  return std::unique_ptr<StatusExpirationControl,
                         std::function<void(StatusExpirationControl*)>>(
      new StatusExpirationControl{data_},
      [](StatusExpirationControl* status_expiration_control) {
        auto data = status_expiration_control->data.lock();
        if (data != nullptr) {
          data->text = L"";
        }
        delete status_expiration_control;
      });
}

void Status::SetWarningText(std::wstring text) {
  ValidatePreconditions();

  LOG(INFO) << "Warning: " << text;
  GenerateAlert(audio_player_);
  if (data_->prompt_buffer != nullptr) {
    return;
  }
  data_ = std::make_shared<Data>(
      Data{.type = Type::kWarning, .text = std::move(text)});
  ValidatePreconditions();
}

struct timespec Status::last_change_time() const {
  return data_->creation_time;
}

void Status::Reset() {
  ValidatePreconditions();
  data_ = std::make_shared<Data>();
  ValidatePreconditions();
}

void Status::Bell() {
  ValidatePreconditions();
  auto text = data_->text;
  if (!all_of(text.begin(), text.end(), [](const wchar_t& c) {
        return c == L'♪' || c == L'♫' || c == L'…' || c == L' ' || c == L'𝄞';
      })) {
    text = L" 𝄞";
  } else if (text.size() >= 40) {
    text = L"…" + text.substr(text.size() - 40, text.size());
  }
  text += +L" " + std::wstring(text.back() == L'♪' ? L"♫" : L"♪");
}

const std::wstring& Status::text() const {
  ValidatePreconditions();
  return data_->text;
}

void Status::ValidatePreconditions() const {
  CHECK(data_ != nullptr);
  CHECK((data_->prompt_buffer != nullptr) == (data_->type == Type::kPrompt));
}

}  // namespace editor
}  // namespace afc

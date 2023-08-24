#include "src/status.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/line.h"

namespace afc::editor {
namespace gc = language::gc;
namespace error = language::error;

using language::Error;
using language::MakeNonNullShared;
using language::NonNull;
using language::VisitPointer;

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

language::NonNull<std::unique_ptr<StatusPromptExtraInformation::Version>>
StatusPromptExtraInformation::StartNewVersion() {
  data_->version_id++;
  data_->last_version_state = Data::VersionExecution::kRunning;
  return MakeNonNullUnique<Version>(Version::ConstructorAccessKey{}, data_);
}

StatusPromptExtraInformation::Version::Version(
    ConstructorAccessKey,
    const NonNull<std::shared_ptr<StatusPromptExtraInformation::Data>>& data)
    : data_(data.get_shared()), version_id_(data->version_id) {}

bool StatusPromptExtraInformation::Version::IsExpired() const {
  return VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        return version_id_ < data->version_id;
      },
      [] { return true; });
}

void StatusPromptExtraInformation::Version::SetValue(Key key,
                                                     std::wstring value) {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        if (auto& entry = data->information[key];
            entry.version_id <= version_id_) {
          entry = {.version_id = version_id_, .value = value};
        }
      },
      [] {});
}

void StatusPromptExtraInformation::Version::SetValue(Key key, int value) {
  return SetValue(key, std::to_wstring(value));
}

Line StatusPromptExtraInformation::GetLine() const {
  LineBuilder options;
  static const auto dim = LineModifierSet{LineModifier::kDim};
  static const auto empty = LineModifierSet{};

  if (!data_->information.empty()) {
    options.AppendString(L"    -- ", dim);
    bool need_separator = false;
    for (const auto& [key, value] : data_->information) {
      if (need_separator) {
        options.AppendString(L" ", empty);
      }
      need_separator = true;

      const auto& modifiers =
          value.version_id < data_->version_id ? dim : empty;
      options.AppendString(key.value, modifiers);
      if (!value.value.empty()) {
        options.AppendString(L":", dim);
        options.AppendString(value.value, modifiers);
      }
    }
  }
  switch (data_->last_version_state) {
    case Data::VersionExecution::kDone:
      break;
    case Data::VersionExecution::kRunning:
      options.AppendString(L" …", dim);
      break;
  }

  return std::move(options).Build();
}

void StatusPromptExtraInformation::Version::MarkDone() {
  VisitPointer(
      data_,
      [&](NonNull<std::shared_ptr<Data>> data) {
        auto it = data->information.begin();
        while (it != data->information.end()) {
          if (it->second.version_id < version_id_) {
            it = data->information.erase(it);
          } else {
            ++it;
          }
        }
        if (data->version_id == version_id_) {
          data->last_version_state = Data::VersionExecution::kDone;
        }
      },
      [] {});
}

Status::Status(infrastructure::audio::Player& audio_player)
    : audio_player_(audio_player) {
  ValidatePreconditions();
}

void Status::CopyFrom(const Status& status) { data_ = status.data_; }

Status::Type Status::GetType() const {
  ValidatePreconditions();
  return data_->type;
}

void Status::set_prompt(std::wstring text, gc::Root<OpenBuffer> buffer) {
  ValidatePreconditions();
  data_ = MakeNonNullShared<Data>(Data{
      .type = Status::Type::kPrompt,
      .text = std::move(text),
      .prompt_buffer = std::move(buffer),
      .extra_information = std::make_unique<StatusPromptExtraInformation>()});
  ValidatePreconditions();
}

void Status::set_context(std::optional<gc::Root<OpenBuffer>> context) {
  ValidatePreconditions();
  data_->context = std::move(context);
  ValidatePreconditions();
}

const std::optional<gc::Root<OpenBuffer>>& Status::prompt_buffer() const {
  ValidatePreconditions();
  return data_->prompt_buffer;
}

const std::optional<gc::Root<OpenBuffer>>& Status::context() const {
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
  LOG(INFO) << "SetInformationText: " << text;
  if (data_->prompt_buffer.has_value()) {
    return;
  }
  data_ = MakeNonNullShared<Data>(
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
  if (data_->prompt_buffer.has_value()) {
    return nullptr;
  }

  return std::unique_ptr<StatusExpirationControl,
                         std::function<void(StatusExpirationControl*)>>(
      new StatusExpirationControl{data_.get_shared()},
      [](StatusExpirationControl* status_expiration_control) {
        VisitPointer(
            status_expiration_control->data,
            [](NonNull<std::shared_ptr<Status::Data>> data) {
              data->text = L"";
            },
            [] {});
        delete status_expiration_control;
      });
}

void Status::SetWarningText(std::wstring text) { Set(Error(text)); }

void Status::Set(Error error) {
  ValidatePreconditions();

  LOG(INFO) << "Warning: " << error;
  GenerateAlert(audio_player_);
  if (data_->prompt_buffer.has_value()) {
    return;
  }
  data_ = MakeNonNullShared<Data>(
      Data{.type = Type::kWarning, .text = std::move(error.read())});
  ValidatePreconditions();
}

error::Log::InsertResult Status::InsertError(
    language::Error error, infrastructure::Duration duration) {
  error::Log::InsertResult output = errors_log_.Insert(error, duration);
  if (output == error::Log::InsertResult::kInserted) Set(error);
  return output;
}

struct timespec Status::last_change_time() const {
  return data_->creation_time;
}

void Status::Reset() {
  ValidatePreconditions();
  data_ = MakeNonNullShared<Data>();
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
  CHECK((data_->prompt_buffer.has_value()) == (data_->type == Type::kPrompt));
}
}  // namespace afc::editor

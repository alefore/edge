#include "src/status.h"

#include <glog/logging.h>

#include <cmath>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/text/line.h"
#include "src/language/text/line_builder.h"
#include "src/language/text/line_sequence.h"

using afc::concurrent::VersionPropertyReceiver;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::VisitPointer;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::FindFirstColumnWithPredicate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineSequence;

namespace afc::editor {
namespace gc = language::gc;
namespace error = language::error;

using ::operator<<;

namespace {
NonEmptySingleLine Braille(size_t counter) {
  wchar_t c = L'⠀';
  c += (counter & 0x80 ? 0x01 : 0) + (counter & 0x40 ? 0x08 : 0) +
       (counter & 0x20 ? 0x02 : 0) + (counter & 0x10 ? 0x10 : 0) +
       (counter & 0x08 ? 0x04 : 0) + (counter & 0x04 ? 0x20 : 0) +
       (counter & 0x02 ? 0x40 : 0) + (counter & 0x01 ? 0x80 : 0);
  return NonEmptySingleLine{SingleLine{LazyString{ColumnNumberDelta{1}, c}}};
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
}  // namespace

NonEmptySingleLine ProgressString(size_t counter,
                                  OverflowBehavior overflow_behavior) {
  static const std::vector<NonEmptySingleLine> values = {
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

  return values[HandleOverflow(counter, overflow_behavior, kLargestValue)];
}

NonEmptySingleLine ProgressStringFillUp(size_t lines,
                                        OverflowBehavior overflow_behavior) {
  if (lines <= 1) return NonEmptySingleLine{SingleLine::Char<L'∅'>()};
  static const SingleLine output = SINGLE_LINE_CONSTANT(L" _▁▂▃▄▅▆▇█");
  static const size_t kInitial = 32;
  if (lines < kInitial) return NonEmptySingleLine{SingleLine::Char<L' '>()};
  const ColumnNumber index{HandleOverflow(
      floor(log2(lines / kInitial)), overflow_behavior, output.size().read())};
  return NonEmptySingleLine(output.Substring(index, ColumnNumberDelta{1}));
}

Status::Status(infrastructure::audio::Player& audio_player)
    : audio_player_(audio_player) {
  ValidatePreconditions();
}

void Status::CopyFrom(const Status& status) { data_ = status.data_; }

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Status::Expand() const {
  return {};
}

Status::Type Status::GetType() const {
  ValidatePreconditions();
  return data_->type;
}

void Status::set_prompt(Line text, gc::Root<OpenBuffer> buffer) {
  ValidatePreconditions();
  data_ = MakeNonNullShared<Data>(
      Data{.type = Status::Type::kPrompt,
           .text = std::move(text),
           .prompt_buffer = std::move(buffer),
           .extra_information = std::make_unique<VersionPropertyReceiver>()});
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

VersionPropertyReceiver* Status::prompt_extra_information() {
  return data_->extra_information.get();
}

const VersionPropertyReceiver* Status::prompt_extra_information() const {
  return data_->extra_information.get();
}

Line Status::prompt_extra_information_line() const {
  static const auto dim = LineModifierSet{LineModifier::kDim};
  static const auto empty = LineModifierSet{};

  const VersionPropertyReceiver* const receiver = prompt_extra_information();
  if (receiver == nullptr) return LineBuilder().Build();
  const VersionPropertyReceiver::PropertyValues values = receiver->GetValues();
  LineBuilder options;
  if (!values.property_values.empty()) {
    options.AppendString(SINGLE_LINE_CONSTANT(L"    🛈  "), dim);
    bool need_separator = false;
    for (const auto& [key, value] : values.property_values) {
      if (need_separator) {
        options.AppendString(SingleLine::Char<L' '>(), empty);
      }
      need_separator = true;

      const auto& modifiers = value.status ==
                                      VersionPropertyReceiver::PropertyValues::
                                          Value::Status::kExpired
                                  ? dim
                                  : empty;
      options.AppendString(key.read().read(), modifiers);
      if (!std::holds_alternative<SingleLine>(value.value) ||
          !std::get<SingleLine>(value.value).empty()) {
        options.AppendString(SingleLine::Char<L':'>(), dim);
        options.AppendString(std::visit(overload{[](SingleLine v) { return v; },
                                                 [](int v) {
                                                   return SingleLine{LazyString{
                                                       std::to_wstring(v)}};
                                                 }},
                                        value.value),
                             modifiers);
      }
    }
  }
  switch (values.last_version_state) {
    case VersionPropertyReceiver::VersionExecution::kDone:
      break;
    case VersionPropertyReceiver::VersionExecution::kRunning:
      options.AppendString(SingleLine::Char<L' '>() + SingleLine::Char<L'…'>(),
                           dim);
      break;
  }

  return std::move(options).Build();
}

void Status::SetInformationText(Line text) {
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
Status::SetExpiringInformationText(Line text) {
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
              data->text = Line();
            },
            [] {});
        delete status_expiration_control;
      });
}

void Status::Set(Error error) {
  ValidatePreconditions();

  LOG(INFO) << "Warning: " << error;
  GenerateAlert(audio_player_);
  if (data_->prompt_buffer.has_value()) {
    return;
  }
  LineBuilder text;
  text.AppendString(LineSequence::BreakLines(error.read()).FoldLines(),
                    LineModifierSet({LineModifier::kRed, LineModifier::kBold}));
  data_ = MakeNonNullShared<Data>(
      Data{.type = Type::kWarning, .text = std::move(text).Build()});

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
  static const ColumnNumberDelta kMaxLength(40);

  static const std::vector<SingleLine> notes = {SingleLine::Char<L'🎵'>(),
                                                SingleLine::Char<L'🎶'>()};

  LineBuilder output;
  if (FindFirstColumnWithPredicate(
          data_->text.contents(), [&](ColumnNumber, const wchar_t& c) {
            return c != L'🎼' && c != L'…' && c != L' ' &&
                   std::find(notes.begin(), notes.end(),
                             SingleLine{LazyString{ColumnNumberDelta{1}, c}}) ==
                       notes.end();
          }) != std::nullopt) {
    output.AppendString(SingleLine{LazyString{L"🎼"}});
  } else {
    LineBuilder previous = LineBuilder(std::move(data_->text));
    if (previous.contents().size() > kMaxLength) {
      previous.DeleteCharacters(ColumnNumber(),
                                previous.contents().size() - kMaxLength);
      output.AppendString(SingleLine{LazyString{L"…"}});
    }
    output.Append(std::move(previous));
  }

  static const std::vector<LineModifier> colors = {
      LineModifier::kRed,  LineModifier::kGreen,  LineModifier::kBlue,
      LineModifier::kCyan, LineModifier::kYellow, LineModifier::kMagenta,
      LineModifier::kWhite};
  static const std::vector<LineModifier> effects = {
      LineModifier::kBold, LineModifier::kItalic, LineModifier::kReverse};
  output.AppendString(
      SingleLine::Char<L' '>() + notes.at(rand() % notes.size()),
      LineModifierSet{colors.at(rand() % colors.size()),
                      effects.at(rand() % effects.size())});
  data_->text = std::move(output).Build();
}

const Line& Status::text() const {
  ValidatePreconditions();
  return data_->text;
}

void Status::ValidatePreconditions() const {
  CHECK((data_->prompt_buffer.has_value()) == (data_->type == Type::kPrompt));
}
}  // namespace afc::editor

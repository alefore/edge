#ifndef __AFC_EDITOR_STATUS_H__
#define __AFC_EDITOR_STATUS_H__

#include <map>
#include <memory>

#include "src/infrastructure/audio.h"
#include "src/infrastructure/time.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/overload.h"

namespace afc::editor {

class OpenBuffer;
class Line;

enum class OverflowBehavior { kModulo, kMaximum };
std::wstring ProgressString(size_t counter, OverflowBehavior overflow_behavior);
std::wstring ProgressStringFillUp(size_t counter,
                                  OverflowBehavior overflow_behavior);

// Opaque type returned by `SetExpiringInformationText`.
struct StatusExpirationControl;

class StatusPromptExtraInformationKey {
 public:
  GHOST_TYPE_CONSTRUCTOR(StatusPromptExtraInformationKey, std::wstring, value);
  GHOST_TYPE_EQ(StatusPromptExtraInformationKey, value);
  GHOST_TYPE_ORDER(StatusPromptExtraInformationKey, value);
  GHOST_TYPE_OUTPUT_FRIEND(StatusPromptExtraInformationKey, value);
  GHOST_TYPE_HASH_FRIEND(StatusPromptExtraInformationKey, value);

 private:
  friend class StatusPromptExtraInformation;
  const std::wstring& read() const { return value; }
  std::wstring value;
};

using ::operator<<;
GHOST_TYPE_OUTPUT(StatusPromptExtraInformationKey, value);
}  // namespace afc::editor

GHOST_TYPE_HASH(afc::editor::StatusPromptExtraInformationKey);

namespace afc::editor {
class StatusPromptExtraInformation {
  using Key = StatusPromptExtraInformationKey;

 public:
  int StartNewVersion();
  int current_version() const;
  void SetValue(Key key, int version, std::wstring value);
  void SetValue(Key key, int version, int value);

  // Once the caller thinks that it won't be doing any additional calls to
  // SetValue for a given version, it should call `MarkVersionDone`. Completion
  // will be reflected by `GetLine`. It is okay to call `SetValue` after this,
  // but it will be misleading to the user (who will think that the values
  // displayed are final).
  void MarkVersionDone(int version);

  // Prints the line.
  Line GetLine() const;

 private:
  struct Value {
    int version;
    std::wstring value;
  };
  std::map<Key, Value> information_;
  int version_ = 0;
  enum class VersionExecution {
    // MarkVersionDone hasn't executed for the last value of version_.
    kRunning,
    // MarkVersionDone has run with the last value of version_.
    kDone
  };
  VersionExecution last_version_state_ = VersionExecution::kDone;
};

class Status {
 public:
  Status(infrastructure::audio::Player& audio_player);
  Status(const Status&) = delete;
  void CopyFrom(const Status& status);

  enum class Type { kWarning, kInformation, kPrompt };
  Type GetType() const;

  void set_prompt(std::wstring text, language::gc::Root<OpenBuffer> buffer);
  // Sets the context buffer.
  //
  // Can be called with `std::nullopt` to remove the context.
  void set_context(std::optional<language::gc::Root<OpenBuffer>> context);
  const std::optional<language::gc::Root<OpenBuffer>>& context() const;

  const std::optional<language::gc::Root<OpenBuffer>>& prompt_buffer() const;

  // Returns nullptr if the status type isn't kPrompt.
  StatusPromptExtraInformation* prompt_extra_information();
  // Returns nullptr if the status type isn't kPrompt.
  const StatusPromptExtraInformation* prompt_extra_information() const;

  void SetInformationText(std::wstring text);

  // Sets the status to a given text and returns an opaque token. The caller
  // uses the opaque token to control when the text given is retired (by letting
  // the token expire).
  std::unique_ptr<StatusExpirationControl,
                  std::function<void(StatusExpirationControl*)>>
  SetExpiringInformationText(std::wstring text);
  void SetWarningText(std::wstring text);
  void Set(language::Error text);

  // Returns the time of the last call to a method in this class that changed
  // the state of this instance.
  struct timespec last_change_time() const;

  template <typename T>
  T ConsumeErrors(language::ValueOrError<T> value_or_error,
                  T replacement_value) {
    return std::visit(language::overload{[&](language::Error error) {
                                           Set(error);
                                           return replacement_value;
                                         },
                                         [](T value) { return value; }},
                      std::move(value_or_error));
  }

  template <typename T>
  language::ValueOrError<T> LogErrors(language::ValueOrError<T> value) {
    std::visit(language::overload{[&](language::Error error) { Set(error); },
                                  [](const T&) {}},
               value);
    return value;
  }

  void Reset();

  void Bell();

  const std::wstring& text() const;

 private:
  friend StatusExpirationControl;
  void ValidatePreconditions() const;

  infrastructure::audio::Player& audio_player_;

  // We nest our mutable fields in `struct Data`. This allows us to implement
  // `SetExpiringInformationText`, where we can detect if the status hasn't
  // changed (between the call to `SetExpiringInformationText` and the moment
  // when the `StatusExpirationControl` that it returns is deleted).
  struct Data {
    const struct timespec creation_time = infrastructure::Now();

    const Type type = Type::kInformation;
    std::wstring text;
    const std::optional<language::gc::Root<OpenBuffer>> prompt_buffer =
        std::nullopt;

    // When `prompt_buffer` isn't nullptr, `context` may be set to a
    // buffer that contains either a preview of the results of executing the
    // prompt or possible completions.
    std::optional<language::gc::Root<OpenBuffer>> context = std::nullopt;

    // Should only be used when type is Type::kPrompt.
    std::unique_ptr<StatusPromptExtraInformation> extra_information = nullptr;
  };

  language::NonNull<std::shared_ptr<Data>> data_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_STATUS_H__

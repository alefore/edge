#ifndef __AFC_EDITOR_STATUS_H__
#define __AFC_EDITOR_STATUS_H__

#include <memory>

#include "src/audio.h"
#include "src/line_column.h"
#include "src/time.h"

namespace afc {
namespace editor {

class OpenBuffer;
class Line;

enum class OverflowBehavior { kModulo, kMaximum };
std::wstring ProgressString(size_t counter, OverflowBehavior overflow_behavior);
std::wstring ProgressStringFillUp(size_t counter,
                                  OverflowBehavior overflow_behavior);

// Opaque type returned by `SetExpiringInformationText`.
struct StatusExpirationControl;

class StatusPromptExtraInformation {
 public:
  int StartNewVersion();
  void SetValue(std::wstring key, int version, std::wstring value);
  // Once the caller thinks that it won't be doing any additional calls to
  // SetValue for a given version, it should call `MarkVersionDone`. Completion
  // will be reflected by `GetLine`. It is okay to call `SetValue` after this,
  // but it will be misleading to the user (who will think that the values
  // displayed are final).
  void MarkVersionDone(int version);
  std::shared_ptr<Line> GetLine() const;

 private:
  struct Value {
    int version;
    std::wstring value;
  };
  std::unordered_map<std::wstring, Value> information_;
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
  Status(std::shared_ptr<OpenBuffer> console, AudioPlayer* audio_player);
  void CopyFrom(const Status& status);

  enum class Type { kWarning, kInformation, kPrompt };
  Type GetType() const;

  void set_prompt(std::wstring text, std::shared_ptr<OpenBuffer> buffer);
  // Can be called with `nullptr` to remove the context. Should only be called
  // with a non-null value if `GetType` returns kPrompt.
  void set_prompt_context(std::shared_ptr<OpenBuffer> prompt_context);
  // May be nullptr.
  const std::shared_ptr<OpenBuffer>& prompt_buffer() const;
  const std::shared_ptr<OpenBuffer>& prompt_context() const;

  // Returns nullptr if the status type isn't kPrompt.
  StatusPromptExtraInformation* prompt_extra_information();
  // Returns nullptr if the status type isn't kPrompt.
  const StatusPromptExtraInformation* prompt_extra_information() const;

  void SetInformationText(std::wstring text);
  std::unique_ptr<StatusExpirationControl,
                  std::function<void(StatusExpirationControl*)>>
  SetExpiringInformationText(std::wstring text);
  void SetWarningText(std::wstring text);

  // Returns the time of the last call to a method in this class that changed
  // the state of this instance.
  struct timespec last_change_time() const;

  template <typename T>
  T ConsumeErrors(ValueOrError<T> value, T replacement_value) {
    if (value.IsError()) {
      SetWarningText(value.error.value());
      return replacement_value;
    }
    return value.value.value();
  }

  void Reset();

  void Bell();

  const std::wstring& text() const;

 private:
  friend StatusExpirationControl;
  void ValidatePreconditions() const;

  const std::shared_ptr<OpenBuffer> console_;
  AudioPlayer* const audio_player_;

  // We nest our mutable fields in `struct Data`. This allows us to implement
  // `SetExpiringInformationText`, where we can detect if the status hasn't
  // changed (between the call to `SetExpiringInformationText` and the moment
  // when the `StatusExpirationControl` that it returns is deleted).
  struct Data {
    const struct timespec creation_time = Now();

    const Type type = Type::kInformation;
    std::wstring text;
    const std::shared_ptr<OpenBuffer> prompt_buffer = nullptr;

    // When `prompt_buffer` isn't nullptr, `prompt_context` may be set to a
    // buffer that contains either a preview of the results of executing the
    // prompt or possible completions.
    std::shared_ptr<OpenBuffer> prompt_context = nullptr;

    // Should only be used when type is Type::kPrompt.
    std::unique_ptr<StatusPromptExtraInformation> extra_information = nullptr;
  };

  std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_H__

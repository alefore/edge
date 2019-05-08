#ifndef __AFC_EDITOR_STATUS_H__
#define __AFC_EDITOR_STATUS_H__

#include <memory>

#include "src/audio.h"
#include "src/line_column.h"

namespace afc {
namespace editor {

class OpenBuffer;

enum class OverflowBehavior { kModulo, kMaximum };
std::wstring ProgressString(size_t counter, OverflowBehavior overflow_behavior);
std::wstring ProgressStringFillUp(size_t counter,
                                  OverflowBehavior overflow_behavior);

class Status {
 public:
  Status(std::shared_ptr<OpenBuffer> console, AudioPlayer* audio_player);

  enum class Type { kWarning, kInformation, kPrompt };
  Type GetType() const;

  LineNumberDelta DesiredLines() const;

  void set_prompt(std::wstring text, std::shared_ptr<OpenBuffer> buffer);
  // May be nullptr.
  const OpenBuffer* prompt_buffer() const;

  void SetInformationText(std::wstring text);
  void SetWarningText(std::wstring text);
  void Reset();

  void Bell();

  const std::wstring& text() const;

 private:
  void ValidatePreconditions() const;

  const std::shared_ptr<OpenBuffer> console_;
  AudioPlayer* const audio_player_;

  std::shared_ptr<OpenBuffer> prompt_buffer_;

  Type type_ = Type::kInformation;
  std::wstring text_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_H__

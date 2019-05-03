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
  Status(std::shared_ptr<OpenBuffer> console, AudioPlayer* audio_player,
         std::function<void()> updates_listener);

  enum class Type { kWarning, kInformation, kPrompt };
  Type GetType() const;

  LineNumberDelta DesiredLines() const;

  void set_prompt(std::wstring text, ColumnNumber column);
  std::optional<ColumnNumber> prompt_column() const;

  void SetInformationText(std::wstring text);
  void SetWarningText(std::wstring text);
  void Reset();

  void Bell();

  const std::wstring& text() const;

 private:
  const std::shared_ptr<OpenBuffer> console_;
  AudioPlayer* const audio_player_;
  const std::function<void()> updates_listener_;

  std::optional<ColumnNumber> prompt_column_;

  Type type_ = Type::kInformation;
  std::wstring text_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_STATUS_H__

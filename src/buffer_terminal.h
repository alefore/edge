#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc {
namespace editor {

class OpenBuffer;
class BufferContents;

class BufferTerminal {
 public:
  BufferTerminal(OpenBuffer* buffer, BufferContents* contents);

  LineColumn position() const;
  void SetPosition(LineColumn position);

  void SetSize(LineNumberDelta lines, ColumnNumberDelta columns);

  void ProcessCommandInput(shared_ptr<LazyString> str,
                           const std::function<void()>& new_line_callback);

 private:
  ColumnNumber ProcessTerminalEscapeSequence(
      std::shared_ptr<LazyString> str, ColumnNumber read_index,
      std::unordered_set<LineModifier, std::hash<int>>* modifiers);

  void MoveToNextLine();

  OpenBuffer* const buffer_;

  // TODO: Find a way to remove this? I.e. always use buffer_.
  BufferContents* const contents_;

  LineNumberDelta lines_;
  ColumnNumberDelta columns_;

  LineColumn position_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TERMINAL_H__

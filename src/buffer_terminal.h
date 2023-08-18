#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/fuzz_testable.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc::editor {

class OpenBuffer;
class BufferContents;
class Status;

// If the buffer holds an underlying process with a terminal (PTS),
// BufferTerminal holds its state.
//
// TODO(trivial, 2023-08-18): Find a better name. Perhaps
// `TerminalInputProcessor`.
class BufferTerminal : public fuzz::FuzzTestable {
 public:
  class Receiver {
   public:
    // Erases all lines in range [first, last).
    virtual void EraseLines(LineNumber first, LineNumber last) = 0;

    virtual void AppendEmptyLine() = 0;

    virtual Status& status() = 0;

    virtual const BufferContents& contents() = 0;

    virtual void JumpToPosition(LineColumn position) = 0;
  };

  BufferTerminal(std::unique_ptr<Receiver> receiver, OpenBuffer& buffer,
                 BufferContents& contents);

  // Propagates the last view size to buffer->fd().
  void UpdateSize();

  LineColumn position() const;
  void SetPosition(LineColumn position);

  void ProcessCommandInput(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      const std::function<void()>& new_line_callback);

  std::vector<fuzz::Handler> FuzzHandlers() override;

 private:
  struct Data {
    // The last size written to buffer->fd() by UpdateSize.
    std::optional<LineColumnDelta> last_updated_size = std::nullopt;

    std::unique_ptr<Receiver> receiver;
    OpenBuffer& buffer;

    // TODO: Find a way to remove this? I.e. always use buffer_.
    BufferContents& contents;

    LineColumn position = LineColumn();
  };

  static void InternalUpdateSize(Data& data);

  language::lazy_string::ColumnNumber ProcessTerminalEscapeSequence(
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>> str,
      language::lazy_string::ColumnNumber read_index,
      LineModifierSet* modifiers);

  void MoveToNextLine();

  static LineColumnDelta LastViewSize(Data& data);

  const language::NonNull<std::shared_ptr<Data>> data_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_TERMINAL_H__

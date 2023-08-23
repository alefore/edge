#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/infrastructure/file_system_driver.h"
#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/line_column.h"
#include "src/tests/fuzz_testable.h"

namespace afc::infrastructure::audio {
class Player;
}
namespace afc::editor {

class OpenBuffer;
class BufferContents;
class BufferName;

// Decodes input from a terminal-associated file descriptor.
//
// This input is received incrementally through `ProcessCommandInput`. As it is
// decoded, `TerminalInputParser` calls the associated methods in the `Receiver`
// instance.
class TerminalInputParser : public fuzz::FuzzTestable {
 public:
  class Receiver {
   public:
    virtual ~Receiver() = default;

    // Erases all lines in range [first, last).
    virtual void EraseLines(LineNumber first, LineNumber last) = 0;

    virtual void AppendEmptyLine() = 0;

    virtual BufferName name() = 0;

    virtual std::optional<infrastructure::FileDescriptor> fd() = 0;

    // Every buffer should keep track of the last size of a widget that has
    // displayed it. TerminalInputParser uses this to be notified when it
    // changes and propagate that information to the underlying file descriptor
    // (e.g., so that $LINES shell variable is updated).
    virtual language::ObservableValue<LineColumnDelta>& view_size() = 0;

    virtual void Bell() = 0;
    virtual void Warn(std::wstring warning_text) = 0;

    virtual const BufferContents& contents() = 0;

    // Return the position of the start of the current view.
    virtual LineColumn current_widget_view_start() = 0;

    virtual void JumpToPosition(LineColumn position) = 0;
  };

  TerminalInputParser(language::NonNull<std::unique_ptr<Receiver>> receiver,
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

    language::NonNull<std::unique_ptr<Receiver>> receiver;

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

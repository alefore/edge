#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/fuzz_testable.h"
#include "src/language/safe_types.h"
#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"

namespace afc {
namespace editor {

class OpenBuffer;
class BufferContents;

class BufferTerminal : public fuzz::FuzzTestable {
 public:
  BufferTerminal(OpenBuffer& buffer, BufferContents& contents);

  // Propagates the last view size to buffer->fd().
  void UpdateSize();

  LineColumn position() const;
  void SetPosition(LineColumn position);

  void ProcessCommandInput(language::NonNull<std::shared_ptr<LazyString>> str,
                           const std::function<void()>& new_line_callback);

  std::vector<fuzz::Handler> FuzzHandlers() override;

 private:
  struct Data {
    // The last size written to buffer->fd() by UpdateSize.
    std::optional<LineColumnDelta> last_updated_size = std::nullopt;

    OpenBuffer& buffer;

    // TODO: Find a way to remove this? I.e. always use buffer_.
    BufferContents& contents;

    LineColumn position = LineColumn();
  };

  static void InternalUpdateSize(Data& data);

  ColumnNumber ProcessTerminalEscapeSequence(
      language::NonNull<std::shared_ptr<LazyString>> str,
      ColumnNumber read_index, LineModifierSet* modifiers);

  void MoveToNextLine();

  static LineColumnDelta LastViewSize(Data& data);

  // TODO(easy, 2022-05-02): NonNull.
  const std::shared_ptr<Data> data_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TERMINAL_H__

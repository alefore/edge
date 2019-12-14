#ifndef __AFC_EDITOR_BUFFER_TERMINAL_H__
#define __AFC_EDITOR_BUFFER_TERMINAL_H__

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/fuzz_testable.h"
#include "src/lazy_string.h"
#include "src/line_column.h"
#include "src/line_modifier.h"
#include "src/viewers.h"

namespace afc {
namespace editor {

class OpenBuffer;
class BufferContents;

class BufferTerminal : public fuzz::FuzzTestable {
 public:
  BufferTerminal(OpenBuffer* buffer, BufferContents* contents);

  // Propagates the last view size to buffer->fd().
  void UpdateSize();

  LineColumn position() const;
  void SetPosition(LineColumn position);

  void ProcessCommandInput(std::shared_ptr<LazyString> str,
                           const std::function<void()>& new_line_callback);

  std::vector<fuzz::Handler> FuzzHandlers() override;

 private:
  ColumnNumber ProcessTerminalEscapeSequence(
      std::shared_ptr<LazyString> str, ColumnNumber read_index,
      std::unordered_set<LineModifier, std::hash<int>>* modifiers);

  void MoveToNextLine();

  LineColumnDelta LastViewSize();

  // The last size written to buffer->fd() by UpdateSize.
  std::optional<LineColumnDelta> last_updated_size_;

  OpenBuffer* const buffer_;

  // TODO: Find a way to remove this? I.e. always use buffer_.
  BufferContents* const contents_;

  const Viewers::Registration listener_registration_;

  LineColumn position_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_TERMINAL_H__

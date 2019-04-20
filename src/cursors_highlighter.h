#ifndef __AFC_EDITOR_CURSORS_HIGHLIGHTER_H__
#define __AFC_EDITOR_CURSORS_HIGHLIGHTER_H__

#include <memory>
#include <set>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

struct CursorsHighlighterOptions {
  std::unique_ptr<OutputReceiver> delegate;

  // A set with all the columns in the current line in which there are
  // cursors that should be drawn.
  std::set<size_t> columns;

  bool multiple_cursors;

  std::optional<size_t> active_cursor_input;

  // Output parameter. If the active cursor is found in this line, we set this
  // to the column in the screen to which it should be moved. This is used to
  // handle multi-width characters.
  std::optional<size_t>* active_cursor_output;
};

std::unique_ptr<OutputReceiver> NewCursorsHighlighter(
    CursorsHighlighterOptions options);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CURSORS_HIGHLIGHTER_H__

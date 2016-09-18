#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "line_column.h"

namespace afc {
namespace editor {

using std::wstring;

struct EditorState;
class OpenBuffer;
class LineColumn;

void SearchHandlerPredictor(
    EditorState* editor_state,
    const wstring& current_query,
    OpenBuffer* target);

struct SearchOptions {
  // The position in which to start searching for positions.
  LineColumn starting_position;

  // The regular expression to search.
  wstring search_query;
};

std::vector<LineColumn> SearchHandler(
    EditorState* editor_state, const SearchOptions& options);

}  // namespace editor
}  // namespace afc

#endif

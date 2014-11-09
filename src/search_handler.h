#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>

namespace afc {
namespace editor {

using std::string;

struct EditorState;
class OpenBuffer;
class LineColumn;

void SearchHandlerPredictor(
    EditorState* editor_state,
    const string& current_query,
    OpenBuffer* target);

// starting_position must be the position in which the buffer was when the
// search was started.  This is used to detect if the user has already navigated
// the search through the predictor, in which case there's not much work to do.
void SearchHandler(const LineColumn& starting_position,
                   const string& input,
                   EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif

#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>

namespace afc {
namespace editor {

using std::string;

struct EditorState;
class OpenBuffer;

void SearchHandlerPredictor(
    EditorState* editor_state,
    const string& current_query,
    OpenBuffer* target);

void SearchHandler(const string& input, EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif

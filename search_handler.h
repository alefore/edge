#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>

namespace afc {
namespace editor {

using std::string;

struct EditorState;

void SearchHandler(const string& input, EditorState* editor_state);

}  // namespace editor
}  // namespace afc

#endif

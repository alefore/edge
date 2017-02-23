#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <string>

#include "buffer.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::string;

int MaybeConnectToServer(const string& address, wstring* error);
int MaybeConnectToParentServer(wstring* error);

class EditorState;

void StartServer(EditorState* editor_state);

shared_ptr<OpenBuffer>
OpenServerBuffer(EditorState* editor_state, const wstring& address);

}  // namespace editor
}  // namespace afc

#endif

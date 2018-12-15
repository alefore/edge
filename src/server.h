#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <string>
#include <unordered_set>

#include "buffer.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::string;

wstring CppEscapeString(wstring input);

void Daemonize(const std::unordered_set<int>& surviving_fd);

int MaybeConnectToServer(const string& address, wstring* error);
int MaybeConnectToParentServer(wstring* error);

class EditorState;

// address can be empty, in which case it'll use a temporary file in /tmp. The
// actual address used is returned through output parameter actual_address.
bool StartServer(EditorState* editor_state, wstring address,
                 wstring* actual_address, wstring* error);

shared_ptr<OpenBuffer> OpenServerBuffer(EditorState* editor_state,
                                        const wstring& address);

}  // namespace editor
}  // namespace afc

#endif

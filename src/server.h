#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <string>
#include <unordered_set>

#include "src/buffer.h"
#include "src/infrastructure/dirname.h"
#include "src/language/value_or_error.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::string;

void Daemonize(const std::unordered_set<int>& surviving_fd);

ValueOrError<int> MaybeConnectToServer(const Path& address);
ValueOrError<int> MaybeConnectToParentServer();

class EditorState;

// address can be empty, in which case it'll use a temporary file in /tmp. The
// actual address used is returned.
ValueOrError<Path> StartServer(EditorState& editor_state,
                               std::optional<Path> address);

shared_ptr<OpenBuffer> OpenServerBuffer(EditorState& editor_state,
                                        const Path& address);

}  // namespace editor
}  // namespace afc

#endif

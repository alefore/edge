#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <unordered_set>

#include "src/buffer.h"
#include "src/infrastructure/dirname.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"

namespace afc::editor {

// TODO(easy, 2022-04-20): Convert to FileDescriptor.
void Daemonize(const std::unordered_set<int>& surviving_fd);

language::ValueOrError<int> MaybeConnectToServer(
    const infrastructure::Path& address);
language::ValueOrError<int> MaybeConnectToParentServer();

class EditorState;

// address can be empty, in which case it'll use a temporary file in /tmp. The
// actual address used is returned.
language::ValueOrError<infrastructure::Path> StartServer(
    EditorState& editor_state, std::optional<infrastructure::Path> address);

language::NonNull<std::shared_ptr<OpenBuffer>> OpenServerBuffer(
    EditorState& editor_state, const infrastructure::Path& address);

}  // namespace afc::editor

#endif

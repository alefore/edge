#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <unordered_set>

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/language/value_or_error.h"

namespace afc::editor {

void Daemonize(
    const std::unordered_set<infrastructure::FileDescriptor>& surviving_fd);

language::ValueOrError<infrastructure::FileDescriptor> ConnectToServer(
    const infrastructure::Path& address);
language::ValueOrError<infrastructure::FileDescriptor> ConnectToParentServer();

class EditorState;
class OpenBuffer;

// address can be empty, in which case it'll use a temporary file in /tmp. The
// actual address used is returned.
language::ValueOrError<infrastructure::Path> StartServer(
    EditorState& editor_state, std::optional<infrastructure::Path> address);

language::gc::Root<OpenBuffer> OpenServerBuffer(
    EditorState& editor_state, const infrastructure::Path& address);

}  // namespace afc::editor

#endif

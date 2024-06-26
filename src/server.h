#ifndef __AFC_EDITOR_SERVER_H__
#define __AFC_EDITOR_SERVER_H__

#include <memory>
#include <unordered_set>

#include "src/infrastructure/dirname.h"
#include "src/infrastructure/file_system_driver.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::editor {

void Daemonize(
    const std::unordered_set<infrastructure::FileDescriptor>& surviving_fd);

// These methods block. You may want to call them in a background thread.
language::PossibleError SyncSendCommandsToServer(
    infrastructure::FileDescriptor server_fd,
    language::lazy_string::LazyString commands_to_run);
language::ValueOrError<infrastructure::FileDescriptor> SyncConnectToServer(
    const infrastructure::Path& address);
language::ValueOrError<infrastructure::FileDescriptor>
SyncConnectToParentServer();

class EditorState;
class OpenBuffer;

// address can be empty, in which case it'll use a temporary file in /tmp. The
// actual address used is returned.
language::ValueOrError<infrastructure::Path> StartServer(
    EditorState& editor_state, std::optional<infrastructure::Path> address);

void OpenServerBuffer(EditorState& editor_state,
                      const infrastructure::Path& address);

}  // namespace afc::editor

#endif

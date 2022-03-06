#include "src/command.h"

#include <glog/logging.h>

namespace afc::editor {
Command::CursorMode Command::cursor_mode() const {
  return CursorMode::kDefault;
}
}  // namespace afc::editor

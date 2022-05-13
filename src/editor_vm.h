#include "src/language/gc.h"

namespace afc::vm {
class Environment;
}
namespace afc::editor {
class EditorState;
// Builds the environment for a given editor instance. The editor instance may
// not be fully constructed; we will at most call EditorState::gc_pool.
language::gc::Root<vm::Environment> BuildEditorEnvironment(EditorState& editor);
}  // namespace afc::editor

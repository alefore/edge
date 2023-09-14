#include "src/language/gc.h"
#include "src/vm/public/callbacks.h"

namespace afc::vm {
class Environment;
}  // namespace afc::vm
namespace afc::editor {
class EditorState;
// Builds the environment for a given editor instance. The editor instance may
// not be fully constructed; we will at most call EditorState::gc_pool.
language::gc::Root<vm::Environment> BuildEditorEnvironment(EditorState& editor);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<afc::editor::EditorState> {
  static editor::EditorState& get(Value& value);
  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm

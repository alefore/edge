#include "src/infrastructure/file_system_driver.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"

namespace afc::vm {
class Environment;
}  // namespace afc::vm
namespace afc::editor {
class EditorState;
// Builds the environment for a given editor.
language::gc::Root<vm::Environment> BuildEditorEnvironment(
    language::gc::Pool& pool,
    language::NonNull<std::shared_ptr<infrastructure::FileSystemDriver>>);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<afc::editor::EditorState> {
  static editor::EditorState& get(Value& value);
  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm

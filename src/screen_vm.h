#ifndef __AFC_EDITOR_SCREEN_VM_H__
#define __AFC_EDITOR_SCREEN_VM_H__

#include <memory>

#include "src/infrastructure/screen/screen.h"
#include "src/vm/callbacks.h"
#include "src/vm/escape.h"

namespace afc::infrastructure {
class FileDescriptor;
}
namespace afc::vm {
struct VMType;
class Environment;
}  // namespace afc::vm
namespace afc::editor {
class EditorState;
void RegisterScreenType(EditorState& editor, vm::Environment& environment);
std::unique_ptr<infrastructure::screen::Screen> NewScreenVm(
    infrastructure::FileDescriptor fd);
const vm::types::ObjectName& GetScreenVmType();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SCREEN_VM_H__

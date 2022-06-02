#ifndef __AFC_EDITOR_SCREEN_VM_H__
#define __AFC_EDITOR_SCREEN_VM_H__

#include <memory>

#include "screen.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/escape.h"

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
std::unique_ptr<Screen> NewScreenVm(infrastructure::FileDescriptor fd);
const vm::VMType& GetScreenVmType();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SCREEN_VM_H__

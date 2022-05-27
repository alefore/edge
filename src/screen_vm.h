#ifndef __AFC_EDITOR_SCREEN_VM_H__
#define __AFC_EDITOR_SCREEN_VM_H__

#include <memory>

#include "screen.h"
#include "src/vm/public/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
struct VMType;
class Environment;
}  // namespace afc::vm
namespace afc::editor {
void RegisterScreenType(language::gc::Pool& pool, vm::Environment& environment);
std::unique_ptr<Screen> NewScreenVm(int fd);
const vm::VMType& GetScreenVmType();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SCREEN_VM_H__

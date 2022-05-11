#ifndef __AFC_EDITOR_SCREEN_VM_H__
#define __AFC_EDITOR_SCREEN_VM_H__

#include "screen.h"
#include "src/vm/public/types.h"

namespace afc {
namespace editor {

void RegisterScreenType(vm::Environment& environment);
std::unique_ptr<Screen> NewScreenVm(int fd);
const vm::VMType& GetScreenVmType();

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_SCREEN_VM_H__

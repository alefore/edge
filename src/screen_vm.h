#ifndef __AFC_EDITOR_SCREEN_VM_H__
#define __AFC_EDITOR_SCREEN_VM_H__

#include "screen.h"

namespace afc {
namespace editor {

void RegisterScreenType(vm::Environment* environment);
std::unique_ptr<Screen> NewScreenVm(int fd);

}  // namespace afc
}  // namespace editor

#endif  // __AFC_EDITOR_SCREEN_VM_H__

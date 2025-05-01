#ifndef __AFC_EDITOR_SEARCH_HANDLER_VM_H__
#define __AFC_EDITOR_SEARCH_HANDLER_VM_H__

#include <memory>

#include "src/language/gc.h"
#include "src/vm/environment.h"

namespace afc::editor {
void RegisterSearchOptionsVm(language::gc::Pool& pool,
                             vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SEARCH_HANDLER_VM_H__

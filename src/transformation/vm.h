#ifndef __AFC_EDITOR_TRANSFORMATION_VM_H__
#define __AFC_EDITOR_TRANSFORMATION_VM_H__

#include <glog/logging.h>

#include <list>
#include <memory>

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
}  // namespace afc::vm
namespace afc::editor {
void RegisterTransformations(language::gc::Pool& pool,
                             vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_VM_H__

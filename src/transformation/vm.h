#ifndef __AFC_EDITOR_TRANSFORMATION_VM_H__
#define __AFC_EDITOR_TRANSFORMATION_VM_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/language/safe_types.h"
#include "src/transformation/type.h"
#include "src/vm/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
template <>
const types::ObjectName VMTypeMapper<language::NonNull<
    std::shared_ptr<afc::editor::transformation::Variant>>>::object_type_name;
}  // namespace afc::vm
namespace afc::editor {
void RegisterTransformations(language::gc::Pool& pool,
                             vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_VM_H__

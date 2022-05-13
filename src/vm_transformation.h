#ifndef __AFC_EDITOR_VM_TRANSFORMATION_H__
#define __AFC_EDITOR_VM_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/language/safe_types.h"
#include "src/transformation.h"
#include "src/vm/public/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;

template <>
struct VMTypeMapper<editor::transformation::Variant*> {
  static editor::transformation::Variant* get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::transformation::Variant* value);
  static const VMType vmtype;
};
}  // namespace afc::vm
namespace afc::editor {
void RegisterTransformations(language::gc::Pool& pool,
                             vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_VM_TRANSFORMATION_H__

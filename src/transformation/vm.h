#ifndef __AFC_EDITOR_TRANSFORMATION_VM_H__
#define __AFC_EDITOR_TRANSFORMATION_VM_H__

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
struct VMTypeMapper<language::NonNull<editor::transformation::Variant*>> {
  static language::NonNull<editor::transformation::Variant*> get(Value& value);
  static const VMType vmtype;
};

template <>
struct VMTypeMapper<
    language::NonNull<std::unique_ptr<editor::transformation::Variant>>> {
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      language::NonNull<std::unique_ptr<editor::transformation::Variant>>
          value);
  static const VMType vmtype;
};
}  // namespace afc::vm
namespace afc::editor {
void RegisterTransformations(language::gc::Pool& pool,
                             vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_VM_H__

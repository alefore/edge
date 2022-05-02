#ifndef __AFC_EDITOR_VM_TRANSFORMATION_H__
#define __AFC_EDITOR_VM_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/language/safe_types.h"
#include "src/transformation.h"
#include "src/vm/public/callbacks.h"

namespace afc::vm {
class Environment;

template <>
struct VMTypeMapper<editor::transformation::Variant*> {
  static editor::transformation::Variant* get(Value& value);
  static language::NonNull<Value::Ptr> New(
      editor::transformation::Variant* value);
  static const VMType vmtype;
};
}  // namespace afc::vm
namespace afc::editor {
void RegisterTransformations(EditorState* editor, vm::Environment* environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_VM_TRANSFORMATION_H__

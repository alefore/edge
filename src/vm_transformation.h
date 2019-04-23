#ifndef __AFC_EDITOR_VM_TRANSFORMATION_H__
#define __AFC_EDITOR_VM_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/transformation.h"
#include "src/vm/public/callbacks.h"

namespace afc {
namespace vm {
class Environment;

template <>
struct VMTypeMapper<editor::Transformation*> {
  static editor::Transformation* get(Value* value);
  static Value::Ptr New(editor::Transformation* value);
  static const VMType vmtype;
};
}  // namespace vm
namespace editor {
void RegisterTransformations(EditorState* editor, vm::Environment* environment);
}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VM_TRANSFORMATION_H__

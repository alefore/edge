#ifndef __AFC_EDITOR_LINE_COLUMN_VM_H__
#define __AFC_EDITOR_LINE_COLUMN_VM_H__

#include <limits>
#include <string>

#include "src/language/safe_types.h"
#include "src/line_column.h"
#include "src/vm/public/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
template <>
struct VMTypeMapper<editor::LineColumn> {
  static editor::LineColumn get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::LineColumn value);
  static const VMTypeObjectTypeName object_type_name;
};
template <>
struct VMTypeMapper<editor::LineColumnDelta> {
  static editor::LineColumnDelta get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::LineColumnDelta value);
  static const VMTypeObjectTypeName object_type_name;
};
template <>
struct VMTypeMapper<editor::Range> {
  static editor::Range get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       editor::Range value);
  static const VMTypeObjectTypeName object_type_name;
};
}  // namespace afc::vm
namespace afc::editor {
void LineColumnRegister(language::gc::Pool& pool, vm::Environment& environment);
void LineColumnDeltaRegister(language::gc::Pool& pool,
                             vm::Environment& environment);
void RangeRegister(language::gc::Pool& pool, vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_LINE_COLUMN_VM_H__

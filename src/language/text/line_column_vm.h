#ifndef __AFC_EDITOR_LINE_COLUMN_VM_H__
#define __AFC_EDITOR_LINE_COLUMN_VM_H__

#include <limits>
#include <string>

#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/vm/public/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
template <>
struct VMTypeMapper<language::text::LineColumn> {
  static language::text::LineColumn get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       language::text::LineColumn value);
  static const types::ObjectName object_type_name;
};
template <>
struct VMTypeMapper<language::text::LineColumnDelta> {
  static language::text::LineColumnDelta get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       language::text::LineColumnDelta value);
  static const types::ObjectName object_type_name;
};
template <>
struct VMTypeMapper<language::text::Range> {
  static language::text::Range get(Value& value);
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       language::text::Range value);
  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm
namespace afc::language::text {
void LineColumnRegister(language::gc::Pool& pool, vm::Environment& environment);
void LineColumnDeltaRegister(language::gc::Pool& pool,
                             vm::Environment& environment);
void RangeRegister(language::gc::Pool& pool, vm::Environment& environment);
}  // namespace afc::language::text

#endif  // __AFC_EDITOR_LINE_COLUMN_VM_H__

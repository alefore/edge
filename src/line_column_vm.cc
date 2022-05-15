#include "src/line_column_vm.h"

#include <set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/set.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"

using afc::language::MakeNonNullUnique;

namespace gc = afc::language::gc;

namespace afc::vm {
template <>
const VMType VMTypeMapper<std::vector<editor::LineColumn>*>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"VectorLineColumn"));

template <>
const VMType VMTypeMapper<std::set<editor::LineColumn>*>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"SetLineColumn"));

/* static */
editor::LineColumn VMTypeMapper<editor::LineColumn>::get(Value& value) {
  return *static_cast<editor::LineColumn*>(value.get_user_value(vmtype).get());
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumn>::New(
    gc::Pool& pool, editor::LineColumn value) {
  return Value::NewObject(
      pool, vmtype.object_type,
      shared_ptr<void>(new editor::LineColumn(value), [](void* v) {
        delete static_cast<editor::LineColumn*>(v);
      }));
}

const VMType VMTypeMapper<editor::LineColumn>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"LineColumn"));

/* static */
editor::LineColumnDelta VMTypeMapper<editor::LineColumnDelta>::get(
    Value& value) {
  return *static_cast<editor::LineColumnDelta*>(
      value.get_user_value(vmtype).get());
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumnDelta>::New(
    gc::Pool& pool, editor::LineColumnDelta value) {
  return Value::NewObject(
      pool, vmtype.object_type,
      shared_ptr<void>(new editor::LineColumnDelta(value), [](void* v) {
        delete static_cast<editor::LineColumnDelta*>(v);
      }));
}

const VMType VMTypeMapper<editor::LineColumnDelta>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"LineColumnDelta"));

/* static */
editor::Range VMTypeMapper<editor::Range>::get(Value& value) {
  return *static_cast<editor::Range*>(value.get_user_value(vmtype).get());
}

/* static */
gc::Root<Value> VMTypeMapper<editor::Range>::New(gc::Pool& pool,
                                                 editor::Range range) {
  return Value::NewObject(
      pool, vmtype.object_type,
      shared_ptr<void>(new editor::Range(range),
                       [](void* v) { delete static_cast<editor::Range*>(v); }));
}

const VMType VMTypeMapper<editor::Range>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Range"));
}  // namespace afc::vm
namespace afc::editor {
using vm::Environment;
using vm::NewCallback;
using vm::ObjectType;
using vm::VMTypeMapper;
using vm::VMTypeObjectTypeName;

void LineColumnRegister(gc::Pool& pool, Environment& environment) {
  auto line_column =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<LineColumn>::vmtype);

  // Methods for LineColumn.
  environment.Define(
      L"LineColumn", NewCallback(pool, [](int line_number, int column_number) {
        return LineColumn(LineNumber(line_number), ColumnNumber(column_number));
      }));

  line_column->AddField(L"line", NewCallback(pool, [](LineColumn line_column) {
                          return static_cast<int>(line_column.line.line);
                        }));

  line_column->AddField(L"column",
                        NewCallback(pool, [](LineColumn line_column) {
                          return static_cast<int>(line_column.column.column);
                        }));

  line_column->AddField(
      L"tostring", NewCallback(pool, [](LineColumn line_column) {
        return std::to_wstring(line_column.line.line) + L", " +
               std::to_wstring(line_column.column.column);
      }));

  environment.DefineType(std::move(line_column));
}

void RangeRegister(gc::Pool& pool, Environment& environment) {
  auto range = MakeNonNullUnique<ObjectType>(VMTypeMapper<Range>::vmtype);

  // Methods for Range.
  environment.Define(L"Range",
                     NewCallback(pool, [](LineColumn begin, LineColumn end) {
                       return Range(begin, end);
                     }));

  range->AddField(L"begin",
                  NewCallback(pool, [](Range range) { return range.begin; }));

  range->AddField(L"end",
                  NewCallback(pool, [](Range range) { return range.end; }));

  environment.DefineType(std::move(range));

  vm::VMTypeMapper<std::vector<LineColumn>*>::Export(pool, environment);
  vm::VMTypeMapper<std::set<LineColumn>*>::Export(pool, environment);
}
}  // namespace afc::editor
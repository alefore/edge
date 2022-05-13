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
    VMType::ObjectType(L"VectorLineColumn");

template <>
const VMType VMTypeMapper<std::set<editor::LineColumn>*>::vmtype =
    VMType::ObjectType(L"SetLineColumn");
}  // namespace afc::vm

namespace afc::editor {
/* static */ void LineColumn::Register(language::gc::Pool& pool,
                                       vm::Environment* environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto line_column = MakeNonNullUnique<ObjectType>(L"LineColumn");

  // Methods for LineColumn.
  environment->Define(
      L"LineColumn",
      vm::NewCallback(pool, [](int line_number, int column_number) {
        return LineColumn(LineNumber(line_number), ColumnNumber(column_number));
      }));

  line_column->AddField(L"line",
                        vm::NewCallback(pool, [](LineColumn line_column) {
                          return static_cast<int>(line_column.line.line);
                        }));

  line_column->AddField(L"column",
                        vm::NewCallback(pool, [](LineColumn line_column) {
                          return static_cast<int>(line_column.column.column);
                        }));

  line_column->AddField(
      L"tostring", vm::NewCallback(pool, [](LineColumn line_column) {
        return std::to_wstring(line_column.line.line) + L", " +
               std::to_wstring(line_column.column.column);
      }));

  environment->DefineType(L"LineColumn", std::move(line_column));
}

/* static */ void Range::Register(language::gc::Pool& pool,
                                  vm::Environment* environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto range = MakeNonNullUnique<ObjectType>(L"Range");

  // Methods for Range.
  environment->Define(
      L"Range", vm::NewCallback(pool, [](LineColumn begin, LineColumn end) {
        return Range(begin, end);
      }));

  range->AddField(
      L"begin", vm::NewCallback(pool, [](Range range) { return range.begin; }));

  range->AddField(L"end",
                  vm::NewCallback(pool, [](Range range) { return range.end; }));

  environment->DefineType(L"Range", std::move(range));
  vm::VMTypeMapper<std::vector<LineColumn>*>::Export(pool, environment);
  vm::VMTypeMapper<std::set<LineColumn>*>::Export(pool, environment);
}
}  // namespace afc::editor

namespace afc::vm {
/* static */
editor::LineColumn VMTypeMapper<editor::LineColumn>::get(Value& value) {
  CHECK_EQ(value.type, vmtype);
  CHECK(value.user_value != nullptr);
  return *static_cast<editor::LineColumn*>(value.user_value.get());
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
    VMType::ObjectType(L"LineColumn");

/* static */
editor::LineColumnDelta VMTypeMapper<editor::LineColumnDelta>::get(
    Value& value) {
  CHECK_EQ(value.type, vmtype);
  CHECK(value.user_value != nullptr);
  return *static_cast<editor::LineColumnDelta*>(value.user_value.get());
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
    VMType::ObjectType(L"LineColumnDelta");

/* static */
editor::Range VMTypeMapper<editor::Range>::get(Value& value) {
  CHECK_EQ(value.type, vmtype);
  CHECK(value.user_value != nullptr);
  return *static_cast<editor::Range*>(value.user_value.get());
}

/* static */
gc::Root<Value> VMTypeMapper<editor::Range>::New(gc::Pool& pool,
                                                 editor::Range range) {
  return Value::NewObject(
      pool, L"Range", shared_ptr<void>(new editor::Range(range), [](void* v) {
        delete static_cast<editor::Range*>(v);
      }));
}

const VMType VMTypeMapper<editor::Range>::vmtype = VMType::ObjectType(L"Range");
}  // namespace afc::vm

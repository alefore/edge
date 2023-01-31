#include "src/line_column_vm.h"

#include <set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/container.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;

namespace gc = afc::language::gc;

namespace afc::vm {
template <>
const VMTypeObjectTypeName VMTypeMapper<NonNull<
    std::shared_ptr<std::vector<editor::LineColumn>>>>::object_type_name =
    VMTypeObjectTypeName(L"VectorLineColumn");

template <>
const VMTypeObjectTypeName VMTypeMapper<
    NonNull<std::shared_ptr<std::set<editor::LineColumn>>>>::object_type_name =
    VMTypeObjectTypeName(L"SetLineColumn");

/* static */
editor::LineColumn VMTypeMapper<editor::LineColumn>::get(Value& value) {
  return value.get_user_value<editor::LineColumn>(object_type_name).value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumn>::New(
    gc::Pool& pool, editor::LineColumn value) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<editor::LineColumn>(value));
}

const VMTypeObjectTypeName VMTypeMapper<editor::LineColumn>::object_type_name =
    VMTypeObjectTypeName(L"LineColumn");

/* static */
editor::LineColumnDelta VMTypeMapper<editor::LineColumnDelta>::get(
    Value& value) {
  return value.get_user_value<editor::LineColumnDelta>(object_type_name)
      .value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumnDelta>::New(
    gc::Pool& pool, editor::LineColumnDelta value) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<editor::LineColumnDelta>(value));
}

const VMTypeObjectTypeName
    VMTypeMapper<editor::LineColumnDelta>::object_type_name =
        VMTypeObjectTypeName(L"LineColumnDelta");

/* static */
editor::Range VMTypeMapper<editor::Range>::get(Value& value) {
  return value.get_user_value<editor::Range>(object_type_name).value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::Range>::New(gc::Pool& pool,
                                                 editor::Range range) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<editor::Range>(range));
}

const VMTypeObjectTypeName VMTypeMapper<editor::Range>::object_type_name =
    VMTypeObjectTypeName(L"Range");
}  // namespace afc::vm
namespace afc::editor {
using vm::Environment;
using vm::NewCallback;
using vm::ObjectType;
using vm::PurityType;
using vm::VMTypeMapper;
using vm::VMTypeObjectTypeName;

void LineColumnRegister(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> line_column =
      ObjectType::New(pool, VMTypeMapper<LineColumn>::object_type_name);

  // Methods for LineColumn.
  environment.Define(L"LineColumn",
                     NewCallback(pool, PurityType::kPure,
                                 [](int line_number, int column_number) {
                                   return LineColumn(
                                       LineNumber(line_number),
                                       ColumnNumber(column_number));
                                 }));

  line_column.ptr()->AddField(
      L"line", NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
                 return static_cast<int>(line_column.line.read());
               }).ptr());

  line_column.ptr()->AddField(
      L"column",
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return static_cast<int>(line_column.column.read());
      }).ptr());

  line_column.ptr()->AddField(
      L"tostring",
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return std::to_wstring(line_column.line.read()) + L", " +
               std::to_wstring(line_column.column.read());
      }).ptr());

  environment.DefineType(line_column.ptr());
}

void LineColumnDeltaRegister(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> line_column_delta =
      ObjectType::New(pool, VMTypeMapper<LineColumnDelta>::object_type_name);

  // Methods for LineColumn.
  environment.Define(L"LineColumnDelta",
                     NewCallback(pool, PurityType::kPure,
                                 [](int line_number, int column_number) {
                                   return LineColumnDelta(
                                       LineNumberDelta(line_number),
                                       ColumnNumberDelta(column_number));
                                 }));

  line_column_delta.ptr()->AddField(
      L"line", NewCallback(pool, PurityType::kPure,
                           [](LineColumnDelta line_column_delta) {
                             return line_column_delta.line.read();
                           })
                   .ptr());

  line_column_delta.ptr()->AddField(
      L"column", NewCallback(pool, PurityType::kPure,
                             [](LineColumnDelta line_column_delta) {
                               return line_column_delta.column.read();
                             })
                     .ptr());

  line_column_delta.ptr()->AddField(
      L"tostring",
      NewCallback(pool, PurityType::kPure,
                  [](LineColumnDelta line_column_delta) {
                    return std::to_wstring(line_column_delta.line.read()) +
                           L", " +
                           std::to_wstring(line_column_delta.column.read());
                  })
          .ptr());

  environment.DefineType(line_column_delta.ptr());
}

void RangeRegister(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> range =
      ObjectType::New(pool, VMTypeMapper<Range>::object_type_name);

  // Methods for Range.
  environment.Define(L"Range",
                     NewCallback(pool, PurityType::kPure,
                                 [](LineColumn begin, LineColumn end) {
                                   return Range(begin, end);
                                 }));

  range.ptr()->AddField(L"begin",
                        NewCallback(pool, PurityType::kPure, [](Range range) {
                          return range.begin;
                        }).ptr());

  range.ptr()->AddField(L"end",
                        NewCallback(pool, PurityType::kPure, [](Range range) {
                          return range.end;
                        }).ptr());

  environment.DefineType(range.ptr());

  vm::container::Export<typename std::vector<LineColumn>>(pool, environment);
  vm::container::Export<typename std::set<LineColumn>>(pool, environment);
}
}  // namespace afc::editor

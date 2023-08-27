#include "src/language/text/line_column_vm.h"

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
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    std::vector<language::text::LineColumn>>>>::object_type_name =
    types::ObjectName(L"VectorLineColumn");

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<std::set<language::text::LineColumn>>>>::object_type_name =
    types::ObjectName(L"SetLineColumn");

/* static */
language::text::LineColumn VMTypeMapper<language::text::LineColumn>::get(
    Value& value) {
  return value.get_user_value<language::text::LineColumn>(object_type_name)
      .value();
}

/* static */
gc::Root<Value> VMTypeMapper<language::text::LineColumn>::New(
    gc::Pool& pool, language::text::LineColumn value) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<language::text::LineColumn>(value));
}

const types::ObjectName
    VMTypeMapper<language::text::LineColumn>::object_type_name =
        types::ObjectName(L"LineColumn");

/* static */
language::text::LineColumnDelta
VMTypeMapper<language::text::LineColumnDelta>::get(Value& value) {
  return value.get_user_value<language::text::LineColumnDelta>(object_type_name)
      .value();
}

/* static */
gc::Root<Value> VMTypeMapper<language::text::LineColumnDelta>::New(
    gc::Pool& pool, language::text::LineColumnDelta value) {
  return Value::NewObject(
      pool, object_type_name,
      MakeNonNullShared<language::text::LineColumnDelta>(value));
}

const types::ObjectName
    VMTypeMapper<language::text::LineColumnDelta>::object_type_name =
        types::ObjectName(L"LineColumnDelta");

/* static */
language::text::Range VMTypeMapper<language::text::Range>::get(Value& value) {
  return value.get_user_value<language::text::Range>(object_type_name).value();
}

/* static */
gc::Root<Value> VMTypeMapper<language::text::Range>::New(
    gc::Pool& pool, language::text::Range range) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<language::text::Range>(range));
}

const types::ObjectName VMTypeMapper<language::text::Range>::object_type_name =
    types::ObjectName(L"Range");
}  // namespace afc::vm
namespace afc::language::text {
using vm::Environment;
using vm::NewCallback;
using vm::ObjectType;
using vm::PurityType;
using vm::VMTypeMapper;
using vm::types::ObjectName;

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
}  // namespace afc::language::text

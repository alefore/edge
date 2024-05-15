#include "src/language/text/line_column_vm.h"

#include <set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"
#include "src/vm/optional.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::vm::Identifier;

namespace afc::vm {
template <>
const types::ObjectName VMTypeMapper<NonNull<std::shared_ptr<
    std::vector<language::text::LineColumn>>>>::object_type_name =
    types::ObjectName(L"VectorLineColumn");

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<std::set<language::text::LineColumn>>>>::object_type_name =
    types::ObjectName(L"SetLineColumn");

template <>
const types::ObjectName VMTypeMapper<NonNull<
    std::shared_ptr<std::optional<language::text::Range>>>>::object_type_name =
    types::ObjectName(L"OptionalRange");

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
  gc::Root<ObjectType> line_column_type =
      ObjectType::New(pool, VMTypeMapper<LineColumn>::object_type_name);

  // Methods for LineColumn.
  environment.Define(
      Identifier(L"LineColumn"),
      NewCallback(pool, PurityType::kPure, [](int line, int column) {
        return LineColumn(LineNumber(line), ColumnNumber(column));
      }));

  line_column_type.ptr()->AddField(
      Identifier(L"line"),
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return line_column.line.read();
      }).ptr());

  line_column_type.ptr()->AddField(
      Identifier(L"column"),
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return line_column.column.read();
      }).ptr());

  line_column_type.ptr()->AddField(
      Identifier(L"tostring"),
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return std::to_wstring(line_column.line.read()) + L", " +
               std::to_wstring(line_column.column.read());
      }).ptr());

  environment.DefineType(line_column_type.ptr());
}

void LineColumnDeltaRegister(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> line_column_delta_type =
      ObjectType::New(pool, VMTypeMapper<LineColumnDelta>::object_type_name);

  // Methods for LineColumn.
  environment.Define(
      Identifier(L"LineColumnDelta"),
      NewCallback(pool, PurityType::kPure, [](int line, int column) {
        return LineColumnDelta(LineNumberDelta(line),
                               ColumnNumberDelta(column));
      }));

  line_column_delta_type.ptr()->AddField(
      Identifier(L"line"), NewCallback(pool, PurityType::kPure,
                                       [](LineColumnDelta line_column_delta) {
                                         return line_column_delta.line.read();
                                       })
                               .ptr());

  line_column_delta_type.ptr()->AddField(
      Identifier(L"column"),
      NewCallback(pool, PurityType::kPure,
                  [](LineColumnDelta line_column_delta) {
                    return line_column_delta.column.read();
                  })
          .ptr());

  line_column_delta_type.ptr()->AddField(
      Identifier(L"tostring"),
      NewCallback(pool, PurityType::kPure,
                  [](LineColumnDelta line_column_delta) {
                    return std::to_wstring(line_column_delta.line.read()) +
                           L", " +
                           std::to_wstring(line_column_delta.column.read());
                  })
          .ptr());

  environment.DefineType(line_column_delta_type.ptr());
}

void RangeRegister(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> range_type =
      ObjectType::New(pool, VMTypeMapper<Range>::object_type_name);

  // Methods for Range.
  environment.Define(Identifier(L"Range"),
                     NewCallback(pool, PurityType::kPure,
                                 [](LineColumn begin, LineColumn end) {
                                   return Range(begin, end);
                                 }));

  range_type.ptr()->AddField(
      Identifier(L"begin"),
      NewCallback(pool, PurityType::kPure, [](Range range) {
        return range.begin();
      }).ptr());

  range_type.ptr()->AddField(
      Identifier(L"end"), NewCallback(pool, PurityType::kPure, [](Range range) {
                            return range.end();
                          }).ptr());

  environment.DefineType(range_type.ptr());

  vm::container::Export<typename std::vector<LineColumn>>(pool, environment);
  vm::container::Export<typename std::set<LineColumn>>(pool, environment);
  vm::optional::Export<Range>(pool, environment);
}
}  // namespace afc::language::text

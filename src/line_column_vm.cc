#include "src/line_column_vm.h"

#include <set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/container.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"

using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;

namespace gc = afc::language::gc;

namespace afc::vm {
template <>
const VMType VMTypeMapper<
    NonNull<std::shared_ptr<std::vector<editor::LineColumn>>>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"VectorLineColumn"));

template <>
const VMType VMTypeMapper<
    NonNull<std::shared_ptr<std::set<editor::LineColumn>>>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"SetLineColumn"));

/* static */
editor::LineColumn VMTypeMapper<editor::LineColumn>::get(Value& value) {
  return value.get_user_value<editor::LineColumn>(vmtype).value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumn>::New(
    gc::Pool& pool, editor::LineColumn value) {
  return Value::NewObject(pool, vmtype.object_type,
                          MakeNonNullShared<editor::LineColumn>(value));
}

const VMType VMTypeMapper<editor::LineColumn>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"LineColumn"));

/* static */
editor::LineColumnDelta VMTypeMapper<editor::LineColumnDelta>::get(
    Value& value) {
  return value.get_user_value<editor::LineColumnDelta>(vmtype).value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::LineColumnDelta>::New(
    gc::Pool& pool, editor::LineColumnDelta value) {
  return Value::NewObject(pool, vmtype.object_type,
                          MakeNonNullShared<editor::LineColumnDelta>(value));
}

const VMType VMTypeMapper<editor::LineColumnDelta>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"LineColumnDelta"));

/* static */
editor::Range VMTypeMapper<editor::Range>::get(Value& value) {
  return value.get_user_value<editor::Range>(vmtype).value();
}

/* static */
gc::Root<Value> VMTypeMapper<editor::Range>::New(gc::Pool& pool,
                                                 editor::Range range) {
  return Value::NewObject(pool, vmtype.object_type,
                          MakeNonNullShared<editor::Range>(range));
}

const VMType VMTypeMapper<editor::Range>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Range"));
}  // namespace afc::vm
namespace afc::editor {
using vm::Environment;
using vm::NewCallback;
using vm::ObjectType;
using vm::PurityType;
using vm::VMTypeMapper;
using vm::VMTypeObjectTypeName;

void LineColumnRegister(gc::Pool& pool, Environment& environment) {
  auto line_column =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<LineColumn>::vmtype);

  // Methods for LineColumn.
  environment.Define(L"LineColumn",
                     NewCallback(pool, PurityType::kPure,
                                 [](int line_number, int column_number) {
                                   return LineColumn(
                                       LineNumber(line_number),
                                       ColumnNumber(column_number));
                                 }));

  line_column->AddField(
      L"line", NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return static_cast<int>(line_column.line.read());
      }));

  line_column->AddField(
      L"column",
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return static_cast<int>(line_column.column.read());
      }));

  line_column->AddField(
      L"tostring",
      NewCallback(pool, PurityType::kPure, [](LineColumn line_column) {
        return std::to_wstring(line_column.line.read()) + L", " +
               std::to_wstring(line_column.column.read());
      }));

  environment.DefineType(std::move(line_column));
}

void LineColumnDeltaRegister(gc::Pool& pool, Environment& environment) {
  auto line_column_delta =
      MakeNonNullUnique<ObjectType>(VMTypeMapper<LineColumnDelta>::vmtype);

  // Methods for LineColumn.
  environment.Define(L"LineColumnDelta",
                     NewCallback(pool, PurityType::kPure,
                                 [](int line_number, int column_number) {
                                   return LineColumnDelta(
                                       LineNumberDelta(line_number),
                                       ColumnNumberDelta(column_number));
                                 }));

  line_column_delta->AddField(
      L"line",
      NewCallback(pool, PurityType::kPure,
                  [](LineColumnDelta line_column_delta) {
                    // TODO(easy, 2022-06-05): Why the need for the cast?
                    return static_cast<int>(line_column_delta.line.read());
                  }));

  line_column_delta->AddField(
      L"column",
      NewCallback(pool, PurityType::kPure,
                  [](LineColumnDelta line_column_delta) {
                    // TODO(easy, 2022-06-05): Why the need for the cast?
                    return static_cast<int>(line_column_delta.column.read());
                  }));

  line_column_delta->AddField(
      L"tostring",
      NewCallback(
          pool, PurityType::kPure, [](LineColumnDelta line_column_delta) {
            return std::to_wstring(line_column_delta.line.read()) + L", " +
                   std::to_wstring(line_column_delta.column.read());
          }));

  environment.DefineType(std::move(line_column_delta));
}

void RangeRegister(gc::Pool& pool, Environment& environment) {
  auto range = MakeNonNullUnique<ObjectType>(VMTypeMapper<Range>::vmtype);

  // Methods for Range.
  environment.Define(L"Range",
                     NewCallback(pool, PurityType::kPure,
                                 [](LineColumn begin, LineColumn end) {
                                   return Range(begin, end);
                                 }));

  range->AddField(L"begin",
                  NewCallback(pool, PurityType::kPure,
                              [](Range range) { return range.begin; }));

  range->AddField(L"end", NewCallback(pool, PurityType::kPure,
                                      [](Range range) { return range.end; }));

  environment.DefineType(std::move(range));

  vm::container::Export<typename std::vector<LineColumn>>(pool, environment);
  vm::container::Export<typename std::set<LineColumn>>(pool, environment);
}
}  // namespace afc::editor

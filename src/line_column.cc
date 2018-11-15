#include "line_column.h"

#include <glog/logging.h>

#include "vm/public/environment.h"
#include "vm/public/value.h"
#include "wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const LineColumn& lc) {
    os << "["
       << (lc.line == std::numeric_limits<size_t>::max()
               ? "inf" : std::to_string(lc.line))
       << ":"
       << (lc.column == std::numeric_limits<size_t>::max()
               ? "inf" : std::to_string(lc.column))
       << "]";
    return os;
}

bool LineColumn::operator!=(const LineColumn& other) const {
  return line != other.line || column != other.column;
}

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin << ", " << range.end << ")";
  return os;
}

std::wstring LineColumn::ToCppString() const {
  return L"LineColumn(" + std::to_wstring(line) + L", " +
      std::to_wstring(column) + L")";
}

/* static */ void LineColumn::Register(vm::Environment* environment) {
  using vm::Value;
  using vm::VMType;
  using vm::ObjectType;
  auto line_column = std::make_unique<ObjectType>(L"LineColumn");

  // Methods for LineColumn.
  environment->Define(L"LineColumn", Value::NewFunction(
      { VMType::ObjectType(line_column.get()), VMType::Integer(),
        VMType::Integer() },
        [](std::vector<Value::Ptr> args) {
          CHECK_EQ(args.size(), size_t(2));
          CHECK_EQ(args[0]->type, VMType::VM_INTEGER);
          CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
          return Value::NewObject(L"LineColumn", std::make_shared<LineColumn>(
              args[0]->integer, args[1]->integer));
        }));

  line_column->AddField(L"line", Value::NewFunction(
      { VMType::Integer(), VMType::ObjectType(line_column.get()) },
      [](std::vector<Value::Ptr> args) {
        CHECK_EQ(args.size(), size_t(1));
        CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
        auto line_column = static_cast<LineColumn*>(args[0]->user_value.get());
        CHECK(line_column != nullptr);
        return Value::NewInteger(line_column->line);
      }));

  line_column->AddField(L"column", Value::NewFunction(
      { VMType::Integer(), VMType::ObjectType(line_column.get()) },
      [](std::vector<Value::Ptr> args) {
        CHECK_EQ(args.size(), size_t(1));
        CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
        auto line_column = static_cast<LineColumn*>(args[0]->user_value.get());
        CHECK(line_column != nullptr);
        return Value::NewInteger(line_column->column);
      }));

  environment->DefineType(L"LineColumn", std::move(line_column));
}

} // namespace editor
} // namespace afc

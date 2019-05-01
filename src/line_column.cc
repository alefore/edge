#include "src/line_column.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/vm/public/environment.h"
#include "src/vm/public/set.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vector.h"
#include "src/wstring.h"

namespace afc {
namespace vm {
template <>
const VMType VMTypeMapper<std::vector<editor::LineColumn>*>::vmtype =
    VMType::ObjectType(L"VectorLineColumn");

template <>
const VMType VMTypeMapper<std::set<editor::LineColumn>*>::vmtype =
    VMType::ObjectType(L"SetLineColumn");
}  // namespace vm
namespace editor {

ColumnNumberDelta& ColumnNumberDelta::operator=(
    const ColumnNumberDelta& delta) {
  value = delta.value;
  return *this;
}

bool operator==(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.value == b.value;
}

std::ostream& operator<<(std::ostream& os, const ColumnNumberDelta& lc) {
  os << "[column delta: " << lc.value << "]";
  return os;
}

bool operator<(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.value < b.value;
}
bool operator<=(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.value <= b.value;
}

bool operator>(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.value > b.value;
}

bool operator>=(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.value >= b.value;
}

ColumnNumberDelta operator+(ColumnNumberDelta a, const ColumnNumberDelta& b) {
  a.value += b.value;
  return a;
}

ColumnNumberDelta operator-(ColumnNumberDelta a, const ColumnNumberDelta& b) {
  a.value -= b.value;
  return a;
}

ColumnNumberDelta operator-(ColumnNumberDelta a) {
  a.value = -a.value;
  return a;
}

ColumnNumberDelta operator*(ColumnNumberDelta a, const size_t& b) {
  a.value *= b;
  return a;
}

ColumnNumberDelta operator*(const size_t& a, ColumnNumberDelta b) {
  b.value *= a;
  return b;
}

ColumnNumberDelta operator/(ColumnNumberDelta a, const size_t& b) {
  a.value /= b;
  return a;
}

ColumnNumberDelta& operator+=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value) {
  a.value += value.value;
  return a;
}

ColumnNumberDelta& operator-=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value) {
  a.value -= value.value;
  return a;
}

ColumnNumberDelta& operator++(ColumnNumberDelta& a) {
  a.value++;
  return a;
}

ColumnNumberDelta operator++(ColumnNumberDelta& a, int) {
  ColumnNumberDelta copy = a;
  a.value++;
  return copy;
}

ColumnNumberDelta operator--(ColumnNumberDelta& a, int) {
  ColumnNumberDelta copy = a;
  a.value--;
  return copy;
}

ColumnNumber::ColumnNumber(size_t value) : column(value) {}

ColumnNumber& ColumnNumber::operator=(const ColumnNumber& other) {
  column = other.column;
  return *this;
}

ColumnNumberDelta ColumnNumber::ToDelta() const {
  return *this - ColumnNumber(0);
}

std::wstring ColumnNumber::ToUserString() const {
  return std::to_wstring(column + 1);
}

bool operator==(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column == b.column;
}

bool operator!=(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column != b.column;
}

bool operator<(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column < b.column;
}

bool operator<=(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column <= b.column;
}

bool operator>(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column > b.column;
}

bool operator>=(const ColumnNumber& a, const ColumnNumber& b) {
  return a.column >= b.column;
}

ColumnNumber& operator+=(ColumnNumber& a, const ColumnNumberDelta& delta) {
  a.column += delta.value;
  return a;
}

ColumnNumber& operator++(ColumnNumber& a) {
  a.column++;
  return a;
}

ColumnNumber operator++(ColumnNumber& a, int) {
  auto output = a;
  a.column++;
  return output;
}

ColumnNumber& operator--(ColumnNumber& a) {
  a.column--;
  return a;
}

ColumnNumber operator--(ColumnNumber& a, int) {
  auto output = a;
  a.column--;
  return output;
}

ColumnNumber operator+(ColumnNumber a, const ColumnNumberDelta& delta) {
  if (delta.value < 0) {
    CHECK_GE(a.column, static_cast<size_t>(-delta.value));
  }
  a.column += delta.value;
  return a;
}

ColumnNumber operator-(ColumnNumber a, const ColumnNumberDelta& delta) {
  if (delta.value > 0) {
    CHECK_GE(a.column, static_cast<size_t>(delta.value));
  }
  a.column -= delta.value;
  return a;
}

ColumnNumber operator-(const ColumnNumber& a) {
  return ColumnNumber(-a.column);
}

ColumnNumberDelta operator-(const ColumnNumber& a, const ColumnNumber& b) {
  return ColumnNumberDelta(a.column - b.column);
}

std::ostream& operator<<(std::ostream& os, const ColumnNumber& lc) {
  os << "[Column " << lc.column << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const LineColumn& lc) {
  os << "["
     << (lc.line == std::numeric_limits<size_t>::max()
             ? "inf"
             : std::to_string(lc.line))
     << ":"
     << (lc.column == std::numeric_limits<ColumnNumber>::max()
             ? "inf"
             : std::to_string(lc.column.column))
     << "]";
  return os;
}

bool LineColumn::operator!=(const LineColumn& other) const {
  return line != other.line || column != other.column;
}

std::wstring LineColumn::ToString() const {
  return std::to_wstring(line) + L" " + std::to_wstring(column.column);
}

/* static */ Range Range::InLine(size_t line, ColumnNumber column,
                                 ColumnNumberDelta size) {
  return Range(LineColumn(line, column), LineColumn(line, column + size));
}

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin << ", " << range.end << ")";
  return os;
}

LineColumn LineColumn::operator+(const LineNumberDelta& value) const {
  auto output = *this;
  output += value;
  return output;
}

LineColumn LineColumn::operator-(const LineNumberDelta& value) const {
  return *this + LineNumberDelta(-value.value);
}

LineColumn& LineColumn::operator+=(const LineNumberDelta& value) {
  if (value.value < 0) {
    CHECK_GE(line, static_cast<size_t>(-value.value));
  }
  line += value.value;
  return *this;
}

LineColumn& LineColumn::operator-=(const LineNumberDelta& value) {
  return operator+=(LineNumberDelta(-value.value));
}

LineColumn LineColumn::operator+(const ColumnNumberDelta& value) const {
  return LineColumn(line, column + value);
}

LineColumn LineColumn::operator-(const ColumnNumberDelta& value) const {
  return *this + -value;
}

LineColumn LineColumn::operator+(const LineColumnDelta& value) const {
  return *this + value.line + value.column;
}

std::wstring LineColumn::ToCppString() const {
  return L"LineColumn(" + std::to_wstring(line) + L", " +
         std::to_wstring(column.column) + L")";
}

/* static */ void LineColumn::Register(vm::Environment* environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto line_column = std::make_unique<ObjectType>(L"LineColumn");

  // Methods for LineColumn.
  environment->Define(
      L"LineColumn",
      Value::NewFunction({VMType::ObjectType(line_column.get()),
                          VMType::Integer(), VMType::Integer()},
                         [](std::vector<Value::Ptr> args) {
                           CHECK_EQ(args.size(), size_t(2));
                           CHECK_EQ(args[0]->type, VMType::VM_INTEGER);
                           CHECK_EQ(args[1]->type, VMType::VM_INTEGER);
                           return Value::NewObject(
                               L"LineColumn",
                               std::make_shared<LineColumn>(
                                   args[0]->integer,
                                   ColumnNumber(args[1]->integer)));
                         }));

  line_column->AddField(
      L"line", Value::NewFunction(
                   {VMType::Integer(), VMType::ObjectType(line_column.get())},
                   [](std::vector<Value::Ptr> args) {
                     CHECK_EQ(args.size(), size_t(1));
                     CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                     auto line_column =
                         static_cast<LineColumn*>(args[0]->user_value.get());
                     CHECK(line_column != nullptr);
                     return Value::NewInteger(line_column->line);
                   }));

  line_column->AddField(
      L"column", Value::NewFunction(
                     {VMType::Integer(), VMType::ObjectType(line_column.get())},
                     [](std::vector<Value::Ptr> args) {
                       CHECK_EQ(args.size(), size_t(1));
                       CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
                       auto line_column =
                           static_cast<LineColumn*>(args[0]->user_value.get());
                       CHECK(line_column != nullptr);
                       return Value::NewInteger(line_column->column.column);
                     }));

  line_column->AddField(
      L"tostring",
      Value::NewFunction(
          {VMType::String(), VMType::ObjectType(line_column.get())},
          [](std::vector<Value::Ptr> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto line_column =
                static_cast<LineColumn*>(args[0]->user_value.get());
            CHECK(line_column != nullptr);
            return Value::NewString(
                std::to_wstring(line_column->line) + L", " +
                std::to_wstring(line_column->column.column));
          }));

  environment->DefineType(L"LineColumn", std::move(line_column));
}

/* static */ void Range::Register(vm::Environment* environment) {
  using vm::ObjectType;
  using vm::Value;
  using vm::VMType;
  auto range = std::make_unique<ObjectType>(L"Range");

  // Methods for Range.
  environment->Define(
      L"Range",
      Value::NewFunction(
          {VMType::ObjectType(range.get()), VMType::ObjectType(L"LineColumn"),
           VMType::ObjectType(L"LineColumn")},
          [](std::vector<Value::Ptr> args) {
            CHECK_EQ(args.size(), size_t(2));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            CHECK_EQ(args[1]->type, VMType::OBJECT_TYPE);
            return Value::NewObject(
                L"Range",
                std::make_shared<Range>(
                    *static_cast<LineColumn*>(args[0]->user_value.get()),
                    *static_cast<LineColumn*>(args[1]->user_value.get())));
          }));

  range->AddField(
      L"begin",
      Value::NewFunction(
          {VMType::ObjectType(L"LineColumn"), VMType::ObjectType(range.get())},
          [](std::vector<Value::Ptr> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto range = static_cast<Range*>(args[0]->user_value.get());
            CHECK(range != nullptr);
            return Value::NewObject(L"LineColumn",
                                    std::make_shared<LineColumn>(range->begin));
          }));

  range->AddField(
      L"end",
      Value::NewFunction(
          {VMType::ObjectType(L"LineColumn"), VMType::ObjectType(range.get())},
          [](std::vector<Value::Ptr> args) {
            CHECK_EQ(args.size(), size_t(1));
            CHECK_EQ(args[0]->type, VMType::OBJECT_TYPE);
            auto range = static_cast<Range*>(args[0]->user_value.get());
            CHECK(range != nullptr);
            return Value::NewObject(L"LineColumn",
                                    std::make_shared<LineColumn>(range->end));
          }));

  environment->DefineType(L"Range", std::move(range));
  vm::VMTypeMapper<std::vector<LineColumn>*>::Export(environment);
  vm::VMTypeMapper<std::set<LineColumn>*>::Export(environment);
}

}  // namespace editor
namespace vm {
/* static */
editor::LineColumn VMTypeMapper<editor::LineColumn>::get(Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"LineColumn");
  CHECK(value->user_value != nullptr);
  return *static_cast<editor::LineColumn*>(value->user_value.get());
}

/* static */
Value::Ptr VMTypeMapper<editor::LineColumn>::New(editor::LineColumn value) {
  return Value::NewObject(
      L"LineColumn",
      shared_ptr<void>(new editor::LineColumn(value), [](void* v) {
        delete static_cast<editor::LineColumn*>(v);
      }));
}

const VMType VMTypeMapper<editor::LineColumn>::vmtype =
    VMType::ObjectType(L"LineColumn");
}  // namespace vm
}  // namespace afc

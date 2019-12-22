#include "src/line_column.h"

#include <glog/logging.h>

#include <set>
#include <vector>

#include "src/char_buffer.h"
#include "src/lazy_string.h"
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

bool operator==(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta == b.line_delta;
}

bool operator!=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const LineNumberDelta& lc) {
  os << "[line delta: " << lc.line_delta << "]";
  return os;
}

bool operator<(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta < b.line_delta;
}
bool operator<=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta <= b.line_delta;
}

bool operator>(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta > b.line_delta;
}

bool operator>=(const LineNumberDelta& a, const LineNumberDelta& b) {
  return a.line_delta >= b.line_delta;
}

LineNumberDelta operator+(LineNumberDelta a, const LineNumberDelta& b) {
  a.line_delta += b.line_delta;
  return a;
}

LineNumberDelta operator-(LineNumberDelta a, const LineNumberDelta& b) {
  a.line_delta -= b.line_delta;
  return a;
}

LineNumberDelta operator-(LineNumberDelta a) {
  a.line_delta = -a.line_delta;
  return a;
}

LineNumberDelta operator*(LineNumberDelta a, const size_t& b) {
  a.line_delta *= b;
  return a;
}

LineNumberDelta operator*(const size_t& a, LineNumberDelta b) {
  b.line_delta *= a;
  return b;
}

LineNumberDelta operator*(LineNumberDelta a, const double& b) {
  a.line_delta *= b;
  return a;
}

LineNumberDelta operator*(const double& a, LineNumberDelta b) {
  b.line_delta *= a;
  return b;
}

LineNumberDelta operator/(LineNumberDelta a, const size_t& b) {
  a.line_delta /= b;
  return a;
}

LineNumberDelta& operator+=(LineNumberDelta& a, const LineNumberDelta& value) {
  a.line_delta += value.line_delta;
  return a;
}

LineNumberDelta& operator-=(LineNumberDelta& a, const LineNumberDelta& value) {
  a.line_delta -= value.line_delta;
  return a;
}

LineNumberDelta& operator++(LineNumberDelta& a) {
  a.line_delta++;
  return a;
}

LineNumberDelta operator++(LineNumberDelta& a, int) {
  LineNumberDelta copy = a;
  a.line_delta++;
  return copy;
}

LineNumberDelta& operator--(LineNumberDelta& a) {
  a.line_delta--;
  return a;
}

LineNumberDelta operator--(LineNumberDelta& a, int) {
  LineNumberDelta copy = a;
  a.line_delta--;
  return copy;
}

/* static */ std::shared_ptr<LazyString> ColumnNumberDelta::PaddingString(
    const ColumnNumberDelta& length, wchar_t fill) {
  if (length < ColumnNumberDelta(0)) {
    return EmptyString();
  }
  return NewLazyString(length, fill);
}

ColumnNumberDelta& ColumnNumberDelta::operator=(
    const ColumnNumberDelta& delta) {
  column_delta = delta.column_delta;
  return *this;
}

bool operator==(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.column_delta == b.column_delta;
}

bool operator!=(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return !(a == b);
}

std::ostream& operator<<(std::ostream& os, const ColumnNumberDelta& lc) {
  os << "[column delta: " << lc.column_delta << "]";
  return os;
}

bool operator<(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.column_delta < b.column_delta;
}
bool operator<=(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.column_delta <= b.column_delta;
}

bool operator>(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.column_delta > b.column_delta;
}

bool operator>=(const ColumnNumberDelta& a, const ColumnNumberDelta& b) {
  return a.column_delta >= b.column_delta;
}

ColumnNumberDelta operator+(ColumnNumberDelta a, const ColumnNumberDelta& b) {
  a.column_delta += b.column_delta;
  return a;
}

ColumnNumberDelta operator-(ColumnNumberDelta a, const ColumnNumberDelta& b) {
  a.column_delta -= b.column_delta;
  return a;
}

ColumnNumberDelta operator-(ColumnNumberDelta a) {
  a.column_delta = -a.column_delta;
  return a;
}

ColumnNumberDelta operator*(ColumnNumberDelta a, const size_t& b) {
  a.column_delta *= b;
  return a;
}

ColumnNumberDelta operator*(const size_t& a, ColumnNumberDelta b) {
  b.column_delta *= a;
  return b;
}

ColumnNumberDelta operator/(ColumnNumberDelta a, const size_t& b) {
  a.column_delta /= b;
  return a;
}

ColumnNumberDelta& operator+=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value) {
  a.column_delta += value.column_delta;
  return a;
}

ColumnNumberDelta& operator-=(ColumnNumberDelta& a,
                              const ColumnNumberDelta& value) {
  a.column_delta -= value.column_delta;
  return a;
}

ColumnNumberDelta& operator++(ColumnNumberDelta& a) {
  a.column_delta++;
  return a;
}

ColumnNumberDelta operator++(ColumnNumberDelta& a, int) {
  ColumnNumberDelta copy = a;
  a.column_delta++;
  return copy;
}

ColumnNumberDelta& operator--(ColumnNumberDelta& a) {
  a.column_delta--;
  return a;
}

ColumnNumberDelta operator--(ColumnNumberDelta& a, int) {
  ColumnNumberDelta copy = a;
  a.column_delta--;
  return copy;
}

LineColumnDelta::LineColumnDelta(LineNumberDelta line, ColumnNumberDelta column)
    : line(line), column(column) {}

std::ostream& operator<<(std::ostream& os, const LineColumnDelta& lc) {
  os << "[" << lc.line << " " << lc.column << "]";
  return os;
}

bool operator==(const LineColumnDelta& a, const LineColumnDelta& b) {
  return a.line == b.line && a.column == b.column;
}

bool operator!=(const LineColumnDelta& a, const LineColumnDelta& b) {
  return !(a == b);
}

bool operator<(const LineColumnDelta& a, const LineColumnDelta& b) {
  return a.line < b.line || (a.line == b.line && a.column < b.column);
}

LineNumber::LineNumber(size_t value) : line(value) {}

LineNumber& LineNumber::operator=(const LineNumber& other) {
  line = other.line;
  return *this;
}

LineNumberDelta LineNumber::ToDelta() const { return *this - LineNumber(0); }

std::wstring LineNumber::ToUserString() const {
  return std::to_wstring(line + 1);
}

std::wstring LineNumber::Serialize() const { return std::to_wstring(line); }

LineNumber LineNumber::next() const {
  if (line == std::numeric_limits<size_t>::max()) {
    return *this;
  }
  return LineNumber(line + 1);
}

LineNumber LineNumber::previous() const {
  CHECK(line != 0);
  return LineNumber(line - 1);
}

LineNumber LineNumber::MinusHandlingOverflow(
    const LineNumberDelta& value) const {
  return this->ToDelta() > value ? *this - value : LineNumber(0);
}

bool operator==(const LineNumber& a, const LineNumber& b) {
  return a.line == b.line;
}

bool operator!=(const LineNumber& a, const LineNumber& b) {
  return a.line != b.line;
}

bool operator<(const LineNumber& a, const LineNumber& b) {
  return a.line < b.line;
}

bool operator<=(const LineNumber& a, const LineNumber& b) {
  return a.line <= b.line;
}

bool operator>(const LineNumber& a, const LineNumber& b) {
  return a.line > b.line;
}

bool operator>=(const LineNumber& a, const LineNumber& b) {
  return a.line >= b.line;
}

LineNumber& operator+=(LineNumber& a, const LineNumberDelta& delta) {
  if (delta.line_delta < 0) {
    CHECK_GE(a.line, static_cast<size_t>(-delta.line_delta));
  }

  a.line += delta.line_delta;
  return a;
}

LineNumber& operator-=(LineNumber& a, const LineNumberDelta& delta) {
  a += -delta;
  return a;
}

LineNumber& operator++(LineNumber& a) {
  a.line++;
  return a;
}

LineNumber operator++(LineNumber& a, int) {
  auto output = a;
  a.line++;
  return output;
}

LineNumber& operator--(LineNumber& a) {
  a.line--;
  return a;
}

LineNumber operator--(LineNumber& a, int) {
  auto output = a;
  a.line--;
  return output;
}

LineNumber operator%(LineNumber a, const LineNumberDelta& delta) {
  CHECK_NE(delta, LineNumberDelta(0));
  return LineNumber(a.line % delta.line_delta);
}

LineNumber operator+(LineNumber a, const LineNumberDelta& delta) {
  if (delta.line_delta < 0) {
    CHECK_GE(a.line, static_cast<size_t>(-delta.line_delta));
  }
  a.line += delta.line_delta;
  return a;
}

LineNumber operator-(LineNumber a, const LineNumberDelta& delta) {
  if (delta.line_delta > 0) {
    CHECK_GE(a.line, static_cast<size_t>(delta.line_delta));
  }
  a.line -= delta.line_delta;
  return a;
}

LineNumber operator-(const LineNumber& a) { return LineNumber(-a.line); }

LineNumberDelta operator-(const LineNumber& a, const LineNumber& b) {
  return LineNumberDelta(a.line - b.line);
}

std::ostream& operator<<(std::ostream& os, const LineNumber& lc) {
  os << "[Line " << lc.line << "]";
  return os;
}

namespace fuzz {
std::optional<LineNumber> Reader<LineNumber>::Read(Stream& input_stream) {
  auto value = Reader<size_t>::Read(input_stream);
  if (!value.has_value()) {
    VLOG(8) << "Fuzz: LineNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = LineNumber(value.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
};
}  // namespace fuzz

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

std::wstring ColumnNumber::Serialize() const { return std::to_wstring(column); }

ColumnNumber ColumnNumber::MinusHandlingOverflow(
    const ColumnNumberDelta& value) const {
  return this->ToDelta() > value ? *this - value : ColumnNumber(0);
}

bool ColumnNumber::IsZero() const { return *this == ColumnNumber(); }

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
  a.column += delta.column_delta;
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

ColumnNumber operator%(ColumnNumber a, const ColumnNumberDelta& delta) {
  CHECK_NE(delta, ColumnNumberDelta(0));
  return ColumnNumber(a.column % delta.column_delta);
}

ColumnNumber operator+(ColumnNumber a, const ColumnNumberDelta& delta) {
  if (delta.column_delta < 0) {
    CHECK_GE(a.column, static_cast<size_t>(-delta.column_delta));
  }
  a.column += delta.column_delta;
  return a;
}

ColumnNumber operator-(ColumnNumber a, const ColumnNumberDelta& delta) {
  if (delta.column_delta > 0) {
    CHECK_GE(a.column, static_cast<size_t>(delta.column_delta));
  }
  a.column -= delta.column_delta;
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

namespace fuzz {
std::optional<ColumnNumber> Reader<ColumnNumber>::Read(Stream& input_stream) {
  auto value = Reader<size_t>::Read(input_stream);
  if (!value.has_value()) {
    VLOG(8) << "Fuzz: ColumnNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = ColumnNumber(value.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
};
}  // namespace fuzz

std::ostream& operator<<(std::ostream& os, const LineColumn& lc) {
  os << "["
     << (lc.line == std::numeric_limits<LineNumber>::max()
             ? "inf"
             : std::to_string(lc.line.line))
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
  return std::to_wstring(line.line) + L" " + std::to_wstring(column.column);
}

std::wstring LineColumn::Serialize() const {
  return L"LineColumn(" + line.Serialize() + L", " + column.Serialize() + L")";
}

/* static */ Range Range::InLine(LineNumber line, ColumnNumber column,
                                 ColumnNumberDelta size) {
  return Range(LineColumn(line, column), LineColumn(line, column + size));
}

std::optional<Range> Range::Union(const Range& other) const {
  if (end < other.begin || begin > other.end) return std::nullopt;
  return Range(std::min(begin, other.begin), std::max(end, other.end));
}

std::ostream& operator<<(std::ostream& os, const Range& range) {
  os << "[" << range.begin << ", " << range.end << ")";
  return os;
}

bool operator<(const Range& a, const Range& b) {
  return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
}

LineColumn LineColumn::operator+(const LineNumberDelta& value) const {
  auto output = *this;
  output += value;
  return output;
}

LineColumn LineColumn::operator-(const LineNumberDelta& value) const {
  return *this + LineNumberDelta(-value.line_delta);
}

LineColumn& LineColumn::operator+=(const LineNumberDelta& value) {
  line += value;
  return *this;
}

LineColumn& LineColumn::operator-=(const LineNumberDelta& value) {
  return operator+=(LineNumberDelta(-value.line_delta));
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
  return L"LineColumn(" + std::to_wstring(line.line) + L", " +
         std::to_wstring(column.column) + L")";
}

namespace fuzz {
/* static */ std::optional<LineColumn> Reader<LineColumn>::Read(
    fuzz::Stream& input_stream) {
  auto line = Reader<LineNumber>::Read(input_stream);
  auto column = Reader<ColumnNumber>::Read(input_stream);
  if (!line.has_value() || !column.has_value()) {
    VLOG(8) << "Fuzz: LineNumber: Unable to read.";
    return std::nullopt;
  }
  auto output = LineColumn(line.value(), column.value());
  VLOG(9) << "Fuzz: Read: " << output;
  return output;
}
}  // namespace fuzz

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
                                   LineNumber(args[0]->integer),
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
                     return Value::NewInteger(line_column->line.line);
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
                std::to_wstring(line_column->line.line) + L", " +
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

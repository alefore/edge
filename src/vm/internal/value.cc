#include "../public/value.h"

#include "../public/vm.h"
#include "src/tests/tests.h"
#include "wstring.h"

namespace afc::vm {
using language::Error;
using language::MakeNonNullUnique;
using language::NonNull;
using language::Success;
using language::ValueOrError;

namespace gc = language::gc;

std::wstring CppEscapeString(std::wstring input) {
  std::wstring output;
  output.reserve(input.size() * 2);
  for (wchar_t c : input) {
    switch (c) {
      case '\n':
        output += L"\\n";
        break;
      case '"':
        output += L"\\\"";
        break;
      case '\\':
        output += L"\\\\";
        break;
      case '\'':
        output += L"\\'";
        break;
      default:
        output += c;
    }
  }
  return output;
}

std::optional<std::wstring> CppUnescapeString(std::wstring input) {
  std::wstring output;
  output.reserve(input.size() * 2);
  for (size_t i = 0; i < input.size(); ++i) {
    switch (input[i]) {
      case '\\':
        if (++i >= input.size()) return {};
        switch (input[i]) {
          case 'n':
            output += '\n';
            break;
          case '"':
          case '\\':
          case '\'':
            output += input[i];
            break;
          default:
            return {};
        }
        break;
      default:
        output += input[i];
    }
  }
  return output;
}

namespace {
bool cpp_unescape_string_tests_registration =
    tests::Register(L"CppUnescapeString", [] {
      auto t = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              std::wstring output =
                  CppUnescapeString(CppEscapeString(input)).value();
              LOG(INFO) << "Comparing: " << input << " to " << output;
              CHECK(input == output);
            }};
      };
      return std::vector<tests::Test>({
          t(L"EmptyString", L""),
          t(L"Simple", L"Simple"),
          t(L"SingleNewline", L"\n"),
          t(L"EndNewLine", L"foo\n"),
          t(L"StartNewLine", L"\nfoo"),
          t(L"NewlinesInText", L"Foo\nbar\nquux."),
          t(L"SomeQuotes", L"Foo \"with bar\" is 'good'."),
          t(L"SingleBackslash", L"\\"),
          t(L"SomeTextWithBackslash", L"Tab (escaped) is: \\t"),
      });
    }());
}

/* static */ language::gc::Root<Value> Value::New(language::gc::Pool& pool,
                                                  const VMType& type) {
  return pool.NewRoot(MakeNonNullUnique<Value>(ConstructorAccessTag(), type));
}

/* static */ gc::Root<Value> Value::NewVoid(gc::Pool& pool) {
  return New(pool, VMType::Void());
}

/* static */ gc::Root<Value> Value::NewBool(gc::Pool& pool, bool value) {
  gc::Root<Value> output = New(pool, VMType::Bool());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewInteger(gc::Pool& pool, int value) {
  gc::Root<Value> output = New(pool, VMType::Integer());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewDouble(gc::Pool& pool, double value) {
  gc::Root<Value> output = New(pool, VMType::Double());
  output.ptr()->value_ = value;
  return output;
}

/* static */ gc::Root<Value> Value::NewString(gc::Pool& pool, wstring value) {
  gc::Root<Value> output = New(pool, VMType::String());
  output.ptr()->str = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewSymbol(gc::Pool& pool, wstring value) {
  gc::Root<Value> output = New(pool, VMType::Symbol());
  output.ptr()->str = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewObject(gc::Pool& pool, std::wstring name,
                                              std::shared_ptr<void> value) {
  gc::Root<Value> output = New(pool, VMType::ObjectType(std::move(name)));
  output.ptr()->user_value = std::move(value);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(gc::Pool& pool,
                                                std::vector<VMType> arguments,
                                                Value::Callback callback) {
  // TODO(easy, 2022-05-13): Receive the purity type explicitly.
  gc::Root<Value> output =
      New(pool,
          VMType::Function(std::move(arguments), VMType::PurityType::kUnknown));
  output.ptr()->callback = std::move(callback);
  return output;
}

/* static */ gc::Root<Value> Value::NewFunction(
    gc::Pool& pool, std::vector<VMType> arguments,
    std::function<gc::Root<Value>(std::vector<gc::Root<Value>>)> callback) {
  return NewFunction(
      pool, arguments,
      [callback](std::vector<gc::Root<Value>> args, Trampoline&) {
        return futures::Past(
            Success(EvaluationOutput::New(callback(std::move(args)))));
      });
}

bool Value::get_bool() const {
  CHECK_EQ(type.type, VMType::VM_BOOLEAN);
  return std::get<bool>(value_);
}

int Value::get_int() const {
  CHECK_EQ(type.type, VMType::VM_INTEGER);
  return std::get<int>(value_);
}

double Value::get_double() const {
  CHECK_EQ(type.type, VMType::VM_DOUBLE);
  return std::get<double>(value_);
}

Value::Callback Value::LockCallback() {
  CHECK(IsFunction());
  return callback;
}

ValueOrError<double> Value::ToDouble() const {
  switch (type.type) {
    case VMType::VM_INTEGER:
      return get_int();
    case VMType::VM_DOUBLE:
      return get_double();
    default:
      return Error(L"Unexpected value of type: " + type.ToString());
  }
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  switch (value.type.type) {
    case VMType::VM_INTEGER:
      os << value.get_int();
      break;
    case VMType::VM_STRING:
      os << '"' << CppEscapeString(value.str) << '"';
      break;
    case VMType::VM_BOOLEAN:
      os << (value.get_bool() ? "true" : "false");
      break;
    case VMType::VM_DOUBLE:
      os << value.get_double();
      break;
    default:
      os << value.type.ToString();
  }
  return os;
}

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const afc::vm::Value&) {
  return {};
}
}  // namespace afc::language::gc

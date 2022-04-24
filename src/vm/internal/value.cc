#include "../public/value.h"

#include "../public/vm.h"
#include "src/tests/tests.h"
#include "wstring.h"

namespace afc::vm {
using language::Success;

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

/* static */ std::unique_ptr<Value> Value::NewVoid() {
  return std::make_unique<Value>(VMType::VM_VOID);
}

/* static */ std::unique_ptr<Value> Value::NewBool(bool value) {
  auto output = std::make_unique<Value>(VMType::Bool());
  output->boolean = value;
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewInteger(int value) {
  auto output = std::make_unique<Value>(VMType::Integer());
  output->integer = value;
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewDouble(double value) {
  auto output = std::make_unique<Value>(VMType::Double());
  output->double_value = value;
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewString(wstring value) {
  auto output = std::make_unique<Value>(VMType::String());
  output->str = std::move(value);
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewObject(
    std::wstring name, std::shared_ptr<void> value) {
  auto output = std::make_unique<Value>(VMType::ObjectType(std::move(name)));
  output->user_value = std::move(value);
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewFunction(
    std::vector<VMType> arguments, Value::Callback callback) {
  auto output = std::make_unique<Value>(VMType::FUNCTION);
  output->type.type_arguments = std::move(arguments);
  output->callback = std::move(callback);
  return output;
}

/* static */ std::unique_ptr<Value> Value::NewFunction(
    std::vector<VMType> arguments,
    std::function<Value::Ptr(std::vector<Value::Ptr>)> callback) {
  return NewFunction(arguments, [callback](std::vector<Ptr> args, Trampoline*) {
    return futures::Past(
        Success(EvaluationOutput::New(callback(std::move(args)))));
  });
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  if (value.IsInteger()) {
    os << value.integer;
  } else if (value.IsString()) {
    os << '"' << CppEscapeString(value.str) << '"';
  } else if (value.IsBool()) {
    os << (value.boolean ? "true" : "false");
  } else if (value.IsDouble()) {
    os << value.double_value;
  } else {
    os << value.type.ToString();
  }
  return os;
}

}  // namespace afc::vm

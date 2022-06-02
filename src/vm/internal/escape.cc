#include "src/vm/public/escape.h"

#include "../public/value.h"
#include "../public/vm.h"
#include "src/tests/tests.h"
#include "src/vm/internal/wstring.h"

namespace afc::vm {
using language::Error;
using language::ValueOrDie;

/* static */ CppString CppString::FromString(std::wstring input) {
  return CppString(std::move(input));
}

/* static */ language::ValueOrError<CppString> CppString::FromEscapedString(
    std::wstring input) {
  std::wstring original_string;
  original_string.reserve(input.size() * 2);
  for (size_t i = 0; i < input.size(); ++i) {
    switch (input[i]) {
      case '\\':
        if (++i >= input.size())
          return Error(L"String ends in escape character.");
        switch (input[i]) {
          case 'n':
            original_string += '\n';
            break;
          case '"':
          case '\\':
          case '\'':
            original_string += input[i];
            break;
          default:
            return Error(L"Unknown escaped character: " +
                         std::wstring(1, input[i]));
        }
        break;
      default:
        original_string += input[i];
    }
  }
  return CppString(original_string);
}

// Returns an escaped representation.
std::wstring CppString::Escape() const {
  std::wstring output;
  output.reserve(input_.size() * 2);
  for (wchar_t c : input_) {
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

// Returns the original (unescaped) string.
std::wstring CppString::OriginalString() const { return input_; }

CppString::CppString(std::wstring input) : input_(std::move(input)) {}

namespace {
bool cpp_unescape_string_tests_registration = tests::Register(L"CppString", [] {
  auto t = [](std::wstring name, std::wstring input) {
    return tests::Test{
        .name = name, .callback = [input] {
          std::wstring output =
              ValueOrDie(CppString::FromEscapedString(
                             CppString::FromString(input).Escape()))
                  .OriginalString();
          LOG(INFO) << "Comparing: " << input << " to " << output;
          CHECK(input == output);
        }};
  };
  auto fail = [](std::wstring name, std::wstring input) {
    return tests::Test{.name = name, .callback = [input] {
                         LOG(INFO) << "Expecting failure from: " << input;
                         CHECK(std::holds_alternative<Error>(
                             CppString::FromEscapedString(input)));
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
      fail(L"InvalidEscapeCharacter", L"Foo \\o bar"),
      fail(L"EndsInEscape", L"foo\\"),
  });
}());
}
}  // namespace afc::vm

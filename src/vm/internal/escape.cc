#include "../public/value.h"
#include "../public/vm.h"
#include "src/tests/tests.h"
#include "src/vm/internal/wstring.h"

namespace afc::vm {
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
}  // namespace afc::vm

#include "src/vm/escape.h"

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::NewError;
using afc::language::NonNull;
using afc::language::ValueOrDie;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::EmptyString;
using afc::language::lazy_string::ForEachColumn;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;

namespace afc::vm {
/* static */ EscapedString EscapedString::FromString(LazyString input) {
  return EscapedString(input);
}

/* static */ language::ValueOrError<EscapedString> EscapedString::Parse(
    language::lazy_string::LazyString input) {
  LazyString original_string = EmptyString();
  for (ColumnNumber i; i.ToDelta() < input.size(); ++i) {
    switch (input.get(i)) {
      case '\\':
        if ((++i).ToDelta() >= input.size())
          return Error(L"String ends in escape character.");
        switch (input.get(i)) {
          case 'n':
            original_string += NewLazyString(L"\n");
            break;
          case '"':
          case '\\':
          case '\'':
            original_string += input.Substring(i, ColumnNumberDelta(1));
            break;
          default:
            return NewError(
                NewLazyString(L"Unknown escaped character: ")
                    .Append(input.Substring(i, ColumnNumberDelta(1))));
        }
        break;
      default:
        original_string += input.Substring(i, ColumnNumberDelta(1));
    }
  }
  return EscapedString(original_string);
}

// Returns an escaped representation.
LazyString EscapedString::EscapedRepresentation() const {
  LazyString output = EmptyString();
  ForEachColumn(input_, [&output](ColumnNumber, wchar_t c) {
    switch (c) {
      case '\n':
        output += NewLazyString(L"\\n");
        break;
      case '"':
        output += NewLazyString(L"\\\"");
        break;
      case '\\':
        output += NewLazyString(L"\\\\");
        break;
      case '\'':
        output += NewLazyString(L"\\'");
        break;
      default:
        output += NewLazyString(std::wstring(1, c));
    }
  });
  return output;
}

std::wstring EscapedString::CppRepresentation() const {
  // TODO(trivial, 2023-12-31): Get rid of ToString.
  return L"\"" + EscapedRepresentation().ToString() + L"\"";
}

// Returns the original (unescaped) string.
LazyString EscapedString::OriginalString() const { return input_; }

EscapedString::EscapedString(LazyString input) : input_(std::move(input)) {}

namespace {
bool cpp_unescape_string_tests_registration =
    tests::Register(L"EscapedString", [] {
      using ::operator<<;
      auto t = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              std::wstring output =
                  ValueOrDie(EscapedString::Parse(
                                 EscapedString::FromString(NewLazyString(input))
                                     .EscapedRepresentation()))
                      .OriginalString()
                      .ToString();
              LOG(INFO) << "Comparing: " << input << " to " << output;
              CHECK(input == output);
            }};
      };
      auto fail = [](std::wstring name, std::wstring input) {
        return tests::Test{.name = name, .callback = [input] {
                             LOG(INFO) << "Expecting failure from: " << input;
                             CHECK(std::holds_alternative<Error>(
                                 EscapedString::Parse(NewLazyString(input))));
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

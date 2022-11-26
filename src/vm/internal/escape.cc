#include "src/vm/public/escape.h"

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::vm {
using language::Error;
using language::NonNull;
using language::ValueOrDie;
using language::lazy_string::ColumnNumber;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;

/* static */ EscapedString EscapedString::FromString(
    NonNull<std::shared_ptr<LazyString>> input) {
  // TODO(easy, 2022-06-10): Get rid of this call to ToString.
  return EscapedString(input->ToString());
}

/* static */ language::ValueOrError<EscapedString> EscapedString::Parse(
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>
        input) {
  std::wstring original_string;
  for (ColumnNumber i; i.ToDelta() < input->size(); ++i) {
    switch (input->get(i)) {
      case '\\':
        if ((++i).ToDelta() >= input->size())
          return Error(L"String ends in escape character.");
        switch (input->get(i)) {
          case 'n':
            original_string += '\n';
            break;
          case '"':
          case '\\':
          case '\'':
            original_string += input->get(i);
            break;
          default:
            return Error(L"Unknown escaped character: " +
                         std::wstring(1, input->get(i)));
        }
        break;
      default:
        original_string += input->get(i);
    }
  }
  return EscapedString(original_string);
}

// Returns an escaped representation.
std::wstring EscapedString::EscapedRepresentation() const {
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

std::wstring EscapedString::CppRepresentation() const {
  return L"\"" + EscapedRepresentation() + L"\"";
}

// Returns the original (unescaped) string.
NonNull<std::shared_ptr<LazyString>> EscapedString::OriginalString() const {
  // TODO(easy, 2022-11-26): Get rid of NewLazyString here; store the lazy
  // string directly.
  return NewLazyString(input_);
}

EscapedString::EscapedString(std::wstring input) : input_(std::move(input)) {}

namespace {
bool cpp_unescape_string_tests_registration =
    tests::Register(L"EscapedString", [] {
      using ::operator<<;
      auto t = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              std::wstring output =
                  ValueOrDie(EscapedString::Parse(NewLazyString(
                                 EscapedString::FromString(NewLazyString(input))
                                     .EscapedRepresentation())))
                      .OriginalString()
                      ->ToString();
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

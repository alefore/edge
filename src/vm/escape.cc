#include "src/vm/escape.h"

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/tokenize.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

using afc::language::Error;
using afc::language::NonNull;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::ForEachColumn;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::Token;

namespace afc::vm {
/* static */ EscapedString EscapedString::FromString(LazyString input) {
  return EscapedString(input);
}

/* static */ ValueOrError<EscapedString> EscapedString::Parse(
    ValueOrError<SingleLine> input_or_error) {
  DECLARE_OR_RETURN(SingleLine input, input_or_error);
  TRACK_OPERATION(EscapedString_Parse);
  LazyString original_string;
  ColumnNumber position;
  while (position.ToDelta() < input.size()) {
    std::optional<ColumnNumber> escape = FindFirstOf(input, {L'\\'}, position);
    original_string += input.Substring(
        position, escape.value_or(ColumnNumber{} + input.size()) - position);
    position = escape.value_or(ColumnNumber{} + input.size());
    if (escape.has_value()) {
      if ((++position).ToDelta() >= input.size())
        return Error{LazyString{L"String ends in escape character."}};
      switch (input.get(position)) {
        case 'n':
          original_string += LazyString{L"\n"};
          break;
        case '"':
        case '\\':
        case '\'':
          original_string += input.Substring(position, ColumnNumberDelta(1));
          break;
        default:
          return Error{LazyString{L"Unknown escaped character: "} +
                       input.Substring(position, ColumnNumberDelta(1))};
      }
      ++position;
    }
  }
  return EscapedString(original_string);
}

ValueOrError<size_t> HexCharToSizeT(wchar_t hex_char) {
  if (hex_char >= L'0' && hex_char <= L'9') {
    return static_cast<size_t>(hex_char - L'0');
  } else if (hex_char >= L'a' && hex_char <= L'f') {
    return static_cast<size_t>(hex_char - L'a' + 10);
  } else if (hex_char >= L'A' && hex_char <= L'F') {
    return static_cast<size_t>(hex_char - L'A' + 10);
  }
  return Error{LazyString{L"Invalid hex character"}};
}

// Function to convert two hex characters (from an escape sequence) to a wchar_t
ValueOrError<wchar_t> URLEscapeDecode(wchar_t first, wchar_t second) {
  DECLARE_OR_RETURN(size_t high, HexCharToSizeT(first));
  DECLARE_OR_RETURN(size_t low, HexCharToSizeT(second));
  CHECK_LE(high, 15ul);
  CHECK_LE(low, 15ul);
  return static_cast<wchar_t>((high << 4) | low);
}

/* static */ ValueOrError<EscapedString> EscapedString::ParseURL(
    language::lazy_string::SingleLine input) {
  TRACK_OPERATION(EscapedString_ParseURL);
  LazyString original_string;
  ColumnNumber position;
  while (position.ToDelta() < input.size()) {
    std::optional<ColumnNumber> escape = FindFirstOf(input, {L'%'}, position);
    original_string =
        original_string +
        input.Substring(
            position,
            escape.value_or(ColumnNumber{} + input.size()) - position);
    position = escape.value_or(ColumnNumber{} + input.size());
    if (escape.has_value()) {
      if (position.ToDelta() + ColumnNumberDelta{3} > input.size())
        return Error{LazyString{L"URL string finished inside escape code."}};
      DECLARE_OR_RETURN(
          wchar_t next_char,
          URLEscapeDecode(input.get(position + ColumnNumberDelta{1}),
                          input.get(position + ColumnNumberDelta{2})));
      original_string += LazyString{ColumnNumberDelta{1}, next_char};
      position += ColumnNumberDelta{3};
    }
  }
  return EscapedString(original_string);
}

// Returns an escaped representation.
SingleLine EscapedString::EscapedRepresentation() const {
  SingleLine output;
  ForEachColumn(read(), [&output](ColumnNumber, wchar_t c) {
    switch (c) {
      case '\n':
        output += SingleLine{LazyString{L"\\n"}};
        break;
      case '"':
        output += SingleLine{LazyString{L"\\\""}};
        break;
      case '\\':
        output += SingleLine{LazyString{L"\\\\"}};
        break;
      case '\'':
        output += SingleLine{LazyString{L"\\'"}};
        break;
      default:
        output += SingleLine{LazyString{ColumnNumberDelta(1), c}};
    }
  });
  return output;
}

NonEmptySingleLine EscapedString::CppRepresentation() const {
  return NON_EMPTY_SINGLE_LINE_CONSTANT(L"\"") + EscapedRepresentation() +
         NON_EMPTY_SINGLE_LINE_CONSTANT(L"\"");
}

SingleLine EscapedString::URLRepresentation() const {
  SingleLine output;
  // TODO(2024-09-20): This could be optimized based on
  // FindFirstColumnWithPredicate, avoiding fragmentation.
  ForEachColumn(read(), [&output](ColumnNumber, wchar_t c) {
    if (!(iswalnum(c) || c == L'-' || c == L'_' || c == L'.' || c == L'~')) {
      static const SingleLine kHexDigits =
          SINGLE_LINE_CONSTANT(L"0123456789ABCDEF");
      output += SingleLine::Char<'%'>() +
                kHexDigits.Substring(
                    ColumnNumber{static_cast<size_t>((c >> 4) & 0xF)},
                    ColumnNumberDelta{1}) +
                kHexDigits.Substring(ColumnNumber{static_cast<size_t>(c & 0xF)},
                                     ColumnNumberDelta{1});
    } else {
      output += SingleLine{LazyString{ColumnNumberDelta{1}, c}};
    }
  });
  return output;
}

// Returns the original (unescaped) string.
LazyString EscapedString::OriginalString() const { return read(); }

namespace {
bool cpp_unescape_string_tests_registration =
    tests::Register(L"EscapedString", [] {
      using ::operator<<;
      auto t = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              CHECK_EQ(
                  LazyString{input},
                  ValueOrDie(EscapedString::Parse(
                                 EscapedString::FromString(LazyString{input})
                                     .EscapedRepresentation()))
                      .OriginalString());
            }};
      };
      auto fail = [](std::wstring name, std::wstring input) {
        return tests::Test{
            .name = name, .callback = [input] {
              LOG(INFO) << "Expecting failure from: " << input;
              CHECK(std::holds_alternative<Error>(
                  EscapedString::Parse(SingleLine{LazyString{input}})));
            }};
      };
      return std::vector<tests::Test>({
          t(L"EmptyString", L""),
          t(L"Simple", L"Simple"),
          t(L"SomeQuotes", L"Foo \"with bar\" is 'good'."),
          t(L"SingleBackslash", L"\\"),
          t(L"SomeTextWithBackslash", L"Tab (escaped) is: \\t"),
          fail(L"InvalidEscapeCharacter", L"Foo \\o bar"),
          fail(L"EndsInEscape", L"foo\\"),
      });
    }());
}  // namespace

EscapedMap::EscapedMap(Map input) : input_(std::move(input)) {}

/* static */ ValueOrError<EscapedMap> EscapedMap::Parse(SingleLine input) {
  TRACK_OPERATION(EscapedMap_Parse);
  EscapedMap::Map output;
  for (const Token& token : TokenizeBySpaces(input)) {
    std::optional<ColumnNumber> colon = FindFirstOf(token.value, {L':'});
    if (colon == std::nullopt)
      return Error{
          LazyString{L"Unable to parse map line (no colon found in token): "} +
          input};
    ColumnNumber value_start = token.begin + colon->ToDelta();
    ++value_start;  // Skip the colon.
    ColumnNumber value_end = token.end;
    if (value_end <= value_start + ColumnNumberDelta(1) ||
        input.get(value_start) != '\"' ||
        input.get(value_end.previous()) != '\"')
      return Error{
          LazyString{L"Unable to parse prompt line (expected quote): "} +
          input};
    // Skip quotes:
    ++value_start;
    --value_end;
    DECLARE_OR_RETURN(
        Identifier id,
        Identifier::New(NonEmptySingleLine::New(
            token.value.Substring(ColumnNumber{0}, colon->ToDelta()))));
    DECLARE_OR_RETURN(EscapedString parsed_value,
                      EscapedString::Parse(input.Substring(
                          value_start, value_end - value_start)));
    output.insert({id, parsed_value});
  }

  return EscapedMap{output};
}

SingleLine EscapedMap::Serialize() const {
  return Concatenate(
      input_ |
      std::views::transform(
          [](std::pair<Identifier, EscapedString> data) -> SingleLine {
            return data.first.read().read() + SingleLine{LazyString{L":"}} +
                   data.second.CppRepresentation().read();
          }) |
      Intersperse(SingleLine::Char<L' '>()));
}

const EscapedMap::Map& EscapedMap::read() const { return input_; }

}  // namespace afc::vm

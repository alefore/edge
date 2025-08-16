#include "src/parsers/py.h"

#include <glog/logging.h>

#include <ranges>

#include "src/language/container.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/safe_types.h"
#include "src/lru_cache.h"
#include "src/parse_tools.h"
#include "src/parsers/util.h"
#include "src/seek.h"

namespace container = afc::language::container;

using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::language::Error;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::container::MaterializeUnorderedSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::LineNumberDelta;
using afc::language::text::LineSequence;
using afc::language::text::Range;

namespace afc::editor::parsers {

namespace {
enum State {
  DEFAULT,
  AFTER_HASH,
  IN_TRIPLE_SINGLE_QUOTE_STRING,
  IN_TRIPLE_DOUBLE_QUOTE_STRING,

  // States for matching parentheses/brackets/braces
  BRACKET_DEFAULT,
  PARENS_DEFAULT,
  BRACE_DEFAULT,
};

static const std::unordered_set<wchar_t> identifier_chars =
    MaterializeUnorderedSet(std::wstring_view{
        L"_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"});
static const std::unordered_set<wchar_t> digit_chars =
    MaterializeUnorderedSet(std::wstring_view{L"1234567890"});
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

bool Contains(const std::unordered_set<NonEmptySingleLine>& values,
              const SingleLine& pattern) {
  return std::visit(overload{[](Error) { return false; },
                             [&values](NonEmptySingleLine non_empty_pattern) {
                               return values.contains(non_empty_pattern);
                             }},
                    NonEmptySingleLine::New(pattern));
}

class PyTreeParser : public parsers::LineOrientedTreeParser {
  const NonNull<std::unique_ptr<TreeParser>> words_parser_;
  const std::unordered_set<NonEmptySingleLine> keywords_;
  const std::unordered_set<NonEmptySingleLine> typos_;
  const IdentifierBehavior identifier_behavior_;

 public:
  PyTreeParser(std::unordered_set<NonEmptySingleLine> keywords,
               std::unordered_set<NonEmptySingleLine> typos,
               IdentifierBehavior identifier_behavior)
      : words_parser_(NewWordsTreeParser(
            LazyString{L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"},
            typos, NewNullTreeParser())),
        keywords_(std::move(keywords)),
        typos_(std::move(typos)),
        identifier_behavior_(identifier_behavior) {}

 protected:
  void ParseLine(ParseData* result) override {
    bool done = false;
    while (!done) {
      LineColumn original_position = result->position();  // For validation.
      done = result->seek().read() == L'\n';
      switch (result->state()) {
        case DEFAULT:
          DefaultState(DEFAULT, result);
          break;
        case IN_TRIPLE_SINGLE_QUOTE_STRING:
          InsideTripleQuoteString(L'\'', result);
          break;
        case IN_TRIPLE_DOUBLE_QUOTE_STRING:
          InsideTripleQuoteString(L'"', result);
          break;
        case BRACKET_DEFAULT:
          DefaultState(BRACKET_DEFAULT, result);
          break;
        case PARENS_DEFAULT:
          DefaultState(PARENS_DEFAULT, result);
          break;
        case BRACE_DEFAULT:
          DefaultState(BRACE_DEFAULT, result);
          break;
      }
      CHECK_LE(original_position, result->position());
    }
  }

 private:
  void InsideTripleQuoteString(wchar_t quote_char, ParseData* result) {
    Seek seek = result->seek();
    while (true) {
      wchar_t c = seek.read();
      seek.Once();

      if (c == quote_char) {
        if (seek.read() == quote_char) {  // Second quote.
          seek.Once();
          if (seek.read() == quote_char) {  // Third quote.
            seek.Once();
            result->PopBack();
            return;
          }
        }
      } else if (c == L'\\') {
        seek.Once();
      } else if (c == L'\n') {
        return;
      }
    }
  }

  void Identifier(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, ColumnNumber(1));
    original_position.column--;

    static const std::unordered_set<wchar_t> identifier_and_digit_chars =
        container::MaterializeUnorderedSet(
            std::array{identifier_chars, digit_chars} |
            std::ranges::views::join);
    result->seek().UntilCurrentCharNotIn(identifier_and_digit_chars);

    CHECK_EQ(original_position.line, result->position().line);
    CHECK_GT(result->position().column, original_position.column);
    ColumnNumberDelta length =
        result->position().column - original_position.column;
    SingleLine str = result->buffer()
                         .at(original_position.line)
                         .contents()
                         .Substring(original_position.column, length);
    LineModifierSet modifiers;
    if (Contains(keywords_, str)) {
      modifiers.insert(LineModifier::kCyan);
    } else if (Contains(typos_, str)) {
      modifiers.insert(LineModifier::kRed);
    } else if (identifier_behavior_ == IdentifierBehavior::kColorByHash) {
      modifiers = HashToModifiers(std::hash<SingleLine>{}(str),
                                  HashToModifiersBold::kNever);
    }
    result->PushAndPop(length, std::move(modifiers));
  }

  void DefaultState(State current_state, ParseData* result) {
    Seek seek = result->seek();

    // The most common transition (but sometimes overriden below).
    result->SetState(current_state);

    wchar_t c = seek.read();
    seek.Once();
    if (c == L'\n' || c == L'\t' || c == L' ') return;

    if (c == '#') {
      LineColumn original_position = result->position();
      result->seek().ToEndOfLine();
      result->PushAndPop(result->position().column + ColumnNumberDelta(1) -
                             original_position.column,
                         {LineModifier::kBlue});
      return;
    }

    if (identifier_chars.contains(tolower(c))) {
      Identifier(result);
      return;
    }

    if (c == L'"' || c == L'\'') {
      LineColumn position_after_first_quote = result->position();
      if (seek.read() == c) {
        seek.Once();
        if (seek.read() == c) {
          seek.Once();
          result->Push(c == L'"' ? IN_TRIPLE_DOUBLE_QUOTE_STRING
                                 : IN_TRIPLE_SINGLE_QUOTE_STRING,
                       ColumnNumberDelta(3), {LineModifier::kYellow}, {});
          return;
        }
      }
      result->set_position(position_after_first_quote);
      ParseQuotedString(result, c, {LineModifier::kYellow}, {});
      return;
    }

    if (c == L'[' || c == L'(' || c == L'{') {
      State next_state;
      if (c == L'[')
        next_state = BRACKET_DEFAULT;
      else if (c == L'(')
        next_state = PARENS_DEFAULT;
      else
        next_state = BRACE_DEFAULT;
      result->Push(next_state, ColumnNumberDelta(1), {}, {});
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      return;
    }

    if (c == L']' || c == L')' || c == L'}') {
      State expected_state;
      if (c == L']')
        expected_state = BRACKET_DEFAULT;
      else if (c == L')')
        expected_state = PARENS_DEFAULT;
      else
        expected_state = BRACE_DEFAULT;

      if (result->state() == expected_state) {
        LineModifierSet modifiers = HashToModifiers(
            result->AddAndGetNesting(), HashToModifiersBold::kSometimes);
        result->PushAndPop(ColumnNumberDelta(1), modifiers);
        result->SetFirstChildModifiers(modifiers);
        result->PopBack();
      } else {
        result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      }
      return;
    }

    if (isdigit(c)) {
      parsers::ParseNumber(result, {LineModifier::kYellow}, {});
      return;
    }
  }

  enum class HashToModifiersBold { kSometimes, kNever };
  LineModifierSet HashToModifiers(int nesting,
                                  HashToModifiersBold bold_behavior) {
    LineModifierSet output;
    static std::vector<LineModifier> modifiers = {
        LineModifier::kCyan, LineModifier::kYellow, LineModifier::kRed,
        LineModifier::kBlue, LineModifier::kGreen,  LineModifier::kMagenta,
        LineModifier::kWhite};
    output.insert(modifiers[nesting % modifiers.size()]);
    if (bold_behavior == HashToModifiersBold::kSometimes &&
        ((nesting / modifiers.size()) % 2) == 0) {
      output.insert(LineModifier::kBold);
    }
    return output;
  }
};

}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewPyTreeParser(
    std::unordered_set<NonEmptySingleLine> keywords,
    std::unordered_set<NonEmptySingleLine> typos,
    IdentifierBehavior identifier_behavior) {
  return MakeNonNullUnique<PyTreeParser>(std::move(keywords), std::move(typos),
                                         identifier_behavior);
}

}  // namespace afc::editor::parsers
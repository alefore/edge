#include "src/parsers/css.h"

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

using afc::editor::parsers::CurrentState;
using afc::editor::parsers::MultipleLinesSupport;
using afc::editor::parsers::NestedExpressionSyntax;
using afc::editor::parsers::ParseQuotedString;
using afc::editor::parsers::ParseQuotedStringState;
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
  IN_MULTI_LINE_COMMENT,
  IN_MULTIPLE_LINE_STRING_SINGLE_QUOTE,
  IN_MULTIPLE_LINE_STRING_DOUBLE_QUOTE,
};

static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

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
      ((nesting / modifiers.size()) % 2) == 0)
    output.insert(LineModifier::kBold);
  return output;
}

class CssTreeParser : public parsers::LineOrientedTreeParser {
  const ParserId parser_id_;

 public:
  CssTreeParser(ParserId parser_id) : parser_id_(parser_id) {}

 protected:
  void ParseLine(ParseData* result) override {
    bool done = false;
    VLOG(4) << "Parse line: " << result->position()
            << ": range: " << result->seek().range();
    while (!done) {
      LineColumn original_position = result->position();
      done = result->seek().read() == L'\n';

      switch (result->state()) {
        case DEFAULT:
          ParseDefaultState(result);
          break;
        case IN_MULTI_LINE_COMMENT:
          ParseMultiLineComment(result);
          break;
        case IN_MULTIPLE_LINE_STRING_SINGLE_QUOTE:
        case IN_MULTIPLE_LINE_STRING_DOUBLE_QUOTE:
          ParseMultipleLineString(result);
          break;
      }
      if (!done) CHECK_LT(original_position, result->position());
    }
  }

 private:
  // Invariant: the previous character (before result->position()) must be in
  // `char_set`.
  void ParseWordLikeToken(ParseData* result,
                          const std::unordered_set<wchar_t>& char_set,
                          LineModifierSet modifiers) {
    CHECK_GT(result->position().column, ColumnNumber{});
    LineColumn original_word_start = result->position() - ColumnNumberDelta{1};

    result->seek().UntilCurrentCharNotIn(char_set);
    CHECK_EQ(original_word_start.line, result->position().line);
    ColumnNumberDelta length =
        result->position().column - original_word_start.column;
    CHECK_GT(length, ColumnNumberDelta{});
    result->PushAndPop(length, std::move(modifiers));
  }

  void ParseDefaultState(ParseData* result) {
    Seek seek = result->seek();
    wchar_t c = seek.read();

    if (c == L'\n') return;

    seek.Once();

    if (c == L'\t' || c == L' ') return;

    if (c == L'/') {
      if (seek.read() == L'*') {
        seek.Once();
        result->Push(IN_MULTI_LINE_COMMENT, ColumnNumberDelta(2),
                     {LineModifier::kBlue}, {});
      } else
        result->PushAndPop(ColumnNumberDelta(1), {});
      return;
    }

    if (c == L'\'' || c == L'"') {
      if (ParseQuotedString(result, c, {LineModifier::kYellow}, {},
                            std::nullopt, MultipleLinesSupport::kAccept,
                            parsers::CurrentState::kStart) !=
          ParseQuotedStringState::kDone) {
        result->SetState(c == L'\'' ? IN_MULTIPLE_LINE_STRING_SINGLE_QUOTE
                                    : IN_MULTIPLE_LINE_STRING_DOUBLE_QUOTE);
        CHECK(seek.read() == L'\n');
      }
      return;
    }

    if (c == L'{') {
      result->Push(DEFAULT, ColumnNumberDelta(1), {}, {});
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      return;
    }

    if (c == L'}') {
      if (result->parse_results().states_stack.size() > 1) {
        LineModifierSet modifiers = HashToModifiers(
            result->AddAndGetNesting(), HashToModifiersBold::kSometimes);
        result->PushAndPop(ColumnNumberDelta(1), modifiers);
        result->SetFirstChildModifiers(modifiers);
        result->PopBack();
      } else
        result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      return;
    }

    if (result->parse_results().states_stack.size() > 1) {
      if (c == L':')
        result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim});
      else if (c == L';')
        result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim});
      else {
        static const std::unordered_set<wchar_t> css_prop_val_chars =
            container::MaterializeUnorderedSet(
                std::wstring_view{L"_-"
                                  L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                                  L"rstuvwxyz0123456789"
                                  L"."
                                  L"%"});
        if (css_prop_val_chars.contains(c)) {
          ParseWordLikeToken(result, css_prop_val_chars,
                             {LineModifier::kWhite});
          return;
        }
      }
    } else {
      static const std::unordered_set<wchar_t> css_selector_chars =
          container::MaterializeUnorderedSet(
              std::wstring_view{L"_-"
                                L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                                L"rstuvwxyz0123456789"
                                L"."
                                L"#"
                                L"*"});
      if (css_selector_chars.contains(c)) {
        ParseWordLikeToken(result, css_selector_chars, {LineModifier::kCyan});
        return;
      }
    }

    if (isdigit(c)) {
      parsers::ParseNumber(result, {LineModifier::kYellow}, {});
      return;
    }
  }

  void ParseMultiLineComment(ParseData* result) {
    Seek seek = result->seek();
    ColumnNumber start_column_for_highlight = result->position().column;

    while (seek.read() != L'\n') {
      wchar_t c = seek.read();
      seek.Once();
      if (c == L'*' && seek.read() == L'/') {
        seek.Once();
        result->PopBack();
        result->PushAndPop(
            result->position().column - start_column_for_highlight,
            {LineModifier::kBlue});
        return;
      }
    }
    result->PushAndPop(result->position().column - start_column_for_highlight,
                       {LineModifier::kBlue});
  }

  void ParseMultipleLineString(ParseData* result) {
    auto original_state = result->state();
    LOG_IF(FATAL, original_state != IN_MULTIPLE_LINE_STRING_SINGLE_QUOTE &&
                      original_state != IN_MULTIPLE_LINE_STRING_DOUBLE_QUOTE)
        << "Invalid state for multi-line string continuation.";

    if (ParseQuotedString(result,
                          original_state == IN_MULTIPLE_LINE_STRING_SINGLE_QUOTE
                              ? L'\''
                              : L'"',
                          {LineModifier::kYellow}, {}, std::nullopt,
                          MultipleLinesSupport::kAccept,
                          CurrentState::kContinuationInDefault) ==
        ParseQuotedStringState::kDone)
      return;
    result->SetState(original_state);
    CHECK(result->seek().read() == '\n');
  }
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCssTreeParser(ParserId parser_id) {
  return MakeNonNullUnique<CssTreeParser>(parser_id);
}

}  // namespace afc::editor::parsers

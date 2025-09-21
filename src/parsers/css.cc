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
  IN_DECLARATION_BLOCK_EXPECT_PROPERTY,
  IN_DECLARATION_BLOCK_EXPECT_COLON,
  IN_DECLARATION_BLOCK_EXPECT_VALUE,
  PROPERTY_BORDER_STYLE_COLON,
  PROPERTY_BORDER_STYLE_VALUE,
  INVALID_PROPERTY_VALUE,
};

static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::kBgRed, LineModifier::kBold});

static const LineModifierSet RECOGNIZED_RULE_MODIFIERS =
    LineModifierSet({LineModifier::kMagenta, LineModifier::kBold});

static const LineModifierSet VALID_BORDER_STYLE_VALUE_MODIFIERS =
    LineModifierSet({LineModifier::kGreen});

static const std::unordered_set<SingleLine> kBorderStyleValues =
    container::MaterializeUnorderedSet(std::vector<SingleLine>{
        SINGLE_LINE_CONSTANT(L"none"), SINGLE_LINE_CONSTANT(L"hidden"),
        SINGLE_LINE_CONSTANT(L"dotted"), SINGLE_LINE_CONSTANT(L"dashed"),
        SINGLE_LINE_CONSTANT(L"solid"), SINGLE_LINE_CONSTANT(L"double"),
        SINGLE_LINE_CONSTANT(L"groove"), SINGLE_LINE_CONSTANT(L"ridge"),
        SINGLE_LINE_CONSTANT(L"inset"), SINGLE_LINE_CONSTANT(L"outset")});

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
      VLOG(5) << "State: " << result->state();

      switch (result->state()) {
        case DEFAULT:
        case IN_DECLARATION_BLOCK_EXPECT_PROPERTY:
        case IN_DECLARATION_BLOCK_EXPECT_VALUE:
        case PROPERTY_BORDER_STYLE_VALUE:
        case INVALID_PROPERTY_VALUE:
          ParseDefaultState(result);
          break;
        case IN_DECLARATION_BLOCK_EXPECT_COLON:
        case PROPERTY_BORDER_STYLE_COLON:
          ParseColon(result);
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
  void ParseColon(ParseData* result) {
    Seek seek = result->seek();
    wchar_t c = seek.read();

    if (c == L'\n') return;

    seek.Once();

    if (c == L'\t' || c == L' ') return;
    if (c != L':') {
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      if (c == L';') result->SetState(IN_DECLARATION_BLOCK_EXPECT_PROPERTY);
      return;
    }

    result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim});
    switch (result->state()) {
      case PROPERTY_BORDER_STYLE_COLON:
        result->SetState(PROPERTY_BORDER_STYLE_VALUE);
        break;
      case IN_DECLARATION_BLOCK_EXPECT_COLON:
        result->SetState(IN_DECLARATION_BLOCK_EXPECT_VALUE);
        break;
      default:
        LOG(FATAL) << "ParseColon called in invalid state.";
    }
  }

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
      result->Push(IN_DECLARATION_BLOCK_EXPECT_PROPERTY, ColumnNumberDelta(1),
                   {}, {});
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
        // After popping, if the previous state was a declaration block, reset
        // to DEFAULT as we're outside a declaration.
        if (result->state() == IN_DECLARATION_BLOCK_EXPECT_PROPERTY ||
            result->state() == IN_DECLARATION_BLOCK_EXPECT_VALUE ||
            result->state() == PROPERTY_BORDER_STYLE_VALUE ||
            result->state() == INVALID_PROPERTY_VALUE) {
          result->SetState(DEFAULT);
        }
      } else
        result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      return;
    }

    switch (result->state()) {
      case DEFAULT: {
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
        break;
      }
      case IN_DECLARATION_BLOCK_EXPECT_PROPERTY:
      case IN_DECLARATION_BLOCK_EXPECT_VALUE:
      case PROPERTY_BORDER_STYLE_VALUE:
      case INVALID_PROPERTY_VALUE: {
        if (c == L':') {
          // Missing the property.
          result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
        } else if (c == L';') {
          result->PushAndPop(ColumnNumberDelta(1), {LineModifier::kDim});
          // After a semicolon, we expect a new property.
          result->SetState(IN_DECLARATION_BLOCK_EXPECT_PROPERTY);
        } else {
          static const std::unordered_set<wchar_t> css_prop_val_chars =
              container::MaterializeUnorderedSet(std::wstring_view{
                  L"_-"
                  L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopq"
                  L"rstuvwxyz0123456789"
                  L"."
                  L"%"});
          if (css_prop_val_chars.contains(c)) {
            LineColumn original_word_start =
                result->position() - ColumnNumberDelta{1};
            result->seek().UntilCurrentCharNotIn(css_prop_val_chars);
            ColumnNumberDelta length =
                result->position().column - original_word_start.column;
            CHECK_GT(length, ColumnNumberDelta{});

            const auto& line_object =
                result->buffer().at(result->position().line);
            const SingleLine& current_line_contents = line_object.contents();
            SingleLine token = current_line_contents.Substring(
                original_word_start.column, length);

            if (result->state() == IN_DECLARATION_BLOCK_EXPECT_PROPERTY) {
              if (token == SINGLE_LINE_CONSTANT(L"border-style")) {
                result->PushAndPop(length, RECOGNIZED_RULE_MODIFIERS);
                result->SetState(PROPERTY_BORDER_STYLE_COLON);
              } else {
                result->PushAndPop(length, {LineModifier::kWhite});
                result->SetState(IN_DECLARATION_BLOCK_EXPECT_COLON);
              }
            } else if (result->state() == PROPERTY_BORDER_STYLE_VALUE) {
              VLOG(5) << "Check value: " << token;
              if (kBorderStyleValues.contains(token)) {
                result->PushAndPop(length, VALID_BORDER_STYLE_VALUE_MODIFIERS);
                // TODO(trivial, 2025-09-22):
                // result->SetState(IN_DECLARATION_BLOCK_EXPECT_SEMICOLON);
              } else {
                result->PushAndPop(length, BAD_PARSE_MODIFIERS);
                result->SetState(INVALID_PROPERTY_VALUE);
              }
            } else if (result->state() == INVALID_PROPERTY_VALUE) {
              result->PushAndPop(length, BAD_PARSE_MODIFIERS);
            } else {  // IN_DECLARATION_BLOCK_EXPECT_VALUE
              result->PushAndPop(length, {LineModifier::kWhite});
            }
          }
        }
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
    CHECK(result->seek().read() == L'\n');
  }
};
}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCssTreeParser(ParserId parser_id) {
  return MakeNonNullUnique<CssTreeParser>(parser_id);
}

}  // namespace afc::editor::parsers
#include "src/cpp_parse_tree.h"

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

using afc::editor::parsers::ParseDoubleQuotedString;
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

namespace afc::editor {

namespace {
enum State {
  DEFAULT_AT_START_OF_LINE,
  DEFAULT,
  AFTER_SLASH,
  COMMENT,

  BRACKET_DEFAULT_AT_START_OF_LINE,
  BRACKET_DEFAULT,
  BRACKET_AFTER_SLASH,

  PARENS_DEFAULT_AT_START_OF_LINE,
  PARENS_DEFAULT,
  PARENS_AFTER_SLASH,
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

class CppTreeParser : public parsers::LineOrientedTreeParser {
  const NonNull<std::unique_ptr<TreeParser>> words_parser_;
  const std::unordered_set<NonEmptySingleLine> keywords_;
  const std::unordered_set<NonEmptySingleLine> typos_;
  const IdentifierBehavior identifier_behavior_;

 public:
  CppTreeParser(std::unordered_set<NonEmptySingleLine> keywords,
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
        case DEFAULT_AT_START_OF_LINE:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH, true,
                       result);
          break;

        case BRACKET_DEFAULT_AT_START_OF_LINE:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, true, result);
          break;

        case PARENS_DEFAULT_AT_START_OF_LINE:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, true, result);
          break;

        case DEFAULT:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH, false,
                       result);
          break;

        case BRACKET_DEFAULT:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, false, result);
          break;

        case PARENS_DEFAULT:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, false, result);
          break;

        case AFTER_SLASH:
          AfterSlash(DEFAULT, DEFAULT_AT_START_OF_LINE, result);
          break;

        case BRACKET_AFTER_SLASH:
          AfterSlash(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE, result);
          break;

        case PARENS_AFTER_SLASH:
          AfterSlash(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE, result);
          break;

        case COMMENT:
          InsideComment(result);
          break;
      }

      CHECK_LE(original_position, result->position());
    }
  }

 private:
  void AfterSlash(State state_default, State state_default_at_start_of_line,
                  ParseData* result) {
    auto seek = result->seek();
    switch (seek.read()) {
      case '/':
        result->SetState(state_default_at_start_of_line);
        CommentToEndOfLine(result);
        break;
      case '*':
        result->Push(COMMENT, ColumnNumberDelta(1), {LineModifier::kBlue}, {});
        seek.Once();
        break;
      default:
        result->SetState(state_default);
    }
  }

  void CommentToEndOfLine(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GT(original_position.column, ColumnNumber(0));
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column + ColumnNumberDelta(1) -
                           original_position.column,
                       {LineModifier::kBlue});
    // TODO: words_parser_->FindChildren(result->buffer(), comment_tree);
  }

  void InsideComment(ParseData* result) {
    auto seek = result->seek();
    auto c = seek.read();
    seek.Once();
    if (c == '*' && seek.read() == '/') {
      seek.Once();
      result->PopBack();
    }
  }

  void LiteralCharacter(ParseData* result) {
    Seek seek = result->seek();
    ColumnNumberDelta rewind_column(1);
    auto original_position = result->position();
    if (seek.read() == '\\') {
      seek.Once();
      ++rewind_column;
    }

    seek.Once();  // Skip the character.
    ++rewind_column;

    if (seek.read() == '\'') {
      seek.Once();
      ++rewind_column;
      result->PushAndPop(rewind_column, {LineModifier::kYellow});
    } else {
      result->set_position(original_position);
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
    }
  }

  void LiteralString(ParseData* result) {
    ParseDoubleQuotedString(result, {LineModifier::kYellow}, {});
  }

  void PreprocessorDirective(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, ColumnNumber(1));
    --original_position.column;

    result->seek().ToEndOfLine();
    CHECK_GT(result->position().column, original_position.column);
    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::kYellow});
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
    auto length = result->position().column - original_position.column;
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

  void DefaultState(State state_default, State state_default_at_start_of_line,
                    State state_after_slash, bool after_newline,
                    ParseData* result) {
    Seek seek = result->seek();

    // The most common transition (but sometimes overriden below).
    result->SetState(state_default);

    auto c = seek.read();
    seek.Once();
    if (c == L'\n') {
      result->SetState(state_default_at_start_of_line);
      return;
    }
    if (c == L'\t' || c == L' ') {
      return;
    }

    if (after_newline && c == '#') {
      PreprocessorDirective(result);
      result->SetState(state_default_at_start_of_line);
      return;
    }

    if (identifier_chars.contains(tolower(c))) {
      Identifier(result);
      return;
    }

    if (c == '/') {
      result->SetState(state_after_slash);
      return;
    }

    if (c == L'"') {
      LiteralString(result);
      return;
    }

    if (c == L'\'') {
      LiteralCharacter(result);
      return;
    }

    if (c == L'{' || c == L'(') {
      result->Push(c == L'{' ? BRACKET_DEFAULT : PARENS_DEFAULT,
                   ColumnNumberDelta(1), {}, {});
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
      return;
    }

    if (c == L'}' || c == ')') {
      if ((c == L'}' && state_default == BRACKET_DEFAULT) ||
          (c == L')' && state_default == PARENS_DEFAULT)) {
        auto modifiers = HashToModifiers(result->AddAndGetNesting(),
                                         HashToModifiersBold::kSometimes);
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

NonNull<std::unique_ptr<TreeParser>> NewCppTreeParser(
    std::unordered_set<NonEmptySingleLine> keywords,
    std::unordered_set<NonEmptySingleLine> typos,
    IdentifierBehavior identifier_behavior) {
  return MakeNonNullUnique<CppTreeParser>(std::move(keywords), std::move(typos),
                                          identifier_behavior);
}

}  // namespace afc::editor

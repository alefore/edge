#include "src/cpp_parse_tree.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/infrastructure/tracker.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/safe_types.h"
#include "src/lru_cache.h"
#include "src/parse_tools.h"
#include "src/seek.h"

namespace afc::editor {
using infrastructure::Tracker;
using language::MakeNonNullUnique;
using language::NonNull;

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

static const std::wstring identifier_chars =
    L"_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static const std::wstring digit_chars = L"1234567890";
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::BG_RED, LineModifier::BOLD});

class CppTreeParser : public TreeParser {
 public:
  CppTreeParser(std::unordered_set<std::wstring> keywords,
                std::unordered_set<std::wstring> typos,
                IdentifierBehavior identifier_behavior)
      : words_parser_(NewWordsTreeParser(
            L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", typos,
            NewNullTreeParser())),
        keywords_(std::move(keywords)),
        typos_(std::move(typos)),
        identifier_behavior_(identifier_behavior),
        cache_(1) {}

  ParseTree FindChildren(const BufferContents& buffer, Range range) override {
    static Tracker top_tracker(L"CppTreeParser::FindChildren");
    auto call = top_tracker.Call();
    cache_.SetMaxSize(buffer.size().read());

    std::vector<size_t> states_stack = {DEFAULT_AT_START_OF_LINE};
    std::vector<ParseTree> trees = {ParseTree(range)};
    range.ForEachLine([&](LineNumber i) {
      size_t hash = GetLineHash(buffer.at(i)->contents().value(), states_stack);
      auto parse_results = cache_.Get(hash, [&] {
        static Tracker tracker(L"CppTreeParser::FindChildren::Parse");
        auto call = tracker.Call();
        ParseData data(buffer, std::move(states_stack),
                       std::min(LineColumn(i + LineNumberDelta(1)), range.end));
        data.set_position(std::max(LineColumn(i), range.begin));
        ParseLine(&data);
        return *data.parse_results();
      });

      static Tracker execute_tracker(
          L"CppTreeParser::FindChildren::ExecuteActions");
      auto execute_call = execute_tracker.Call();
      for (auto& action : parse_results->actions) {
        action.Execute(&trees, i);
      }
      states_stack = parse_results->states_stack;
    });

    auto final_position =
        LineColumn(buffer.EndLine(), buffer.back()->EndColumn());
    if (final_position >= range.end) {
      DVLOG(5) << "Draining final states: " << states_stack.size();
      ParseData data(buffer, std::move(states_stack),
                     std::min(LineColumn(LineNumber(0) + buffer.size() +
                                         LineNumberDelta(1)),
                              range.end));
      while (data.parse_results()->states_stack.size() > 1) {
        data.PopBack();
      }
      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees, final_position.line);
      }
    }
    CHECK(!trees.empty());
    return trees[0];
  }

  void ParseLine(ParseData* result) {
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
  size_t GetLineHash(const LazyString& line,
                     const std::vector<size_t>& states) {
    using language::compute_hash;
    using language::MakeHashableIteratorRange;
    static Tracker tracker(L"CppTreeParser::GetLineHash");
    auto call = tracker.Call();
    return compute_hash(line, MakeHashableIteratorRange(states));
  }

  void AfterSlash(State state_default, State state_default_at_start_of_line,
                  ParseData* result) {
    auto seek = result->seek();
    switch (seek.read()) {
      case '/':
        result->SetState(state_default_at_start_of_line);
        CommentToEndOfLine(result);
        break;
      case '*':
        result->Push(COMMENT, ColumnNumberDelta(1), {LineModifier::BLUE}, {});
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
                       {LineModifier::BLUE});
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
      result->PushAndPop(rewind_column, {LineModifier::YELLOW});
    } else {
      result->set_position(original_position);
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
    }
  }

  void LiteralString(ParseData* result) {
    auto original_position = result->position();
    CHECK_GT(original_position.column, ColumnNumber(0));

    Seek seek = result->seek();
    while (seek.read() != L'"' && seek.read() != L'\n' && !seek.AtRangeEnd()) {
      if (seek.read() == '\\') {
        seek.Once();
      }
      seek.Once();
    }
    if (seek.read() == L'"') {
      seek.Once();
      CHECK_EQ(result->position().line, original_position.line);
      result->PushAndPop(result->position().column - original_position.column +
                             ColumnNumberDelta(1),
                         {LineModifier::YELLOW});
      // TODO: words_parser_->FindChildren(result->buffer(), tree);
    } else {
      result->set_position(original_position);
      result->PushAndPop(ColumnNumberDelta(1), BAD_PARSE_MODIFIERS);
    }
  }

  void PreprocessorDirective(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, ColumnNumber(1));
    --original_position.column;

    result->seek().ToEndOfLine();
    CHECK_GT(result->position().column, original_position.column);
    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
  }

  void Identifier(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, ColumnNumber(1));
    original_position.column--;

    static const std::wstring cont = identifier_chars + digit_chars;
    result->seek().UntilCurrentCharNotIn(cont);

    CHECK_EQ(original_position.line, result->position().line);
    CHECK_GT(result->position().column, original_position.column);
    auto length = result->position().column - original_position.column;
    NonNull<std::shared_ptr<const LazyString>> str =
        Substring(result->buffer().at(original_position.line)->contents(),
                  original_position.column, length);
    LineModifierSet modifiers;
    // TODO(2022-04-22): Avoid the call to ToString?
    if (keywords_.find(str->ToString()) != keywords_.end()) {
      modifiers.insert(LineModifier::CYAN);
    } else if (typos_.find(str->ToString()) != typos_.end()) {
      modifiers.insert(LineModifier::RED);
    } else if (identifier_behavior_ == IdentifierBehavior::kColorByHash) {
      modifiers = HashToModifiers(std::hash<std::wstring>{}(str->ToString()),
                                  HashToModifiersBold::kNever);
    }
    result->PushAndPop(length, std::move(modifiers));
  }

  void LiteralNumber(ParseData* result) {
    CHECK_GE(result->position().column, ColumnNumber(1));
    LineColumn original_position = result->position();
    original_position.column--;

    result->seek().UntilCurrentCharNotIn(digit_chars);
    CHECK_EQ(result->position().line, original_position.line);
    CHECK_GT(result->position(), original_position);

    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
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

    if (identifier_chars.find(tolower(c)) != identifier_chars.npos) {
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
      LiteralNumber(result);
      return;
    }
  }

  enum class HashToModifiersBold { kSometimes, kNever };
  LineModifierSet HashToModifiers(int nesting,
                                  HashToModifiersBold bold_behavior) {
    LineModifierSet output;
    static std::vector<LineModifier> modifiers = {
        LineModifier::CYAN, LineModifier::YELLOW, LineModifier::RED,
        LineModifier::BLUE, LineModifier::GREEN,  LineModifier::MAGENTA,
        LineModifier::WHITE};
    output.insert(modifiers[nesting % modifiers.size()]);
    if (bold_behavior == HashToModifiersBold::kSometimes &&
        ((nesting / modifiers.size()) % 2) == 0) {
      output.insert(LineModifier::BOLD);
    }
    return output;
  }

  const NonNull<std::unique_ptr<TreeParser>> words_parser_;
  const std::unordered_set<std::wstring> keywords_;
  const std::unordered_set<std::wstring> typos_;
  const IdentifierBehavior identifier_behavior_;

  // Allows us to avoid reparsing previously parsed lines. The key is the hash
  // of:
  //
  // - The contents of a line.
  // - The stack of states available when parsing of the line starts.
  //
  // The values are the results of parsing the line.
  LRUCache<size_t, ParseResults> cache_;
};

}  // namespace

NonNull<std::unique_ptr<TreeParser>> NewCppTreeParser(
    std::unordered_set<std::wstring> keywords,
    std::unordered_set<std::wstring> typos,
    IdentifierBehavior identifier_behavior) {
  return MakeNonNullUnique<CppTreeParser>(std::move(keywords), std::move(typos),
                                          identifier_behavior);
}

}  // namespace afc::editor

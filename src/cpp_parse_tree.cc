#include "cpp_parse_tree.h"

#include <glog/logging.h>

#include "buffer.h"

namespace afc {
namespace editor {
namespace {

enum State {
  FINISH,
  DEFAULT_AT_START_OF_LINE,
  DEFAULT,
  PREPROCESSOR_DIRECTIVE,
  IDENTIFIER,
  AFTER_SLASH,
  COMMENT_TO_END_OF_LINE,
  LITERAL_STRING,
  LITERAL_CHARACTER,
  LITERAL_NUMBER,

  BRACKET_DEFAULT_AT_START_OF_LINE,
  BRACKET_DEFAULT,
  BRACKET_AFTER_SLASH,

  PARENS_DEFAULT_AT_START_OF_LINE,
  PARENS_DEFAULT,
  PARENS_AFTER_SLASH,
};

static const wstring identifier_chars = L"_abcdefghijklmnopqrstuvwxyz";

class CppTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;
    int nesting = 0;
    Consume(DEFAULT_AT_START_OF_LINE, buffer, root, &nesting, nullptr, nullptr,
            root->range.begin, root->range.end);
    return;
  }

 private:
  bool IsReservedToken(const wstring& str) {
    // TODO: Allow the buffer to specify this through a variable.
    static const std::unordered_set<wstring> tokens = {
        L"static", L"extern", L"override", L"virtual",
        L"class", L"struct", L"private", L"public",
        L"using", L"typedef", L"namespace", L"sizeof",
        L"static_cast", L"dynamic_cast",
        L"delete", L"new",
        // Flow control.
        L"switch", L"case", L"default",
        L"if", L"else",
        L"for", L"while", L"do",
        L"return",
        // Types
        L"void", L"const", L"auto",
        L"unique_ptr", L"shared_ptr",
        L"std", L"function", L"vector", L"list",
        L"map", L"unordered_map", L"set", L"unordered_set",
        L"int", L"double", L"float", L"string", L"wstring", L"bool", L"char",
        L"size_t",
        // Values
        L"true", L"false", L"nullptr", L"NULL" };
    return tokens.find(str) != tokens.end();
  }

  struct ParseResult {
    // Input/output parameters:
    LineColumn position;
    LineColumn limit;
    ParseTree* tree;

    // Output parameters:
    State state = DEFAULT;
    bool has_nested_state = false;
    State nested_state = DEFAULT;
    size_t nested_state_rewind_column = 0;

    // Only checked if has_nested_state is true:
    bool has_first_child = false;
    Range first_child_range;
    LineModifierSet first_child_modifiers;
  };

  void Consume(
      State state, const BufferContents& buffer, ParseTree* block, int* nesting,
      const ParseTree* open_character, wint_t* closing_character,
      LineColumn position, LineColumn limit) {
    CHECK(block != nullptr);
    std::vector<State> states = {state};
    std::vector<ParseTree*> trees = {block};
    while (!trees.empty()) {
      CHECK_EQ(states.size(), trees.size());
      CHECK(trees.back() != nullptr);

      ParseResult result;
      result.position = position;
      result.limit = limit;
      result.tree = trees.back();

      // Skip spaces.
      AdvancePositionUntil(buffer, &result,
          [](wchar_t c) { return !iswspace(c) || c == L'\n'; });

      if (result.position >= result.limit) {
        trees.back()->range.end = limit;
        states.pop_back();
        trees.pop_back();
        CHECK(trees.empty() || trees.back() != nullptr);
        position = result.position;
        continue;
      }

      switch (states.back()) {
        case FINISH:
          states.pop_back();
          trees.pop_back();
          CHECK(trees.empty() || trees.back() != nullptr);
          continue;

        case DEFAULT_AT_START_OF_LINE:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       buffer, nesting, true, &result);
          break;

        case BRACKET_DEFAULT_AT_START_OF_LINE:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, buffer, nesting, true, &result);
          break;

        case PARENS_DEFAULT_AT_START_OF_LINE:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, buffer, nesting, true, &result);
          break;

        case DEFAULT:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       buffer, nesting, false, &result);
          break;

        case BRACKET_DEFAULT:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, buffer, nesting, false, &result);
          break;

        case PARENS_DEFAULT:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, buffer, nesting, false, &result);
          break;

        case PREPROCESSOR_DIRECTIVE:
          AdvanceUntilEndOfLine(buffer, &result);
          trees.back()->range.end = result.position;
          trees.back()->modifiers.insert(LineModifier::YELLOW);
          result.state = FINISH;
          break;

        case IDENTIFIER:
          Identifier(buffer, &result);
          break;

        case AFTER_SLASH:
          AfterSlash(DEFAULT, DEFAULT_AT_START_OF_LINE, buffer, &result);
          break;

        case BRACKET_AFTER_SLASH:
          AfterSlash(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE, buffer,
                     &result);
          break;

        case PARENS_AFTER_SLASH:
          AfterSlash(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE, buffer,
                     &result);
          break;

        case COMMENT_TO_END_OF_LINE:
          CommentToEndOfLine(buffer, &result);
          break;

        case LITERAL_STRING:
          LiteralString(buffer, &result);
          break;

        case LITERAL_CHARACTER:
          LiteralCharacter(buffer, &result);
          break;

        case LITERAL_NUMBER:
          LiteralNumber(buffer, &result);
          break;
      }

      states.back() = result.state;
      CHECK_LE(position, result.position);
      position = result.position;
      if (result.has_nested_state) {
        states.push_back(result.nested_state);
        trees.push_back(PushChild(trees.back()).release());
        CHECK(trees.back() != nullptr);
        trees.back()->range.begin = position;
        CHECK_GE(position.column, result.nested_state_rewind_column);
        trees.back()->range.begin.column -= result.nested_state_rewind_column;

        if (result.has_first_child) {
          auto first_child = PushChild(trees.back());
          first_child->range = result.first_child_range;
          first_child->modifiers = result.first_child_modifiers;
        }
      }
    }
  }

  void AfterSlash(State state_default, State state_default_at_start_of_line,
                  const BufferContents& buffer, ParseResult* result) {
    auto c = buffer.character_at(result->position);
    AdvancePosition(buffer, result);
    if (c == '/') {
      result->state = state_default_at_start_of_line;
      result->has_nested_state = true;
      result->nested_state = COMMENT_TO_END_OF_LINE;
      result->nested_state_rewind_column = 2;
    } else {
      result->state = state_default;
    }
  }

  void CommentToEndOfLine(const BufferContents& buffer, ParseResult* result) {
    AdvanceUntilEndOfLine(buffer, result);
    result->tree->modifiers.insert(LineModifier::BLUE);
    result->tree->range.end = result->position;
    words_parser_->FindChildren(buffer, result->tree);
    result->state = FINISH;
  }

  void LiteralCharacter(const BufferContents& buffer, ParseResult* result) {
    auto original_position = result->position;
    if (buffer.character_at(result->position) == '\\') {
      AdvancePosition(buffer, result);
    }
    AdvancePosition(buffer, result);
    if (buffer.character_at(result->position) == '\'') {
      result->tree->modifiers = {LineModifier::YELLOW};
      AdvancePosition(buffer, result);
    } else {
      result->position = original_position;
      result->tree->modifiers = {LineModifier::BG_RED, LineModifier::BOLD};
    }
    result->tree->range.end = result->position;
    result->state = FINISH;
  }

  void LiteralString(const BufferContents& buffer, ParseResult* result) {
    while (buffer.character_at(result->position) != L'"' &&
           result->position < result->limit) {
      if (buffer.character_at(result->position) == '\\') {
       AdvancePosition(buffer, result);
      }
      AdvancePosition(buffer, result);
    }
    AdvancePosition(buffer, result);  // Skip the closing character.
    result->tree->range.end = result->position;
    words_parser_->FindChildren(buffer, result->tree);
    result->tree->modifiers.insert(LineModifier::YELLOW);
    result->state = FINISH;
  }

  void Identifier(const BufferContents& buffer, ParseResult* result) {
    AdvancePositionUntil(buffer, result,
        [](wchar_t c) {
          static const wstring cont = identifier_chars + L"0123456789";
          return cont.find(tolower(c)) == cont.npos;
        });
    auto* range = &result->tree->range;
    range->end = result->position;
    if (range->begin.line == range->end.line) {
      auto str = Substring(
              buffer.at(range->begin.line)->contents(),
              range->begin.column, range->end.column - range->begin.column)
          ->ToString();
      if (IsReservedToken(str)) {
        result->tree->modifiers.insert(LineModifier::CYAN);
      }
    }
    result->state = FINISH;
  }

  void LiteralNumber(const BufferContents& buffer, ParseResult* result) {
    AdvancePositionUntil(buffer, result, [](wchar_t c) { return !isdigit(c); });
    result->tree->range.end = result->position;
    result->tree->modifiers.insert(LineModifier::YELLOW);
    result->state = FINISH;
  }

  void DefaultState(
      State state_default, State state_default_at_start_of_line,
      State state_after_slash, const BufferContents& buffer, int* nesting,
      bool after_newline, ParseResult* result) {
    auto original_position = result->position;
    auto c = buffer.character_at(result->position);
    AdvancePosition(buffer, result);

    if (c == L'\n') {
      result->state = state_default_at_start_of_line;
      return;
    }

    if (after_newline && c == '#') {
      result->state = state_default_at_start_of_line;
      result->has_nested_state = true;
      result->nested_state = PREPROCESSOR_DIRECTIVE;
      result->nested_state_rewind_column = 1;
      return;
    }

    if (identifier_chars.find(tolower(c)) != identifier_chars.npos) {
      result->state = state_default;
      result->has_nested_state = true;
      result->nested_state = IDENTIFIER;
      result->nested_state_rewind_column = 1;
      return;
    }

    if (c == '/') {
      result->state = state_after_slash;
      return;
    }

    if (c == L'"') {
      result->state = state_default;
      result->has_nested_state = true;
      result->nested_state = LITERAL_STRING;
      result->nested_state_rewind_column = 1;
      return;
    }

    if (c == L'\'') {
      result->state = state_default;
      result->has_nested_state = true;
      result->nested_state = LITERAL_CHARACTER;
      result->nested_state_rewind_column = 1;
      return;
    }

    if (c == L'{' || c == L'(') {
      result->state = state_default;
      result->has_nested_state = true;
      result->nested_state = c == L'{' ? BRACKET_DEFAULT : PARENS_DEFAULT;
      result->has_first_child = true;
      result->first_child_range = Range(original_position, result->position);
      result->first_child_modifiers =
          {LineModifier::BG_RED, LineModifier::BOLD};
      return;
    }

    if (c == L'}' || c == ')') {
      if ((c == L'}' && state_default == BRACKET_DEFAULT) ||
          (c == L')' && state_default == PARENS_DEFAULT)) {
        auto tree_end = PushChild(result->tree);
        tree_end->range = Range(original_position, result->position);

        auto modifiers = ModifierForNesting((*nesting)++);
        result->tree->children.front().modifiers = modifiers;
        result->tree->children.back().modifiers = modifiers;

        result->state = FINISH;
        result->tree->range.end = result->position;
        result->state = FINISH;
      } else {
        auto tree_end = PushChild(result->tree);
        tree_end->range = Range(original_position, result->position);
        tree_end->modifiers = {LineModifier::BG_RED, LineModifier::BOLD};
      }
      return;
    }

    if (isdigit(c)) {
      result->state = state_default;
      result->has_nested_state = true;
      result->nested_state = LITERAL_NUMBER;
      result->nested_state_rewind_column = 1;
      return;
    }

    result->state = state_default;
  }

  void AdvancePosition(const BufferContents& buffer, ParseResult* result) {
    if (result->position >= result->limit) { return; }
    if (buffer.at(result->position.line)->size() > result->position.column) {
      result->position.column++;
    } else if (buffer.size() > result->position.line + 1) {
      result->position.line++;
      result->position.column = 0;
    }
  }

  void AdvanceUntilEndOfLine(
      const BufferContents& buffer, ParseResult* result) {
    result->position.column = buffer.at(result->position.line)->size();
    result->position = min(result->limit, result->position);
  }

  std::unordered_set<LineModifier, hash<int>> ModifierForNesting(
      int nesting) {
    std::unordered_set<LineModifier, hash<int>> output;
    switch (nesting % 5) {
      case 0:
        output.insert(LineModifier::CYAN);
        break;
      case 1:
        output.insert(LineModifier::YELLOW);
        break;
      case 2:
        output.insert(LineModifier::RED);
        break;
      case 3:
        output.insert(LineModifier::BLUE);
        break;
      case 4:
        output.insert(LineModifier::GREEN);
        break;
    }
    if (((nesting / 5) % 2) == 0) {
      output.insert(LineModifier::BOLD);
    }
    return output;
  }

  void AdvancePositionUntil(const BufferContents& buffer,
                            ParseResult* result,
                            std::function<bool(wchar_t)> predicate) {
    wstring valid = L"abcdefghijklmnopqrstuvwxyz";
    while (!predicate(buffer.character_at(result->position))) {
      auto old_position = result->position;
      AdvancePosition(buffer, result);
      if (result->position == old_position ||
          result->position >= result->limit) {
        return;
      }
    }
  }

  std::unique_ptr<TreeParser> words_parser_ =
      NewWordsTreeParser(
           L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
           NewNullTreeParser());
};

}  // namespace

std::unique_ptr<TreeParser> NewCppTreeParser() {
  return std::unique_ptr<TreeParser>(new CppTreeParser());
}

}  // namespace editor
}  // namespace afc

#include "cpp_parse_tree.h"

#include <glog/logging.h>

#include "buffer.h"

namespace afc {
namespace editor {
namespace {

enum State {
  DEFAULT_AT_START_OF_LINE,
  DEFAULT,
  AFTER_SLASH,

  PREPROCESSOR_DIRECTIVE,
  IDENTIFIER,
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
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::BG_RED, LineModifier::BOLD});

class ParseResult {
 public:
  ParseResult(const BufferContents& buffer, ParseTree* root,
              State initial_state)
      : buffer_(buffer),
        trees_({root}),
        states_({initial_state}),
        position_(root->range.begin),
        limit_(root->range.end) {}

  void CheckInvariants() const {
    CHECK_EQ(states_.size(), trees_.size());
    CHECK(empty() || tree() != nullptr);
  }

  const BufferContents& buffer() const {
    return buffer_;
  }

  LineColumn position() const {
    return position_;
  }

  void set_position(LineColumn position) {
    CHECK_LE(position, limit_);
    position_ = position;
  }

  bool reached_final_position() const {
    return position_ >= limit_;
  }

  void AdvancePosition() {
    if (reached_final_position()) { return; }
    if (buffer_.at(position_.line)->size() > position_.column) {
      position_.column++;
    } else if (buffer_.size() > position_.line + 1) {
      position_.line++;
      position_.column = 0;
    }
  }

  void AdvancePositionUntilEndOfLine() {
    position_.column = buffer_.at(position_.line)->size();
    position_ = min(limit_, position_);
  }

  void AdvancePositionUntil(std::function<bool(wchar_t)> predicate) {
    wstring valid = L"abcdefghijklmnopqrstuvwxyz";
    while (!predicate(read()) && !reached_final_position()) {
      auto old_position = position_;
      AdvancePosition();
      CHECK_LT(old_position, position());
    }
  }

  wchar_t read() const {
    return buffer_.character_at(position_);
  }

  State state() const {
    return states_.back();
  }

  void SetState(State state) {
    states_.back() = state;
  }

  ParseTree* tree() const {
    return trees_.back();
  }

  ParseTree* PushTree() {
    return PushChild(trees_.back()).release();
  }

  bool empty() const {
    return trees_.empty();
  }

  ParseTree* PopBack() {
    CheckInvariants();
    CHECK(!trees_.empty());
    auto output = trees_.back();
    output->range.end = min(position_, limit_);
    states_.pop_back();
    trees_.pop_back();
    return output;
  }

  ParseTree* Push(State nested_state, size_t rewind_column) {
    CheckInvariants();
    states_.push_back(nested_state);
    trees_.push_back(PushChild(trees_.back()).release());
    trees_.back()->range.begin = position_;
    CHECK_GE(position_.column, rewind_column);
    trees_.back()->range.begin.column -= rewind_column;
    CheckInvariants();
    return tree();
  }

 private:
  const BufferContents& buffer_;
  std::vector<ParseTree*> trees_;
  std::vector<State> states_;
  LineColumn position_;
  LineColumn limit_;
};

class CppTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;

    ParseResult result(buffer, root, DEFAULT_AT_START_OF_LINE);

    int nesting = 0;
    while (!result.empty()) {
      result.CheckInvariants();

      // Skip spaces.
      result.AdvancePositionUntil(
          [](wchar_t c) { return !iswspace(c) || c == L'\n'; });

      if (result.reached_final_position()) {
        result.PopBack();
        result.CheckInvariants();
        continue;
      }

      // Only used for validation:
      LineColumn original_position = result.position();

      switch (result.state()) {
        case DEFAULT_AT_START_OF_LINE:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       &nesting, true, &result);
          break;

        case BRACKET_DEFAULT_AT_START_OF_LINE:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, &nesting, true, &result);
          break;

        case PARENS_DEFAULT_AT_START_OF_LINE:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, &nesting, true, &result);
          break;

        case DEFAULT:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       &nesting, false, &result);
          break;

        case BRACKET_DEFAULT:
          DefaultState(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                       BRACKET_AFTER_SLASH, &nesting, false, &result);
          break;

        case PARENS_DEFAULT:
          DefaultState(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                       PARENS_AFTER_SLASH, &nesting, false, &result);
          break;

        case PREPROCESSOR_DIRECTIVE:
          result.AdvancePositionUntilEndOfLine();
          result.tree()->modifiers.insert(LineModifier::YELLOW);
          result.PopBack();
          break;

        case IDENTIFIER:
          Identifier(&result);
          break;

        case AFTER_SLASH:
          AfterSlash(DEFAULT, DEFAULT_AT_START_OF_LINE, &result);
          break;

        case BRACKET_AFTER_SLASH:
          AfterSlash(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                     &result);
          break;

        case PARENS_AFTER_SLASH:
          AfterSlash(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE, &result);
          break;

        case COMMENT_TO_END_OF_LINE:
          CommentToEndOfLine(&result);
          break;

        case LITERAL_STRING:
          LiteralString(&result);
          break;

        case LITERAL_CHARACTER:
          LiteralCharacter(&result);
          break;

        case LITERAL_NUMBER:
          LiteralNumber(&result);
          break;
      }

      CHECK_LE(original_position, result.position());
    }
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

  void AfterSlash(State state_default, State state_default_at_start_of_line,
                  ParseResult* result) {
    if (result->read() == '/') {
      result->SetState(state_default_at_start_of_line);
      result->Push(COMMENT_TO_END_OF_LINE, 1);
    } else {
      result->SetState(state_default);
    }
    result->AdvancePosition();
  }

  void CommentToEndOfLine(ParseResult* result) {
    result->AdvancePositionUntilEndOfLine();
    auto comment_tree = result->PopBack();
    comment_tree->modifiers.insert(LineModifier::BLUE);
    words_parser_->FindChildren(result->buffer(), comment_tree);
  }

  void LiteralCharacter(ParseResult* result) {
    auto original_position = result->position();
    if (result->read() == '\\') {
      result->AdvancePosition();
    }
    result->AdvancePosition();  // Skip the character.
    if (result->read() == '\'') {
      result->AdvancePosition();
      result->PopBack()->modifiers = {LineModifier::YELLOW};
    } else {
      result->set_position(original_position);
      result->PopBack();
    }
  }

  void LiteralString(ParseResult* result) {
    auto original_position = result->position();
    while (result->read() != L'"' && !result->reached_final_position()) {
      if (result->read() == '\\') {
       result->AdvancePosition();
      }
      result->AdvancePosition();
    }
    if (result->read() == L'"') {
      result->AdvancePosition();  // Skip the closing character.
      auto tree = result->PopBack();
      tree->modifiers = {LineModifier::YELLOW};
      words_parser_->FindChildren(result->buffer(), tree);
    } else {
      result->set_position(original_position);
      result->PopBack();
    }
  }

  void Identifier(ParseResult* result) {
    result->AdvancePositionUntil(
        [](wchar_t c) {
          static const wstring cont = identifier_chars + L"0123456789";
          return cont.find(tolower(c)) == cont.npos;
        });
    auto tree = result->PopBack();
    if (tree->range.begin.line == tree->range.end.line) {
      auto str = Substring(
              result->buffer().at(tree->range.begin.line)->contents(),
              tree->range.begin.column,
              tree->range.end.column - tree->range.begin.column);
      if (IsReservedToken(str->ToString())) {
        tree->modifiers.insert(LineModifier::CYAN);
      }
    }
  }

  void LiteralNumber(ParseResult* result) {
    result->AdvancePositionUntil([](wchar_t c) { return !isdigit(c); });
    result->PopBack()->modifiers.insert(LineModifier::YELLOW);
  }

  void DefaultState(
      State state_default, State state_default_at_start_of_line,
      State state_after_slash, int* nesting,
      bool after_newline, ParseResult* result) {
    // The most common transition (but sometimes overriden below).
    result->SetState(state_default);

    auto original_position = result->position();
    auto c = result->read();
    result->AdvancePosition();
    CHECK_GT(result->position(), original_position);

    if (c == L'\n') {
      result->SetState(state_default_at_start_of_line);
      return;
    }

    if (after_newline && c == '#') {
      result->SetState(state_default_at_start_of_line);
      result->Push(PREPROCESSOR_DIRECTIVE, 1);
      return;
    }

    if (identifier_chars.find(tolower(c)) != identifier_chars.npos) {
      result->Push(IDENTIFIER, 1);
      return;
    }

    if (c == '/') {
      result->SetState(state_after_slash);
      return;
    }

    if (c == L'"') {
      result->Push(LITERAL_STRING, 1)->modifiers = BAD_PARSE_MODIFIERS;
      return;
    }

    if (c == L'\'') {
      result->Push(LITERAL_CHARACTER, 1)->modifiers = BAD_PARSE_MODIFIERS;
      return;
    }

    if (c == L'{' || c == L'(') {
      result->Push(c == L'{' ? BRACKET_DEFAULT : PARENS_DEFAULT, 0);
      auto first_child = result->PushTree();
      first_child->range = Range(original_position, result->position());
      first_child->modifiers = BAD_PARSE_MODIFIERS;
      return;
    }

    if (c == L'}' || c == ')') {
      if ((c == L'}' && state_default == BRACKET_DEFAULT) ||
          (c == L')' && state_default == PARENS_DEFAULT)) {
        auto modifiers = ModifierForNesting((*nesting)++);
        auto tree = result->PopBack();
        PushChild(tree)->range = Range(original_position, result->position());
        tree->children.front().modifiers = modifiers;
        tree->children.back().modifiers = modifiers;
      } else {
        auto tree_end = result->PushTree();
        tree_end->range = Range(original_position, result->position());
        tree_end->modifiers = BAD_PARSE_MODIFIERS;
      }
      return;
    }

    if (isdigit(c)) {
      result->Push(LITERAL_NUMBER, 1);
      return;
    }
  }

  LineModifierSet ModifierForNesting(int nesting) {
    LineModifierSet output;
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

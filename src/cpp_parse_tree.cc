#include "cpp_parse_tree.h"

#include <glog/logging.h>

#include "buffer.h"
#include "src/seek.h"

namespace afc {
namespace editor {
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

static const wstring identifier_chars = L"_abcdefghijklmnopqrstuvwxyz";
static const wstring digit_chars = L"1234567890";
static const LineModifierSet BAD_PARSE_MODIFIERS =
    LineModifierSet({LineModifier::BG_RED, LineModifier::BOLD});

struct Action {
  static Action Push(size_t column, LineModifierSet modifiers) {
    return Action(PUSH, column, std::move(modifiers));
  }

  static Action Pop(size_t column) { return Action(POP, column, {}); }

  static Action SetFirstChildModifiers(LineModifierSet modifiers) {
    return Action(SET_FIRST_CHILD_MODIFIERS, 0, std::move(modifiers));
  }

  void Execute(std::vector<ParseTree*>* trees, size_t line) {
    switch (action_type) {
      case PUSH:
        trees->push_back(PushChild(trees->back()).release());
        trees->back()->range.begin = LineColumn(line, column);
        trees->back()->modifiers = modifiers;
        break;

      case POP:
        trees->back()->range.end = LineColumn(line, column);
        trees->pop_back();
        break;

      case SET_FIRST_CHILD_MODIFIERS:
        trees->back()->children.front().modifiers = modifiers;
        break;
    }
  }

  enum ActionType {
    PUSH,
    POP,

    // Set the modifiers of the first child of the current tree.
    SET_FIRST_CHILD_MODIFIERS,
  };

  ActionType action_type = PUSH;

  size_t column;

  // Used by PUSH and by SET_FIRST_CHILD_MODIFIERS.
  LineModifierSet modifiers;

 private:
  Action(ActionType action_type, size_t column, LineModifierSet modifiers)
      : action_type(action_type),
        column(column),
        modifiers(std::move(modifiers)) {}
};

struct ParseResults {
  std::vector<size_t> states_stack;
  std::vector<Action> actions;
};

class ParseData {
 public:
  ParseData(const BufferContents& buffer, std::vector<size_t> initial_states,
            LineColumn limit)
      : buffer_(buffer), seek_(buffer_, &position_) {
    parse_results_.states_stack = std::move(initial_states);
    seek_.WithRange(Range(LineColumn(), limit)).WrappingLines();
  }

  const BufferContents& buffer() const { return buffer_; }

  Seek& seek() { return seek_; }

  ParseResults* parse_results() { return &parse_results_; }

  LineColumn position() const { return position_; }

  void set_position(LineColumn position) { position_ = position; }

  int AddAndGetNesting() { return nesting_++; }

  size_t state() const { return parse_results_.states_stack.back(); }

  void SetState(State state) { parse_results_.states_stack.back() = state; }

  void SetFirstChildModifiers(LineModifierSet modifiers) {
    parse_results_.actions.push_back(Action::SetFirstChildModifiers(modifiers));
  }

  void PopBack() {
    parse_results_.states_stack.pop_back();
    parse_results_.actions.push_back(Action::Pop(position_.column));
  }

  void Push(State nested_state, size_t rewind_column,
            LineModifierSet modifiers) {
    CHECK_GE(position_.column, rewind_column);

    parse_results_.states_stack.push_back(nested_state);

    parse_results_.actions.push_back(
        Action::Push(position_.column - rewind_column, modifiers));
  }

  void PushAndPop(size_t rewind_column, LineModifierSet modifiers) {
    State ignored_state = DEFAULT;
    Push(ignored_state, rewind_column, std::move(modifiers));
    PopBack();
  }

 private:
  const BufferContents& buffer_;
  ParseResults parse_results_;
  LineColumn position_;
  Seek seek_;
  int nesting_ = 0;
};

class CppTreeParser : public TreeParser {
 public:
  CppTreeParser(std::unordered_set<wstring> keywords)
      : keywords_(std::move(keywords)) {}

  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;

    // TODO: Does this actually clean up expired references? Probably not?
    cache_.erase(std::weak_ptr<LazyString>());

    std::vector<size_t> states_stack = {DEFAULT_AT_START_OF_LINE};
    std::vector<ParseTree*> trees = {root};
    for (size_t i = root->range.begin.line; i < root->range.end.line; i++) {
      auto insert_results = cache_[buffer.at(i)->contents()].insert(
          {states_stack, ParseResults()});
      if (insert_results.second) {
        ParseData data(buffer, std::move(states_stack),
                       min(LineColumn(i + 1, 0), root->range.end));
        data.set_position(max(LineColumn(i, 0), root->range.begin));
        ParseLine(&data);
        insert_results.first->second = *data.parse_results();
      }
      for (auto& action : insert_results.first->second.actions) {
        action.Execute(&trees, i);
      }
      states_stack = insert_results.first->second.states_stack;
    }
  }

  void ParseLine(ParseData* result) {
    while (result->seek().read() != L'\n') {
      LineColumn original_position = result->position();  // For validation.

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
        result->Push(COMMENT, 1, {LineModifier::BLUE});
        seek.Once();
        break;
      default:
        result->SetState(state_default);
    }
  }

  void CommentToEndOfLine(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GT(original_position.column, size_t(0));
    result->seek().ToEndOfLine();
    result->PushAndPop(result->position().column - original_position.column + 1,
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
    size_t rewind_column = 1;
    auto original_position = result->position();
    if (seek.read() == '\\') {
      seek.Once();
      rewind_column++;
    }

    seek.Once();  // Skip the character.
    rewind_column++;

    if (seek.read() == '\'') {
      seek.Once();
      rewind_column++;
      result->PushAndPop(rewind_column, {LineModifier::YELLOW});
    } else {
      result->set_position(original_position);
      result->PushAndPop(1, BAD_PARSE_MODIFIERS);
    }
  }

  void LiteralString(ParseData* result) {
    auto original_position = result->position();
    CHECK_GT(original_position.column, size_t(0));

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
      result->PushAndPop(
          result->position().column - original_position.column + 1,
          {LineModifier::YELLOW});
      // TODO: words_parser_->FindChildren(result->buffer(), tree);
    } else {
      result->set_position(original_position);
      result->PushAndPop(1, BAD_PARSE_MODIFIERS);
    }
  }

  void PreprocessorDirective(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, 1u);
    original_position.column--;

    result->seek().ToEndOfLine();
    CHECK_GT(result->position().column, original_position.column);
    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
  }

  void Identifier(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, 1u);
    original_position.column--;

    static const wstring cont = identifier_chars + digit_chars;
    result->seek().UntilCurrentCharNotIn(cont);

    CHECK_EQ(original_position.line, result->position().line);
    CHECK_GT(result->position().column, original_position.column);
    auto length = result->position().column - original_position.column;
    auto str =
        Substring(result->buffer().at(original_position.line)->contents(),
                  original_position.column, length);
    LineModifierSet modifiers;
    if (keywords_.find(str->ToString()) != keywords_.end()) {
      modifiers.insert(LineModifier::CYAN);
    }
    result->PushAndPop(length, std::move(modifiers));
  }

  void LiteralNumber(ParseData* result) {
    CHECK_GE(result->position().column, 1u);
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
    seek.UntilCurrentCharNotIn(L" \t");

    auto original_position = result->position();
    auto c = seek.read();
    seek.Once();
    if (c == L'\n') {
      result->SetState(state_default_at_start_of_line);
      return;
    }

    CHECK_GT(result->position(), original_position);

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
      result->Push(c == L'{' ? BRACKET_DEFAULT : PARENS_DEFAULT, 0, {});
      result->PushAndPop(1, BAD_PARSE_MODIFIERS);
      return;
    }

    if (c == L'}' || c == ')') {
      if ((c == L'}' && state_default == BRACKET_DEFAULT) ||
          (c == L')' && state_default == PARENS_DEFAULT)) {
        auto modifiers = ModifierForNesting(result->AddAndGetNesting());
        result->PushAndPop(1, modifiers);
        result->SetFirstChildModifiers(modifiers);
        result->PopBack();
      } else {
        result->PushAndPop(1, BAD_PARSE_MODIFIERS);
      }
      return;
    }

    if (isdigit(c)) {
      LiteralNumber(result);
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

  const std::unique_ptr<TreeParser> words_parser_ = NewWordsTreeParser(
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
      NewNullTreeParser());

  const std::unordered_set<wstring> keywords_;

  std::map<std::weak_ptr<LazyString>,
           std::map<std::vector<size_t>, ParseResults>,
           std::owner_less<std::weak_ptr<LazyString>>>
      cache_;
};

}  // namespace

std::unique_ptr<TreeParser> NewCppTreeParser(
    std::unordered_set<wstring> keywords) {
  return std::make_unique<CppTreeParser>(std::move(keywords));
}

}  // namespace editor
}  // namespace afc

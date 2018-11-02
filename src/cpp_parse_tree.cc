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
  COMMENT,

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

struct Action {
  static Action Push(LineColumn position, LineModifierSet modifiers) {
    return Action(PUSH, position, std::move(modifiers));
  }

  static Action Pop(LineColumn position) {
    return Action(POP, position, {});
  }

  static Action SetFirstChildModifiers(LineColumn position,
                                       LineModifierSet modifiers) {
    return Action(SET_FIRST_CHILD_MODIFIERS, position, std::move(modifiers));
  }

  void Execute(std::vector<ParseTree*>* trees) {
    switch (action_type) {
      case PUSH:
        trees->push_back(PushChild(trees->back()).release());
        trees->back()->range.begin = position;
        trees->back()->modifiers = modifiers;
        break;

      case POP:
        trees->back()->range.end = position;
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

  LineColumn position;

  // Used by PUSH and by SET_FIRST_CHILD_MODIFIERS.
  LineModifierSet modifiers;

 private:
  Action(ActionType action_type, LineColumn position, LineModifierSet modifiers)
      : action_type(action_type),
        position(position),
        modifiers(std::move(modifiers)) {}
};

struct ParseResults {
  std::vector<size_t> states_stack;
  std::vector<Action> actions;
};

class ParseData {
 public:
  ParseData(const BufferContents& buffer, std::vector<size_t> initial_states)
      : buffer_(buffer) {
    parse_results_.states_stack = std::move(initial_states);
  }

  const BufferContents& buffer() const {
    return buffer_;
  }

  ParseResults* parse_results() { return &parse_results_; }

  LineColumn position() const {
    return position_;
  }

  void set_position(LineColumn position) {
    CHECK_LE(position, limit_);
    position_ = position;
  }

  void set_limit(LineColumn limit) {
    limit_ = limit;
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

  void SkipSpaces() {
    AdvancePositionUntil([](wchar_t c) { return !iswspace(c) || c == L'\n'; });
  }

  wchar_t read() const {
    return buffer_.character_at(position_);
  }

  int AddAndGetNesting() {
    return nesting_++;
  }

  size_t state() const {
    return parse_results_.states_stack.back();
  }

  void SetState(State state) {
    parse_results_.states_stack.back() = state;
  }

  bool empty() const {
    return parse_results_.states_stack.empty();
  }

  void SetFirstChildModifiers(LineModifierSet modifiers) {
    parse_results_.actions.push_back(
        Action::SetFirstChildModifiers(position_, modifiers));
  }

  void PopBack() {
    parse_results_.states_stack.pop_back();
    parse_results_.actions.push_back(Action::Pop(min(position_, limit_)));
  }

  void Push(State nested_state, size_t rewind_column,
            LineModifierSet modifiers) {
    CHECK_GE(position_.column, rewind_column);

    parse_results_.states_stack.push_back(nested_state);

    parse_results_.actions.push_back(Action::Push(
        LineColumn(position_.line, position_.column - rewind_column),
        modifiers));
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
  LineColumn limit_;
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

    std::vector<size_t> states_stack = { DEFAULT_AT_START_OF_LINE };
    std::vector<ParseTree*> trees = {root};
    for (size_t i = root->range.begin.line; i < root->range.end.line; i++) {
      ParseData data(buffer, std::move(states_stack));
      data.set_limit(min(LineColumn(i + 1, 0), root->range.end));
      data.set_position(max(LineColumn(i, 0), root->range.begin));
      ParseLine(&data);
      states_stack = std::move(data.parse_results()->states_stack);

      for (auto& action : data.parse_results()->actions) {
        action.Execute(&trees);
      }
    }
  }

  void ParseLine(ParseData* result) {
    while (!result->reached_final_position()) {
      LineColumn original_position = result->position();  // For validation.

      switch (result->state()) {
        case DEFAULT_AT_START_OF_LINE:
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       true, result);
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
          DefaultState(DEFAULT, DEFAULT_AT_START_OF_LINE, AFTER_SLASH,
                       false, result);
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
          AfterSlash(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE,
                     result);
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
    switch (result->read()) {
      case '/':
        result->SetState(state_default_at_start_of_line);
        CommentToEndOfLine(result);
        break;
      case '*':
        result->Push(COMMENT, 1, {LineModifier::BLUE});
        result->AdvancePosition();
        break;
      default:
        result->SetState(state_default);
    }
  }

  void CommentToEndOfLine(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GT(original_position.column, size_t(0));
    result->AdvancePositionUntilEndOfLine();
    result->PushAndPop(result->position().column - original_position.column + 1,
                       {LineModifier::BLUE});
    // TODO: words_parser_->FindChildren(result->buffer(), comment_tree);
  }

  void InsideComment(ParseData* result) {
    auto c = result->read();
    result->AdvancePosition();
    if (c == '*' && result->read() == '/') {
      result->AdvancePosition();
      result->PopBack();
    }
  }

  void LiteralCharacter(ParseData* result) {
    size_t rewind_column = 1;
    auto original_position = result->position();
    if (result->read() == '\\') {
      result->AdvancePosition();
      rewind_column++;
    }

    result->AdvancePosition();  // Skip the character.
    rewind_column++;

    if (result->read() == '\'') {
      result->AdvancePosition();
      rewind_column++;
      result->PushAndPop(rewind_column, {LineModifier::YELLOW});
    } else {
      result->set_position(original_position);
      result->PushAndPop(1, BAD_PARSE_MODIFIERS);
    }
  }

  void LiteralString(ParseData* result) {
    auto original_position = result->position();
    CHECK_GT(original_position.column, 0);

    while (result->read() != L'"' && result->read() != L'\n' &&
           !result->reached_final_position()) {
      if (result->read() == '\\') {
        result->AdvancePosition();
      }
      result->AdvancePosition();
    }
    if (result->read() == L'"') {
      result->AdvancePosition();
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

  void PreprocessorDirective(State state, ParseData* result) {
    result->SetState(state);

    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, 1);
    original_position.column--;

    result->AdvancePositionUntilEndOfLine();
    CHECK_GT(result->position().column, original_position.column);
    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
  }

  void Identifier(ParseData* result) {
    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, 1);
    original_position.column--;

    result->AdvancePositionUntil(
        [](wchar_t c) {
          static const wstring cont = identifier_chars + L"0123456789";
          return cont.find(tolower(c)) == cont.npos;
        });

    CHECK_EQ(original_position.line, result->position().line);
    CHECK_GT(result->position().column, original_position.column);
    auto length = result->position().column - original_position.column;
    auto str = Substring(
        result->buffer().at(original_position.line)->contents(),
        original_position.column, length);
    LineModifierSet modifiers;
    if (keywords_.find(str->ToString()) != keywords_.end()) {
      modifiers.insert(LineModifier::CYAN);
    }
    result->PushAndPop(length, std::move(modifiers));
  }

  void LiteralNumber(ParseData* result) {
    CHECK_GE(result->position().column, 1);
    LineColumn original_position = result->position();
    original_position.column--;

    result->AdvancePositionUntil([](wchar_t c) { return !isdigit(c); });
    CHECK_EQ(result->position().line, original_position.line);
    CHECK_GT(result->position(), original_position);

    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
  }

  void DefaultState(
      State state_default, State state_default_at_start_of_line,
      State state_after_slash, bool after_newline, ParseData* result) {
    // The most common transition (but sometimes overriden below).
    result->SetState(state_default);
    result->SkipSpaces();

    auto original_position = result->position();
    auto c = result->read();
    result->AdvancePosition();
    CHECK_GT(result->position(), original_position);

    if (c == L'\n') {
      result->SetState(state_default_at_start_of_line);
      return;
    }

    if (after_newline && c == '#') {
      PreprocessorDirective(state_default_at_start_of_line, result);
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

  const std::unique_ptr<TreeParser> words_parser_ =
      NewWordsTreeParser(
           L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
           NewNullTreeParser());

  const std::unordered_set<wstring> keywords_;
};

}  // namespace

std::unique_ptr<TreeParser> NewCppTreeParser(
    std::unordered_set<wstring> keywords) {
  return std::unique_ptr<TreeParser>(new CppTreeParser(std::move(keywords)));
}

}  // namespace editor
}  // namespace afc

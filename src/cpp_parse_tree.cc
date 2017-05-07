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
  LineColumn position;

  enum ActionType {
    PUSH,
    POP,
  };

  ActionType action_type = PUSH;

  LineModifierSet push_modifiers;
};

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
    CHECK(empty() || trees_.back() != nullptr);
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

  State state() const {
    return states_.back();
  }

  void SetState(State state) {
    states_.back() = state;
  }

  bool empty() const {
    return trees_.empty();
  }

  ParseTree* PopBack() {
    CheckInvariants();
    CHECK(!trees_.empty());
    states_.pop_back();

    Action action;
    action.position = position_;
    action.action_type = Action::POP;
    log_.push_back(action);

    auto output = trees_.back();
    output->range.end = min(position_, limit_);

    trees_.pop_back();
    return output;
  }

  void Push(State nested_state, size_t rewind_column,
            LineModifierSet modifiers) {
    CheckInvariants();
    CHECK_GE(position_.column, rewind_column);

    states_.push_back(nested_state);

    Action action;
    action.position = position_;
    action.position.column -= rewind_column;
    action.action_type = Action::PUSH;
    action.push_modifiers = modifiers;
    log_.push_back(action);

    trees_.push_back(PushChild(trees_.back()).release());
    trees_.back()->range.begin = position_;
    trees_.back()->range.begin.column -= rewind_column;
    trees_.back()->modifiers = std::move(modifiers);

    CheckInvariants();
  }

  void PushAndPop(size_t rewind_column, LineModifierSet modifiers) {
    State ignored_state = DEFAULT;
    Push(ignored_state, rewind_column, std::move(modifiers));
    PopBack();
  }

 private:
  const BufferContents& buffer_;
  std::vector<ParseTree*> trees_;
  std::vector<State> states_;
  LineColumn position_;
  LineColumn limit_;
  int nesting_ = 0;
  std::vector<Action> log_;
};

class CppTreeParser : public TreeParser {
 public:
  void FindChildren(const BufferContents& buffer, ParseTree* root) override {
    CHECK(root != nullptr);
    root->children.clear();
    root->depth = 0;

    ParseResult result(buffer, root, DEFAULT_AT_START_OF_LINE);
    for (size_t i = root->range.begin.line; i < root->range.end.line; i++) {
      result.set_position(max(LineColumn(i, 0), root->range.begin));
      result.set_limit(min(LineColumn(i + 1, 0), root->range.end));
      ParseLine(&result);
    }

    while (!result.empty()) {
      result.PopBack();
      result.CheckInvariants();
    }
  }

  void ParseLine(ParseResult* result) {
    while (!result->reached_final_position()) {
      result->CheckInvariants();
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
          AfterSlash(BRACKET_DEFAULT, BRACKET_DEFAULT_AT_START_OF_LINE,
                     result);
          break;

        case PARENS_AFTER_SLASH:
          AfterSlash(PARENS_DEFAULT, PARENS_DEFAULT_AT_START_OF_LINE, result);
          break;
      }

      CHECK_LE(original_position, result->position());
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
      CommentToEndOfLine(result);
    } else {
      result->SetState(state_default);
    }
  }

  void CommentToEndOfLine(ParseResult* result) {
    LineColumn original_position = result->position();
    CHECK_GT(original_position.column, 0);
    result->AdvancePositionUntilEndOfLine();
    result->PushAndPop(result->position().column - original_position.column + 1,
                       {LineModifier::BLUE});
    // TODO: words_parser_->FindChildren(result->buffer(), comment_tree);
  }

  void LiteralCharacter(ParseResult* result) {
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

  void LiteralString(ParseResult* result) {
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

  void PreprocessorDirective(State state, ParseResult* result) {
    result->SetState(state);

    LineColumn original_position = result->position();
    CHECK_GE(original_position.column, 1);
    original_position.column--;

    result->AdvancePositionUntilEndOfLine();
    CHECK_GT(result->position().column, original_position.column);
    result->PushAndPop(result->position().column - original_position.column,
                       {LineModifier::YELLOW});
  }

  void Identifier(ParseResult* result) {
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
    if (IsReservedToken(str->ToString())) {
      modifiers.insert(LineModifier::CYAN);
    }
    result->PushAndPop(length, std::move(modifiers));
  }

  void LiteralNumber(ParseResult* result) {
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
      State state_after_slash, bool after_newline, ParseResult* result) {
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
        result->PopBack()->children.front().modifiers = modifiers;
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

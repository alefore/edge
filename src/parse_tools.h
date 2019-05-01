#ifndef __AFC_EDITOR_PARSE_TOOLS_H__
#define __AFC_EDITOR_PARSE_TOOLS_H__

#include <memory>

#include "src/line_column.h"
#include "src/parse_tree.h"
#include "src/seek.h"

namespace afc {
namespace editor {

struct Action {
  static Action Push(size_t column, LineModifierSet modifiers) {
    return Action(PUSH, column, std::move(modifiers));
  }

  static Action Pop(size_t column) { return Action(POP, column, {}); }

  static Action SetFirstChildModifiers(LineModifierSet modifiers) {
    return Action(SET_FIRST_CHILD_MODIFIERS, 0, std::move(modifiers));
  }

  void Execute(std::vector<ParseTree*>* trees, size_t line);

  enum ActionType {
    PUSH,
    POP,

    // Set the modifiers of the first child of the current tree.
    SET_FIRST_CHILD_MODIFIERS,
  };

  ActionType action_type = PUSH;

  // TODO: Use ColumnNumber.
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

  void SetState(size_t state) { parse_results_.states_stack.back() = state; }

  void SetFirstChildModifiers(LineModifierSet modifiers) {
    parse_results_.actions.push_back(Action::SetFirstChildModifiers(modifiers));
  }

  void PopBack() {
    CHECK(!parse_results_.states_stack.empty());
    parse_results_.states_stack.pop_back();
    parse_results_.actions.push_back(Action::Pop(position_.column.value));
  }

  void Push(size_t nested_state, size_t rewind_column,
            LineModifierSet modifiers) {
    CHECK_GE(position_.column.value, rewind_column);

    parse_results_.states_stack.push_back(nested_state);

    parse_results_.actions.push_back(
        Action::Push(position_.column.value - rewind_column, modifiers));
  }

  void PushAndPop(size_t rewind_column, LineModifierSet modifiers) {
    size_t ignored_state = 0;
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

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_PARSE_TOOLS_H__

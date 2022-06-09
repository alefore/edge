#ifndef __AFC_EDITOR_PARSE_TOOLS_H__
#define __AFC_EDITOR_PARSE_TOOLS_H__

#include <memory>

#include "src/line_column.h"
#include "src/parse_tree.h"
#include "src/seek.h"

namespace afc::editor {

struct Action {
  static Action Push(language::lazy_string::ColumnNumber column,
                     LineModifierSet modifiers,
                     std::unordered_set<ParseTreeProperty> properties) {
    return Action(PUSH, column, std::move(modifiers), std::move(properties));
  }

  static Action Pop(language::lazy_string::ColumnNumber column) {
    return Action(POP, column, {}, {});
  }

  static Action SetFirstChildModifiers(LineModifierSet modifiers);

  void Execute(std::vector<ParseTree>* trees, LineNumber line);

  enum ActionType {
    PUSH,
    POP,

    // Set the modifiers of the first child of the current tree.
    SET_FIRST_CHILD_MODIFIERS,
  };

  ActionType action_type = PUSH;

  language::lazy_string::ColumnNumber column;

  // Used by PUSH and by SET_FIRST_CHILD_MODIFIERS.
  LineModifierSet modifiers;

  // Used by PUSH.
  std::unordered_set<ParseTreeProperty> properties;

 private:
  Action(ActionType input_action_type,
         language::lazy_string::ColumnNumber input_column,
         LineModifierSet input_modifiers,
         std::unordered_set<ParseTreeProperty> input_properties)
      : action_type(input_action_type),
        column(input_column),
        modifiers(std::move(input_modifiers)),
        properties(std::move(input_properties)) {}
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

  void PopBack();

  void Push(size_t nested_state,
            language::lazy_string::ColumnNumberDelta rewind_column,
            LineModifierSet modifiers,
            std::unordered_set<ParseTreeProperty> properties);

  void PushAndPop(language::lazy_string::ColumnNumberDelta rewind_column,
                  LineModifierSet modifiers);

 private:
  const BufferContents& buffer_;
  ParseResults parse_results_;
  LineColumn position_;
  Seek seek_;
  int nesting_ = 0;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_PARSE_TOOLS_H__

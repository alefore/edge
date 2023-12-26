#ifndef __AFC_EDITOR_PARSE_TOOLS_H__
#define __AFC_EDITOR_PARSE_TOOLS_H__

#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line_column.h"
#include "src/parse_tree.h"
#include "src/seek.h"

namespace afc::editor {

struct ActionPush {
  language::lazy_string::ColumnNumber column;
  infrastructure::screen::LineModifierSet modifiers;
  std::unordered_set<ParseTreeProperty> properties;
};

struct ActionPop {
  language::lazy_string::ColumnNumber column;
};

struct ActionSetFirstChildModifiers {
  infrastructure::screen::LineModifierSet modifiers;
};

using Action =
    std::variant<ActionPush, ActionPop, ActionSetFirstChildModifiers>;

void Execute(const Action& action, std::vector<ParseTree>* trees,
             language::text::LineNumber line);

struct ParseResults {
  std::vector<size_t> states_stack;
  std::vector<Action> actions = {};
};

class ParseData {
 public:
  ParseData(const language::text::LineSequence& buffer,
            std::vector<size_t> initial_states,
            language::text::LineColumn limit)
      : buffer_(buffer), seek_(buffer_, &position_) {
    parse_results_.states_stack = std::move(initial_states);
    seek_.WithRange(language::text::Range(language::text::LineColumn(), limit))
        .WrappingLines();
  }

  const language::text::LineSequence& buffer() const { return buffer_; }

  Seek& seek() { return seek_; }

  const ParseResults& parse_results() const { return parse_results_; }

  language::text::LineColumn position() const { return position_; }

  void set_position(language::text::LineColumn position) {
    position_ = position;
  }

  int AddAndGetNesting() { return nesting_++; }

  size_t state() const { return parse_results_.states_stack.back(); }

  void SetState(size_t state) { parse_results_.states_stack.back() = state; }

  void SetFirstChildModifiers(
      infrastructure::screen::LineModifierSet modifiers) {
    parse_results_.actions.emplace_back(
        ActionSetFirstChildModifiers{.modifiers = modifiers});
  }

  void PopBack();

  void Push(size_t nested_state,
            language::lazy_string::ColumnNumberDelta rewind_column,
            infrastructure::screen::LineModifierSet modifiers,
            std::unordered_set<ParseTreeProperty> properties);

  void PushAndPop(language::lazy_string::ColumnNumberDelta rewind_column,
                  infrastructure::screen::LineModifierSet modifiers,
                  std::unordered_set<ParseTreeProperty> properties = {});

 private:
  const language::text::LineSequence& buffer_;
  ParseResults parse_results_;
  language::text::LineColumn position_;
  Seek seek_;
  int nesting_ = 0;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_PARSE_TOOLS_H__

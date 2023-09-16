#include "src/parse_tools.h"

namespace afc::editor {
using infrastructure::screen::LineModifierSet;
using language::lazy_string::ColumnNumber;
using language::lazy_string::ColumnNumberDelta;
using language::text::LineColumn;
using language::text::LineNumber;
using language::text::Range;

/* static */ Action Action::SetFirstChildModifiers(LineModifierSet modifiers) {
  return Action(SET_FIRST_CHILD_MODIFIERS, ColumnNumber(), std::move(modifiers),
                {});
}

void Action::Execute(std::vector<ParseTree>* trees, LineNumber line) {
  switch (action_type) {
    case PUSH: {
      trees->emplace_back(Range(LineColumn(line, column), LineColumn()));
      trees->back().set_modifiers(modifiers);
      trees->back().set_properties(properties);
      DVLOG(5) << "Tree: Push: " << trees->back().range();
      break;
    }

    case POP: {
      auto child = std::move(trees->back());
      trees->pop_back();

      auto range = child.range();
      range.end = LineColumn(line, column);
      child.set_range(range);
      DVLOG(5) << "Tree: Pop: " << child.range();
      CHECK(!trees->empty());
      trees->back().PushChild(std::move(child));
      break;
    }

    case SET_FIRST_CHILD_MODIFIERS:
      DVLOG(5) << "Tree: SetModifiers: " << trees->back().range();
      trees->back().MutableChildren(0)->set_modifiers(modifiers);
      break;
  }
}

void ParseData::PopBack() {
  CHECK(!parse_results_.states_stack.empty());
  parse_results_.states_stack.pop_back();
  parse_results_.actions.push_back(Action::Pop(position_.column));
}

void ParseData::Push(size_t nested_state, ColumnNumberDelta rewind_column,
                     LineModifierSet modifiers,
                     std::unordered_set<ParseTreeProperty> properties) {
  CHECK_GE(position_.column.ToDelta(), rewind_column);

  parse_results_.states_stack.push_back(nested_state);

  parse_results_.actions.push_back(
      Action::Push(position_.column - rewind_column, std::move(modifiers),
                   std::move(properties)));
}

void ParseData::PushAndPop(ColumnNumberDelta rewind_column,
                           LineModifierSet modifiers,
                           std::unordered_set<ParseTreeProperty> properties) {
  size_t ignored_state = 0;
  Push(ignored_state, rewind_column, std::move(modifiers),
       std::move(properties));
  PopBack();
}

}  // namespace afc::editor

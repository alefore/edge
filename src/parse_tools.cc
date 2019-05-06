#include "src/parse_tools.h"

namespace afc {
namespace editor {

/* static */ Action Action::SetFirstChildModifiers(LineModifierSet modifiers) {
  return Action(SET_FIRST_CHILD_MODIFIERS, ColumnNumber(),
                std::move(modifiers));
}

void Action::Execute(std::vector<ParseTree*>* trees, LineNumber line) {
  switch (action_type) {
    case PUSH:
      trees->push_back(trees->back()->PushChild().release());
      trees->back()->range.begin = LineColumn(line, column);
      trees->back()->modifiers = modifiers;
      DVLOG(5) << "Tree: Push: " << trees->back()->range;
      break;

    case POP:
      trees->back()->range.end = LineColumn(line, column);
      DVLOG(5) << "Tree: Pop: " << trees->back()->range;
      trees->pop_back();
      break;

    case SET_FIRST_CHILD_MODIFIERS:
      DVLOG(5) << "Tree: SetModifiers: " << trees->back()->range;
      trees->back()->children.front().modifiers = modifiers;
      break;
  }
}

void ParseData::PopBack() {
  CHECK(!parse_results_.states_stack.empty());
  parse_results_.states_stack.pop_back();
  parse_results_.actions.push_back(Action::Pop(position_.column));
}

void ParseData::Push(size_t nested_state, ColumnNumberDelta rewind_column,
                     LineModifierSet modifiers) {
  CHECK_GE(position_.column.ToDelta(), rewind_column);

  parse_results_.states_stack.push_back(nested_state);

  parse_results_.actions.push_back(
      Action::Push(position_.column - rewind_column, modifiers));
}

void ParseData::PushAndPop(ColumnNumberDelta rewind_column,
                           LineModifierSet modifiers) {
  size_t ignored_state = 0;
  Push(ignored_state, rewind_column, std::move(modifiers));
  PopBack();
}

}  // namespace editor
}  // namespace afc

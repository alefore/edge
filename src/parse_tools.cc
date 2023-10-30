#include "src/parse_tools.h"

using afc::infrastructure::screen::LineModifierSet;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::text::LineColumn;
using afc::language::text::LineNumber;
using afc::language::text::Range;

namespace afc::editor {
namespace {
void ExecuteBase(const ActionPush& action, std::vector<ParseTree>* trees,
                 LineNumber line) {
  trees->emplace_back(
      Range(LineColumn(line, action.column), LineColumn(line, action.column)));
  trees->back().set_modifiers(action.modifiers);
  trees->back().set_properties(action.properties);
  DVLOG(5) << "Tree: Push: " << trees->back().range();
}

void ExecuteBase(const ActionPop& action, std::vector<ParseTree>* trees,
                 LineNumber line) {
  CHECK(!trees->empty());
  auto child = std::move(trees->back());
  trees->pop_back();

  auto range = child.range();
  range.set_end(std::max(range.begin(), LineColumn(line, action.column)));
  child.set_range(range);
  DVLOG(5) << "Tree: Pop: " << child.range();
  CHECK(!trees->empty());
  trees->back().PushChild(std::move(child));
}

void ExecuteBase(const ActionSetFirstChildModifiers& action,
                 std::vector<ParseTree>* trees, LineNumber) {
  CHECK(!trees->empty());
  DVLOG(5) << "Tree: SetModifiers: " << trees->back().range();
  trees->back().MutableChildren(0)->set_modifiers(action.modifiers);
}
}  // namespace

void Execute(const Action& action, std::vector<ParseTree>* trees,
             language::text::LineNumber line) {
  std::visit([&](auto& t) { ExecuteBase(t, trees, line); }, action);
}

void ParseData::PopBack() {
  CHECK(!parse_results_.states_stack.empty());
  parse_results_.states_stack.pop_back();
  parse_results_.actions.push_back(ActionPop{position_.column});
}

void ParseData::Push(size_t nested_state, ColumnNumberDelta rewind_column,
                     LineModifierSet modifiers,
                     std::unordered_set<ParseTreeProperty> properties) {
  CHECK_GE(position_.column.ToDelta(), rewind_column);

  parse_results_.states_stack.push_back(nested_state);

  parse_results_.actions.push_back(ActionPush{position_.column - rewind_column,
                                              std::move(modifiers),
                                              std::move(properties)});
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

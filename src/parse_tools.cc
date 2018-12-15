#include "parse_tools.h"

namespace afc {
namespace editor {

void Action::Execute(std::vector<ParseTree*>* trees, size_t line) {
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

}  // namespace editor
}  // namespace afc

#include "parse_tools.h"

namespace afc {
namespace editor {

void Action::Execute(std::vector<ParseTree*>* trees, size_t line) {
  switch (action_type) {
    case PUSH:
      trees->push_back(PushChild(trees->back()).release());
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

}  // namespace editor
}  // namespace afc

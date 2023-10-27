#ifndef __AFC_EDITOR_GOTO_COMMAND_H__
#define __AFC_EDITOR_GOTO_COMMAND_H__

#include <memory>

#include "src/language/gc.h"
#include "src/transformation/composite.h"

namespace afc::editor {
class Command;
class GotoTransformation : public CompositeTransformation {
 public:
  GotoTransformation(int calls);

  std::wstring Serialize() const override;

  futures::Value<Output> Apply(Input input) const override;

 private:
  const int calls_;
};

language::gc::Root<Command> NewGotoCommand(EditorState& editor_state);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_GOTO_COMMAND_H__

#ifndef __AFC_EDITOR_GOTO_COMMAND_H__
#define __AFC_EDITOR_GOTO_COMMAND_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/transformation/composite.h"

namespace afc::editor {
class Command;
class GotoTransformation : public CompositeTransformation {
 public:
  GotoTransformation(int calls);

  std::wstring Serialize() const override;

  futures::Value<Output> Apply(Input input) const override;

  language::NonNull<std::unique_ptr<CompositeTransformation>> Clone()
      const override;

 private:
  const int calls_;
};

language::NonNull<std::unique_ptr<Command>> NewGotoCommand(
    EditorState& editor_state);

}  // namespace afc::editor

#endif  // __AFC_EDITOR_GOTO_COMMAND_H__

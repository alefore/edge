#ifndef __AFC_EDITOR_GOTO_COMMAND_H__
#define __AFC_EDITOR_GOTO_COMMAND_H__

#include <memory>

#include "command.h"
#include "src/transformation/composite.h"

namespace afc {
namespace editor {
class GotoTransformation : public CompositeTransformation {
 public:
  GotoTransformation(int calls);

  std::wstring Serialize() const override;

  futures::Value<Output> Apply(Input input) const override;

  std::unique_ptr<CompositeTransformation> Clone() const override;

 private:
  const int calls_;
};

std::unique_ptr<Command> NewGotoCommand(EditorState& editor_state);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_GOTO_COMMAND_H__

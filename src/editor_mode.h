#ifndef __AFC_EDITOR_EDITOR_MODE_H__
#define __AFC_EDITOR_EDITOR_MODE_H__

#include <memory>

namespace afc {
namespace editor {

struct EditorState;

class EditorMode {
 public:
  virtual ~EditorMode() {}
  virtual void ProcessInput(int c, EditorState* editor_state) = 0;
};

}  // namespace editor
}  // namespace afc

#endif

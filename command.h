#ifndef __AFC_EDITOR_COMMAND_H__
#define __AFC_EDITOR_COMMAND_H__

#include <string>

#include "editor_mode.h"

namespace afc {
namespace editor {

using std::string;

struct EditorState;

class Command : public EditorMode {
 public:
  virtual ~Command() {}
  virtual const string Description() = 0;
  virtual void ProcessInput(int c, EditorState* editor_state) = 0;
};

}  // namespace editor
}  // namespace afc

#endif

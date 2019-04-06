#ifndef __AFC_EDITOR_COMMAND_H__
#define __AFC_EDITOR_COMMAND_H__

#include <string>

#include "editor_mode.h"

namespace afc {
namespace editor {

class EditorState;

class Command : public EditorMode {
 public:
  virtual ~Command() {}
  virtual std::wstring Category() const = 0;
  virtual std::wstring Description() const = 0;
  virtual void ProcessInput(wint_t c, EditorState* editor_state) = 0;
};

}  // namespace editor
}  // namespace afc

#endif

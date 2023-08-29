#ifndef __AFC_EDITOR_COMMAND_H__
#define __AFC_EDITOR_COMMAND_H__

#include <string>

#include "src/editor_mode.h"

namespace afc::editor {
class Command : public EditorMode {
 public:
  virtual ~Command() {}
  virtual std::wstring Category() const = 0;
  virtual std::wstring Description() const = 0;
  CursorMode cursor_mode() const override;
};
}  // namespace afc::editor

#endif

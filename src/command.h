#ifndef __AFC_EDITOR_COMMAND_H__
#define __AFC_EDITOR_COMMAND_H__

#include <string>

#include "src/editor_mode.h"
#include "src/language/ghost_type_class.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::editor {
struct CommandCategory
    : public language::GhostType<CommandCategory,
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;

  static const CommandCategory& kBuffers();
  static const CommandCategory& kEdit();
  static const CommandCategory& kEditor();
  static const CommandCategory& kExtensions();
  static const CommandCategory& kNavigate();
  static const CommandCategory& kPrompt();
};

class Command : public EditorMode {
 public:
  virtual ~Command() {}
  virtual CommandCategory Category() const = 0;
  virtual language::lazy_string::LazyString Description() const = 0;
  CursorMode cursor_mode() const override;
};
}  // namespace afc::editor

#endif

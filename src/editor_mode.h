#ifndef __AFC_EDITOR_EDITOR_MODE_H__
#define __AFC_EDITOR_EDITOR_MODE_H__

#include <memory>
#include <vector>

#include "src/language/gc.h"

namespace afc::editor {

// Rename to something like 'KeyboardHandler'.
class EditorMode {
 public:
  virtual ~EditorMode() {}
  virtual void ProcessInput(wint_t c) = 0;

  enum class CursorMode { kDefault, kInserting, kOverwriting };
  virtual CursorMode cursor_mode() const = 0;

  virtual std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const = 0;
};

std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Expand(const EditorMode& t);

}  // namespace afc::editor

#endif

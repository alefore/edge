#ifndef __AFC_EDITOR_EDITOR_MODE_H__
#define __AFC_EDITOR_EDITOR_MODE_H__

namespace afc::editor {

// Rename to something like 'KeyboardHandler'.
class EditorMode {
 public:
  virtual ~EditorMode() {}
  virtual void ProcessInput(wint_t c) = 0;

  enum class CursorMode { kDefault, kInserting, kOverwriting };
  virtual CursorMode cursor_mode() const = 0;
};

}  // namespace afc::editor

#endif

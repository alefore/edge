#ifndef __AFC_EDITOR_EDITOR_MODE_H__
#define __AFC_EDITOR_EDITOR_MODE_H__

#include <memory>
#include <vector>

#include "src/infrastructure/extended_char.h"
#include "src/language/gc.h"

namespace afc::editor {

// Rename to something like 'KeyboardHandler'.
class InputReceiver {
 public:
  virtual ~InputReceiver() = default;

  // Starts processing characters from `input` starting at the index
  // `start_index`. Returns the number of characters processed.
  //
  // Precondition: `start_index` must be less than `input.size()`.
  // Postcondition: The returned value must be greater than 0.
  // Postcondition: The returned value must be less than `input.size() -
  // start_index`.
  virtual size_t Receive(const std::vector<infrastructure::ExtendedChar>& input,
                         size_t start_index) = 0;

  enum class CursorMode { kDefault, kInserting, kOverwriting };
  virtual CursorMode cursor_mode() const = 0;

  virtual std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const = 0;
};

// TODO(2023-11-29, trivial): Rename this class to `SimpleInputReceiver`.
class EditorMode : public InputReceiver {
 public:
  size_t Receive(const std::vector<infrastructure::ExtendedChar>& input,
                 size_t start_index) override;
  virtual void ProcessInput(infrastructure::ExtendedChar c) = 0;
};
}  // namespace afc::editor

#endif
